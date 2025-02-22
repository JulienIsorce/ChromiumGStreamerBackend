// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/paint/LayoutObjectDrawingRecorder.h"

#include "core/layout/LayoutTestHelper.h"
#include "core/layout/LayoutView.h"
#include "core/paint/PaintControllerPaintTest.h"
#include "core/paint/PaintLayer.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/paint/DrawingDisplayItem.h"
#include "platform/graphics/paint/PaintController.h"
#include <gtest/gtest.h>

namespace blink {

using LayoutObjectDrawingRecorderTest = PaintControllerPaintTest;
using LayoutObjectDrawingRecorderTestForSlimmingPaintV2 = PaintControllerPaintTestForSlimmingPaintV2;

namespace {

void drawNothing(GraphicsContext& context, const LayoutView& layoutView, PaintPhase phase, const LayoutRect& bound)
{
    if (LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(context, layoutView, phase, LayoutPoint()))
        return;

    LayoutObjectDrawingRecorder drawingRecorder(context, layoutView, phase, bound, LayoutPoint());
}

void drawRect(GraphicsContext& context, LayoutView& layoutView, PaintPhase phase, const LayoutRect& bound)
{
    if (LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(context, layoutView, phase, LayoutPoint()))
        return;
    LayoutObjectDrawingRecorder drawingRecorder(context, layoutView, phase, bound, LayoutPoint());
    IntRect rect(0, 0, 10, 10);
    context.drawRect(rect);
}

TEST_F(LayoutObjectDrawingRecorderTest, Nothing)
{
    GraphicsContext context(rootPaintController());
    LayoutRect bound = layoutView().viewRect();
    EXPECT_EQ((size_t)0, rootPaintController().displayItemList().size());

    drawNothing(context, layoutView(), PaintPhaseForeground, bound);
    rootPaintController().commitNewDisplayItems();
    EXPECT_DISPLAY_LIST(rootPaintController().displayItemList(), 1,
        TestDisplayItem(layoutView(), DisplayItem::paintPhaseToDrawingType(PaintPhaseForeground)));
    EXPECT_FALSE(static_cast<const DrawingDisplayItem&>(rootPaintController().displayItemList()[0]).picture());
}

TEST_F(LayoutObjectDrawingRecorderTest, Rect)
{
    GraphicsContext context(rootPaintController());
    LayoutRect bound = layoutView().viewRect();
    drawRect(context, layoutView(), PaintPhaseForeground, bound);
    rootPaintController().commitNewDisplayItems();
    EXPECT_DISPLAY_LIST(rootPaintController().displayItemList(), 1,
        TestDisplayItem(layoutView(), DisplayItem::paintPhaseToDrawingType(PaintPhaseForeground)));
}

TEST_F(LayoutObjectDrawingRecorderTest, Cached)
{
    GraphicsContext context(rootPaintController());
    LayoutRect bound = layoutView().viewRect();
    drawNothing(context, layoutView(), PaintPhaseBlockBackground, bound);
    drawRect(context, layoutView(), PaintPhaseForeground, bound);
    rootPaintController().commitNewDisplayItems();

    EXPECT_DISPLAY_LIST(rootPaintController().displayItemList(), 2,
        TestDisplayItem(layoutView(), DisplayItem::paintPhaseToDrawingType(PaintPhaseBlockBackground)),
        TestDisplayItem(layoutView(), DisplayItem::paintPhaseToDrawingType(PaintPhaseForeground)));

    drawNothing(context, layoutView(), PaintPhaseBlockBackground, bound);
    drawRect(context, layoutView(), PaintPhaseForeground, bound);

    EXPECT_DISPLAY_LIST(rootPaintController().newDisplayItemList(), 2,
        TestDisplayItem(layoutView(), DisplayItem::drawingTypeToCachedDrawingType(DisplayItem::paintPhaseToDrawingType(PaintPhaseBlockBackground))),
        TestDisplayItem(layoutView(), DisplayItem::drawingTypeToCachedDrawingType(DisplayItem::paintPhaseToDrawingType(PaintPhaseForeground))));

    rootPaintController().commitNewDisplayItems();

    EXPECT_DISPLAY_LIST(rootPaintController().displayItemList(), 2,
        TestDisplayItem(layoutView(), DisplayItem::paintPhaseToDrawingType(PaintPhaseBlockBackground)),
        TestDisplayItem(layoutView(), DisplayItem::paintPhaseToDrawingType(PaintPhaseForeground)));
}

template <typename T>
FloatRect drawAndGetCullRect(PaintController& controller, const LayoutObject& layoutObject, const T& bounds)
{
    controller.invalidateAll();
    {
        // Draw some things which will produce a non-null picture.
        GraphicsContext context(controller);
        LayoutObjectDrawingRecorder recorder(
            context, layoutObject, DisplayItem::BoxDecorationBackground, bounds, LayoutPoint());
        context.drawRect(enclosedIntRect(FloatRect(bounds)));
    }
    controller.commitNewDisplayItems();
    const auto& drawing = static_cast<const DrawingDisplayItem&>(controller.displayItemList()[0]);
    return drawing.picture()->cullRect();
}

TEST_F(LayoutObjectDrawingRecorderTest, CullRectMatchesProvidedClip)
{
    // It's safe for the picture's cull rect to be expanded (though doing so
    // excessively may harm performance), but it cannot be contracted.
    // For now, this test expects the two rects to match completely.
    //
    // This rect is chosen so that in the x direction, pixel snapping rounds in
    // the opposite direction to enclosing, and in the y direction, the edges
    // are exactly on a half-pixel boundary. The numbers chosen map nicely to
    // both float and LayoutUnit, to make equality checking reliable.
    FloatRect rect(20.75, -5.5, 5.375, 10);
    EXPECT_EQ(rect, drawAndGetCullRect(rootPaintController(), layoutView(), rect));
    EXPECT_EQ(rect, drawAndGetCullRect(rootPaintController(), layoutView(), LayoutRect(rect)));
}

TEST_F(LayoutObjectDrawingRecorderTest, PaintOffsetCache)
{
    RuntimeEnabledFeatures::setSlimmingPaintOffsetCachingEnabled(true);

    GraphicsContext context(rootPaintController());
    LayoutRect bounds = layoutView().viewRect();
    LayoutPoint paintOffset(1, 2);

    rootPaintController().invalidateAll();
    EXPECT_FALSE(LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(context, layoutView(), PaintPhaseForeground, paintOffset));
    {
        LayoutObjectDrawingRecorder drawingRecorder(context, layoutView(), PaintPhaseForeground, bounds, paintOffset);
        IntRect rect(0, 0, 10, 10);
        context.drawRect(rect);
    }

    rootPaintController().commitNewDisplayItems();
    EXPECT_DISPLAY_LIST(rootPaintController().displayItemList(), 1,
        TestDisplayItem(layoutView(), DisplayItem::paintPhaseToDrawingType(PaintPhaseForeground)));

    // Ensure we cannot use the cache with a new paint offset.
    LayoutPoint newPaintOffset(2, 3);
    EXPECT_FALSE(LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(context, layoutView(), PaintPhaseForeground, newPaintOffset));

    // Test that a new paint offset is recorded.
    {
        LayoutObjectDrawingRecorder drawingRecorder(context, layoutView(), PaintPhaseForeground, bounds, newPaintOffset);
        IntRect rect(0, 0, 10, 10);
        context.drawRect(rect);
    }

    rootPaintController().commitNewDisplayItems();
    EXPECT_DISPLAY_LIST(rootPaintController().displayItemList(), 1,
        TestDisplayItem(layoutView(), DisplayItem::paintPhaseToDrawingType(PaintPhaseForeground)));

    // Ensure the old paint offset cannot be used.
    EXPECT_FALSE(LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(context, layoutView(), PaintPhaseForeground, paintOffset));

    // Ensure the new paint offset can be used.
    EXPECT_TRUE(LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(context, layoutView(), PaintPhaseForeground, newPaintOffset));
    rootPaintController().commitNewDisplayItems();
    EXPECT_DISPLAY_LIST(rootPaintController().displayItemList(), 1,
        TestDisplayItem(layoutView(), DisplayItem::paintPhaseToDrawingType(PaintPhaseForeground)));
}

} // namespace
} // namespace blink
