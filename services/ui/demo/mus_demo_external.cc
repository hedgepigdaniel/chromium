// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/demo/mus_demo_external.h"

#include "base/command_line.h"
#include "base/strings/string_number_conversions.h"
#include "services/service_manager/public/cpp/service_context.h"
#include "services/ui/demo/window_tree_data.h"
#include "services/ui/public/interfaces/constants.mojom.h"
#include "services/ui/public/interfaces/window_tree_host.mojom.h"
#include "ui/aura/mus/window_tree_client.h"
#include "ui/aura/mus/window_tree_host_mus.h"
#include "ui/display/display.h"

namespace ui {
namespace demo {

namespace {

class WindowTreeDataExternal : public WindowTreeData {
 public:
  // Creates a new window tree host associated to the WindowTreeData.
  WindowTreeDataExternal(aura::WindowTreeClient* window_tree_client,
                         int square_size)
      : WindowTreeData(square_size) {
    SetWindowTreeHost(
        base::MakeUnique<aura::WindowTreeHostMus>(window_tree_client));
  }

  DISALLOW_COPY_AND_ASSIGN(WindowTreeDataExternal);
};

int GetSquareSizeForWindow(int window_index) {
  return 50 * window_index + 400;
};

}  // namespace

MusDemoExternal::MusDemoExternal() {}

MusDemoExternal::~MusDemoExternal() {}

std::unique_ptr<aura::WindowTreeClient>
MusDemoExternal::CreateWindowTreeClient() {
  return base::MakeUnique<aura::WindowTreeClient>(context()->connector(), this);
}

void MusDemoExternal::OnStartImpl() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch("external-window-count")) {
    if (!base::StringToSizeT(
            command_line->GetSwitchValueASCII("external-window-count"),
            &number_of_windows_)) {
      LOG(FATAL) << "Invalid value for \'external-window-count\'";
      return;
    }
  }

  window_tree_client()->ConnectViaWindowTreeHostFactory();

  // TODO(tonikitoo,fwang): Implement management of displays in external mode.
  // For now, a fake display is created in order to work around an assertion in
  // aura::GetDeviceScaleFactorFromDisplay().
  AddPrimaryDisplay(display::Display(0));

  for (size_t i = 0; i < number_of_windows_; ++i)
    OpenNewWindow(i);
}

void MusDemoExternal::OpenNewWindow(size_t window_index) {
  AppendWindowTreeData(base::MakeUnique<WindowTreeDataExternal>(
      window_tree_client(), GetSquareSizeForWindow(window_index)));
}

void MusDemoExternal::OnEmbedRootReady(
    aura::WindowTreeHostMus* window_tree_host) {
  DCHECK(window_tree_host);
  auto window_tree_data = FindWindowTreeData(window_tree_host);
  (*window_tree_data)->Init();
}

void MusDemoExternal::OnEmbedRootDestroyed(
    aura::WindowTreeHostMus* window_tree_host) {
  RemoveWindowTreeData(window_tree_host);
}

}  // namespace demo
}  // namespace ui
