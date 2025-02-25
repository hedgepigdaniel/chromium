// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/platform_window/x11/x11_window_base.h"

#include <X11/extensions/XInput2.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "ui/base/platform_window_defaults.h"
#include "ui/base/x/x11_util.h"
#include "ui/base/x/x11_window_event_manager.h"
#include "ui/events/devices/x11/touch_factory_x11.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/platform/platform_event_dispatcher.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/events/platform/x11/x11_event_source.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/platform_window/platform_window_delegate.h"

namespace ui {

namespace {

// Constants that are part of EWMH.
const int k_NET_WM_STATE_ADD = 1;
const int k_NET_WM_STATE_REMOVE = 0;

const char* kAtomsToCache[] = {"UTF8_STRING",
                               "WM_DELETE_WINDOW",
                               "_NET_WM_NAME",
                               "_NET_WM_PID",
                               "_NET_WM_PING",
                               "_NET_WM_STATE",
                               "_NET_WM_STATE_HIDDEN",
                               "_NET_WM_STATE_MAXIMIZED_HORZ",
                               "_NET_WM_STATE_MAXIMIZED_VERT",
                               "_NET_WM_STATE_FULLSCREEN",
                               "_NET_WM_WINDOW_TYPE_MENU",
                               "_NET_WM_WINDOW_TYPE_NORMAL",
                               "_NET_WM_WINDOW_TYPE",
                               NULL};

XID FindXEventTarget(const XEvent& xev) {
  XID target = xev.xany.window;
  if (xev.type == GenericEvent)
    target = static_cast<XIDeviceEvent*>(xev.xcookie.data)->event;
  return target;
}

}  // namespace

X11WindowBase::X11WindowBase(PlatformWindowDelegate* delegate,
                             const gfx::Rect& bounds)
    : delegate_(delegate),
      xdisplay_(gfx::GetXDisplay()),
      xwindow_(None),
      xroot_window_(DefaultRootWindow(xdisplay_)),
      atom_cache_(xdisplay_, kAtomsToCache),
      bounds_(bounds) {
  DCHECK(delegate_);
  TouchFactory::SetTouchDeviceListFromCommandLine();
}

X11WindowBase::~X11WindowBase() {
  Destroy();
}

void X11WindowBase::Destroy() {
  if (xwindow_ == None)
    return;

  // Stop processing events.
  XID xwindow = xwindow_;
  XDisplay* xdisplay = xdisplay_;
  xwindow_ = None;
  delegate_->OnClosed();
  // |this| might be deleted because of the above call.

  XDestroyWindow(xdisplay, xwindow);
}

void X11WindowBase::Create() {
  DCHECK(!bounds_.size().IsEmpty());

  XSetWindowAttributes swa;
  memset(&swa, 0, sizeof(swa));
  swa.background_pixmap = None;
  swa.bit_gravity = NorthWestGravity;
  swa.override_redirect = UseTestConfigForPlatformWindows();

  ::Atom window_type;
  // There is now default initialization for this type. Initialize it
  // to ::WINDOW here. It will be changed by delelgate if it know the
  // type of the window.
  ui::mojom::WindowType ui_window_type = ui::mojom::WindowType::WINDOW;
  delegate_->GetWindowType(&ui_window_type);
  if (ui_window_type != ui::mojom::WindowType::WINDOW) {
    // Setting this to True, doesn't allow X server to set different
    // properties, e.g. decorations.
    // TODO(msisov): Investigate further.
    // https://tronche.com/gui/x/xlib/window/attributes/override-redirect.html
    swa.override_redirect = True;
    window_type = atom_cache_.GetAtom("_NET_WM_WINDOW_TYPE_MENU");
  } else {
    window_type = atom_cache_.GetAtom("_NET_WM_WINDOW_TYPE_NORMAL");
  }

  xwindow_ =
      XCreateWindow(xdisplay_, xroot_window_, bounds_.x(), bounds_.y(),
                    bounds_.width(), bounds_.height(),
                    0,               // border width
                    CopyFromParent,  // depth
                    InputOutput,
                    CopyFromParent,  // visual
                    CWBackPixmap | CWBitGravity | CWOverrideRedirect, &swa);

  XChangeProperty(
      xdisplay_, xwindow_, atom_cache_.GetAtom("_NET_WM_WINDOW_TYPE"), XA_ATOM,
      32, PropModeReplace, reinterpret_cast<unsigned char*>(&window_type), 1);

  // Setup XInput event mask.
  long event_mask = ButtonPressMask | ButtonReleaseMask | FocusChangeMask |
                    KeyPressMask | KeyReleaseMask | EnterWindowMask |
                    LeaveWindowMask | ExposureMask | VisibilityChangeMask |
                    StructureNotifyMask | PropertyChangeMask |
                    PointerMotionMask;
  xwindow_events_.reset(new ui::XScopedEventSelector(xwindow_, event_mask));

  // Setup XInput2 event mask.
  unsigned char mask[XIMaskLen(XI_LASTEVENT)];
  memset(mask, 0, sizeof(mask));

  XISetMask(mask, XI_TouchBegin);
  XISetMask(mask, XI_TouchUpdate);
  XISetMask(mask, XI_TouchEnd);
  XISetMask(mask, XI_ButtonPress);
  XISetMask(mask, XI_ButtonRelease);
  XISetMask(mask, XI_Motion);
  XISetMask(mask, XI_KeyPress);
  XISetMask(mask, XI_KeyRelease);
  XISetMask(mask, XI_HierarchyChanged);

  XIEventMask evmask;
  evmask.deviceid = XIAllDevices;
  evmask.mask_len = sizeof(mask);
  evmask.mask = mask;
  XISelectEvents(xdisplay_, xwindow_, &evmask, 1);
  XFlush(xdisplay_);

  ::Atom protocols[2];
  protocols[0] = atom_cache_.GetAtom("WM_DELETE_WINDOW");
  protocols[1] = atom_cache_.GetAtom("_NET_WM_PING");
  XSetWMProtocols(xdisplay_, xwindow_, protocols, 2);

  // We need a WM_CLIENT_MACHINE and WM_LOCALE_NAME value so we integrate with
  // the desktop environment.
  XSetWMProperties(xdisplay_, xwindow_, NULL, NULL, NULL, 0, NULL, NULL, NULL);

  // Likewise, the X server needs to know this window's pid so it knows which
  // program to kill if the window hangs.
  // XChangeProperty() expects "pid" to be long.
  static_assert(sizeof(long) >= sizeof(pid_t),
                "pid_t should not be larger than long");
  long pid = getpid();
  XChangeProperty(xdisplay_, xwindow_, atom_cache_.GetAtom("_NET_WM_PID"),
                  XA_CARDINAL, 32, PropModeReplace,
                  reinterpret_cast<unsigned char*>(&pid), 1);
  // Before we map the window, set size hints. Otherwise, some window managers
  // will ignore toplevel XMoveWindow commands.
  XSizeHints size_hints;
  size_hints.flags = PPosition | PWinGravity;
  size_hints.x = bounds_.x();
  size_hints.y = bounds_.y();
  // Set StaticGravity so that the window position is not affected by the
  // frame width when running with window manager.
  size_hints.win_gravity = StaticGravity;
  XSetWMNormalHints(xdisplay_, xwindow_, &size_hints);

  // TODO(sky): provide real scale factor.
  delegate_->OnAcceleratedWidgetAvailable(xwindow_, 1.f);
}

void X11WindowBase::Show() {
  if (window_mapped_)
    return;
  if (xwindow_ == None)
    Create();

  XMapWindow(xdisplay_, xwindow_);

  // We now block until our window is mapped. Some X11 APIs will crash and
  // burn if passed |xwindow_| before the window is mapped, and XMapWindow is
  // asynchronous.
  if (X11EventSource::GetInstance())
    X11EventSource::GetInstance()->BlockUntilWindowMapped(xwindow_);
  window_mapped_ = true;
}

void X11WindowBase::Hide() {
  if (!window_mapped_ || IsMinimized())
    return;
  XWithdrawWindow(xdisplay_, xwindow_, 0);
  window_mapped_ = false;
}

void X11WindowBase::Close() {
  Destroy();
}

void X11WindowBase::SetBounds(const gfx::Rect& bounds) {
  if (window_mapped_) {
    XWindowChanges changes = {0};
    unsigned value_mask = 0;

    if (bounds_.size() != bounds.size()) {
      changes.width = bounds.width();
      changes.height = bounds.height();
      value_mask |= CWHeight | CWWidth;
    }

    if (bounds_.origin() != bounds.origin()) {
      changes.x = bounds.x();
      changes.y = bounds.y();
      value_mask |= CWX | CWY;
    }

    if (value_mask)
      XConfigureWindow(xdisplay_, xwindow_, value_mask, &changes);
  }

  // Assume that the resize will go through as requested, which should be the
  // case if we're running without a window manager.  If there's a window
  // manager, it can modify or ignore the request, but (per ICCCM) we'll get a
  // (possibly synthetic) ConfigureNotify about the actual size and correct
  // |bounds_| later.
  bounds_ = bounds;

  // Even if the pixel bounds didn't change this call to the delegate should
  // still happen. The device scale factor may have changed which effectively
  // changes the bounds.
  delegate_->OnBoundsChanged(bounds_);
}

gfx::Rect X11WindowBase::GetBounds() {
  return bounds_;
}

void X11WindowBase::SetTitle(const base::string16& title) {
  if (window_title_ == title)
    return;
  window_title_ = title;
  std::string utf8str = base::UTF16ToUTF8(title);
  XChangeProperty(xdisplay_, xwindow_, atom_cache_.GetAtom("_NET_WM_NAME"),
                  atom_cache_.GetAtom("UTF8_STRING"), 8, PropModeReplace,
                  reinterpret_cast<const unsigned char*>(utf8str.c_str()),
                  utf8str.size());
  XTextProperty xtp;
  char* c_utf8_str = const_cast<char*>(utf8str.c_str());
  if (Xutf8TextListToTextProperty(xdisplay_, &c_utf8_str, 1, XUTF8StringStyle,
                                  &xtp) == Success) {
    XSetWMName(xdisplay_, xwindow_, &xtp);
    XFree(xtp.value);
  }
}

void X11WindowBase::SetCapture() {}

void X11WindowBase::ReleaseCapture() {}

void X11WindowBase::ToggleFullscreen() {
  SetWMSpecState(!is_fullscreen_,
                 atom_cache_.GetAtom("_NET_WM_STATE_FULLSCREEN"), None);
  is_fullscreen_ = !is_fullscreen_;
}

void X11WindowBase::Maximize() {
  // Unfullscreen the window if it is fullscreen.
  if (IsFullScreen())
    ToggleFullscreen();

  // When we are in the process of requesting to maximize a window, we can
  // accurately keep track of our restored bounds instead of relying on the
  // heuristics that are in the PropertyNotify and ConfigureNotify handlers.
  restored_bounds_in_pixels_ = bounds_;

  SetWMSpecState(true, atom_cache_.GetAtom("_NET_WM_STATE_MAXIMIZED_VERT"),
                 atom_cache_.GetAtom("_NET_WM_STATE_MAXIMIZED_HORZ"));
}

void X11WindowBase::Minimize() {
  if (IsMinimized())
    return;
  XIconifyWindow(xdisplay_, xwindow_, 0);
}

void X11WindowBase::Restore() {
  if (IsFullScreen())
    ToggleFullscreen();

  if (IsMaximized()) {
    SetWMSpecState(false, atom_cache_.GetAtom("_NET_WM_STATE_MAXIMIZED_VERT"),
                   atom_cache_.GetAtom("_NET_WM_STATE_MAXIMIZED_HORZ"));
  }
}

void X11WindowBase::MoveCursorTo(const gfx::Point& location) {
  XWarpPointer(xdisplay_, None, xroot_window_, 0, 0, 0, 0,
               bounds_.x() + location.x(), bounds_.y() + location.y());
}

void X11WindowBase::ConfineCursorToBounds(const gfx::Rect& bounds) {}

PlatformImeController* X11WindowBase::GetPlatformImeController() {
  return nullptr;
}

bool X11WindowBase::IsEventForXWindow(const XEvent& xev) const {
  return xwindow_ != None && FindXEventTarget(xev) == xwindow_;
}

void X11WindowBase::ProcessXWindowEvent(XEvent* xev) {
  switch (xev->type) {
    case Expose: {
      gfx::Rect damage_rect(xev->xexpose.x, xev->xexpose.y, xev->xexpose.width,
                            xev->xexpose.height);
      delegate_->OnDamageRect(damage_rect);
      break;
    }

    case FocusOut:
      if (xev->xfocus.mode != NotifyGrab)
        delegate_->OnLostCapture();
      break;

    case ConfigureNotify: {
      DCHECK_EQ(xwindow_, xev->xconfigure.event);
      DCHECK_EQ(xwindow_, xev->xconfigure.window);
      // It's possible that the X window may be resized by some other means than
      // from within aura (e.g. the X window manager can change the size). Make
      // sure the root window size is maintained properly.
      int translated_x_in_pixels = xev->xconfigure.x;
      int translated_y_in_pixels = xev->xconfigure.y;
      if (!xev->xconfigure.send_event && !xev->xconfigure.override_redirect) {
        Window unused;
        XTranslateCoordinates(xdisplay_, xwindow_, xroot_window_, 0, 0,
                              &translated_x_in_pixels, &translated_y_in_pixels,
                              &unused);
      }
      gfx::Rect bounds(translated_x_in_pixels, translated_y_in_pixels,
                       xev->xconfigure.width, xev->xconfigure.height);
      if (bounds_ != bounds) {
        bounds_ = bounds;
        delegate_->OnBoundsChanged(bounds_);
      }
      break;
    }

    case ClientMessage: {
      Atom message = static_cast<Atom>(xev->xclient.data.l[0]);
      if (message == atom_cache_.GetAtom("WM_DELETE_WINDOW")) {
        delegate_->OnCloseRequest();
      } else if (message == atom_cache_.GetAtom("_NET_WM_PING")) {
        XEvent reply_event = *xev;
        reply_event.xclient.window = xroot_window_;

        XSendEvent(xdisplay_, reply_event.xclient.window, False,
                   SubstructureRedirectMask | SubstructureNotifyMask,
                   &reply_event);
        XFlush(xdisplay_);
      }
      break;
    }

    case PropertyNotify: {
      ::Atom changed_atom = xev->xproperty.atom;
      if (changed_atom == atom_cache_.GetAtom("_NET_WM_STATE"))
        OnWMStateUpdated();
      break;
    }
  }
}

void X11WindowBase::SetWMSpecState(bool enabled, ::Atom state1, ::Atom state2) {
  XEvent xclient;
  memset(&xclient, 0, sizeof(xclient));
  xclient.type = ClientMessage;
  xclient.xclient.window = xwindow_;
  xclient.xclient.message_type = atom_cache_.GetAtom("_NET_WM_STATE");
  xclient.xclient.format = 32;
  xclient.xclient.data.l[0] =
      enabled ? k_NET_WM_STATE_ADD : k_NET_WM_STATE_REMOVE;
  xclient.xclient.data.l[1] = state1;
  xclient.xclient.data.l[2] = state2;
  xclient.xclient.data.l[3] = 1;
  xclient.xclient.data.l[4] = 0;

  XSendEvent(xdisplay_, xroot_window_, False,
             SubstructureRedirectMask | SubstructureNotifyMask, &xclient);
}

void X11WindowBase::OnWMStateUpdated() {
  std::vector<::Atom> atom_list;
  // Ignore the return value of ui::GetAtomArrayProperty(). Fluxbox removes the
  // _NET_WM_STATE property when no _NET_WM_STATE atoms are set.
  ui::GetAtomArrayProperty(xwindow_, "_NET_WM_STATE", &atom_list);

  bool was_minimized = IsMinimized();

  window_properties_.clear();
  std::copy(atom_list.begin(), atom_list.end(),
            inserter(window_properties_, window_properties_.begin()));

  // Propagate the window minimization information to the client.
  bool is_minimized = IsMinimized();
  ui::PlatformWindowState state;
  // Check state after minimization/restore.
  if (is_minimized != was_minimized) {
    if (is_minimized) {
      state = ui::PlatformWindowState::PLATFORM_WINDOW_STATE_MINIMIZED;
    } else {
      // When the window is recovered from minimized state, set state to the
      // previous state.
      state = IsMaximized()
                  ? ui::PlatformWindowState::PLATFORM_WINDOW_STATE_MAXIMIZED
                  : ui::PlatformWindowState::PLATFORM_WINDOW_STATE_NORMAL;
    }
    delegate_->OnWindowStateChanged(state);
    return;
  }

  // If it hasn't been a restore or minimize state, then check if the window
  // has been maximized or restored.
  state = ui::PlatformWindowState::PLATFORM_WINDOW_STATE_NORMAL;
  if (IsMaximized())
    state = ui::PlatformWindowState::PLATFORM_WINDOW_STATE_MAXIMIZED;

  // If the window is in the fullscreen mode, there is no need to notify the
  // client about the state as long as it has been the client who has changed
  // the state.
  if (!IsFullScreen())
    delegate_->OnWindowStateChanged(state);
}

bool X11WindowBase::HasWMSpecProperty(const char* property) const {
  return window_properties_.find(atom_cache_.GetAtom(property)) !=
         window_properties_.end();
}

bool X11WindowBase::IsMinimized() const {
  return HasWMSpecProperty("_NET_WM_STATE_HIDDEN");
}

bool X11WindowBase::IsMaximized() const {
  return (HasWMSpecProperty("_NET_WM_STATE_MAXIMIZED_VERT") &&
          HasWMSpecProperty("_NET_WM_STATE_MAXIMIZED_HORZ"));
}

bool X11WindowBase::IsFullScreen() const {
  return is_fullscreen_;
}

}  // namespace ui
