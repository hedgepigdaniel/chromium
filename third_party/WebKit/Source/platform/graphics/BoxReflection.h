// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BoxReflection_h
#define BoxReflection_h

#include "platform/PlatformExport.h"
#include "platform/graphics/paint/PaintRecord.h"
#include "third_party/skia/include/core/SkRefCnt.h"

class SkMatrix;

namespace blink {

class FloatRect;

// A reflection, as created by -webkit-box-reflect. Consists of:
// * a direction (either vertical or horizontal)
// * an offset to be applied to the reflection after flipping about the
//   x- or y-axis, according to the direction
// * a mask image, which will be applied to the reflection before the
//   reflection matrix is applied
class PLATFORM_EXPORT BoxReflection {
 public:
  enum ReflectionDirection {
    // Vertically flipped (to appear above or below).
    kVerticalReflection,
    // Horizontally flipped (to appear to the left or right).
    kHorizontalReflection,
  };

  BoxReflection(ReflectionDirection direction,
                float offset,
                sk_sp<PaintRecord> mask = nullptr)
      : direction_(direction), offset_(offset), mask_(std::move(mask)) {}

  ReflectionDirection Direction() const { return direction_; }
  float Offset() const { return offset_; }
  const sk_sp<PaintRecord>& Mask() const { return mask_; }

  // Returns a matrix which maps points between the original content and its
  // reflection. Reflections are self-inverse, so this matrix can be used to
  // map in either direction.
  SkMatrix ReflectionMatrix() const;

  // Maps a source rectangle to the destination rectangle it can affect,
  // including this reflection. Due to the symmetry of reflections, this can
  // also be used to map from a destination rectangle to the source rectangle
  // which contributes to it.
  FloatRect MapRect(const FloatRect&) const;

 private:
  ReflectionDirection direction_;
  float offset_;
  sk_sp<PaintRecord> mask_;
};

inline bool operator==(const BoxReflection& a, const BoxReflection& b) {
  return a.Direction() == b.Direction() && a.Offset() == b.Offset() &&
         a.Mask() == b.Mask();
}

inline bool operator!=(const BoxReflection& a, const BoxReflection& b) {
  return !(a == b);
}

}  // namespace blink

#endif  // BoxReflection_h
