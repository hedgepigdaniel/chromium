// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cdm/ppapi/external_clear_key/clear_key_cdm.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <utility>

#include "base/bind.h"
#include "base/files/file.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "media/base/cdm_callback_promise.h"
#include "media/base/cdm_key_information.h"
#include "media/base/decoder_buffer.h"
#include "media/base/decrypt_config.h"
#include "media/cdm/api/content_decryption_module_ext.h"
#include "media/cdm/json_web_key.h"
#include "media/cdm/ppapi/cdm_file_io_test.h"
#include "media/cdm/ppapi/external_clear_key/cdm_video_decoder.h"
#include "url/gurl.h"

#if defined(CLEAR_KEY_CDM_USE_FAKE_AUDIO_DECODER)
const int64_t kNoTimestamp = INT64_MIN;
#endif  // CLEAR_KEY_CDM_USE_FAKE_AUDIO_DECODER

#if defined(CLEAR_KEY_CDM_USE_FFMPEG_DECODER)
#include "base/at_exit.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "media/base/media.h"
#include "media/cdm/ppapi/external_clear_key/ffmpeg_cdm_audio_decoder.h"
#include "media/cdm/ppapi/external_clear_key/ffmpeg_cdm_video_decoder.h"

// Include FFmpeg avformat.h for av_register_all().
extern "C" {
// Temporarily disable possible loss of data warning.
MSVC_PUSH_DISABLE_WARNING(4244);
#include <libavformat/avformat.h>
MSVC_POP_WARNING();
}  // extern "C"

#if !defined COMPONENT_BUILD
static base::AtExitManager g_at_exit_manager;
#endif
#endif  // CLEAR_KEY_CDM_USE_FFMPEG_DECODER

const char kClearKeyCdmVersion[] = "0.1.0.1";
const char kExternalClearKeyKeySystem[] = "org.chromium.externalclearkey";

// Variants of External Clear Key key system to test different scenarios.
const char kExternalClearKeyDecryptOnlyKeySystem[] =
    "org.chromium.externalclearkey.decryptonly";
const char kExternalClearKeyRenewalKeySystem[] =
    "org.chromium.externalclearkey.renewal";
const char kExternalClearKeyFileIOTestKeySystem[] =
    "org.chromium.externalclearkey.fileiotest";
const char kExternalClearKeyOutputProtectionTestKeySystem[] =
    "org.chromium.externalclearkey.outputprotectiontest";
const char kExternalClearKeyPlatformVerificationTestKeySystem[] =
    "org.chromium.externalclearkey.platformverificationtest";
const char kExternalClearKeyCrashKeySystem[] =
    "org.chromium.externalclearkey.crash";
const char kExternalClearKeyVerifyCdmHostTestKeySystem[] =
    "org.chromium.externalclearkey.verifycdmhosttest";

// Constants for the enumalted session that can be loaded by LoadSession().
// These constants need to be in sync with
// chrome/test/data/media/encrypted_media_utils.js
const char kLoadableSessionId[] = "LoadableSession";
const uint8_t kLoadableSessionKeyId[] = "0123456789012345";
const uint8_t kLoadableSessionKey[] = {0xeb, 0xdd, 0x62, 0xf1, 0x68, 0x14,
                                       0xd2, 0x7b, 0x68, 0xef, 0x12, 0x2a,
                                       0xfc, 0xe4, 0xae, 0x3c};

const int64_t kSecondsPerMinute = 60;
const int64_t kMsPerSecond = 1000;
const int64_t kInitialTimerDelayMs = 200;
const int64_t kMaxTimerDelayMs = 1 * kSecondsPerMinute * kMsPerSecond;

// CDM unit test result header. Must be in sync with UNIT_TEST_RESULT_HEADER in
// media/test/data/eme_player_js/globals.js.
const char kUnitTestResultHeader[] = "UNIT_TEST_RESULT";

// Copies |input_buffer| into a media::DecoderBuffer. If the |input_buffer| is
// empty, an empty (end-of-stream) media::DecoderBuffer is returned.
static scoped_refptr<media::DecoderBuffer> CopyDecoderBufferFrom(
    const cdm::InputBuffer& input_buffer) {
  if (!input_buffer.data) {
    DCHECK(!input_buffer.data_size);
    return media::DecoderBuffer::CreateEOSBuffer();
  }

  // TODO(xhwang): Get rid of this copy.
  scoped_refptr<media::DecoderBuffer> output_buffer =
      media::DecoderBuffer::CopyFrom(input_buffer.data, input_buffer.data_size);
  output_buffer->set_timestamp(
      base::TimeDelta::FromMicroseconds(input_buffer.timestamp));

  // TODO(xhwang): Unify how to check whether a buffer is encrypted.
  // See http://crbug.com/675003
  if (input_buffer.iv_size != 0) {
    DCHECK_GT(input_buffer.key_id_size, 0u);
    std::vector<media::SubsampleEntry> subsamples;
    for (uint32_t i = 0; i < input_buffer.num_subsamples; ++i) {
      subsamples.push_back(
          media::SubsampleEntry(input_buffer.subsamples[i].clear_bytes,
                                input_buffer.subsamples[i].cipher_bytes));
    }

    std::unique_ptr<media::DecryptConfig> decrypt_config(
        new media::DecryptConfig(
            std::string(reinterpret_cast<const char*>(input_buffer.key_id),
                        input_buffer.key_id_size),
            std::string(reinterpret_cast<const char*>(input_buffer.iv),
                        input_buffer.iv_size),
            subsamples));

    output_buffer->set_decrypt_config(std::move(decrypt_config));
  }

  return output_buffer;
}

static std::string GetUnitTestResultMessage(bool success) {
  std::string message(kUnitTestResultHeader);
  message += success ? '1' : '0';
  return message;
}

static cdm::Error ConvertException(
    media::CdmPromise::Exception exception_code) {
  switch (exception_code) {
    case media::CdmPromise::NOT_SUPPORTED_ERROR:
      return cdm::kNotSupportedError;
    case media::CdmPromise::INVALID_STATE_ERROR:
      return cdm::kInvalidStateError;
    case media::CdmPromise::INVALID_ACCESS_ERROR:
      return cdm::kInvalidAccessError;
    case media::CdmPromise::QUOTA_EXCEEDED_ERROR:
      return cdm::kQuotaExceededError;
    case media::CdmPromise::UNKNOWN_ERROR:
      return cdm::kUnknownError;
    case media::CdmPromise::CLIENT_ERROR:
      return cdm::kClientError;
    case media::CdmPromise::OUTPUT_ERROR:
      return cdm::kOutputError;
  }
  NOTREACHED();
  return cdm::kUnknownError;
}

static media::CdmSessionType ConvertSessionType(cdm::SessionType session_type) {
  switch (session_type) {
    case cdm::kTemporary:
      return media::CdmSessionType::TEMPORARY_SESSION;
    case cdm::kPersistentLicense:
      return media::CdmSessionType::PERSISTENT_LICENSE_SESSION;
    case cdm::kPersistentKeyRelease:
      return media::CdmSessionType::PERSISTENT_RELEASE_MESSAGE_SESSION;
  }
  NOTREACHED();
  return media::CdmSessionType::TEMPORARY_SESSION;
}

static media::EmeInitDataType ConvertInitDataType(
    cdm::InitDataType init_data_type) {
  switch (init_data_type) {
    case cdm::kCenc:
      return media::EmeInitDataType::CENC;
    case cdm::kKeyIds:
      return media::EmeInitDataType::KEYIDS;
    case cdm::kWebM:
      return media::EmeInitDataType::WEBM;
  }
  NOTREACHED();
  return media::EmeInitDataType::UNKNOWN;
}

cdm::KeyStatus ConvertKeyStatus(media::CdmKeyInformation::KeyStatus status) {
  switch (status) {
    case media::CdmKeyInformation::KeyStatus::USABLE:
      return cdm::kUsable;
    case media::CdmKeyInformation::KeyStatus::INTERNAL_ERROR:
      return cdm::kInternalError;
    case media::CdmKeyInformation::KeyStatus::EXPIRED:
      return cdm::kExpired;
    case media::CdmKeyInformation::KeyStatus::OUTPUT_RESTRICTED:
      return cdm::kOutputRestricted;
    case media::CdmKeyInformation::KeyStatus::OUTPUT_DOWNSCALED:
      return cdm::kOutputDownscaled;
    case media::CdmKeyInformation::KeyStatus::KEY_STATUS_PENDING:
      return cdm::kStatusPending;
    case media::CdmKeyInformation::KeyStatus::RELEASED:
      return cdm::kReleased;
  }
  NOTREACHED();
  return cdm::kInternalError;
}

// Shallow copy all the key information from |keys_info| into |keys_vector|.
// |keys_vector| is only valid for the lifetime of |keys_info| because it
// contains pointers into the latter.
void ConvertCdmKeysInfo(const std::vector<media::CdmKeyInformation*>& keys_info,
                        std::vector<cdm::KeyInformation>* keys_vector) {
  keys_vector->reserve(keys_info.size());
  for (const auto& key_info : keys_info) {
    cdm::KeyInformation key;
    key.key_id = key_info->key_id.data();
    key.key_id_size = key_info->key_id.size();
    key.status = ConvertKeyStatus(key_info->status);
    key.system_code = key_info->system_code;
    keys_vector->push_back(key);
  }
}

void INITIALIZE_CDM_MODULE() {
  DVLOG(1) << __func__;
#if defined(CLEAR_KEY_CDM_USE_FFMPEG_DECODER)
  media::InitializeMediaLibrary();
  av_register_all();
#endif  // CLEAR_KEY_CDM_USE_FFMPEG_DECODER
}

void DeinitializeCdmModule() {
  DVLOG(1) << __func__;
}

void* CreateCdmInstance(int cdm_interface_version,
                        const char* key_system, uint32_t key_system_size,
                        GetCdmHostFunc get_cdm_host_func,
                        void* user_data) {
  DVLOG(1) << "CreateCdmInstance()";

  std::string key_system_string(key_system, key_system_size);
  if (key_system_string != kExternalClearKeyKeySystem &&
      key_system_string != kExternalClearKeyDecryptOnlyKeySystem &&
      key_system_string != kExternalClearKeyRenewalKeySystem &&
      key_system_string != kExternalClearKeyFileIOTestKeySystem &&
      key_system_string != kExternalClearKeyOutputProtectionTestKeySystem &&
      key_system_string != kExternalClearKeyPlatformVerificationTestKeySystem &&
      key_system_string != kExternalClearKeyCrashKeySystem &&
      key_system_string != kExternalClearKeyVerifyCdmHostTestKeySystem) {
    DVLOG(1) << "Unsupported key system:" << key_system_string;
    return NULL;
  }

  if (cdm_interface_version != media::ClearKeyCdmInterface::kVersion)
    return NULL;

  media::ClearKeyCdmHost* host = static_cast<media::ClearKeyCdmHost*>(
      get_cdm_host_func(media::ClearKeyCdmHost::kVersion, user_data));
  if (!host)
    return NULL;

  // TODO(jrummell): Obtain the proper origin for this instance.
  GURL empty_origin;
  return new media::ClearKeyCdm(host, key_system_string, empty_origin);
}

const char* GetCdmVersion() {
  return kClearKeyCdmVersion;
}

static bool g_verify_host_files_result = false;

// Makes sure files and corresponding signature files are readable but not
// writable.
bool VerifyCdmHost_0(const cdm::HostFile* host_files, uint32_t num_files) {
  DVLOG(1) << __func__ << ": " << num_files;

  // We should always have the CDM and CDM adapter and at lease one common file.
  // The common CDM host file (e.g. chrome) might not exist since we are running
  // in browser_tests.
  const uint32_t kMinNumHostFiles = 3;

  // We should always have the CDM and CDM adapter.
  const int kNumCdmFiles = 2;

  if (num_files < kMinNumHostFiles) {
    LOG(ERROR) << "Too few host files: " << num_files;
    g_verify_host_files_result = false;
    return true;
  }

  int num_opened_files = 0;
  for (uint32_t i = 0; i < num_files; ++i) {
    const int kBytesToRead = 10;
    std::vector<char> buffer(kBytesToRead);

    base::File file(static_cast<base::PlatformFile>(host_files[i].file));
    if (!file.IsValid())
      continue;

    num_opened_files++;

    int bytes_read = file.Read(0, buffer.data(), buffer.size());
    if (bytes_read != kBytesToRead) {
      LOG(ERROR) << "File bytes read: " << bytes_read;
      g_verify_host_files_result = false;
      return true;
    }

    // TODO(xhwang): Check that the files are not writable.
    // TODO(xhwang): Also verify the signature file when it's available.
  }

  // We should always have CDM files opened.
  if (num_opened_files < kNumCdmFiles) {
    LOG(ERROR) << "Too few opened files: " << num_opened_files;
    g_verify_host_files_result = false;
    return true;
  }

  g_verify_host_files_result = true;
  return true;
}

namespace media {

ClearKeyCdm::ClearKeyCdm(ClearKeyCdmHost* host,
                         const std::string& key_system,
                         const GURL& origin)
    : decryptor_(new AesDecryptor(
          origin,
          base::Bind(&ClearKeyCdm::OnSessionMessage, base::Unretained(this)),
          base::Bind(&ClearKeyCdm::OnSessionClosed, base::Unretained(this)),
          base::Bind(&ClearKeyCdm::OnSessionKeysChange, base::Unretained(this)),
          base::Bind(&ClearKeyCdm::OnSessionExpirationUpdate,
                     base::Unretained(this)))),
      host_(host),
      key_system_(key_system),
      has_received_keys_change_event_for_emulated_loadsession_(false),
      timer_delay_ms_(kInitialTimerDelayMs),
      renewal_timer_set_(false),
      is_running_output_protection_test_(false),
      is_running_platform_verification_test_(false) {
#if defined(CLEAR_KEY_CDM_USE_FAKE_AUDIO_DECODER)
  channel_count_ = 0;
  bits_per_channel_ = 0;
  samples_per_second_ = 0;
  output_timestamp_base_in_microseconds_ = kNoTimestamp;
  total_samples_generated_ = 0;
#endif  // CLEAR_KEY_CDM_USE_FAKE_AUDIO_DECODER
}

ClearKeyCdm::~ClearKeyCdm() {}

void ClearKeyCdm::Initialize(bool /* allow_distinctive_identifier */,
                             bool /* allow_persistent_state */) {
  // Implementation doesn't use distinctive identifier nor save persistent data,
  // so nothing to do with these values.
}

void ClearKeyCdm::CreateSessionAndGenerateRequest(
    uint32_t promise_id,
    cdm::SessionType session_type,
    cdm::InitDataType init_data_type,
    const uint8_t* init_data,
    uint32_t init_data_size) {
  DVLOG(1) << __func__;

  std::unique_ptr<media::NewSessionCdmPromise> promise(
      new media::CdmCallbackPromise<std::string>(
          base::Bind(&ClearKeyCdm::OnSessionCreated, base::Unretained(this),
                     promise_id),
          base::Bind(&ClearKeyCdm::OnPromiseFailed, base::Unretained(this),
                     promise_id)));
  decryptor_->CreateSessionAndGenerateRequest(
      ConvertSessionType(session_type), ConvertInitDataType(init_data_type),
      std::vector<uint8_t>(init_data, init_data + init_data_size),
      std::move(promise));

  if (key_system_ == kExternalClearKeyFileIOTestKeySystem) {
    StartFileIOTest();
  } else if (key_system_ == kExternalClearKeyOutputProtectionTestKeySystem) {
    StartOutputProtectionTest();
  } else if (key_system_ ==
             kExternalClearKeyPlatformVerificationTestKeySystem) {
    StartPlatformVerificationTest();
  } else if (key_system_ == kExternalClearKeyVerifyCdmHostTestKeySystem) {
    VerifyCdmHostTest();
  }
}

// Loads a emulated stored session. Currently only |kLoadableSessionId|
// (containing a |kLoadableSessionKey| for |kLoadableSessionKeyId|) is
// supported.
void ClearKeyCdm::LoadSession(uint32_t promise_id,
                              cdm::SessionType session_type,
                              const char* session_id,
                              uint32_t session_id_length) {
  DVLOG(1) << __func__;
  DCHECK_EQ(session_type, cdm::kPersistentLicense);

  if (std::string(kLoadableSessionId) !=
      std::string(session_id, session_id_length)) {
    host_->OnResolveNewSessionPromise(promise_id, nullptr, 0);
    return;
  }

  // Only allowed to successfully load this session once.
  DCHECK(session_id_for_emulated_loadsession_.empty());

  std::unique_ptr<media::NewSessionCdmPromise> promise(
      new media::CdmCallbackPromise<std::string>(
          base::Bind(&ClearKeyCdm::OnSessionLoaded, base::Unretained(this),
                     promise_id),
          base::Bind(&ClearKeyCdm::OnPromiseFailed, base::Unretained(this),
                     promise_id)));
  // AesDecryptor does not support loading, so create a temporary session to
  // represent it in other session-related methods.
  std::vector<uint8_t> key_id(
      kLoadableSessionKeyId,
      kLoadableSessionKeyId + arraysize(kLoadableSessionKeyId) - 1);
  decryptor_->CreateSessionAndGenerateRequest(CdmSessionType::TEMPORARY_SESSION,
                                              EmeInitDataType::WEBM, key_id,
                                              std::move(promise));
}

void ClearKeyCdm::UpdateSession(uint32_t promise_id,
                                const char* session_id,
                                uint32_t session_id_length,
                                const uint8_t* response,
                                uint32_t response_size) {
  DVLOG(1) << __func__;
  std::string web_session_str(session_id, session_id_length);

  // If updating the loadable session, use the actual session id generated.
  if (web_session_str == std::string(kLoadableSessionId))
    web_session_str = session_id_for_emulated_loadsession_;

  std::unique_ptr<media::SimpleCdmPromise> promise(
      new media::CdmCallbackPromise<>(
          base::Bind(&ClearKeyCdm::OnPromiseResolved, base::Unretained(this),
                     promise_id),
          base::Bind(&ClearKeyCdm::OnPromiseFailed, base::Unretained(this),
                     promise_id)));
  decryptor_->UpdateSession(
      web_session_str, std::vector<uint8_t>(response, response + response_size),
      std::move(promise));

  // TODO(xhwang): Only schedule renewal and update expiration change when the
  // promise is resolved.

  cdm::Time expiration = 0.0;  // Never expires.

  if (key_system_ == kExternalClearKeyRenewalKeySystem) {
    // For renewal key system, set a non-zero expiration that is approximately
    // 100 years after 01 January 1970 UTC.
    expiration = 3153600000.0;  // 100 * 365 * 24 * 60 * 60;

    if (!renewal_timer_set_) {
      ScheduleNextRenewal();
      renewal_timer_set_ = true;
    }
  }

  host_->OnExpirationChange(session_id, session_id_length, expiration);
}

void ClearKeyCdm::CloseSession(uint32_t promise_id,
                               const char* session_id,
                               uint32_t session_id_length) {
  DVLOG(1) << __func__;
  std::string web_session_str(session_id, session_id_length);

  // If closing the loadable session, use the actual session id generated.
  if (web_session_str == std::string(kLoadableSessionId))
    web_session_str = session_id_for_emulated_loadsession_;

  std::unique_ptr<media::SimpleCdmPromise> promise(
      new media::CdmCallbackPromise<>(
          base::Bind(&ClearKeyCdm::OnPromiseResolved, base::Unretained(this),
                     promise_id),
          base::Bind(&ClearKeyCdm::OnPromiseFailed, base::Unretained(this),
                     promise_id)));
  decryptor_->CloseSession(web_session_str, std::move(promise));
}

void ClearKeyCdm::RemoveSession(uint32_t promise_id,
                                const char* session_id,
                                uint32_t session_id_length) {
  DVLOG(1) << __func__;
  std::string web_session_str(session_id, session_id_length);

  // RemoveSession only allowed for the loadable session.
  if (web_session_str == std::string(kLoadableSessionId))
    web_session_str = session_id_for_emulated_loadsession_;

  std::unique_ptr<media::SimpleCdmPromise> promise(
      new media::CdmCallbackPromise<>(
          base::Bind(&ClearKeyCdm::OnPromiseResolved, base::Unretained(this),
                     promise_id),
          base::Bind(&ClearKeyCdm::OnPromiseFailed, base::Unretained(this),
                     promise_id)));
  decryptor_->RemoveSession(web_session_str, std::move(promise));
}

void ClearKeyCdm::SetServerCertificate(uint32_t promise_id,
                                       const uint8_t* server_certificate_data,
                                       uint32_t server_certificate_data_size) {
  std::unique_ptr<media::SimpleCdmPromise> promise(
      new media::CdmCallbackPromise<>(
          base::Bind(&ClearKeyCdm::OnPromiseResolved, base::Unretained(this),
                     promise_id),
          base::Bind(&ClearKeyCdm::OnPromiseFailed, base::Unretained(this),
                     promise_id)));
  decryptor_->SetServerCertificate(
      std::vector<uint8_t>(
          server_certificate_data,
          server_certificate_data + server_certificate_data_size),
      std::move(promise));
}

void ClearKeyCdm::TimerExpired(void* context) {
  if (context == &session_id_for_emulated_loadsession_) {
    LoadLoadableSession();
    return;
  }

  DCHECK(renewal_timer_set_);
  std::string renewal_message;
  if (!next_renewal_message_.empty() &&
      context == &next_renewal_message_[0]) {
    renewal_message = next_renewal_message_;
  } else {
    renewal_message = "ERROR: Invalid timer context found!";
  }

  host_->OnSessionMessage(last_session_id_.data(), last_session_id_.length(),
                          cdm::kLicenseRenewal, renewal_message.data(),
                          renewal_message.length(), nullptr, 0);

  ScheduleNextRenewal();
}

static void CopyDecryptResults(
    media::Decryptor::Status* status_copy,
    scoped_refptr<media::DecoderBuffer>* buffer_copy,
    media::Decryptor::Status status,
    const scoped_refptr<media::DecoderBuffer>& buffer) {
  *status_copy = status;
  *buffer_copy = buffer;
}

cdm::Status ClearKeyCdm::Decrypt(const cdm::InputBuffer& encrypted_buffer,
                                 cdm::DecryptedBlock* decrypted_block) {
  DVLOG(1) << "Decrypt()";
  DCHECK(encrypted_buffer.data);

  scoped_refptr<media::DecoderBuffer> buffer;
  cdm::Status status = DecryptToMediaDecoderBuffer(encrypted_buffer, &buffer);

  if (status != cdm::kSuccess)
    return status;

  DCHECK(buffer->data());
  decrypted_block->SetDecryptedBuffer(
      host_->Allocate(buffer->data_size()));
  memcpy(reinterpret_cast<void*>(decrypted_block->DecryptedBuffer()->Data()),
         buffer->data(),
         buffer->data_size());
  decrypted_block->DecryptedBuffer()->SetSize(buffer->data_size());
  decrypted_block->SetTimestamp(buffer->timestamp().InMicroseconds());

  return cdm::kSuccess;
}

cdm::Status ClearKeyCdm::InitializeAudioDecoder(
    const cdm::AudioDecoderConfig& audio_decoder_config) {
  if (key_system_ == kExternalClearKeyDecryptOnlyKeySystem)
    return cdm::kSessionError;

#if defined(CLEAR_KEY_CDM_USE_FFMPEG_DECODER)
  if (!audio_decoder_)
    audio_decoder_.reset(new media::FFmpegCdmAudioDecoder(host_));

  if (!audio_decoder_->Initialize(audio_decoder_config))
    return cdm::kSessionError;

  return cdm::kSuccess;
#elif defined(CLEAR_KEY_CDM_USE_FAKE_AUDIO_DECODER)
  channel_count_ = audio_decoder_config.channel_count;
  bits_per_channel_ = audio_decoder_config.bits_per_channel;
  samples_per_second_ = audio_decoder_config.samples_per_second;
  return cdm::kSuccess;
#else
  NOTIMPLEMENTED();
  return cdm::kSessionError;
#endif  // CLEAR_KEY_CDM_USE_FFMPEG_DECODER
}

cdm::Status ClearKeyCdm::InitializeVideoDecoder(
    const cdm::VideoDecoderConfig& video_decoder_config) {
  if (key_system_ == kExternalClearKeyDecryptOnlyKeySystem)
    return cdm::kSessionError;

  if (video_decoder_ && video_decoder_->is_initialized()) {
    DCHECK(!video_decoder_->is_initialized());
    return cdm::kSessionError;
  }

  // Any uninitialized decoder will be replaced.
  video_decoder_ = CreateVideoDecoder(host_, video_decoder_config);
  if (!video_decoder_)
    return cdm::kSessionError;

  return cdm::kSuccess;
}

void ClearKeyCdm::ResetDecoder(cdm::StreamType decoder_type) {
  DVLOG(1) << "ResetDecoder()";
#if defined(CLEAR_KEY_CDM_USE_FFMPEG_DECODER)
  switch (decoder_type) {
    case cdm::kStreamTypeVideo:
      video_decoder_->Reset();
      break;
    case cdm::kStreamTypeAudio:
      audio_decoder_->Reset();
      break;
    default:
      NOTREACHED() << "ResetDecoder(): invalid cdm::StreamType";
  }
#elif defined(CLEAR_KEY_CDM_USE_FAKE_AUDIO_DECODER)
  if (decoder_type == cdm::kStreamTypeAudio) {
    output_timestamp_base_in_microseconds_ = kNoTimestamp;
    total_samples_generated_ = 0;
  }
#endif  // CLEAR_KEY_CDM_USE_FFMPEG_DECODER
}

void ClearKeyCdm::DeinitializeDecoder(cdm::StreamType decoder_type) {
  DVLOG(1) << "DeinitializeDecoder()";
  switch (decoder_type) {
    case cdm::kStreamTypeVideo:
      video_decoder_->Deinitialize();
      break;
    case cdm::kStreamTypeAudio:
#if defined(CLEAR_KEY_CDM_USE_FFMPEG_DECODER)
      audio_decoder_->Deinitialize();
#elif defined(CLEAR_KEY_CDM_USE_FAKE_AUDIO_DECODER)
      output_timestamp_base_in_microseconds_ = kNoTimestamp;
      total_samples_generated_ = 0;
#endif
      break;
    default:
      NOTREACHED() << "DeinitializeDecoder(): invalid cdm::StreamType";
  }
}

cdm::Status ClearKeyCdm::DecryptAndDecodeFrame(
    const cdm::InputBuffer& encrypted_buffer,
    cdm::VideoFrame* decoded_frame) {
  DVLOG(1) << "DecryptAndDecodeFrame()";
  TRACE_EVENT0("media", "ClearKeyCdm::DecryptAndDecodeFrame");

  scoped_refptr<media::DecoderBuffer> buffer;
  cdm::Status status = DecryptToMediaDecoderBuffer(encrypted_buffer, &buffer);

  if (status != cdm::kSuccess)
    return status;

  const uint8_t* data = NULL;
  int32_t size = 0;
  int64_t timestamp = 0;
  if (!buffer->end_of_stream()) {
    data = buffer->data();
    size = buffer->data_size();
    timestamp = encrypted_buffer.timestamp;
  }

  return video_decoder_->DecodeFrame(data, size, timestamp, decoded_frame);
}

cdm::Status ClearKeyCdm::DecryptAndDecodeSamples(
    const cdm::InputBuffer& encrypted_buffer,
    cdm::AudioFrames* audio_frames) {
  DVLOG(1) << "DecryptAndDecodeSamples()";

  // Trigger a crash on purpose for testing purpose.
  if (key_system_ == kExternalClearKeyCrashKeySystem)
    CHECK(false);

  scoped_refptr<media::DecoderBuffer> buffer;
  cdm::Status status = DecryptToMediaDecoderBuffer(encrypted_buffer, &buffer);

  if (status != cdm::kSuccess)
    return status;

#if defined(CLEAR_KEY_CDM_USE_FFMPEG_DECODER)
  const uint8_t* data = NULL;
  int32_t size = 0;
  int64_t timestamp = 0;
  if (!buffer->end_of_stream()) {
    data = buffer->data();
    size = buffer->data_size();
    timestamp = encrypted_buffer.timestamp;
  }

  return audio_decoder_->DecodeBuffer(data, size, timestamp, audio_frames);
#elif defined(CLEAR_KEY_CDM_USE_FAKE_AUDIO_DECODER)
  int64_t timestamp_in_microseconds = kNoTimestamp;
  if (!buffer->end_of_stream()) {
    timestamp_in_microseconds = buffer->GetTimestamp().InMicroseconds();
    DCHECK(timestamp_in_microseconds != kNoTimestamp);
  }
  return GenerateFakeAudioFrames(timestamp_in_microseconds, audio_frames);
#else
  return cdm::kSuccess;
#endif  // CLEAR_KEY_CDM_USE_FAKE_AUDIO_DECODER
}

void ClearKeyCdm::Destroy() {
  DVLOG(1) << "Destroy()";
  delete this;
}

void ClearKeyCdm::ScheduleNextRenewal() {
  // Prepare the next renewal message and set timer.
  std::ostringstream msg_stream;
  msg_stream << "Renewal from ClearKey CDM set at time "
             << host_->GetCurrentWallTime() << ".";
  next_renewal_message_ = msg_stream.str();

  host_->SetTimer(timer_delay_ms_, &next_renewal_message_[0]);

  // Use a smaller timer delay at start-up to facilitate testing. Increase the
  // timer delay up to a limit to avoid message spam.
  if (timer_delay_ms_ < kMaxTimerDelayMs)
    timer_delay_ms_ = std::min(2 * timer_delay_ms_, kMaxTimerDelayMs);
}

cdm::Status ClearKeyCdm::DecryptToMediaDecoderBuffer(
    const cdm::InputBuffer& encrypted_buffer,
    scoped_refptr<media::DecoderBuffer>* decrypted_buffer) {
  DCHECK(decrypted_buffer);
  scoped_refptr<media::DecoderBuffer> buffer =
      CopyDecoderBufferFrom(encrypted_buffer);

  // TODO(xhwang): Unify how to check whether a buffer is encrypted.
  // See http://crbug.com/675003
  if (buffer->end_of_stream() || !buffer->decrypt_config() ||
      !buffer->decrypt_config()->is_encrypted()) {
    *decrypted_buffer = buffer;
    return cdm::kSuccess;
  }

  // Callback is called synchronously, so we can use variables on the stack.
  media::Decryptor::Status status = media::Decryptor::kError;
  // The AesDecryptor does not care what the stream type is. Pass kVideo
  // for both audio and video decryption.
  decryptor_->Decrypt(
      media::Decryptor::kVideo, buffer,
      base::Bind(&CopyDecryptResults, &status, decrypted_buffer));

  if (status == media::Decryptor::kError)
    return cdm::kDecryptError;

  if (status == media::Decryptor::kNoKey)
    return cdm::kNoKey;

  DCHECK_EQ(status, media::Decryptor::kSuccess);
  return cdm::kSuccess;
}

void ClearKeyCdm::OnPlatformChallengeResponse(
    const cdm::PlatformChallengeResponse& response) {
  DVLOG(1) << __func__;

  if (!is_running_platform_verification_test_) {
    NOTREACHED() << "OnPlatformChallengeResponse() called unexpectedly.";
    return;
  }

  is_running_platform_verification_test_ = false;

  // We are good as long as we get some response back. Ignore the challenge
  // response for now.
  // TODO(xhwang): Also test host challenge here.
  OnUnitTestComplete(true);
}

void ClearKeyCdm::OnQueryOutputProtectionStatus(
    cdm::QueryResult result,
    uint32_t link_mask,
    uint32_t output_protection_mask) {
  DVLOG(1) << __func__;

  if (!is_running_output_protection_test_) {
    NOTREACHED() << "OnQueryOutputProtectionStatus() called unexpectedly.";
    return;
  }

  is_running_output_protection_test_ = false;

// On Chrome OS, status query will fail on Linux Chrome OS build. So we ignore
// the query result. On all other platforms, status query should succeed.
// TODO(xhwang): Improve the check on Chrome OS builds. For example, use
// base::SysInfo::IsRunningOnChromeOS() to differentiate between real Chrome OS
// build and Linux Chrome OS build.
#if !defined(OS_CHROMEOS)
  if (result != cdm::kQuerySucceeded || link_mask != 0) {
    OnUnitTestComplete(false);
    return;
  }
#endif
  OnUnitTestComplete(true);
};

void ClearKeyCdm::LoadLoadableSession() {
  std::string jwk_set = GenerateJWKSet(kLoadableSessionKey,
                                       sizeof(kLoadableSessionKey),
                                       kLoadableSessionKeyId,
                                       sizeof(kLoadableSessionKeyId) - 1);
  std::unique_ptr<media::SimpleCdmPromise> promise(
      new media::CdmCallbackPromise<>(
          base::Bind(&ClearKeyCdm::OnLoadSessionUpdated,
                     base::Unretained(this)),
          base::Bind(&ClearKeyCdm::OnPromiseFailed, base::Unretained(this),
                     promise_id_for_emulated_loadsession_)));
  decryptor_->UpdateSession(
      session_id_for_emulated_loadsession_,
      std::vector<uint8_t>(jwk_set.begin(), jwk_set.end()), std::move(promise));
}

void ClearKeyCdm::OnSessionMessage(const std::string& session_id,
                                   CdmMessageType message_type,
                                   const std::vector<uint8_t>& message) {
  DVLOG(1) << "OnSessionMessage: " << message.size();

  // Ignore the message when we are waiting to update the loadable session.
  if (session_id == session_id_for_emulated_loadsession_)
    return;

  // OnSessionMessage() only called during CreateSession(), so no promise
  // involved (OnSessionCreated() called to resolve the CreateSession()
  // promise).
  host_->OnSessionMessage(session_id.data(), session_id.length(),
                          cdm::kLicenseRequest,
                          reinterpret_cast<const char*>(message.data()),
                          message.size(), nullptr, 0);
}

void ClearKeyCdm::OnSessionKeysChange(const std::string& session_id,
                                      bool has_additional_usable_key,
                                      CdmKeysInfo keys_info) {
  DVLOG(1) << "OnSessionKeysChange: " << keys_info.size();

  std::string new_session_id = session_id;
  if (new_session_id == session_id_for_emulated_loadsession_) {
    // Save |keys_info| if the loadable session is still being created. This
    // event will then be forwarded on in OnLoadSessionUpdated().
    if (promise_id_for_emulated_loadsession_ != 0) {
      has_received_keys_change_event_for_emulated_loadsession_ = true;
      keys_info_for_emulated_loadsession_.swap(keys_info);
      return;
    }

    // Loadable session has already been created, so pass this event on,
    // using the session_id callers expect to see.
    new_session_id = std::string(kLoadableSessionId);
  }

  std::vector<cdm::KeyInformation> keys_vector;
  ConvertCdmKeysInfo(keys_info.get(), &keys_vector);
  host_->OnSessionKeysChange(new_session_id.data(), new_session_id.length(),
                             has_additional_usable_key, keys_vector.data(),
                             keys_vector.size());
}

void ClearKeyCdm::OnSessionClosed(const std::string& session_id) {
  std::string new_session_id = session_id;
  if (new_session_id == session_id_for_emulated_loadsession_)
    new_session_id = std::string(kLoadableSessionId);
  host_->OnSessionClosed(new_session_id.data(), new_session_id.length());
}

void ClearKeyCdm::OnSessionExpirationUpdate(const std::string& session_id,
                                            base::Time new_expiry_time) {
  std::string new_session_id = session_id;
  if (new_session_id == session_id_for_emulated_loadsession_)
    new_session_id = std::string(kLoadableSessionId);
  host_->OnExpirationChange(new_session_id.data(), new_session_id.length(),
                            new_expiry_time.ToDoubleT());
}

void ClearKeyCdm::OnSessionCreated(uint32_t promise_id,
                                   const std::string& session_id) {
  // Save the latest session ID for renewal and file IO test messages.
  last_session_id_ = session_id;

  host_->OnResolveNewSessionPromise(promise_id, session_id.data(),
                                    session_id.length());
}

void ClearKeyCdm::OnSessionLoaded(uint32_t promise_id,
                                  const std::string& session_id) {
  // Save the latest session ID for renewal and file IO test messages.
  last_session_id_ = session_id;

  // |decryptor_| created some session as |session_id|, but going forward
  // we need to map that to |kLoadableSessionId|, as that is what callers
  // expect.
  session_id_for_emulated_loadsession_ = session_id;

  // Delay LoadLoadableSession() to test the case where Decrypt*() calls are
  // made before the session is fully loaded.
  const int64_t kDelayToLoadSessionMs = 500;

  // Defer resolving the promise until the session is loaded.
  promise_id_for_emulated_loadsession_ = promise_id;

  // Use the address of |session_id_for_emulated_loadsession_| as the timer
  // context so that we can call LoadLoadableSession() when the timer expires.
  host_->SetTimer(kDelayToLoadSessionMs, &session_id_for_emulated_loadsession_);
}

void ClearKeyCdm::OnLoadSessionUpdated() {
  // This method is only called to finish loading sessions, so handle
  // appropriately.

  // |promise_id_for_emulated_loadsession_| is the LoadSession() promise,
  // so resolve appropriately.
  host_->OnResolveNewSessionPromise(promise_id_for_emulated_loadsession_,
                                    kLoadableSessionId,
                                    strlen(kLoadableSessionId));
  promise_id_for_emulated_loadsession_ = 0;

  // Generate the KeysChange event now that the session is "loaded" if one
  // was seen.
  // TODO(jrummell): Once the order of events is fixed in the spec, either
  // require the keyschange event to have happened, or remove this code.
  // http://crbug.com/448225
  if (has_received_keys_change_event_for_emulated_loadsession_) {
    std::vector<cdm::KeyInformation> keys_vector;
    CdmKeysInfo keys_info;
    keys_info.swap(keys_info_for_emulated_loadsession_);
    has_received_keys_change_event_for_emulated_loadsession_ = false;
    DCHECK(!keys_vector.empty());
    ConvertCdmKeysInfo(keys_info.get(), &keys_vector);
    host_->OnSessionKeysChange(kLoadableSessionId, strlen(kLoadableSessionId),
                               !keys_vector.empty(), keys_vector.data(),
                               keys_vector.size());
  }
}

void ClearKeyCdm::OnPromiseResolved(uint32_t promise_id) {
  host_->OnResolvePromise(promise_id);
}

void ClearKeyCdm::OnPromiseFailed(uint32_t promise_id,
                                  CdmPromise::Exception exception_code,
                                  uint32_t system_code,
                                  const std::string& error_message) {
  host_->OnRejectPromise(promise_id,
                         ConvertException(exception_code),
                         system_code,
                         error_message.data(),
                         error_message.length());
}

#if defined(CLEAR_KEY_CDM_USE_FAKE_AUDIO_DECODER)
int64_t ClearKeyCdm::CurrentTimeStampInMicroseconds() const {
  return output_timestamp_base_in_microseconds_ +
         base::Time::kMicrosecondsPerSecond *
         total_samples_generated_  / samples_per_second_;
}

int ClearKeyCdm::GenerateFakeAudioFramesFromDuration(
    int64_t duration_in_microseconds,
    cdm::AudioFrames* audio_frames) const {
  int64_t samples_to_generate = static_cast<double>(samples_per_second_) *
                                    duration_in_microseconds /
                                    base::Time::kMicrosecondsPerSecond +
                                0.5;
  if (samples_to_generate <= 0)
    return 0;

  int64_t bytes_per_sample = channel_count_ * bits_per_channel_ / 8;
  // |frame_size| must be a multiple of |bytes_per_sample|.
  int64_t frame_size = bytes_per_sample * samples_to_generate;

  int64_t timestamp = CurrentTimeStampInMicroseconds();

  const int kHeaderSize = sizeof(timestamp) + sizeof(frame_size);
  audio_frames->SetFrameBuffer(host_->Allocate(kHeaderSize + frame_size));
  uint8_t* data = audio_frames->FrameBuffer()->Data();

  memcpy(data, &timestamp, sizeof(timestamp));
  data += sizeof(timestamp);
  memcpy(data, &frame_size, sizeof(frame_size));
  data += sizeof(frame_size);
  // You won't hear anything because we have all zeros here. But the video
  // should play just fine!
  memset(data, 0, frame_size);

  audio_frames->FrameBuffer()->SetSize(kHeaderSize + frame_size);

  return samples_to_generate;
}

cdm::Status ClearKeyCdm::GenerateFakeAudioFrames(
    int64_t timestamp_in_microseconds,
    cdm::AudioFrames* audio_frames) {
  if (timestamp_in_microseconds == kNoTimestamp)
    return cdm::kNeedMoreData;

  // Return kNeedMoreData for the first frame because duration is unknown.
  if (output_timestamp_base_in_microseconds_ == kNoTimestamp) {
    output_timestamp_base_in_microseconds_ = timestamp_in_microseconds;
    return cdm::kNeedMoreData;
  }

  int samples_generated = GenerateFakeAudioFramesFromDuration(
      timestamp_in_microseconds - CurrentTimeStampInMicroseconds(),
      audio_frames);
  total_samples_generated_ += samples_generated;

  return samples_generated == 0 ? cdm::kNeedMoreData : cdm::kSuccess;
}
#endif  // CLEAR_KEY_CDM_USE_FAKE_AUDIO_DECODER

void ClearKeyCdm::OnUnitTestComplete(bool success) {
  std::string message = GetUnitTestResultMessage(success);
  host_->OnSessionMessage(last_session_id_.data(), last_session_id_.length(),
                          cdm::kLicenseRequest, message.data(),
                          message.length(), nullptr, 0);
}

void ClearKeyCdm::StartFileIOTest() {
  file_io_test_runner_.reset(new FileIOTestRunner(
      base::Bind(&ClearKeyCdmHost::CreateFileIO, base::Unretained(host_))));
  file_io_test_runner_->RunAllTests(
      base::Bind(&ClearKeyCdm::OnFileIOTestComplete, base::Unretained(this)));
}

void ClearKeyCdm::OnFileIOTestComplete(bool success) {
  DVLOG(1) << __func__ << ": " << success;
  OnUnitTestComplete(success);
  file_io_test_runner_.reset();
}

void ClearKeyCdm::StartOutputProtectionTest() {
  DVLOG(1) << __func__;
  is_running_output_protection_test_ = true;
  host_->QueryOutputProtectionStatus();
}

void ClearKeyCdm::StartPlatformVerificationTest() {
  DVLOG(1) << __func__;
  is_running_platform_verification_test_ = true;

  std::string service_id = "test_service_id";
  std::string challenge = "test_challenge";

  host_->SendPlatformChallenge(service_id.data(), service_id.size(),
                               challenge.data(), challenge.size());
}

void ClearKeyCdm::VerifyCdmHostTest() {
  // VerifyCdmHost() should have already been called and test result stored
  // in |g_verify_host_files_result|.
  OnUnitTestComplete(g_verify_host_files_result);
}

}  // namespace media
