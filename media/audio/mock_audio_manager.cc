// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/audio/mock_audio_manager.h"

#include "base/callback.h"
#include "base/logging.h"
#include "base/single_thread_task_runner.h"
#include "media/base/audio_parameters.h"

namespace media {

MockAudioManager::MockAudioManager(std::unique_ptr<AudioThread> audio_thread)
    : AudioManager(std::move(audio_thread)) {}

MockAudioManager::~MockAudioManager() {
}

void MockAudioManager::ShutdownOnAudioThread() {}

bool MockAudioManager::HasAudioOutputDevices() {
  DCHECK(GetTaskRunner()->BelongsToCurrentThread());
  return has_output_devices_;
}

bool MockAudioManager::HasAudioInputDevices() {
  DCHECK(GetTaskRunner()->BelongsToCurrentThread());
  return has_input_devices_;
}

base::string16 MockAudioManager::GetAudioInputDeviceModel() {
  DCHECK(GetTaskRunner()->BelongsToCurrentThread());
  return base::string16();
}

void MockAudioManager::ShowAudioInputSettings() {
}

void MockAudioManager::GetAudioInputDeviceDescriptions(
    AudioDeviceDescriptions* device_descriptions) {
  DCHECK(GetTaskRunner()->BelongsToCurrentThread());
  if (get_input_device_descriptions_cb_.is_null())
    return;
  get_input_device_descriptions_cb_.Run(device_descriptions);
}

void MockAudioManager::GetAudioOutputDeviceDescriptions(
    AudioDeviceDescriptions* device_descriptions) {
  DCHECK(GetTaskRunner()->BelongsToCurrentThread());
  if (get_output_device_descriptions_cb_.is_null())
    return;
  get_output_device_descriptions_cb_.Run(device_descriptions);
}

media::AudioOutputStream* MockAudioManager::MakeAudioOutputStream(
    const media::AudioParameters& params,
    const std::string& device_id,
    const LogCallback& log_callback) {
  NOTREACHED();
  return NULL;
}

media::AudioOutputStream* MockAudioManager::MakeAudioOutputStreamProxy(
    const media::AudioParameters& params,
    const std::string& device_id) {
  NOTREACHED();
  return NULL;
}

media::AudioInputStream* MockAudioManager::MakeAudioInputStream(
    const media::AudioParameters& params,
    const std::string& device_id,
    const LogCallback& log_callback) {
  NOTREACHED();
  return NULL;
}

void MockAudioManager::AddOutputDeviceChangeListener(
    AudioDeviceListener* listener) {
}

void MockAudioManager::RemoveOutputDeviceChangeListener(
    AudioDeviceListener* listener) {
}

AudioParameters MockAudioManager::GetDefaultOutputStreamParameters() {
  return default_output_params_;
}

AudioParameters MockAudioManager::GetOutputStreamParameters(
      const std::string& device_id) {
  DCHECK(GetTaskRunner()->BelongsToCurrentThread());
  return output_params_;
}

AudioParameters MockAudioManager::GetInputStreamParameters(
    const std::string& device_id) {
  DCHECK(GetTaskRunner()->BelongsToCurrentThread());
  return input_params_;
}

std::string MockAudioManager::GetAssociatedOutputDeviceID(
    const std::string& input_device_id) {
  DCHECK(GetTaskRunner()->BelongsToCurrentThread());
  return get_associated_output_device_id_cb_.is_null()
             ? std::string()
             : get_associated_output_device_id_cb_.Run(input_device_id);
}

std::unique_ptr<AudioLog> MockAudioManager::CreateAudioLog(
    AudioLogFactory::AudioComponent component) {
  return nullptr;
}

void MockAudioManager::InitializeOutputDebugRecording(
    scoped_refptr<base::SingleThreadTaskRunner> file_task_runner) {}

void MockAudioManager::EnableOutputDebugRecording(
    const base::FilePath& base_file_name) {}

void MockAudioManager::DisableOutputDebugRecording() {}

const char* MockAudioManager::GetName() {
  return nullptr;
}

void MockAudioManager::SetInputStreamParameters(const AudioParameters& params) {
  input_params_ = params;
}

void MockAudioManager::SetOutputStreamParameters(
    const AudioParameters& params) {
  output_params_ = params;
}

void MockAudioManager::SetDefaultOutputStreamParameters(
    const AudioParameters& params) {
  default_output_params_ = params;
}

void MockAudioManager::SetHasInputDevices(bool has_input_devices) {
  has_input_devices_ = has_input_devices;
}

void MockAudioManager::SetHasOutputDevices(bool has_output_devices) {
  has_output_devices_ = has_output_devices;
}

void MockAudioManager::SetInputDeviceDescriptionsCallback(
    GetDeviceDescriptionsCallback callback) {
  get_input_device_descriptions_cb_ = std::move(callback);
}

void MockAudioManager::SetOutputDeviceDescriptionsCallback(
    GetDeviceDescriptionsCallback callback) {
  get_output_device_descriptions_cb_ = std::move(callback);
}

void MockAudioManager::SetAssociatedOutputDeviceIDCallback(
    GetAssociatedOutputDeviceIDCallback callback) {
  get_associated_output_device_id_cb_ = std::move(callback);
}

}  // namespace media.
