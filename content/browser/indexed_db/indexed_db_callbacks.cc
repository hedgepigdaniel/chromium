// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/indexed_db/indexed_db_callbacks.h"

#include <stddef.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/fileapi/fileapi_message_filter.h"
#include "content/browser/indexed_db/cursor_impl.h"
#include "content/browser/indexed_db/database_impl.h"
#include "content/browser/indexed_db/indexed_db_connection.h"
#include "content/browser/indexed_db/indexed_db_context_impl.h"
#include "content/browser/indexed_db/indexed_db_cursor.h"
#include "content/browser/indexed_db/indexed_db_database_error.h"
#include "content/browser/indexed_db/indexed_db_return_value.h"
#include "content/browser/indexed_db/indexed_db_tracing.h"
#include "content/browser/indexed_db/indexed_db_transaction.h"
#include "content/browser/indexed_db/indexed_db_value.h"
#include "content/common/indexed_db/indexed_db_constants.h"
#include "content/common/indexed_db/indexed_db_metadata.h"
#include "content/public/browser/browser_thread.h"
#include "mojo/public/cpp/bindings/strong_associated_binding.h"
#include "storage/browser/blob/blob_storage_context.h"
#include "storage/browser/blob/shareable_file_reference.h"
#include "storage/browser/quota/quota_manager.h"

using indexed_db::mojom::CallbacksAssociatedPtrInfo;
using std::swap;
using storage::ShareableFileReference;

namespace content {

namespace {

// If this is destructed with the connection still living inside it, we assume
// we have been killed due to IO thread shutdown, and we need to safely schedule
// the destruction of the connection on the IDB thread, as we should still be in
// a transaction's operation queue, where we cannot destroy the transaction.
struct SafeIOThreadConnectionWrapper {
  SafeIOThreadConnectionWrapper(std::unique_ptr<IndexedDBConnection> connection)
      : connection(std::move(connection)),
        idb_runner(base::ThreadTaskRunnerHandle::Get()) {}
  ~SafeIOThreadConnectionWrapper() {
    if (connection) {
      idb_runner->PostTask(
          FROM_HERE, base::Bind(
                         [](std::unique_ptr<IndexedDBConnection> connection) {
                           connection->ForceClose();
                         },
                         base::Passed(&connection)));
    }
  }
  SafeIOThreadConnectionWrapper(SafeIOThreadConnectionWrapper&& other) =
      default;

  std::unique_ptr<IndexedDBConnection> connection;
  scoped_refptr<base::SequencedTaskRunner> idb_runner;

 private:
  DISALLOW_COPY_AND_ASSIGN(SafeIOThreadConnectionWrapper);
};

void ConvertBlobInfo(
    const std::vector<IndexedDBBlobInfo>& blob_info,
    std::vector<::indexed_db::mojom::BlobInfoPtr>* blob_or_file_info) {
  blob_or_file_info->reserve(blob_info.size());
  for (const auto& iter : blob_info) {
    if (!iter.mark_used_callback().is_null())
      iter.mark_used_callback().Run();

    auto info = ::indexed_db::mojom::BlobInfo::New();
    info->mime_type = iter.type();
    info->size = iter.size();
    if (iter.is_file()) {
      info->file = ::indexed_db::mojom::FileInfo::New();
      info->file->name = iter.file_name();
      info->file->path = iter.file_path();
      info->file->last_modified = iter.last_modified();
    }
    blob_or_file_info->push_back(std::move(info));
  }
}

// Destructively converts an IndexedDBReturnValue to a Mojo ReturnValue.
::indexed_db::mojom::ReturnValuePtr ConvertReturnValue(
    IndexedDBReturnValue* value) {
  auto mojo_value = ::indexed_db::mojom::ReturnValue::New();
  mojo_value->value = ::indexed_db::mojom::Value::New();
  if (value->primary_key.IsValid()) {
    mojo_value->primary_key = value->primary_key;
    mojo_value->key_path = value->key_path;
  }
  if (!value->empty())
    swap(mojo_value->value->bits, value->bits);
  ConvertBlobInfo(value->blob_info, &mojo_value->value->blob_or_file_info);
  return mojo_value;
}

}  // namespace

// Expected to be created and called from IO thread.
class IndexedDBCallbacks::IOThreadHelper {
 public:
  IOThreadHelper(CallbacksAssociatedPtrInfo callbacks_info,
                 base::WeakPtr<IndexedDBDispatcherHost> dispatcher_host,
                 url::Origin origin,
                 scoped_refptr<base::SequencedTaskRunner> idb_runner);
  ~IOThreadHelper();

  void SendError(const IndexedDBDatabaseError& error);
  void SendSuccessStringList(const std::vector<base::string16>& value);
  void SendBlocked(int64_t existing_version);
  void SendUpgradeNeeded(SafeIOThreadConnectionWrapper connection,
                         int64_t old_version,
                         blink::WebIDBDataLoss data_loss,
                         const std::string& data_loss_message,
                         const content::IndexedDBDatabaseMetadata& metadata);
  void SendSuccessDatabase(SafeIOThreadConnectionWrapper connection,
                           const content::IndexedDBDatabaseMetadata& metadata);
  void SendSuccessCursor(std::unique_ptr<IndexedDBCursor> cursor,
                         const IndexedDBKey& key,
                         const IndexedDBKey& primary_key,
                         ::indexed_db::mojom::ValuePtr value,
                         const std::vector<IndexedDBBlobInfo>& blob_info);
  void SendSuccessValue(::indexed_db::mojom::ReturnValuePtr value,
                        const std::vector<IndexedDBBlobInfo>& blob_info);
  void SendSuccessCursorContinue(
      const IndexedDBKey& key,
      const IndexedDBKey& primary_key,
      ::indexed_db::mojom::ValuePtr value,
      const std::vector<IndexedDBBlobInfo>& blob_info);
  void SendSuccessCursorPrefetch(
      const std::vector<IndexedDBKey>& keys,
      const std::vector<IndexedDBKey>& primary_keys,
      std::vector<::indexed_db::mojom::ValuePtr> mojo_values,
      const std::vector<IndexedDBValue>& values);
  void SendSuccessArray(
      std::vector<::indexed_db::mojom::ReturnValuePtr> mojo_values,
      const std::vector<IndexedDBReturnValue>& values);
  void SendSuccessKey(const IndexedDBKey& value);
  void SendSuccessInteger(int64_t value);
  void SendSuccess();

  std::string CreateBlobData(const IndexedDBBlobInfo& blob_info);
  bool CreateAllBlobs(
      const std::vector<IndexedDBBlobInfo>& blob_info,
      std::vector<::indexed_db::mojom::BlobInfoPtr>* blob_or_file_info);
  void OnConnectionError();

 private:
  base::WeakPtr<IndexedDBDispatcherHost> dispatcher_host_;
  ::indexed_db::mojom::CallbacksAssociatedPtr callbacks_;
  url::Origin origin_;
  scoped_refptr<base::SequencedTaskRunner> idb_runner_;

  DISALLOW_COPY_AND_ASSIGN(IOThreadHelper);
};

// static
::indexed_db::mojom::ValuePtr IndexedDBCallbacks::ConvertAndEraseValue(
    IndexedDBValue* value) {
  auto mojo_value = ::indexed_db::mojom::Value::New();
  if (!value->empty())
    swap(mojo_value->bits, value->bits);
  ConvertBlobInfo(value->blob_info, &mojo_value->blob_or_file_info);
  return mojo_value;
}

IndexedDBCallbacks::IndexedDBCallbacks(
    base::WeakPtr<IndexedDBDispatcherHost> dispatcher_host,
    const url::Origin& origin,
    ::indexed_db::mojom::CallbacksAssociatedPtrInfo callbacks_info,
    scoped_refptr<base::SequencedTaskRunner> idb_runner)
    : data_loss_(blink::kWebIDBDataLossNone),
      io_helper_(new IOThreadHelper(std::move(callbacks_info),
                                    std::move(dispatcher_host),
                                    origin,
                                    std::move(idb_runner))) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  thread_checker_.DetachFromThread();
}

IndexedDBCallbacks::~IndexedDBCallbacks() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void IndexedDBCallbacks::OnError(const IndexedDBDatabaseError& error) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendError, base::Unretained(io_helper_.get()),
                 error));
  complete_ = true;

  if (!connection_open_start_time_.is_null()) {
    UMA_HISTOGRAM_MEDIUM_TIMES(
        "WebCore.IndexedDB.OpenTime.Error",
        base::TimeTicks::Now() - connection_open_start_time_);
    connection_open_start_time_ = base::TimeTicks();
  }
}

void IndexedDBCallbacks::OnSuccess(const std::vector<base::string16>& value) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);
  DCHECK(io_helper_);

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendSuccessStringList,
                 base::Unretained(io_helper_.get()), value));
  complete_ = true;
}

void IndexedDBCallbacks::OnBlocked(int64_t existing_version) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);
  DCHECK(io_helper_);

  if (sent_blocked_)
    return;

  sent_blocked_ = true;

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendBlocked,
                 base::Unretained(io_helper_.get()), existing_version));

  if (!connection_open_start_time_.is_null()) {
    UMA_HISTOGRAM_MEDIUM_TIMES(
        "WebCore.IndexedDB.OpenTime.Blocked",
        base::TimeTicks::Now() - connection_open_start_time_);
    connection_open_start_time_ = base::TimeTicks();
  }
}

void IndexedDBCallbacks::OnUpgradeNeeded(
    int64_t old_version,
    std::unique_ptr<IndexedDBConnection> connection,
    const IndexedDBDatabaseMetadata& metadata,
    const IndexedDBDataLossInfo& data_loss_info) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);
  DCHECK(io_helper_);

  DCHECK(!connection_created_);

  data_loss_ = data_loss_info.status;
  connection_created_ = true;

  SafeIOThreadConnectionWrapper wrapper(std::move(connection));
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendUpgradeNeeded,
                 base::Unretained(io_helper_.get()), base::Passed(&wrapper),
                 old_version, data_loss_info.status, data_loss_info.message,
                 metadata));

  if (!connection_open_start_time_.is_null()) {
    UMA_HISTOGRAM_MEDIUM_TIMES(
        "WebCore.IndexedDB.OpenTime.UpgradeNeeded",
        base::TimeTicks::Now() - connection_open_start_time_);
    connection_open_start_time_ = base::TimeTicks();
  }
}

void IndexedDBCallbacks::OnSuccess(
    std::unique_ptr<IndexedDBConnection> connection,
    const IndexedDBDatabaseMetadata& metadata) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);
  DCHECK(io_helper_);

  DCHECK_EQ(connection_created_, !connection);

  scoped_refptr<IndexedDBCallbacks> self(this);

  // Only create a new connection if one was not previously sent in
  // OnUpgradeNeeded.
  std::unique_ptr<IndexedDBConnection> database_connection;
  if (!connection_created_)
    database_connection = std::move(connection);

  SafeIOThreadConnectionWrapper wrapper(std::move(database_connection));
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                          base::Bind(&IOThreadHelper::SendSuccessDatabase,
                                     base::Unretained(io_helper_.get()),
                                     base::Passed(&wrapper), metadata));
  complete_ = true;

  if (!connection_open_start_time_.is_null()) {
    UMA_HISTOGRAM_MEDIUM_TIMES(
        "WebCore.IndexedDB.OpenTime.Success",
        base::TimeTicks::Now() - connection_open_start_time_);
    connection_open_start_time_ = base::TimeTicks();
  }
}

void IndexedDBCallbacks::OnSuccess(std::unique_ptr<IndexedDBCursor> cursor,
                                   const IndexedDBKey& key,
                                   const IndexedDBKey& primary_key,
                                   IndexedDBValue* value) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);
  DCHECK(io_helper_);

  DCHECK_EQ(blink::kWebIDBDataLossNone, data_loss_);

  ::indexed_db::mojom::ValuePtr mojo_value;
  std::vector<IndexedDBBlobInfo> blob_info;
  if (value) {
    mojo_value = ConvertAndEraseValue(value);
    blob_info.swap(value->blob_info);
  }

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendSuccessCursor,
                 base::Unretained(io_helper_.get()), base::Passed(&cursor), key,
                 primary_key, base::Passed(&mojo_value),
                 base::Passed(&blob_info)));
  complete_ = true;
}

void IndexedDBCallbacks::OnSuccess(const IndexedDBKey& key,
                                   const IndexedDBKey& primary_key,
                                   IndexedDBValue* value) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);
  DCHECK(io_helper_);

  DCHECK_EQ(blink::kWebIDBDataLossNone, data_loss_);

  ::indexed_db::mojom::ValuePtr mojo_value;
  std::vector<IndexedDBBlobInfo> blob_info;
  if (value) {
    mojo_value = ConvertAndEraseValue(value);
    blob_info.swap(value->blob_info);
  }

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendSuccessCursorContinue,
                 base::Unretained(io_helper_.get()), key, primary_key,
                 base::Passed(&mojo_value), base::Passed(&blob_info)));
  complete_ = true;
}

void IndexedDBCallbacks::OnSuccessWithPrefetch(
    const std::vector<IndexedDBKey>& keys,
    const std::vector<IndexedDBKey>& primary_keys,
    std::vector<IndexedDBValue>* values) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);
  DCHECK(io_helper_);
  DCHECK_EQ(keys.size(), primary_keys.size());
  DCHECK_EQ(keys.size(), values->size());

  DCHECK_EQ(blink::kWebIDBDataLossNone, data_loss_);

  std::vector<::indexed_db::mojom::ValuePtr> mojo_values;
  mojo_values.reserve(values->size());
  for (size_t i = 0; i < values->size(); ++i)
    mojo_values.push_back(ConvertAndEraseValue(&(*values)[i]));

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendSuccessCursorPrefetch,
                 base::Unretained(io_helper_.get()), keys, primary_keys,
                 base::Passed(&mojo_values), *values));
  complete_ = true;
}

void IndexedDBCallbacks::OnSuccess(IndexedDBReturnValue* value) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);

  DCHECK_EQ(blink::kWebIDBDataLossNone, data_loss_);

  ::indexed_db::mojom::ReturnValuePtr mojo_value;
  std::vector<IndexedDBBlobInfo> blob_info;
  if (value) {
    mojo_value = ConvertReturnValue(value);
    blob_info = value->blob_info;
  }

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendSuccessValue,
                 base::Unretained(io_helper_.get()), base::Passed(&mojo_value),
                 base::Passed(&blob_info)));
  complete_ = true;
}

void IndexedDBCallbacks::OnSuccessArray(
    std::vector<IndexedDBReturnValue>* values) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);
  DCHECK(io_helper_);

  DCHECK_EQ(blink::kWebIDBDataLossNone, data_loss_);

  std::vector<::indexed_db::mojom::ReturnValuePtr> mojo_values;
  mojo_values.reserve(values->size());
  for (size_t i = 0; i < values->size(); ++i)
    mojo_values.push_back(ConvertReturnValue(&(*values)[i]));

  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                          base::Bind(&IOThreadHelper::SendSuccessArray,
                                     base::Unretained(io_helper_.get()),
                                     base::Passed(&mojo_values), *values));
  complete_ = true;
}

void IndexedDBCallbacks::OnSuccess(const IndexedDBKey& value) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);
  DCHECK(io_helper_);

  DCHECK_EQ(blink::kWebIDBDataLossNone, data_loss_);

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendSuccessKey,
                 base::Unretained(io_helper_.get()), value));
  complete_ = true;
}

void IndexedDBCallbacks::OnSuccess(int64_t value) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendSuccessInteger,
                 base::Unretained(io_helper_.get()), value));
  complete_ = true;
}

void IndexedDBCallbacks::OnSuccess() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!complete_);
  DCHECK(io_helper_);

  DCHECK_EQ(blink::kWebIDBDataLossNone, data_loss_);

  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                          base::Bind(&IOThreadHelper::SendSuccess,
                                     base::Unretained(io_helper_.get())));
  complete_ = true;
}

void IndexedDBCallbacks::SetConnectionOpenStartTime(
    const base::TimeTicks& start_time) {
  connection_open_start_time_ = start_time;
}

IndexedDBCallbacks::IOThreadHelper::IOThreadHelper(
    CallbacksAssociatedPtrInfo callbacks_info,
    base::WeakPtr<IndexedDBDispatcherHost> dispatcher_host,
    url::Origin origin,
    scoped_refptr<base::SequencedTaskRunner> idb_runner)
    : dispatcher_host_(std::move(dispatcher_host)),
      origin_(origin),
      idb_runner_(idb_runner) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (callbacks_info.is_valid()) {
    callbacks_.Bind(std::move(callbacks_info));
    callbacks_.set_connection_error_handler(
        base::Bind(&IOThreadHelper::OnConnectionError, base::Unretained(this)));
  }
}

IndexedDBCallbacks::IOThreadHelper::~IOThreadHelper() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
}

void IndexedDBCallbacks::IOThreadHelper::SendError(
    const IndexedDBDatabaseError& error) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!callbacks_)
    return;
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }
  callbacks_->Error(error.code(), error.message());
}

void IndexedDBCallbacks::IOThreadHelper::SendSuccessStringList(
    const std::vector<base::string16>& value) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!callbacks_)
    return;
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }
  callbacks_->SuccessStringList(value);
}

void IndexedDBCallbacks::IOThreadHelper::SendBlocked(int64_t existing_version) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }
  if (callbacks_)
    callbacks_->Blocked(existing_version);
}

void IndexedDBCallbacks::IOThreadHelper::SendUpgradeNeeded(
    SafeIOThreadConnectionWrapper connection_wrapper,
    int64_t old_version,
    blink::WebIDBDataLoss data_loss,
    const std::string& data_loss_message,
    const content::IndexedDBDatabaseMetadata& metadata) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!callbacks_)
    return;
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }

  auto database = base::MakeUnique<DatabaseImpl>(
      std::move(connection_wrapper.connection), origin_, dispatcher_host_.get(),
      idb_runner_);

  ::indexed_db::mojom::DatabaseAssociatedPtrInfo ptr_info;
  auto request = mojo::MakeRequest(&ptr_info);

  dispatcher_host_->AddDatabaseBinding(std::move(database), std::move(request));
  callbacks_->UpgradeNeeded(std::move(ptr_info), old_version, data_loss,
                            data_loss_message, metadata);
}

void IndexedDBCallbacks::IOThreadHelper::SendSuccessDatabase(
    SafeIOThreadConnectionWrapper connection_wrapper,
    const content::IndexedDBDatabaseMetadata& metadata) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!callbacks_)
    return;
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }
  ::indexed_db::mojom::DatabaseAssociatedPtrInfo ptr_info;
  if (connection_wrapper.connection) {
    auto database = base::MakeUnique<DatabaseImpl>(
        std::move(connection_wrapper.connection), origin_,
        dispatcher_host_.get(), idb_runner_);

    auto request = mojo::MakeRequest(&ptr_info);
    dispatcher_host_->AddDatabaseBinding(std::move(database),
                                         std::move(request));
  }
  callbacks_->SuccessDatabase(std::move(ptr_info), metadata);
}

void IndexedDBCallbacks::IOThreadHelper::SendSuccessCursor(
    std::unique_ptr<IndexedDBCursor> cursor,
    const IndexedDBKey& key,
    const IndexedDBKey& primary_key,
    ::indexed_db::mojom::ValuePtr value,
    const std::vector<IndexedDBBlobInfo>& blob_info) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!callbacks_)
    return;
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }
  auto cursor_impl = base::MakeUnique<CursorImpl>(
      std::move(cursor), origin_, dispatcher_host_.get(), idb_runner_);

  if (value && !CreateAllBlobs(blob_info, &value->blob_or_file_info))
    return;

  ::indexed_db::mojom::CursorAssociatedPtrInfo ptr_info;
  auto request = mojo::MakeRequest(&ptr_info);
  dispatcher_host_->AddCursorBinding(std::move(cursor_impl),
                                     std::move(request));
  callbacks_->SuccessCursor(std::move(ptr_info), key, primary_key,
                            std::move(value));
}

void IndexedDBCallbacks::IOThreadHelper::SendSuccessValue(
    ::indexed_db::mojom::ReturnValuePtr value,
    const std::vector<IndexedDBBlobInfo>& blob_info) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!callbacks_)
    return;
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }

  if (!value || CreateAllBlobs(blob_info, &value->value->blob_or_file_info))
    callbacks_->SuccessValue(std::move(value));
}

void IndexedDBCallbacks::IOThreadHelper::SendSuccessArray(
    std::vector<::indexed_db::mojom::ReturnValuePtr> mojo_values,
    const std::vector<IndexedDBReturnValue>& values) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK_EQ(mojo_values.size(), values.size());

  if (!callbacks_)
    return;
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }

  for (size_t i = 0; i < mojo_values.size(); ++i) {
    if (!CreateAllBlobs(values[i].blob_info,
                        &mojo_values[i]->value->blob_or_file_info))
      return;
  }
  callbacks_->SuccessArray(std::move(mojo_values));
}

void IndexedDBCallbacks::IOThreadHelper::SendSuccessCursorContinue(
    const IndexedDBKey& key,
    const IndexedDBKey& primary_key,
    ::indexed_db::mojom::ValuePtr value,
    const std::vector<IndexedDBBlobInfo>& blob_info) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!callbacks_)
    return;
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }

  if (!value || CreateAllBlobs(blob_info, &value->blob_or_file_info))
    callbacks_->SuccessCursorContinue(key, primary_key, std::move(value));
}

void IndexedDBCallbacks::IOThreadHelper::SendSuccessCursorPrefetch(
    const std::vector<IndexedDBKey>& keys,
    const std::vector<IndexedDBKey>& primary_keys,
    std::vector<::indexed_db::mojom::ValuePtr> mojo_values,
    const std::vector<IndexedDBValue>& values) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK_EQ(mojo_values.size(), values.size());

  if (!callbacks_)
    return;
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }

  for (size_t i = 0; i < mojo_values.size(); ++i) {
    if (!CreateAllBlobs(values[i].blob_info,
                        &mojo_values[i]->blob_or_file_info)) {
      return;
    }
  }

  callbacks_->SuccessCursorPrefetch(keys, primary_keys, std::move(mojo_values));
}

void IndexedDBCallbacks::IOThreadHelper::SendSuccessKey(
    const IndexedDBKey& value) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!callbacks_)
    return;
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }
  callbacks_->SuccessKey(value);
}

void IndexedDBCallbacks::IOThreadHelper::SendSuccessInteger(int64_t value) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!callbacks_)
    return;
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }
  callbacks_->SuccessInteger(value);
}

void IndexedDBCallbacks::IOThreadHelper::SendSuccess() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!callbacks_)
    return;
  if (!dispatcher_host_) {
    OnConnectionError();
    return;
  }
  callbacks_->Success();
}

std::string IndexedDBCallbacks::IOThreadHelper::CreateBlobData(
    const IndexedDBBlobInfo& blob_info) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!blob_info.uuid().empty()) {
    // We're sending back a live blob, not a reference into our backing store.
    return dispatcher_host_->HoldBlobData(blob_info);
  }
  scoped_refptr<ShareableFileReference> shareable_file =
      ShareableFileReference::Get(blob_info.file_path());
  if (!shareable_file) {
    shareable_file = ShareableFileReference::GetOrCreate(
        blob_info.file_path(),
        ShareableFileReference::DONT_DELETE_ON_FINAL_RELEASE,
        dispatcher_host_->context()->TaskRunner());
    if (!blob_info.release_callback().is_null())
      shareable_file->AddFinalReleaseCallback(blob_info.release_callback());
  }
  return dispatcher_host_->HoldBlobData(blob_info);
}

bool IndexedDBCallbacks::IOThreadHelper::CreateAllBlobs(
    const std::vector<IndexedDBBlobInfo>& blob_info,
    std::vector<::indexed_db::mojom::BlobInfoPtr>* blob_or_file_info) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!dispatcher_host_) {
    OnConnectionError();
    return false;
  }
  IDB_TRACE("IndexedDBCallbacks::CreateAllBlobs");
  DCHECK_EQ(blob_info.size(), blob_or_file_info->size());
  if (!dispatcher_host_->blob_storage_context())
    return false;
  for (size_t i = 0; i < blob_info.size(); ++i)
    (*blob_or_file_info)[i]->uuid = CreateBlobData(blob_info[i]);
  return true;
}

void IndexedDBCallbacks::IOThreadHelper::OnConnectionError() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  callbacks_.reset();
  dispatcher_host_ = nullptr;
}

}  // namespace content
