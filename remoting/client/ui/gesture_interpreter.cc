// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/client/ui/gesture_interpreter.h"

#include "base/bind.h"
#include "base/time/time.h"
#include "remoting/client/chromoting_session.h"
#include "remoting/client/ui/direct_input_strategy.h"
#include "remoting/client/ui/renderer_proxy.h"
#include "remoting/client/ui/trackpad_input_strategy.h"

namespace {

const float kOneFingerFlingTimeConstant = 325.f;
const float kScrollFlingTimeConstant = 120.f;

}  // namespace

namespace remoting {
GestureInterpreter::GestureInterpreter(RendererProxy* renderer,
                                       ChromotingSession* input_stub)
    : renderer_(renderer),
      input_stub_(input_stub),
      pan_animation_(kOneFingerFlingTimeConstant,
                     base::Bind(&GestureInterpreter::PanWithoutAbortAnimations,
                                base::Unretained(this))),
      scroll_animation_(
          kScrollFlingTimeConstant,
          base::Bind(&GestureInterpreter::ScrollWithoutAbortAnimations,
                     base::Unretained(this))) {
  viewport_.RegisterOnTransformationChangedCallback(
      base::Bind(&RendererProxy::SetTransformation,
                 base::Unretained(renderer_)),
      true);
}

GestureInterpreter::~GestureInterpreter() {}

void GestureInterpreter::SetInputMode(InputMode mode) {
  switch (mode) {
    case DIRECT_INPUT_MODE:
      input_strategy_.reset(new DirectInputStrategy());
      break;
    case TRACKPAD_INPUT_MODE:
      input_strategy_.reset(new TrackpadInputStrategy(viewport_));
      break;
    default:
      NOTREACHED();
  }
  input_mode_ = mode;
  renderer_->SetCursorVisibility(input_strategy_->IsCursorVisible());
  ViewMatrix::Point cursor_position = input_strategy_->GetCursorPosition();
  renderer_->SetCursorPosition(cursor_position.x, cursor_position.y);
}

GestureInterpreter::InputMode GestureInterpreter::GetInputMode() const {
  return input_mode_;
}

void GestureInterpreter::Zoom(float pivot_x,
                              float pivot_y,
                              float scale,
                              GestureState state) {
  AbortAnimations();
  SetGestureInProgress(InputStrategy::ZOOM, state != GESTURE_ENDED);
  input_strategy_->HandleZoom({pivot_x, pivot_y}, scale, &viewport_);
}

void GestureInterpreter::Pan(float translation_x, float translation_y) {
  AbortAnimations();
  PanWithoutAbortAnimations(translation_x, translation_y);
}

void GestureInterpreter::Tap(float x, float y) {
  AbortAnimations();

  ViewMatrix::Point cursor_position = TrackAndGetPosition(x, y);
  StartInputFeedback(cursor_position.x, cursor_position.y,
                     InputStrategy::TAP_FEEDBACK);
  InjectMouseClick(cursor_position.x, cursor_position.y,
                   protocol::MouseEvent_MouseButton_BUTTON_LEFT);
}

void GestureInterpreter::TwoFingerTap(float x, float y) {
  AbortAnimations();

  ViewMatrix::Point cursor_position = TrackAndGetPosition(x, y);
  InjectMouseClick(cursor_position.x, cursor_position.y,
                   protocol::MouseEvent_MouseButton_BUTTON_RIGHT);
}

void GestureInterpreter::Drag(float x, float y, GestureState state) {
  AbortAnimations();

  ViewMatrix::Point cursor_position = TrackAndGetPosition(x, y);

  if (state == GESTURE_BEGAN) {
    StartInputFeedback(cursor_position.x, cursor_position.y,
                       InputStrategy::DRAG_FEEDBACK);
  }

  bool is_dragging_mode = state != GESTURE_ENDED;
  SetGestureInProgress(InputStrategy::DRAG, is_dragging_mode);
  input_stub_->SendMouseEvent(cursor_position.x, cursor_position.y,
                              protocol::MouseEvent_MouseButton_BUTTON_LEFT,
                              is_dragging_mode);
}

void GestureInterpreter::OneFingerFling(float velocity_x, float velocity_y) {
  AbortAnimations();
  pan_animation_.SetVelocity(velocity_x, velocity_y);
  pan_animation_.Tick();
}

void GestureInterpreter::Scroll(float x, float y, float dx, float dy) {
  AbortAnimations();

  ViewMatrix::Point cursor_position = TrackAndGetPosition(x, y);

  // Inject the cursor position to the host so that scrolling can happen on the
  // right place.
  InjectCursorPosition(cursor_position.x, cursor_position.y);

  ScrollWithoutAbortAnimations(dx, dy);
}

void GestureInterpreter::ScrollWithVelocity(float velocity_x,
                                            float velocity_y) {
  AbortAnimations();

  scroll_animation_.SetVelocity(velocity_x, velocity_y);
  scroll_animation_.Tick();
}

void GestureInterpreter::ProcessAnimations() {
  pan_animation_.Tick();
  scroll_animation_.Tick();
}

void GestureInterpreter::OnSurfaceSizeChanged(int width, int height) {
  viewport_.SetSurfaceSize(width, height);
}

void GestureInterpreter::OnDesktopSizeChanged(int width, int height) {
  viewport_.SetDesktopSize(width, height);
}

void GestureInterpreter::PanWithoutAbortAnimations(float translation_x,
                                                   float translation_y) {
  if (input_strategy_->HandlePan({translation_x, translation_y},
                                 gesture_in_progress_, &viewport_)) {
    // Cursor position changed.
    ViewMatrix::Point cursor_position = input_strategy_->GetCursorPosition();
    if (gesture_in_progress_ != InputStrategy::DRAG) {
      // Drag() will inject the position so don't need to do that in that case.
      InjectCursorPosition(cursor_position.x, cursor_position.y);
    }
    renderer_->SetCursorPosition(cursor_position.x, cursor_position.y);
  }
}

void GestureInterpreter::InjectCursorPosition(float x, float y) {
  input_stub_->SendMouseEvent(
      x, y, protocol::MouseEvent_MouseButton_BUTTON_UNDEFINED, false);
}

void GestureInterpreter::ScrollWithoutAbortAnimations(float dx, float dy) {
  ViewMatrix::Point desktopDelta =
      input_strategy_->MapScreenVectorToDesktop({dx, dy}, viewport_);
  input_stub_->SendMouseWheelEvent(desktopDelta.x, desktopDelta.y);
}

void GestureInterpreter::AbortAnimations() {
  pan_animation_.Abort();
}

void GestureInterpreter::InjectMouseClick(
    float x,
    float y,
    protocol::MouseEvent_MouseButton button) {
  input_stub_->SendMouseEvent(x, y, button, true);
  input_stub_->SendMouseEvent(x, y, button, false);
}

void GestureInterpreter::SetGestureInProgress(InputStrategy::Gesture gesture,
                                              bool is_in_progress) {
  if (!is_in_progress && gesture_in_progress_ == gesture) {
    gesture_in_progress_ = InputStrategy::NONE;
    return;
  }
  gesture_in_progress_ = gesture;
}

ViewMatrix::Point GestureInterpreter::TrackAndGetPosition(float touch_x,
                                                          float touch_y) {
  input_strategy_->TrackTouchInput({touch_x, touch_y}, viewport_);
  return input_strategy_->GetCursorPosition();
}

void GestureInterpreter::StartInputFeedback(
    float cursor_x,
    float cursor_y,
    InputStrategy::InputFeedbackType feedback_type) {
  // This radius is on the view's coordinates. Need to be converted to desktop
  // coordinate.
  float feedback_radius = input_strategy_->GetFeedbackRadius(feedback_type);
  if (feedback_radius > 0) {
    // TODO(yuweih): The renderer takes diameter as parameter. Consider moving
    // the *2 logic inside the renderer.
    float diameter_on_desktop =
        2.f * feedback_radius / viewport_.GetTransformation().GetScale();
    renderer_->StartInputFeedback(cursor_x, cursor_y, diameter_on_desktop);
  }
}

}  // namespace remoting
