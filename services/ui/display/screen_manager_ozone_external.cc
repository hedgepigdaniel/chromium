// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/display/screen_manager_ozone_external.h"

#include <memory>

#include "services/service_manager/public/cpp/binder_registry.h"
#include "services/service_manager/public/interfaces/connector.mojom.h"
#include "ui/display/types/display_constants.h"
#include "ui/gfx/geometry/dip_util.h"
#include "ui/ozone/public/ozone_platform.h"

namespace display {

// static
std::unique_ptr<ScreenManager> ScreenManager::Create() {
  return base::MakeUnique<ScreenManagerOzoneExternal>();
}

ScreenManagerOzoneExternal::ScreenManagerOzoneExternal()
    : screen_owned_(base::MakeUnique<ScreenBase>()),
      screen_(screen_owned_.get()),
      weak_ptr_factory_(this) {
  Screen::SetScreenInstance(screen_owned_.get());
}

ScreenManagerOzoneExternal::~ScreenManagerOzoneExternal() {}

void ScreenManagerOzoneExternal::OnHostDisplaysReady(
    const std::vector<gfx::Size>& dimensions) {
  float device_scale_factor = 1.f;
  if (Display::HasForceDeviceScaleFactor())
    device_scale_factor = Display::GetForcedDeviceScaleFactor();

  gfx::Size scaled_size =
      gfx::ConvertSizeToDIP(device_scale_factor, dimensions[0]);

  Display display(next_display_id_++);
  display.set_bounds(gfx::Rect(scaled_size));
  display.set_work_area(display.bounds());
  display.set_device_scale_factor(device_scale_factor);

  screen_->display_list().AddDisplay(display, DisplayList::Type::PRIMARY);

  // TODO(tonikitoo): Before calling out to ScreenManagerDelegate check if
  // more than one host display is available.
  delegate_->OnHostDisplaysReady(service_manager::mojom::kRootUserID);
}

void ScreenManagerOzoneExternal::AddInterfaces(
    service_manager::BinderRegistry* registry) {}

void ScreenManagerOzoneExternal::Init(ScreenManagerDelegate* delegate) {
  delegate_ = delegate;

  ui::OzonePlatform::GetInstance()->QueryHostDisplaysData(
      base::Bind(&ScreenManagerOzoneExternal::OnHostDisplaysReady,
                 weak_ptr_factory_.GetWeakPtr()));
}

void ScreenManagerOzoneExternal::RequestCloseDisplay(int64_t display_id) {}

display::ScreenBase* ScreenManagerOzoneExternal::GetScreen() {
  // TODO: commit into  Allow ws to pass display::Display data to Aura/client
  return screen_;
}

}  // namespace display
