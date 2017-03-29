// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/ws/window_tree_host_factory_registrar.h"

#include "services/ui/ws/default_access_policy.h"
#include "services/ui/ws/window_server.h"
#include "services/ui/ws/window_tree.h"
#include "services/ui/ws/window_tree_host_factory.h"

namespace ui {
namespace ws {

WindowTreeHostFactoryRegistrar::WindowTreeHostFactoryRegistrar(
    WindowServer* window_server,
    const UserId& user_id)
    : window_server_(window_server), user_id_(user_id) {}

WindowTreeHostFactoryRegistrar::~WindowTreeHostFactoryRegistrar() {}

void WindowTreeHostFactoryRegistrar::Register(
    mojom::WindowTreeHostFactoryRequest host_factory_request,
    mojom::WindowTreeRequest tree_request,
    mojom::WindowTreeClientPtr tree_client) {
  std::unique_ptr<WindowTreeHostFactory> host_factory(
      new WindowTreeHostFactory(window_server_, user_id_));

  host_factory->AddBinding(std::move(host_factory_request));

  // FIXME(tonikitoo,msisov,fwang): Do we need our own AccessPolicy?
  std::unique_ptr<ws::WindowTree> tree(
      new ws::WindowTree(window_server_, user_id_, nullptr /*ServerWindow*/,
                         base::WrapUnique(new DefaultAccessPolicy())));

  std::unique_ptr<ws::DefaultWindowTreeBinding> tree_binding(
      new ws::DefaultWindowTreeBinding(tree.get(), window_server_,
                                       std::move(tree_request),
                                       std::move(tree_client)));

  // Pass nullptr as mojom::WindowTreePtr (3rd parameter), because in external
  // window mode, the WindowTreePtr is created on the aura/WindowTreeClient
  // side.
  //
  // NOTE: This call to ::AddTree calls ::Init. However, it will not trigger a
  // ::OnEmbed call because the WindowTree instance was created above passing
  // 'nullptr' as the ServerWindow, hence there is no 'root' yet.
  window_server_->AddTree(std::move(tree), std::move(tree_binding),
                          nullptr /*mojom::WindowTreePtr*/);

  window_server_->set_window_tree_host_factory(std::move(host_factory));
}

}  // namespace ws
}  // namespace ui
