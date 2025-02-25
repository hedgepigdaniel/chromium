// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_DISPLAY_SCREEN_MANAGER_OZONE_EXTERNAL_H_
#define SERVICES_UI_DISPLAY_SCREEN_MANAGER_OZONE_EXTERNAL_H_

#include "services/ui/display/screen_manager.h"

#include "base/memory/weak_ptr.h"
#include "ui/display/screen_base.h"

namespace display {

// In external window mode, the purpose of having a ScreenManager
// does not apply: there is not a ScreenManagerDelegate manager
// responsible for creating Display instances.
// Basically, in this mode WindowTreeHost creates the display instance.
//
// ScreenManagerOzoneExternal provides the stub out implementation
// of ScreenManager for Ozone non-chromeos platforms.
class ScreenManagerOzoneExternal : public ScreenManager {
 public:
  ScreenManagerOzoneExternal();
  ~ScreenManagerOzoneExternal() override;

 private:
  // Callback to be called from the Ozone platform when
  // host displays' data are ready.
  void OnHostDisplaysReady(const std::vector<gfx::Size>&);

  // ScreenManager.
  void AddInterfaces(service_manager::BinderRegistry* registry) override;
  void Init(ScreenManagerDelegate* delegate) override;
  void RequestCloseDisplay(int64_t display_id) override;
  display::ScreenBase* GetScreen() override;

  // A Screen instance is created in the constructor because it might be
  // accessed early. The ownership of this object will be transfered to
  // |display_manager_| when that gets initialized.
  std::unique_ptr<ScreenBase> screen_owned_;

  // Used to add/remove/modify displays.
  ScreenBase* screen_;

  ScreenManagerDelegate* delegate_ = nullptr;
  int next_display_id_ = 0;

  base::WeakPtrFactory<ScreenManagerOzoneExternal> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ScreenManagerOzoneExternal);
};

}  // namespace display

#endif  // SERVICES_UI_DISPLAY_SCREEN_MANAGER_OZONE_EXTERNAL_H_
