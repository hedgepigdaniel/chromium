// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CHROMIUM_QUIC_HTTP_STREAM_H_
#define NET_QUIC_CHROMIUM_QUIC_HTTP_STREAM_H_

#include <stddef.h>
#include <stdint.h>

#include <list>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "net/base/io_buffer.h"
#include "net/base/load_timing_info.h"
#include "net/base/net_export.h"
#include "net/http/http_response_info.h"
#include "net/http/http_server_properties.h"
#include "net/log/net_log_with_source.h"
#include "net/quic/chromium/quic_chromium_client_session.h"
#include "net/quic/chromium/quic_chromium_client_stream.h"
#include "net/quic/core/quic_client_push_promise_index.h"
#include "net/quic/core/quic_packets.h"
#include "net/spdy/chromium/multiplexed_http_stream.h"

namespace net {

namespace test {
class QuicHttpStreamPeer;
}  // namespace test

// The QuicHttpStream is a QUIC-specific HttpStream subclass.  It holds a
// non-owning pointer to a QuicChromiumClientStream which it uses to
// send and receive data.
class NET_EXPORT_PRIVATE QuicHttpStream
    : public QuicChromiumClientStream::Delegate,
      public QuicClientPushPromiseIndex::Delegate,
      public MultiplexedHttpStream {
 public:
  explicit QuicHttpStream(
      std::unique_ptr<QuicChromiumClientSession::Handle> session);

  ~QuicHttpStream() override;

  // HttpStream implementation.
  int InitializeStream(const HttpRequestInfo* request_info,
                       RequestPriority priority,
                       const NetLogWithSource& net_log,
                       const CompletionCallback& callback) override;
  int SendRequest(const HttpRequestHeaders& request_headers,
                  HttpResponseInfo* response,
                  const CompletionCallback& callback) override;
  int ReadResponseHeaders(const CompletionCallback& callback) override;
  int ReadResponseBody(IOBuffer* buf,
                       int buf_len,
                       const CompletionCallback& callback) override;
  void Close(bool not_reusable) override;
  bool IsResponseBodyComplete() const override;
  bool IsConnectionReused() const override;
  int64_t GetTotalReceivedBytes() const override;
  int64_t GetTotalSentBytes() const override;
  bool GetLoadTimingInfo(LoadTimingInfo* load_timing_info) const override;
  bool GetAlternativeService(
      AlternativeService* alternative_service) const override;
  void PopulateNetErrorDetails(NetErrorDetails* details) override;
  void SetPriority(RequestPriority priority) override;

  // QuicChromiumClientStream::Delegate implementation
  void OnInitialHeadersAvailable(const SpdyHeaderBlock& headers,
                                 size_t frame_len) override;
  void OnDataAvailable() override;
  void OnTrailingHeadersAvailable(const SpdyHeaderBlock& headers,
                                  size_t frame_len) override;
  void OnClose() override;
  void OnError(int error) override;

  // QuicClientPushPromiseIndex::Delegate implementation
  bool CheckVary(const SpdyHeaderBlock& client_request,
                 const SpdyHeaderBlock& promise_request,
                 const SpdyHeaderBlock& promise_response) override;
  // TODO(rch): QuicClientPushPromiseIndex::Delegate is part of shared code.
  // Figure out how to make the QuicHttpStream receive a Handle in this
  // case instead of a QuicSpdyStream.
  void OnRendezvousResult(QuicSpdyStream* stream) override;

  static HttpResponseInfo::ConnectionInfo ConnectionInfoFromQuicVersion(
      QuicVersion quic_version);

 private:
  friend class test::QuicHttpStreamPeer;

  enum State {
    STATE_NONE,
    STATE_HANDLE_PROMISE,
    STATE_HANDLE_PROMISE_COMPLETE,
    STATE_REQUEST_STREAM,
    STATE_REQUEST_STREAM_COMPLETE,
    STATE_SET_REQUEST_PRIORITY,
    STATE_SEND_HEADERS,
    STATE_SEND_HEADERS_COMPLETE,
    STATE_READ_REQUEST_BODY,
    STATE_READ_REQUEST_BODY_COMPLETE,
    STATE_SEND_BODY,
    STATE_SEND_BODY_COMPLETE,
    STATE_OPEN,
  };

  void OnIOComplete(int rv);
  void DoCallback(int rv);

  int DoLoop(int rv);
  int DoHandlePromise();
  int DoHandlePromiseComplete(int rv);
  int DoRequestStream();
  int DoRequestStreamComplete(int rv);
  int DoSetRequestPriority();
  int DoSendHeaders();
  int DoSendHeadersComplete(int rv);
  int DoReadRequestBody();
  int DoReadRequestBodyComplete(int rv);
  int DoSendBody();
  int DoSendBodyComplete(int rv);

  int ProcessResponseHeaders(const SpdyHeaderBlock& headers);

  int ReadAvailableData(IOBuffer* buf, int buf_len);
  void EnterStateSendHeaders();

  void ResetStream();

  // If |has_response_status_| is false, sets |response_status| to the result
  // of ComputeResponseStatus(). Returns |response_status_|.
  int GetResponseStatus();
  // Sets the result of |ComputeResponseStatus()| as the |response_status_|.
  void SaveResponseStatus();
  // Sets |response_status_| to |response_status| and sets
  // |has_response_status_| to true.
  void SetResponseStatus(int response_status);
  // Computes the correct response status based on the status of the handshake,
  // |session_error|, |connection_error| and |stream_error|.
  int ComputeResponseStatus() const;

  QuicChromiumClientSession::Handle* quic_session() {
    return static_cast<QuicChromiumClientSession::Handle*>(session());
  }

  const QuicChromiumClientSession::Handle* quic_session() const {
    return static_cast<const QuicChromiumClientSession::Handle*>(session());
  }

  State next_state_;

  std::unique_ptr<QuicChromiumClientStream::Handle> stream_;

  // The following three fields are all owned by the caller and must
  // outlive this object, according to the HttpStream contract.

  // The request to send.
  // Only valid before the response body is read.
  const HttpRequestInfo* request_info_;

  // The request body to send, if any, owned by the caller.
  UploadDataStream* request_body_stream_;
  // Time the request was issued.
  base::Time request_time_;
  // The priority of the request.
  RequestPriority priority_;
  // |response_info_| is the HTTP response data object which is filled in
  // when a the response headers are read.  It is not owned by this stream.
  HttpResponseInfo* response_info_;
  bool has_response_status_;  // true if response_status_ as been set.
  // Because response data is buffered, also buffer the response status if the
  // stream is explicitly closed via OnError or OnClose with an error.
  // Once all buffered data has been returned, this will be used as the final
  // response.
  int response_status_;

  // Serialized request headers.
  SpdyHeaderBlock request_headers_;

  bool response_headers_received_;

  // Number of bytes received by the headers stream on behalf of this stream.
  int64_t headers_bytes_received_;
  // Number of bytes sent by the headers stream on behalf of this stream.
  int64_t headers_bytes_sent_;

  // Number of bytes received when the stream was closed.
  int64_t closed_stream_received_bytes_;
  // Number of bytes sent when the stream was closed.
  int64_t closed_stream_sent_bytes_;
  // True if the stream is the first stream negotiated on the session. Set when
  // the stream was closed. If |stream_| is failed to be created, this takes on
  // the default value of false.
  bool closed_is_first_stream_;

  // The caller's callback to be used for asynchronous operations.
  CompletionCallback callback_;

  // Caller provided buffer for the ReadResponseBody() response.
  scoped_refptr<IOBuffer> user_buffer_;
  int user_buffer_len_;

  // Temporary buffer used to read the request body from UploadDataStream.
  scoped_refptr<IOBufferWithSize> raw_request_body_buf_;
  // Wraps raw_request_body_buf_ to read the remaining data progressively.
  scoped_refptr<DrainableIOBuffer> request_body_buf_;

  NetLogWithSource stream_net_log_;

  int session_error_;  // Error code from the connection shutdown.
  QuicErrorCode quic_connection_error_;       // Cached connection error code.
  QuicRstStreamErrorCode quic_stream_error_;  // Cached stream error code.

  bool found_promise_;
  // |QuicClientPromisedInfo| owns this. It will be set when |Try()|
  // is asynchronous, i.e. it returned QUIC_PENDING, and remains valid
  // until |OnRendezvouResult()| fires or |push_handle_->Cancel()| is
  // invoked.
  QuicClientPushPromiseIndex::TryHandle* push_handle_;

  // Set to true when DoLoop() is being executed, false otherwise.
  bool in_loop_;

  // Session connect timing info.
  LoadTimingInfo::ConnectTiming connect_timing_;

  base::WeakPtrFactory<QuicHttpStream> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(QuicHttpStream);
};

}  // namespace net

#endif  // NET_QUIC_CHROMIUM_QUIC_HTTP_STREAM_H_
