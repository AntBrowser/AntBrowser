// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_REPLACED_PAINTER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_REPLACED_PAINTER_H_

#include "third_party/blink/renderer/platform/geometry/layout_point.h"
#include "third_party/blink/renderer/platform/wtf/allocator.h"

namespace blink {

struct PaintInfo;
class PaintInfoWithOffset;
class LayoutReplaced;

class ReplacedPainter {
  STACK_ALLOCATED();

 public:
  ReplacedPainter(const LayoutReplaced& layout_replaced)
      : layout_replaced_(layout_replaced) {}

  void Paint(const PaintInfo&);

  bool ShouldPaint(const PaintInfoWithOffset&) const;

 private:
  // Paint a hit test display item and record hit test data. This should be
  // called in the background paint phase even if there is no other painted
  // content.
  void RecordHitTestData(const PaintInfo&, const LayoutPoint& paint_offset);
  const LayoutReplaced& layout_replaced_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_REPLACED_PAINTER_H_
