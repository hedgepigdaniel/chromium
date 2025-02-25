// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/ws/display.h"

#include <set>
#include <utility>
#include <vector>

#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "services/service_manager/public/interfaces/connector.mojom.h"
#include "services/ui/common/types.h"
#include "services/ui/display/viewport_metrics.h"
#include "services/ui/public/interfaces/cursor/cursor.mojom.h"
#include "services/ui/ws/display_binding.h"
#include "services/ui/ws/display_manager.h"
#include "services/ui/ws/focus_controller.h"
#include "services/ui/ws/platform_display.h"
#include "services/ui/ws/user_activity_monitor.h"
#include "services/ui/ws/window_manager_display_root.h"
#include "services/ui/ws/window_manager_state.h"
#include "services/ui/ws/window_manager_window_tree_factory.h"
#include "services/ui/ws/window_server.h"
#include "services/ui/ws/window_server_delegate.h"
#include "services/ui/ws/window_tree.h"
#include "services/ui/ws/window_tree_binding.h"
#include "ui/base/cursor/cursor.h"
#include "ui/display/screen.h"

#if defined(USE_OZONE)
#include "ui/ozone/public/ozone_platform.h"
#endif

namespace ui {
namespace ws {

Display::Display(WindowServer* window_server) : window_server_(window_server) {
  window_server_->window_manager_window_tree_factory_set()->AddObserver(this);
  window_server_->user_id_tracker()->AddObserver(this);
}

Display::~Display() {
  window_server_->user_id_tracker()->RemoveObserver(this);

  window_server_->window_manager_window_tree_factory_set()->RemoveObserver(
      this);

  if (!focus_controller_) {
    focus_controller_->RemoveObserver(this);
    focus_controller_.reset();
  }

  // Notify the window manager state that the display is being destroyed.
  for (auto& pair : window_manager_display_root_map_)
    pair.second->window_manager_state()->OnDisplayDestroying(this);

  if (binding_ && !window_manager_display_root_map_.empty()) {
    // If there is a |binding_| then the tree was created specifically for one
    // or more displays, which correspond to WindowTreeHosts.
    WindowManagerDisplayRoot* display_root =
        window_manager_display_root_map_.begin()->second;
    WindowTree* tree = display_root->window_manager_state()->window_tree();
    ServerWindow* root = display_root->root();
    if (tree) {
      // Delete the window root corresponding to that display.
      ClientWindowId root_id;
      if (tree->IsWindowKnown(root, &root_id))
        tree->DeleteWindow(root_id);

      // Destroy the tree once all the roots have been removed.
      if (tree->roots().empty())
        window_server_->DestroyTree(tree);
    }
  }
}

void Display::Init(const display::ViewportMetrics& metrics,
                   std::unique_ptr<DisplayBinding> binding) {
  binding_ = std::move(binding);
  display_manager()->AddDisplay(this);

  CreateRootWindow(metrics.bounds_in_pixels.size());

  platform_display_ = PlatformDisplay::Create(root_.get(), metrics);
  platform_display_->Init(this);
}

int64_t Display::GetId() const {
  // TODO(tonikitoo): Implement a different ID for external window mode.
  return display_.id();
}

void Display::SetDisplay(const display::Display& display) {
  display_ = display;
}

const display::Display& Display::GetDisplay() {
  return display_;
}

DisplayManager* Display::display_manager() {
  return window_server_->display_manager();
}

const DisplayManager* Display::display_manager() const {
  return window_server_->display_manager();
}

gfx::Size Display::GetSize() const {
  DCHECK(root_);
  return root_->bounds().size();
}

ServerWindow* Display::GetRootWithId(const WindowId& id) {
  if (id == root_->id())
    return root_.get();
  for (auto& pair : window_manager_display_root_map_) {
    if (pair.second->root()->id() == id)
      return pair.second->root();
  }
  return nullptr;
}

WindowManagerDisplayRoot* Display::GetWindowManagerDisplayRootWithRoot(
    const ServerWindow* window) {
  for (auto& pair : window_manager_display_root_map_) {
    if (pair.second->root() == window)
      return pair.second;
  }
  return nullptr;
}

const WindowManagerDisplayRoot* Display::GetWindowManagerDisplayRootForUser(
    const UserId& user_id) const {
  auto iter = window_manager_display_root_map_.find(user_id);
  return iter == window_manager_display_root_map_.end() ? nullptr
                                                        : iter->second;
}

const WindowManagerDisplayRoot* Display::GetActiveWindowManagerDisplayRoot()
    const {
  return GetWindowManagerDisplayRootForUser(
      window_server_->user_id_tracker()->active_id());
}

bool Display::SetFocusedWindow(ServerWindow* new_focused_window) {
  ServerWindow* old_focused_window = focus_controller_->GetFocusedWindow();
  if (old_focused_window == new_focused_window)
    return true;
  DCHECK(!new_focused_window || root_window()->Contains(new_focused_window));
  return focus_controller_->SetFocusedWindow(new_focused_window);
}

ServerWindow* Display::GetFocusedWindow() {
  return focus_controller_->GetFocusedWindow();
}

void Display::ActivateNextWindow() {
  // TODO(sky): this is wrong, needs to figure out the next window to activate
  // and then route setting through WindowServer.
  focus_controller_->ActivateNextWindow();
}

void Display::AddActivationParent(ServerWindow* window) {
  activation_parents_.Add(window);
}

void Display::RemoveActivationParent(ServerWindow* window) {
  activation_parents_.Remove(window);
}

void Display::UpdateTextInputState(ServerWindow* window,
                                   const ui::TextInputState& state) {
  // Do not need to update text input for unfocused windows.
  if (!platform_display_ || focus_controller_->GetFocusedWindow() != window)
    return;
  platform_display_->UpdateTextInputState(state);
}

void Display::SetImeVisibility(ServerWindow* window, bool visible) {
  // Do not need to show or hide IME for unfocused window.
  if (focus_controller_->GetFocusedWindow() != window)
    return;
  platform_display_->SetImeVisibility(visible);
}

void Display::SetBounds(const gfx::Rect& bounds) {
  platform_display_->SetViewportBounds(bounds);

  if (root_->bounds() == bounds)
    return;

  root_->SetBounds(bounds, allocator_.GenerateId());

  // WindowManagerDisplayRoot::root_ needs to be at 0,0 position relative
  // to its parent not to break mouse/touch events.
  for (auto& pair : window_manager_display_root_map_)
    pair.second->root()->SetBounds(gfx::Rect(bounds.size()), allocator_.GenerateId());
}

void Display::OnWillDestroyTree(WindowTree* tree) {
  for (auto it = window_manager_display_root_map_.begin();
       it != window_manager_display_root_map_.end(); ++it) {
    if (it->second->window_manager_state()->window_tree() == tree) {
      window_manager_display_root_map_.erase(it);
      break;
    }
  }
}

void Display::RemoveWindowManagerDisplayRoot(
    WindowManagerDisplayRoot* display_root) {
  for (auto it = window_manager_display_root_map_.begin();
       it != window_manager_display_root_map_.end(); ++it) {
    if (it->second == display_root) {
      window_manager_display_root_map_.erase(it);
      return;
    }
  }
  NOTREACHED();
}

void Display::SetNativeCursor(const ui::CursorData& cursor) {
  platform_display_->SetCursor(cursor);
}

void Display::SetSize(const gfx::Size& size) {
  platform_display_->SetViewportSize(size);
}

void Display::SetTitle(const std::string& title) {
  platform_display_->SetTitle(base::UTF8ToUTF16(title));
}

void Display::InitDisplayRoot() {
  DCHECK(window_server_->IsInExternalWindowMode());
  DCHECK(binding_);

  std::unique_ptr<WindowManagerDisplayRoot> display_root_ptr(
      new WindowManagerDisplayRoot(this));
  // TODO(tonikitoo): Code still has assumptions that even in external window
  // mode make 'window_manager_display_root_map_' needed.
  window_manager_display_root_map_[service_manager::mojom::kRootUserID] =
      display_root_ptr.get();

  WindowTree* window_tree = window_server_->GetTreeForExternalWindowMode();
  ServerWindow* server_window = display_root_ptr->root();

  std::unique_ptr<WindowManagerState> window_manager_state =
      base::MakeUnique<WindowManagerState>(window_tree);
  display_root_ptr->window_manager_state_ = window_manager_state.get();
  window_tree->AddExternalModeWindowManagerState(
      std::move(window_manager_state));

  WindowManagerDisplayRoot* display_root = display_root_ptr.get();
  display_root->window_manager_state_->AddWindowManagerDisplayRoot(
      std::move(display_root_ptr));

  window_tree->AddRoot(server_window);
  window_tree->DoOnEmbed(nullptr /*mojom::WindowTreePtr*/, server_window);
}

void Display::InitWindowManagerDisplayRoots() {
  // Tests can create ws::Display instances directly, by-passing
  // WindowTreeHostFactory.
  // TODO(tonikitoo): Check if with the introduction of 'external window mode'
  // this path is still needed.
  if (binding_) {
    std::unique_ptr<WindowManagerDisplayRoot> display_root_ptr(
        new WindowManagerDisplayRoot(this));
    WindowManagerDisplayRoot* display_root = display_root_ptr.get();
    // For this case we never create additional displays roots, so any
    // id works.
    window_manager_display_root_map_[service_manager::mojom::kRootUserID] =
        display_root_ptr.get();
    WindowTree* window_tree = binding_->CreateWindowTree(display_root->root());
    display_root->window_manager_state_ = window_tree->window_manager_state();
    window_tree->window_manager_state()->AddWindowManagerDisplayRoot(
        std::move(display_root_ptr));
  } else {
    CreateWindowManagerDisplayRootsFromFactories();
  }
  display_manager()->OnDisplayUpdate(display_);
}

void Display::CreateWindowManagerDisplayRootsFromFactories() {
  std::vector<WindowManagerWindowTreeFactory*> factories =
      window_server_->window_manager_window_tree_factory_set()->GetFactories();
  for (WindowManagerWindowTreeFactory* factory : factories) {
    if (factory->window_tree())
      CreateWindowManagerDisplayRootFromFactory(factory);
  }
}

void Display::CreateWindowManagerDisplayRootFromFactory(
    WindowManagerWindowTreeFactory* factory) {
  std::unique_ptr<WindowManagerDisplayRoot> display_root_ptr(
      new WindowManagerDisplayRoot(this));
  WindowManagerDisplayRoot* display_root = display_root_ptr.get();
  window_manager_display_root_map_[factory->user_id()] = display_root_ptr.get();
  WindowManagerState* window_manager_state =
      factory->window_tree()->window_manager_state();
  display_root->window_manager_state_ = window_manager_state;
  const bool is_active =
      factory->user_id() == window_server_->user_id_tracker()->active_id();
  display_root->root()->SetVisible(is_active);
  window_manager_state->window_tree()->AddRootForWindowManager(
      display_root->root());
  window_manager_state->AddWindowManagerDisplayRoot(
      std::move(display_root_ptr));
}

void Display::CreateRootWindow(const gfx::Size& size) {
  DCHECK(!root_);

  root_.reset(window_server_->CreateServerWindow(
      display_manager()->GetAndAdvanceNextRootId(),
      ServerWindow::Properties()));
  root_->set_event_targeting_policy(
      mojom::EventTargetingPolicy::DESCENDANTS_ONLY);
  root_->SetBounds(gfx::Rect(size), allocator_.GenerateId());
  root_->SetVisible(true);
  focus_controller_ = base::MakeUnique<FocusController>(this, root_.get());
  focus_controller_->AddObserver(this);
}

ServerWindow* Display::GetRootWindow() {
  return root_.get();
}

EventSink* Display::GetEventSink() {
  return this;
}

void Display::OnAcceleratedWidgetAvailable() {
  display_manager()->OnDisplayAcceleratedWidgetAvailable(this);

  if (window_server_->IsInExternalWindowMode())
    InitDisplayRoot();
  else
    InitWindowManagerDisplayRoots();
}

void Display::OnNativeCaptureLost() {
  WindowManagerDisplayRoot* display_root = GetActiveWindowManagerDisplayRoot();
  if (display_root)
    display_root->window_manager_state()->SetCapture(nullptr, kInvalidClientId);
}

void Display::OnBoundsChanged(const gfx::Rect& new_bounds) {
  if (!window_server_->IsInExternalWindowMode())
    return;

  if (root_->bounds() == new_bounds)
    return;

  root_->OnNewBoundsFromHostServer(new_bounds);

  // WindowManagerDisplayRoot::root_ needs to be at 0,0 position relative
  // to its parent not to break mouse/touch events.
  for (auto& pair : window_manager_display_root_map_)
    pair.second->root()->OnNewBoundsFromHostServer(gfx::Rect(new_bounds.size()));
}

void Display::OnCloseRequest() {
  DCHECK(window_server_->IsInExternalWindowMode());
  DCHECK(binding_);

  WindowTree* window_tree = window_server_->GetTreeForExternalWindowMode();
  WindowManagerDisplayRoot* display_root =
      window_manager_display_root_map_.begin()->second;
  ServerWindow* server_window =
      display_root->window_manager_state()->GetWindowManagerRootForDisplayRoot(
          root_window());
  ClientWindowId window_id;
  if (window_tree && window_tree->IsWindowKnown(server_window, &window_id))
    window_tree->client()->RequestClose(window_id.id);
}

void Display::OnWindowStateChanged(ui::mojom::ShowState new_state) {
  if (!window_server_->IsInExternalWindowMode())
    return;

  WindowTree* window_tree = window_server_->GetTreeForExternalWindowMode();
  WindowManagerDisplayRoot* display_root =
      window_manager_display_root_map_.begin()->second;
  ServerWindow* server_window =
      display_root->window_manager_state()->GetWindowManagerRootForDisplayRoot(
          root_window());
  ClientWindowId window_id;
  // We are calling WindowTreeClient directly from here for the sake not
  // changing WindowTree for such a simple call, adding less code as possible
  // downstream.
  if (window_tree && window_tree->IsWindowKnown(server_window, &window_id))
    window_tree->client()->OnWindowStateChanged(window_id.id, new_state);
}

OzonePlatform* Display::GetOzonePlatform() {
#if defined(USE_OZONE)
  return OzonePlatform::GetInstance();
#else
  return nullptr;
#endif
}

void Display::OnViewportMetricsChanged(
    const display::ViewportMetrics& metrics) {
  platform_display_->UpdateViewportMetrics(metrics);

  if (root_->bounds().size() == metrics.bounds_in_pixels.size())
    return;

  gfx::Rect new_bounds(metrics.bounds_in_pixels.size());
  root_->SetBounds(new_bounds, allocator_.GenerateId());
  for (auto& pair : window_manager_display_root_map_)
    pair.second->root()->SetBounds(new_bounds, allocator_.GenerateId());
}

ServerWindow* Display::GetActiveRootWindow() {
  WindowManagerDisplayRoot* display_root = GetActiveWindowManagerDisplayRoot();
  if (display_root)
    return display_root->root();
  return nullptr;
}

bool Display::CanHaveActiveChildren(ServerWindow* window) const {
  return window && activation_parents_.Contains(window);
}

void Display::OnActivationChanged(ServerWindow* old_active_window,
                                  ServerWindow* new_active_window) {
  // Don't do anything here. We assume the window manager handles restacking. If
  // we did attempt to restack than we would have to ensure clients see the
  // restack.
}

void Display::OnFocusChanged(FocusControllerChangeSource change_source,
                             ServerWindow* old_focused_window,
                             ServerWindow* new_focused_window) {
  // TODO(sky): focus is global, not per windowtreehost. Move.

  // There are up to four clients that need to be notified:
  // . the client containing |old_focused_window|.
  // . the client with |old_focused_window| as its root.
  // . the client containing |new_focused_window|.
  // . the client with |new_focused_window| as its root.
  // Some of these client may be the same. The following takes care to notify
  // each only once.
  WindowTree* owning_tree_old = nullptr;
  WindowTree* embedded_tree_old = nullptr;

  if (old_focused_window) {
    owning_tree_old =
        window_server_->GetTreeWithId(old_focused_window->id().client_id);
    if (owning_tree_old) {
      owning_tree_old->ProcessFocusChanged(old_focused_window,
                                           new_focused_window);
    }
    embedded_tree_old = window_server_->GetTreeWithRoot(old_focused_window);
    if (embedded_tree_old) {
      DCHECK_NE(owning_tree_old, embedded_tree_old);
      embedded_tree_old->ProcessFocusChanged(old_focused_window,
                                             new_focused_window);
    }
  }
  WindowTree* owning_tree_new = nullptr;
  WindowTree* embedded_tree_new = nullptr;
  if (new_focused_window) {
    owning_tree_new =
        window_server_->GetTreeWithId(new_focused_window->id().client_id);
    if (owning_tree_new && owning_tree_new != owning_tree_old &&
        owning_tree_new != embedded_tree_old) {
      owning_tree_new->ProcessFocusChanged(old_focused_window,
                                           new_focused_window);
    }
    embedded_tree_new = window_server_->GetTreeWithRoot(new_focused_window);
    if (embedded_tree_new && embedded_tree_new != owning_tree_old &&
        embedded_tree_new != embedded_tree_old) {
      DCHECK_NE(owning_tree_new, embedded_tree_new);
      embedded_tree_new->ProcessFocusChanged(old_focused_window,
                                             new_focused_window);
    }
  }

  // WindowManagers are always notified of focus changes.
  WindowManagerDisplayRoot* display_root = GetActiveWindowManagerDisplayRoot();
  if (display_root) {
    WindowTree* wm_tree = display_root->window_manager_state()->window_tree();
    if (wm_tree != owning_tree_old && wm_tree != embedded_tree_old &&
        wm_tree != owning_tree_new && wm_tree != embedded_tree_new) {
      wm_tree->ProcessFocusChanged(old_focused_window, new_focused_window);
    }
  }

  UpdateTextInputState(new_focused_window,
                       new_focused_window->text_input_state());
}

void Display::OnUserIdRemoved(const UserId& id) {
  window_manager_display_root_map_.erase(id);
}

void Display::OnWindowManagerWindowTreeFactoryReady(
    WindowManagerWindowTreeFactory* factory) {
  if (!binding_)
    CreateWindowManagerDisplayRootFromFactory(factory);
}

EventDispatchDetails Display::OnEventFromSource(Event* event) {
  // TODO(tonikitoo): Current WindowManagerDisplayRoot class is misnamed, since
  // in external window mode a non-WindowManager specific 'DisplayRoot' is also
  // needed.
  // Bits of WindowManagerState also should be factored out and made available
  // in external window mode, so that event handling is functional.
  // htts://crbug.com/701129
  WindowManagerDisplayRoot* display_root = GetActiveWindowManagerDisplayRoot();
  if (display_root) {
    WindowManagerState* wm_state = display_root->window_manager_state();
    wm_state->ProcessEvent(*event, GetId());
  }

  UserActivityMonitor* activity_monitor =
      window_server_->GetUserActivityMonitorForUser(
          window_server_->user_id_tracker()->active_id());
  activity_monitor->OnUserActivity();
  return EventDispatchDetails();
}

}  // namespace ws
}  // namespace ui
