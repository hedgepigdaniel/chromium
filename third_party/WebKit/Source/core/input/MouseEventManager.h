// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MouseEventManager_h
#define MouseEventManager_h

#include "core/CoreExport.h"
#include "core/dom/SynchronousMutationObserver.h"
#include "core/input/BoundaryEventDispatcher.h"
#include "core/page/DragActions.h"
#include "core/page/EventWithHitTestResults.h"
#include "platform/Timer.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/Time.h"
#include "public/platform/WebInputEventResult.h"
#include "public/platform/WebMouseEvent.h"

namespace blink {

class ContainerNode;
class DragState;
class DataTransfer;
class Element;
class FloatQuad;
class HitTestResult;
class InputDeviceCapabilities;
class LocalFrame;
class ScrollManager;

enum class DragInitiator;

// This class takes care of dispatching all mouse events and keeps track of
// positions and states of mouse.
class CORE_EXPORT MouseEventManager final
    : public GarbageCollectedFinalized<MouseEventManager>,
      public SynchronousMutationObserver {
  WTF_MAKE_NONCOPYABLE(MouseEventManager);
  USING_GARBAGE_COLLECTED_MIXIN(MouseEventManager);

 public:
  MouseEventManager(LocalFrame&, ScrollManager&);
  virtual ~MouseEventManager();
  DECLARE_TRACE();

  WebInputEventResult DispatchMouseEvent(EventTarget*,
                                         const AtomicString&,
                                         const WebMouseEvent&,
                                         const String& canvas_region_id,
                                         EventTarget* related_target,
                                         bool check_for_listener = false);

  WebInputEventResult SetMousePositionAndDispatchMouseEvent(
      Node* target_node,
      const String& canvas_region_id,
      const AtomicString& event_type,
      const WebMouseEvent&);

  WebInputEventResult DispatchMouseClickIfNeeded(
      const MouseEventWithHitTestResults&,
      Element& mouse_release_target);

  WebInputEventResult DispatchDragSrcEvent(const AtomicString& event_type,
                                           const WebMouseEvent&);
  WebInputEventResult DispatchDragEvent(const AtomicString& event_type,
                                        Node* target,
                                        Node* related_target,
                                        const WebMouseEvent&,
                                        DataTransfer*);

  // Resets the internal state of this object.
  void Clear();

  void SendBoundaryEvents(EventTarget* exited_target,
                          EventTarget* entered_target,
                          const String& canvas_region_id,
                          const WebMouseEvent&);

  void SetNodeUnderMouse(Node*,
                         const String& canvas_region_id,
                         const WebMouseEvent&);

  WebInputEventResult HandleMouseFocus(
      const HitTestResult&,
      InputDeviceCapabilities* source_capabilities);

  void FakeMouseMoveEventTimerFired(TimerBase*);

  void CancelFakeMouseMoveEvent();
  void DispatchFakeMouseMoveEventSoon();
  void DispatchFakeMouseMoveEventSoonInQuad(const FloatQuad&);

  void SetLastKnownMousePosition(const WebMouseEvent&);

  bool HandleDragDropIfPossible(const GestureEventWithHitTestResults&);

  WebInputEventResult HandleMouseDraggedEvent(
      const MouseEventWithHitTestResults&);
  WebInputEventResult HandleMousePressEvent(
      const MouseEventWithHitTestResults&);
  WebInputEventResult HandleMouseReleaseEvent(
      const MouseEventWithHitTestResults&);

  DragState& GetDragState();

  void FocusDocumentView();

  // Resets the state that indicates the next events could cause a drag. It is
  // called when we realize the next events should not cause drag based on the
  // drag heuristics.
  void ClearDragHeuristicState();

  void DragSourceEndedAt(const WebMouseEvent&, DragOperation);

  void UpdateSelectionForMouseDrag();

  void HandleMousePressEventUpdateStates(const WebMouseEvent&);
  void HandleMouseReleaseEventUpdateStates();

  // Returns whether pan is handled and resets the state on release.
  bool HandleSvgPanIfNeeded(bool is_release_event);

  void InvalidateClick();

  // TODO: These functions ideally should be private but the code needs more
  // refactoring to be able to remove the dependency from EventHandler.
  Node* GetNodeUnderMouse();
  bool IsMousePositionUnknown();
  IntPoint LastKnownMousePosition();

  bool MousePressed();
  void SetMousePressed(bool);

  bool CapturesDragging() const;
  void SetCapturesDragging(bool);

  void SetMouseDownMayStartAutoscroll() {
    mouse_down_may_start_autoscroll_ = true;
  }

  Node* MousePressNode();
  void SetMousePressNode(Node*);

  void SetClickElement(Element*);
  void SetClickCount(int);

  bool MouseDownMayStartDrag();

 private:
  class MouseEventBoundaryEventDispatcher : public BoundaryEventDispatcher {
    WTF_MAKE_NONCOPYABLE(MouseEventBoundaryEventDispatcher);

   public:
    MouseEventBoundaryEventDispatcher(MouseEventManager*,
                                      const WebMouseEvent*,
                                      EventTarget* exited_target,
                                      const String& canvas_region_id);

   protected:
    void DispatchOut(EventTarget*, EventTarget* related_target) override;
    void DispatchOver(EventTarget*, EventTarget* related_target) override;
    void DispatchLeave(EventTarget*,
                       EventTarget* related_target,
                       bool check_for_listener) override;
    void DispatchEnter(EventTarget*,
                       EventTarget* related_target,
                       bool check_for_listener) override;
    AtomicString GetLeaveEvent() override;
    AtomicString GetEnterEvent() override;

   private:
    void Dispatch(EventTarget*,
                  EventTarget* related_target,
                  const AtomicString&,
                  const String& canvas_region_id,
                  const WebMouseEvent&,
                  bool check_for_listener);
    Member<MouseEventManager> mouse_event_manager_;
    const WebMouseEvent* web_mouse_event_;
    Member<EventTarget> exited_target_;
    String canvas_region_id_;
  };

  // If the given element is a shadow host and its root has delegatesFocus=false
  // flag, slide focus to its inner element. Returns true if the resulting focus
  // is different from the given element.
  bool SlideFocusOnShadowHostIfNecessary(const Element&);

  bool DragThresholdExceeded(const IntPoint&) const;
  bool HandleDrag(const MouseEventWithHitTestResults&, DragInitiator);
  bool TryStartDrag(const MouseEventWithHitTestResults&);
  void ClearDragDataTransfer();
  DataTransfer* CreateDraggingDataTransfer() const;

  void ResetDragState();

  // Implementations of |SynchronousMutationObserver|
  void NodeChildrenWillBeRemoved(ContainerNode&) final;
  void NodeWillBeRemoved(Node& node_to_be_removed) final;

  // NOTE: If adding a new field to this class please ensure that it is
  // cleared in |MouseEventManager::clear()|.

  const Member<LocalFrame> frame_;
  Member<ScrollManager> scroll_manager_;

  // The effective position of the mouse pointer.
  // See
  // https://w3c.github.io/pointerevents/#dfn-tracking-the-effective-position-of-the-legacy-mouse-pointer.
  Member<Node> node_under_mouse_;

  // The last mouse movement position this frame has seen in root frame
  // coordinates.
  IntPoint last_known_mouse_position_;
  IntPoint last_known_mouse_global_position_;

  unsigned is_mouse_position_unknown_ : 1;
  // Current button-press state for mouse/mouse-like-stylus.
  // TODO(crbug.com/563676): Buggy for chorded buttons.
  unsigned mouse_pressed_ : 1;

  unsigned mouse_down_may_start_autoscroll_ : 1;
  unsigned svg_pan_ : 1;
  unsigned captures_dragging_ : 1;
  unsigned mouse_down_may_start_drag_ : 1;

  Member<Node> mouse_press_node_;

  int click_count_;
  Member<Element> click_element_;

  IntPoint mouse_down_pos_;  // In our view's coords.
  TimeTicks mouse_down_timestamp_;
  WebMouseEvent mouse_down_;

  LayoutPoint drag_start_pos_;

  TaskRunnerTimer<MouseEventManager> fake_mouse_move_event_timer_;
};

}  // namespace blink

#endif  // MouseEventManager_h
