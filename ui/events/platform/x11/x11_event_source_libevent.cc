// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/events/platform/x11/x11_event_source_libevent.h"

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#include <memory>

#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/keyboard_code_conversion_x.h"
#include "ui/events/platform/platform_event_dispatcher.h"
#include "ui/events/x/events_x_utils.h"

namespace ui {

namespace {

// Translates XI2 XEvent into a ui::Event.
std::unique_ptr<ui::Event> TranslateXI2EventToEvent(const XEvent& xev) {
  EventType event_type = EventTypeFromXEvent(xev);
  switch (event_type) {
    case ET_KEY_PRESSED:
    case ET_KEY_RELEASED:
      return base::MakeUnique<KeyEvent>(event_type,
                                        KeyboardCodeFromXKeyEvent(&xev),
                                        EventFlagsFromXEvent(xev));
    case ET_MOUSE_PRESSED:
    case ET_MOUSE_MOVED:
    case ET_MOUSE_DRAGGED:
    case ET_MOUSE_RELEASED:
      return base::MakeUnique<MouseEvent>(
          event_type, EventLocationFromXEvent(xev),
          EventSystemLocationFromXEvent(xev), EventTimeFromXEvent(xev),
          EventFlagsFromXEvent(xev), GetChangedMouseButtonFlagsFromXEvent(xev));
    case ET_MOUSEWHEEL:
      return base::MakeUnique<MouseWheelEvent>(
          GetMouseWheelOffsetFromXEvent(xev), EventLocationFromXEvent(xev),
          EventSystemLocationFromXEvent(xev), EventTimeFromXEvent(xev),
          EventFlagsFromXEvent(xev), GetChangedMouseButtonFlagsFromXEvent(xev));
    case ET_SCROLL_FLING_START:
    case ET_SCROLL_FLING_CANCEL: {
      float x_offset, y_offset, x_offset_ordinal, y_offset_ordinal;
      GetFlingDataFromXEvent(xev, &x_offset, &y_offset, &x_offset_ordinal,
                             &y_offset_ordinal, nullptr);
      return base::MakeUnique<ScrollEvent>(
          event_type, EventLocationFromXEvent(xev), EventTimeFromXEvent(xev),
          EventFlagsFromXEvent(xev), x_offset, y_offset, x_offset_ordinal,
          y_offset_ordinal, 0);
    }
    case ET_SCROLL: {
      float x_offset, y_offset, x_offset_ordinal, y_offset_ordinal;
      int finger_count;
      GetScrollOffsetsFromXEvent(xev, &x_offset, &y_offset, &x_offset_ordinal,
                                 &y_offset_ordinal, &finger_count);
      return base::MakeUnique<ScrollEvent>(
          event_type, EventLocationFromXEvent(xev), EventTimeFromXEvent(xev),
          EventFlagsFromXEvent(xev), x_offset, y_offset, x_offset_ordinal,
          y_offset_ordinal, finger_count);
    }
    case ET_TOUCH_MOVED:
    case ET_TOUCH_PRESSED:
    case ET_TOUCH_CANCELLED:
    case ET_TOUCH_RELEASED:
      return base::MakeUnique<TouchEvent>(
          event_type, EventLocationFromXEvent(xev), EventTimeFromXEvent(xev),
          ui::PointerDetails(
              ui::EventPointerType::POINTER_TYPE_TOUCH,
              GetTouchIdFromXEvent(xev), GetTouchRadiusXFromXEvent(xev),
              GetTouchRadiusYFromXEvent(xev), GetTouchForceFromXEvent(xev)));
    case ET_UNKNOWN:
      return nullptr;
    default:
      break;
  }
  return nullptr;
}

// Translates a XEvent into a ui::Event.
std::unique_ptr<ui::Event> TranslateXEventToEvent(const XEvent& xev) {
  int flags = EventFlagsFromXEvent(xev);
  switch (xev.type) {
    case LeaveNotify:
    case EnterNotify:
      // EnterNotify creates ET_MOUSE_MOVED. Mark as synthesized as this is
      // not real mouse move event.
      if (xev.type == EnterNotify)
        flags |= EF_IS_SYNTHESIZED;
      return base::MakeUnique<MouseEvent>(ET_MOUSE_MOVED,
                                          EventLocationFromXEvent(xev),
                                          EventSystemLocationFromXEvent(xev),
                                          EventTimeFromXEvent(xev), flags, 0);

    case KeyPress:
    case KeyRelease:
      return base::MakeUnique<KeyEvent>(EventTypeFromXEvent(xev),
                                        KeyboardCodeFromXKeyEvent(&xev), flags);

    case ButtonPress:
    case ButtonRelease: {
      switch (EventTypeFromXEvent(xev)) {
        case ET_MOUSEWHEEL:
          return base::MakeUnique<MouseWheelEvent>(
              GetMouseWheelOffsetFromXEvent(xev), EventLocationFromXEvent(xev),
              EventSystemLocationFromXEvent(xev), EventTimeFromXEvent(xev),
              flags, 0);
        case ET_MOUSE_PRESSED:
        case ET_MOUSE_RELEASED:
          return base::MakeUnique<MouseEvent>(
              EventTypeFromXEvent(xev), EventLocationFromXEvent(xev),
              EventSystemLocationFromXEvent(xev), EventTimeFromXEvent(xev),
              flags, GetChangedMouseButtonFlagsFromXEvent(xev));
        case ET_UNKNOWN:
          // No event is created for X11-release events for mouse-wheel
          // buttons.
          break;
        default:
          NOTREACHED();
      }
      break;
    }

    case GenericEvent:
      return TranslateXI2EventToEvent(xev);
  }
  return nullptr;
}

}  // namespace

X11EventSourceLibevent::X11EventSourceLibevent(XDisplay* display)
    : event_source_(this, display), watcher_controller_(FROM_HERE) {
  AddEventWatcher();
}

X11EventSourceLibevent::~X11EventSourceLibevent() {}

// static
X11EventSourceLibevent* X11EventSourceLibevent::GetInstance() {
  return static_cast<X11EventSourceLibevent*>(
      PlatformEventSource::GetInstance());
}

void X11EventSourceLibevent::AddXEventDispatcher(XEventDispatcher* dispatcher) {
  dispatchers_xevent_.AddObserver(dispatcher);
}

void X11EventSourceLibevent::RemoveXEventDispatcher(
    XEventDispatcher* dispatcher) {
  dispatchers_xevent_.RemoveObserver(dispatcher);
}

void X11EventSourceLibevent::ProcessXEvent(XEvent* xevent) {
  std::unique_ptr<ui::Event> translated_event = TranslateXEventToEvent(*xevent);
  if (translated_event) {
    EventWithPlatformEvent ewxe = {translated_event.get(), xevent};
    DispatchEvent(&ewxe);
  } else {
    // Only if we can't translate XEvent into ui::Event, try to dispatch XEvent
    // directly to XEventDispatchers.
    DispatchXEventToXEventDispatchers(xevent);
  }
}

void X11EventSourceLibevent::AddEventWatcher() {
  if (initialized_)
    return;
  if (!base::MessageLoop::current())
    return;

  int fd = ConnectionNumber(event_source_.display());
  base::MessageLoopForUI::current()->WatchFileDescriptor(
      fd, true, base::MessagePumpLibevent::WATCH_READ, &watcher_controller_,
      this);
  initialized_ = true;
}

void X11EventSourceLibevent::DispatchXEventToXEventDispatchers(XEvent* xevent) {
  for (XEventDispatcher& dispatcher : dispatchers_xevent_) {
    if (dispatcher.DispatchXEvent(xevent))
      break;
  }
}

void X11EventSourceLibevent::StopCurrentEventStream() {
  event_source_.StopCurrentEventStream();
}

void X11EventSourceLibevent::OnDispatcherListChanged() {
  AddEventWatcher();
  event_source_.OnDispatcherListChanged();
}

void X11EventSourceLibevent::OnFileCanReadWithoutBlocking(int fd) {
  event_source_.DispatchXEvents();
}

void X11EventSourceLibevent::OnFileCanWriteWithoutBlocking(int fd) {
  NOTREACHED();
}

}  // namespace ui
