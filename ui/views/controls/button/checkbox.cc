// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/button/checkbox.h"

#include <stddef.h>

#include <utility>

#include "ui/accessibility/ax_node_data.h"
#include "ui/base/material_design/material_design_controller.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/resources/grit/ui_resources.h"
#include "ui/views/animation/ink_drop_ripple.h"
#include "ui/views/animation/square_ink_drop_ripple.h"
#include "ui/views/controls/button/label_button_border.h"
#include "ui/views/painter.h"
#include "ui/views/resources/grit/views_resources.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/vector_icons.h"

namespace views {

// static
const char Checkbox::kViewClassName[] = "Checkbox";

Checkbox::Checkbox(const base::string16& label)
    : LabelButton(NULL, label),
      checked_(false) {
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetFocusForPlatform();
  SetFocusPainter(nullptr);

  if (UseMd()) {
    set_request_focus_on_press(false);
    SetInkDropMode(InkDropMode::ON);
    set_has_ink_drop_action_on_click(true);
  } else {
    std::unique_ptr<LabelButtonBorder> button_border(new LabelButtonBorder());
    // Inset the trailing side by a couple pixels for the focus border.
    button_border->set_insets(gfx::Insets(0, 0, 0, 2));
    SetBorder(std::move(button_border));
    set_request_focus_on_press(true);

    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();

    // Unchecked/Unfocused images.
    SetCustomImage(false, false, STATE_NORMAL,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX));
    SetCustomImage(false, false, STATE_HOVERED,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_HOVER));
    SetCustomImage(false, false, STATE_PRESSED,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_PRESSED));
    SetCustomImage(false, false, STATE_DISABLED,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_DISABLED));

    // Checked/Unfocused images.
    SetCustomImage(true, false, STATE_NORMAL,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_CHECKED));
    SetCustomImage(true, false, STATE_HOVERED,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_CHECKED_HOVER));
    SetCustomImage(true, false, STATE_PRESSED,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_CHECKED_PRESSED));
    SetCustomImage(true, false, STATE_DISABLED,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_CHECKED_DISABLED));

    // Unchecked/Focused images.
    SetCustomImage(false, true, STATE_NORMAL,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_FOCUSED));
    SetCustomImage(false, true, STATE_HOVERED,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_FOCUSED_HOVER));
    SetCustomImage(false, true, STATE_PRESSED,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_FOCUSED_PRESSED));

    // Checked/Focused images.
    SetCustomImage(true, true, STATE_NORMAL,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_FOCUSED_CHECKED));
    SetCustomImage(true, true, STATE_HOVERED,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_FOCUSED_CHECKED_HOVER));
    SetCustomImage(true, true, STATE_PRESSED,
                   *rb.GetImageSkiaNamed(IDR_CHECKBOX_FOCUSED_CHECKED_PRESSED));
  }

  // Limit the checkbox height to match the legacy appearance.
  const gfx::Size preferred_size(LabelButton::GetPreferredSize());
  SetMinSize(gfx::Size(0, preferred_size.height() + 4));
}

Checkbox::~Checkbox() {
}

void Checkbox::SetChecked(bool checked) {
  checked_ = checked;
  UpdateImage();
}

// static
bool Checkbox::UseMd() {
  return ui::MaterialDesignController::IsSecondaryUiMaterial();
}

const char* Checkbox::GetClassName() const {
  return kViewClassName;
}

void Checkbox::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  LabelButton::GetAccessibleNodeData(node_data);
  node_data->role = ui::AX_ROLE_CHECK_BOX;
  const ui::AXCheckedState checked_state =
      checked() ? ui::AX_CHECKED_STATE_TRUE : ui::AX_CHECKED_STATE_FALSE;
  node_data->AddIntAttribute(ui::AX_ATTR_CHECKED_STATE, checked_state);
  if (enabled()) {
    if (checked()) {
      node_data->AddIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB,
                                 ui::AX_DEFAULT_ACTION_VERB_UNCHECK);
    } else {
      node_data->AddIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB,
                                 ui::AX_DEFAULT_ACTION_VERB_CHECK);
    }
  }
}

void Checkbox::OnPaint(gfx::Canvas* canvas) {
  LabelButton::OnPaint(canvas);

  if (!UseMd() || !HasFocus())
    return;

  cc::PaintFlags focus_flags;
  focus_flags.setAntiAlias(true);
  focus_flags.setColor(
      SkColorSetA(GetNativeTheme()->GetSystemColor(
                      ui::NativeTheme::kColorId_FocusedBorderColor),
                  0x66));
  focus_flags.setStyle(cc::PaintFlags::kStroke_Style);
  focus_flags.setStrokeWidth(2);
  PaintFocusRing(canvas, focus_flags);
}

void Checkbox::OnFocus() {
  LabelButton::OnFocus();
  if (!UseMd())
    UpdateImage();
}

void Checkbox::OnBlur() {
  LabelButton::OnBlur();
  if (!UseMd())
    UpdateImage();
}

void Checkbox::OnNativeThemeChanged(const ui::NativeTheme* theme) {
  LabelButton::OnNativeThemeChanged(theme);
  if (UseMd())
    UpdateImage();
}

std::unique_ptr<InkDropRipple> Checkbox::CreateInkDropRipple() const {
  // The "small" size is 21dp, the large size is 1.33 * 21dp = 28dp.
  const gfx::Size size(21, 21);
  std::unique_ptr<InkDropRipple> ripple(new SquareInkDropRipple(
      CalculateLargeInkDropSize(size), kInkDropLargeCornerRadius, size,
      kInkDropSmallCornerRadius, image()->GetMirroredBounds().CenterPoint(),
      GetInkDropBaseColor(), ink_drop_visible_opacity()));
  return ripple;
}

SkColor Checkbox::GetInkDropBaseColor() const {
  return GetNativeTheme()->GetSystemColor(
      ui::NativeTheme::kColorId_ButtonEnabledColor);
}

gfx::ImageSkia Checkbox::GetImage(ButtonState for_state) const {
  if (UseMd()) {
    return gfx::CreateVectorIcon(
        GetVectorIcon(), 16,
        // When not checked, the icon color matches the button text color.
        GetNativeTheme()->GetSystemColor(
            checked_ ? ui::NativeTheme::kColorId_FocusedBorderColor
                     : ui::NativeTheme::kColorId_ButtonEnabledColor));
  }

  const size_t checked_index = checked_ ? 1 : 0;
  const size_t focused_index = HasFocus() ? 1 : 0;
  if (for_state != STATE_NORMAL &&
      images_[checked_index][focused_index][for_state].isNull())
    return images_[checked_index][focused_index][STATE_NORMAL];
  return images_[checked_index][focused_index][for_state];
}

void Checkbox::SetCustomImage(bool checked,
                              bool focused,
                              ButtonState for_state,
                              const gfx::ImageSkia& image) {
  const size_t checked_index = checked ? 1 : 0;
  const size_t focused_index = focused ? 1 : 0;
  images_[checked_index][focused_index][for_state] = image;
  UpdateImage();
}

void Checkbox::PaintFocusRing(gfx::Canvas* canvas,
                              const cc::PaintFlags& flags) {
  gfx::RectF focus_rect(image()->bounds());
  canvas->DrawRoundRect(focus_rect, 2.f, flags);
}

const gfx::VectorIcon& Checkbox::GetVectorIcon() const {
  return checked() ? kCheckboxActiveIcon : kCheckboxNormalIcon;
}

void Checkbox::NotifyClick(const ui::Event& event) {
  SetChecked(!checked());
  LabelButton::NotifyClick(event);
}

ui::NativeTheme::Part Checkbox::GetThemePart() const {
  return ui::NativeTheme::kCheckbox;
}

void Checkbox::GetExtraParams(ui::NativeTheme::ExtraParams* params) const {
  LabelButton::GetExtraParams(params);
  params->button.checked = checked_;
}

}  // namespace views
