// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_INPUT_SCROLLBAR_ANIMATION_CONTROLLER_H_
#define CC_INPUT_SCROLLBAR_ANIMATION_CONTROLLER_H_

#include "base/cancelable_callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "cc/cc_export.h"
#include "cc/input/single_scrollbar_animation_controller_thinning.h"
#include "cc/layers/layer_impl.h"
#include "cc/layers/scrollbar_layer_impl_base.h"
#include "ui/gfx/geometry/vector2d_f.h"

namespace cc {

class CC_EXPORT ScrollbarAnimationControllerClient {
 public:
  virtual void PostDelayedScrollbarAnimationTask(const base::Closure& task,
                                                 base::TimeDelta delay) = 0;
  virtual void SetNeedsRedrawForScrollbarAnimation() = 0;
  virtual void SetNeedsAnimateForScrollbarAnimation() = 0;
  virtual void DidChangeScrollbarVisibility() = 0;
  virtual ScrollbarSet ScrollbarsFor(ElementId scroll_element_id) const = 0;

 protected:
  virtual ~ScrollbarAnimationControllerClient() {}
};

// This class show scrollbars when scroll and fade out after an idle delay.
// The fade animations works on both scrollbars and is controlled in this class
// This class also passes the mouse state to each
// SingleScrollbarAnimationControllerThinning. The thinning animations are
// independent between vertical/horizontal and are managed by the
// SingleScrollbarAnimationControllerThinnings.
class CC_EXPORT ScrollbarAnimationController {
 public:
  // ScrollbarAnimationController for Android. It only has show & fade out
  // animation.
  static std::unique_ptr<ScrollbarAnimationController>
  CreateScrollbarAnimationControllerAndroid(
      ElementId scroll_element_id,
      ScrollbarAnimationControllerClient* client,
      base::TimeDelta fade_delay,
      base::TimeDelta fade_duration,
      float initial_opacity);

  // ScrollbarAnimationController for Desktop Overlay Scrollbar. It has show &
  // fade out animation and thinning animation.
  static std::unique_ptr<ScrollbarAnimationController>
  CreateScrollbarAnimationControllerAuraOverlay(
      ElementId scroll_element_id,
      ScrollbarAnimationControllerClient* client,
      base::TimeDelta fade_delay,
      base::TimeDelta fade_duration,
      base::TimeDelta thinning_duration,
      float initial_opacity);

  ~ScrollbarAnimationController();

  bool ScrollbarsHidden() const;

  bool Animate(base::TimeTicks now);

  // WillUpdateScroll expects to be called even if the scroll position won't
  // change as a result of the scroll. Only effect Aura Overlay Scrollbar.
  void WillUpdateScroll();

  // DidScrollUpdate expects to be called only if the scroll position change.
  // Effect both Android and Aura Overlay Scrollbar.
  void DidScrollUpdate();

  void DidScrollBegin();
  void DidScrollEnd();

  void DidMouseDown();
  void DidMouseUp();
  void DidMouseLeave();
  void DidMouseMove(const gfx::PointF& device_viewport_point);

  // Called when Blink wants to show the scrollbars (via
  // ScrollableArea::showOverlayScrollbars).
  void DidRequestShowFromMainThread();

  // These methods are public for testing.
  bool MouseIsOverScrollbarThumb(ScrollbarOrientation orientation) const;
  bool MouseIsNearScrollbarThumb(ScrollbarOrientation orientation) const;
  bool MouseIsNearScrollbar(ScrollbarOrientation orientation) const;
  bool MouseIsNearAnyScrollbar() const;

  ScrollbarSet Scrollbars() const;

  static constexpr float kMouseMoveDistanceToTriggerFadeIn = 30.0f;

 private:
  // Describes whether the current animation should FadeIn or FadeOut.
  enum AnimationChange { NONE, FADE_IN, FADE_OUT };

  ScrollbarAnimationController(ElementId scroll_element_id,
                               ScrollbarAnimationControllerClient* client,
                               base::TimeDelta fade_delay,
                               base::TimeDelta fade_duration,
                               float initial_opacity);

  ScrollbarAnimationController(ElementId scroll_element_id,
                               ScrollbarAnimationControllerClient* client,
                               base::TimeDelta fade_delay,
                               base::TimeDelta fade_duration,
                               base::TimeDelta thinning_duration,
                               float initial_opacity);

  SingleScrollbarAnimationControllerThinning& GetScrollbarAnimationController(
      ScrollbarOrientation) const;

  // Returns how far through the animation we are as a progress value from
  // 0 to 1.
  float AnimationProgressAtTime(base::TimeTicks now);
  void RunAnimationFrame(float progress);

  void StartAnimation();
  void StopAnimation();

  void Show();

  void PostDelayedAnimation(AnimationChange animation_change);

  bool Captured() const;

  void ApplyOpacityToScrollbars(float opacity);

  ScrollbarAnimationControllerClient* client_;

  base::TimeTicks last_awaken_time_;

  base::TimeDelta fade_delay_;

  base::TimeDelta fade_duration_;

  bool need_trigger_scrollbar_fade_in_;

  bool is_animating_;
  AnimationChange animation_change_;

  const ElementId scroll_element_id_;
  bool currently_scrolling_;
  bool show_in_fast_scroll_;

  base::CancelableClosure delayed_scrollbar_animation_;

  float opacity_;

  const bool show_scrollbars_on_scroll_gesture_;
  const bool need_thinning_animation_;

  std::unique_ptr<SingleScrollbarAnimationControllerThinning>
      vertical_controller_;
  std::unique_ptr<SingleScrollbarAnimationControllerThinning>
      horizontal_controller_;

  base::WeakPtrFactory<ScrollbarAnimationController> weak_factory_;
};

}  // namespace cc

#endif  // CC_INPUT_SCROLLBAR_ANIMATION_CONTROLLER_H_
