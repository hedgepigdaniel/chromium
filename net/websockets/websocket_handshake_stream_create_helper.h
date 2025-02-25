// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_WEBSOCKETS_WEBSOCKET_HANDSHAKE_STREAM_CREATE_HELPER_H_
#define NET_WEBSOCKETS_WEBSOCKET_HANDSHAKE_STREAM_CREATE_HELPER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/websockets/websocket_handshake_stream_base.h"
#include "net/websockets/websocket_stream.h"

namespace net {

class WebSocketStreamRequest;
class WebSocketBasicHandshakeStream;

// Implementation of WebSocketHandshakeStreamBase::CreateHelper. This class is
// used in the implementation of WebSocketStream::CreateAndConnectStream() and
// is not intended to be used by itself.
//
// Holds the information needed to construct a
// WebSocketBasicHandshakeStreamBase.
class NET_EXPORT_PRIVATE WebSocketHandshakeStreamCreateHelper
    : public WebSocketHandshakeStreamBase::CreateHelper {
 public:
  // |connect_delegate| must out-live this object.
  explicit WebSocketHandshakeStreamCreateHelper(
      WebSocketStream::ConnectDelegate* connect_delegate,
      const std::vector<std::string>& requested_subprotocols);

  ~WebSocketHandshakeStreamCreateHelper() override;

  // WebSocketHandshakeStreamBase::CreateHelper methods

  // Creates a WebSocketBasicHandshakeStream.
  WebSocketHandshakeStreamBase* CreateBasicStream(
      std::unique_ptr<ClientSocketHandle> connection,
      bool using_proxy) override;

  // WebSocketHandshakeStreamCreateHelper methods

  // This method must be called before CreateBasicStream().
  // The |request| pointer must remain valid as long as this object exists.
  void set_stream_request(WebSocketStreamRequest* request) {
    request_ = request;
  }

 protected:
  // This is used by DeterministicKeyWebSocketHandshakeStreamCreateHelper.
  // The default implementation does nothing.
  virtual void OnBasicStreamCreated(WebSocketBasicHandshakeStream* stream);

 private:
  const std::vector<std::string> requested_subprotocols_;

  WebSocketStream::ConnectDelegate* connect_delegate_;

  WebSocketStreamRequest* request_;

  DISALLOW_COPY_AND_ASSIGN(WebSocketHandshakeStreamCreateHelper);
};

}  // namespace net

#endif  // NET_WEBSOCKETS_WEBSOCKET_HANDSHAKE_STREAM_CREATE_HELPER_H_
