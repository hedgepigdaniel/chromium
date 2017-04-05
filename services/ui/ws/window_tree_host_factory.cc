// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/ws/window_tree_host_factory.h"

#include "mojo/public/cpp/bindings/map.h"
#include "services/ui/display/viewport_metrics.h"
#include "services/ui/public/cpp/property_type_converters.h"
#include "services/ui/ws/display.h"
#include "services/ui/ws/display_binding.h"
#include "services/ui/ws/window_server.h"
#include "services/ui/ws/window_tree.h"

namespace ui {
namespace ws {

WindowTreeHostFactory::WindowTreeHostFactory(WindowServer* window_server,
                                             const UserId& user_id)
    : window_server_(window_server), user_id_(user_id) {}

WindowTreeHostFactory::~WindowTreeHostFactory() {}

void WindowTreeHostFactory::AddBinding(
    mojom::WindowTreeHostFactoryRequest request) {
  bindings_.AddBinding(this, std::move(request));
}

void WindowTreeHostFactory::CreatePlatformWindow(
    mojom::WindowTreeHostRequest tree_host_request,
    Id transport_window_id,
    const TransportProperties& transport_properties) {
  WindowTree* tree = window_server_->GetTreeForExternalWindowMode();
  tree->WillCreateRootDisplay(transport_window_id);

  Display* ws_display = new Display(window_server_);

  std::unique_ptr<DisplayBindingImpl> display_binding(
      new DisplayBindingImpl(std::move(tree_host_request), ws_display, user_id_,
                             nullptr, window_server_));

  // Provide an initial size for the Display.
  std::map<std::string, std::vector<uint8_t>> properties =
      mojo::UnorderedMapToMap(transport_properties);

  display::ViewportMetrics metrics;
  metrics.device_scale_factor = 1.0f;
  metrics.ui_scale_factor = 1.0f;

  auto iter = properties.find(ui::mojom::WindowManager::kBounds_InitProperty);
  if (iter != properties.end()) {
    metrics.bounds_in_pixels = mojo::ConvertTo<gfx::Rect>(iter->second);
  } else
    metrics.bounds_in_pixels = gfx::Rect(1024, 768);
  ws_display->Init(metrics, std::move(display_binding));
}

}  // namespace ws
}  // namespace ui
