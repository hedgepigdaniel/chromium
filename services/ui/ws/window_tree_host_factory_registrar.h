// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_WS_WINDOW_TREE_HOST_FACTORY_REGISTRAR_H_
#define SERVICES_UI_WS_WINDOW_TREE_HOST_FACTORY_REGISTRAR_H_

#include "services/ui/public/interfaces/window_tree_host.mojom.h"
#include "services/ui/ws/user_id.h"

namespace ui {
namespace ws {

class WindowServer;

class WindowTreeHostFactoryRegistrar
    : public mojom::WindowTreeHostFactoryRegistrar {
 public:
  WindowTreeHostFactoryRegistrar(WindowServer* window_server,
                                 const UserId& user_id);
  ~WindowTreeHostFactoryRegistrar() override;

  const UserId& user_id() const { return user_id_; }

 private:
  void Register(mojom::WindowTreeHostFactoryRequest,
                mojom::WindowTreeRequest tree_request,
                mojom::WindowTreeClientPtr tree_client) override;

  WindowServer* window_server_;
  const UserId user_id_;

  DISALLOW_COPY_AND_ASSIGN(WindowTreeHostFactoryRegistrar);
};

}  // namespace ws
}  // namespace ui

#endif  // SERVICES_UI_WS_WINDOW_TREE_HOST_FACTORY_REGISTRAR_H_
