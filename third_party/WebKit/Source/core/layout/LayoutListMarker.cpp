/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2006 Andrew Wellington (proton@wiretapped.net)
 * Copyright (C) 2010 Daniel Bates (dbates@intudata.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"
#include "core/layout/LayoutListMarker.h"

#include "core/fetch/ImageResource.h"
#include "core/layout/LayoutAnalyzer.h"
#include "core/layout/LayoutListItem.h"
#include "core/layout/ListMarkerText.h"
#include "core/paint/ListMarkerPainter.h"
#include "core/paint/PaintLayer.h"
#include "platform/fonts/Font.h"

namespace blink {

const int cMarkerPaddingPx = 7;

LayoutListMarker::LayoutListMarker(LayoutListItem* item)
    : LayoutBox(nullptr)
    , m_listItem(item)
{
    // init LayoutObject attributes
    setInline(true); // our object is Inline
    setReplaced(true); // pretend to be replaced
}

LayoutListMarker::~LayoutListMarker()
{
}

void LayoutListMarker::willBeDestroyed()
{
    if (m_image)
        m_image->removeClient(this);
    LayoutBox::willBeDestroyed();
}

LayoutListMarker* LayoutListMarker::createAnonymous(LayoutListItem* item)
{
    Document& document = item->document();
    LayoutListMarker* layoutObject = new LayoutListMarker(item);
    layoutObject->setDocumentForAnonymous(&document);
    return layoutObject;
}

void LayoutListMarker::styleWillChange(StyleDifference diff, const ComputedStyle& newStyle)
{
    if (style() && (newStyle.listStylePosition() != style()->listStylePosition() || newStyle.listStyleType() != style()->listStyleType()))
        setNeedsLayoutAndPrefWidthsRecalcAndFullPaintInvalidation(LayoutInvalidationReason::StyleChange);

    LayoutBox::styleWillChange(diff, newStyle);
}

void LayoutListMarker::styleDidChange(StyleDifference diff, const ComputedStyle* oldStyle)
{
    LayoutBox::styleDidChange(diff, oldStyle);

    if (m_image != style()->listStyleImage()) {
        if (m_image)
            m_image->removeClient(this);
        m_image = style()->listStyleImage();
        if (m_image)
            m_image->addClient(this);
    }
}

InlineBox* LayoutListMarker::createInlineBox()
{
    InlineBox* result = LayoutBox::createInlineBox();
    result->setIsText(isText());
    return result;
}

bool LayoutListMarker::isImage() const
{
    return m_image && !m_image->errorOccurred();
}

LayoutRect LayoutListMarker::localSelectionRect() const
{
    InlineBox* box = inlineBoxWrapper();
    if (!box)
        return LayoutRect(LayoutPoint(), size());
    RootInlineBox& root = inlineBoxWrapper()->root();
    const ComputedStyle* blockStyle = root.block().style();
    LayoutUnit newLogicalTop = blockStyle->isFlippedBlocksWritingMode()
        ? inlineBoxWrapper()->logicalBottom() - root.selectionBottom()
        : root.selectionTop() - inlineBoxWrapper()->logicalTop();
    return blockStyle->isHorizontalWritingMode()
        ? LayoutRect(0, newLogicalTop, size().width(), root.selectionHeight())
        : LayoutRect(newLogicalTop, 0, root.selectionHeight(), size().height());
}

void LayoutListMarker::paint(const PaintInfo& paintInfo, const LayoutPoint& paintOffset) const
{
    ListMarkerPainter(*this).paint(paintInfo, paintOffset);
}

void LayoutListMarker::layout()
{
    ASSERT(needsLayout());
    LayoutAnalyzer::Scope analyzer(*this);

    if (isImage()) {
        updateMarginsAndContent();
        setWidth(m_image->imageSize(this, style()->effectiveZoom()).width());
        setHeight(m_image->imageSize(this, style()->effectiveZoom()).height());
    } else {
        setLogicalWidth(minPreferredLogicalWidth());
        setLogicalHeight(style()->fontMetrics().height());
    }

    setMarginStart(0);
    setMarginEnd(0);

    Length startMargin = style()->marginStart();
    Length endMargin = style()->marginEnd();
    if (startMargin.isFixed())
        setMarginStart(startMargin.value());
    if (endMargin.isFixed())
        setMarginEnd(endMargin.value());

    clearNeedsLayout();
}

void LayoutListMarker::imageChanged(WrappedImagePtr o, const IntRect*)
{
    // A list marker can't have a background or border image, so no need to call the base class method.
    if (o != m_image->data())
        return;

    if (size() != m_image->imageSize(this, style()->effectiveZoom()) || m_image->errorOccurred())
        setNeedsLayoutAndPrefWidthsRecalcAndFullPaintInvalidation(LayoutInvalidationReason::ImageChanged);
    else
        setShouldDoFullPaintInvalidation();
}

void LayoutListMarker::updateMarginsAndContent()
{
    updateContent();
    updateMargins();
}

void LayoutListMarker::updateContent()
{
    // FIXME: This if-statement is just a performance optimization, but it's messy to use the preferredLogicalWidths dirty bit for this.
    // It's unclear if this is a premature optimization.
    if (!preferredLogicalWidthsDirty())
        return;

    m_text = "";

    if (isImage()) {
        // FIXME: This is a somewhat arbitrary width.  Generated images for markers really won't become particularly useful
        // until we support the CSS3 marker pseudoclass to allow control over the width and height of the marker box.
        int bulletWidth = style()->fontMetrics().ascent() / 2;
        IntSize defaultBulletSize(bulletWidth, bulletWidth);
        IntSize imageSize = calculateImageIntrinsicDimensions(m_image.get(), defaultBulletSize, DoNotScaleByEffectiveZoom);
        m_image->setContainerSizeForLayoutObject(this, imageSize, style()->effectiveZoom());
        return;
    }

    switch (listStyleCategory()) {
    case ListStyleCategory::None:
        break;
    case ListStyleCategory::Symbol:
        m_text = ListMarkerText::text(style()->listStyleType(), 0); // value is ignored for these types
        break;
    case ListStyleCategory::Language:
        m_text = ListMarkerText::text(style()->listStyleType(), m_listItem->value());
        break;
    }
}

LayoutUnit LayoutListMarker::getWidthOfTextWithSuffix() const
{
    if (m_text.isEmpty())
        return 0;
    const Font& font = style()->font();
    LayoutUnit itemWidth = font.width(m_text);
    // TODO(wkorman): Look into constructing a text run for both text and suffix
    // and painting them together.
    UChar suffix[2] = { ListMarkerText::suffix(style()->listStyleType(), m_listItem->value()), ' ' };
    TextRun run = constructTextRun(font, suffix, 2, styleRef(), style()->direction());
    LayoutUnit suffixSpaceWidth = font.width(run);
    return itemWidth + suffixSpaceWidth;
}

void LayoutListMarker::computePreferredLogicalWidths()
{
    ASSERT(preferredLogicalWidthsDirty());
    updateContent();

    if (isImage()) {
        LayoutSize imageSize = m_image->imageSize(this, style()->effectiveZoom());
        m_minPreferredLogicalWidth = m_maxPreferredLogicalWidth = style()->isHorizontalWritingMode() ? imageSize.width() : imageSize.height();
        clearPreferredLogicalWidthsDirty();
        updateMargins();
        return;
    }

    const Font& font = style()->font();

    LayoutUnit logicalWidth = 0;
    switch (listStyleCategory()) {
    case ListStyleCategory::None:
        break;
    case ListStyleCategory::Symbol:
        logicalWidth = (font.fontMetrics().ascent() * 2 / 3 + 1) / 2 + 2;
        break;
    case ListStyleCategory::Language:
        logicalWidth = getWidthOfTextWithSuffix();
        break;
    }

    m_minPreferredLogicalWidth = logicalWidth;
    m_maxPreferredLogicalWidth = logicalWidth;

    clearPreferredLogicalWidthsDirty();

    updateMargins();
}

void LayoutListMarker::updateMargins()
{
    const FontMetrics& fontMetrics = style()->fontMetrics();

    LayoutUnit marginStart = 0;
    LayoutUnit marginEnd = 0;

    if (isInside()) {
        if (isImage()) {
            marginEnd = cMarkerPaddingPx;
        } else {
            switch (listStyleCategory()) {
            case ListStyleCategory::Symbol:
                marginStart = -1;
                marginEnd = fontMetrics.ascent() - minPreferredLogicalWidth() + 1;
                break;
            default:
                break;
            }
        }
    } else {
        if (style()->isLeftToRightDirection()) {
            if (isImage()) {
                marginStart = -minPreferredLogicalWidth() - cMarkerPaddingPx;
            } else {
                int offset = fontMetrics.ascent() * 2 / 3;
                switch (listStyleCategory()) {
                case ListStyleCategory::None:
                    break;
                case ListStyleCategory::Symbol:
                    marginStart = -offset - cMarkerPaddingPx - 1;
                    break;
                default:
                    marginStart = m_text.isEmpty() ? LayoutUnit() : -minPreferredLogicalWidth();
                }
            }
            marginEnd = -marginStart - minPreferredLogicalWidth();
        } else {
            if (isImage()) {
                marginEnd = cMarkerPaddingPx;
            } else {
                int offset = fontMetrics.ascent() * 2 / 3;
                switch (listStyleCategory()) {
                case ListStyleCategory::None:
                    break;
                case ListStyleCategory::Symbol:
                    marginEnd = offset + cMarkerPaddingPx + 1 - minPreferredLogicalWidth();
                    break;
                default:
                    marginEnd = 0;
                }
            }
            marginStart = -marginEnd - minPreferredLogicalWidth();
        }

    }

    mutableStyleRef().setMarginStart(Length(marginStart, Fixed));
    mutableStyleRef().setMarginEnd(Length(marginEnd, Fixed));
}

LayoutUnit LayoutListMarker::lineHeight(bool firstLine, LineDirectionMode direction, LinePositionMode linePositionMode) const
{
    if (!isImage())
        return m_listItem->lineHeight(firstLine, direction, PositionOfInteriorLineBoxes);
    return LayoutBox::lineHeight(firstLine, direction, linePositionMode);
}

int LayoutListMarker::baselinePosition(FontBaseline baselineType, bool firstLine, LineDirectionMode direction, LinePositionMode linePositionMode) const
{
    ASSERT(linePositionMode == PositionOnContainingLine);
    if (!isImage())
        return m_listItem->baselinePosition(baselineType, firstLine, direction, PositionOfInteriorLineBoxes);
    return LayoutBox::baselinePosition(baselineType, firstLine, direction, linePositionMode);
}

LayoutListMarker::ListStyleCategory LayoutListMarker::listStyleCategory() const
{
    switch (style()->listStyleType()) {
    case NoneListStyle:
        return ListStyleCategory::None;
    case Disc:
    case Circle:
    case Square:
        return ListStyleCategory::Symbol;
    case ArabicIndic:
    case Armenian:
    case Bengali:
    case Cambodian:
    case CJKIdeographic:
    case CjkEarthlyBranch:
    case CjkHeavenlyStem:
    case DecimalLeadingZero:
    case DecimalListStyle:
    case Devanagari:
    case EthiopicHalehame:
    case EthiopicHalehameAm:
    case EthiopicHalehameTiEr:
    case EthiopicHalehameTiEt:
    case Georgian:
    case Gujarati:
    case Gurmukhi:
    case Hangul:
    case HangulConsonant:
    case Hebrew:
    case Hiragana:
    case HiraganaIroha:
    case Kannada:
    case Katakana:
    case KatakanaIroha:
    case Khmer:
    case KoreanHangulFormal:
    case KoreanHanjaFormal:
    case KoreanHanjaInformal:
    case Lao:
    case LowerAlpha:
    case LowerArmenian:
    case LowerGreek:
    case LowerLatin:
    case LowerRoman:
    case Malayalam:
    case Mongolian:
    case Myanmar:
    case Oriya:
    case Persian:
    case SimpChineseFormal:
    case SimpChineseInformal:
    case Telugu:
    case Thai:
    case Tibetan:
    case TradChineseFormal:
    case TradChineseInformal:
    case UpperAlpha:
    case UpperArmenian:
    case UpperLatin:
    case UpperRoman:
    case Urdu:
        return ListStyleCategory::Language;
    default:
        ASSERT_NOT_REACHED();
        return ListStyleCategory::Language;
    }
}

bool LayoutListMarker::isInside() const
{
    return m_listItem->notInList() || style()->listStylePosition() == INSIDE;
}

IntRect LayoutListMarker::getRelativeMarkerRect() const
{
    if (isImage())
        return IntRect(0, 0, m_image->imageSize(this, style()->effectiveZoom()).width(), m_image->imageSize(this, style()->effectiveZoom()).height());

    IntRect relativeRect;
    switch (listStyleCategory()) {
    case ListStyleCategory::None:
        return IntRect();
    case ListStyleCategory::Symbol: {
        // TODO(wkorman): Review and clean up/document the calculations below.
        // http://crbug.com/543193
        const FontMetrics& fontMetrics = style()->fontMetrics();
        int ascent = fontMetrics.ascent();
        int bulletWidth = (ascent * 2 / 3 + 1) / 2;
        relativeRect = IntRect(1, 3 * (ascent - ascent * 2 / 3) / 2, bulletWidth, bulletWidth);
        }
        break;
    case ListStyleCategory::Language:
        relativeRect = IntRect(0, 0, getWidthOfTextWithSuffix(), style()->font().fontMetrics().height());
        break;
    }

    if (!style()->isHorizontalWritingMode()) {
        relativeRect = relativeRect.transposedRect();
        relativeRect.setX(size().width() - relativeRect.x() - relativeRect.width());
    }

    return relativeRect;
}

void LayoutListMarker::setSelectionState(SelectionState state)
{
    // The selection state for our containing block hierarchy is updated by the base class call.
    LayoutBox::setSelectionState(state);

    if (inlineBoxWrapper() && canUpdateSelectionOnRootLineBoxes())
        inlineBoxWrapper()->root().setHasSelectedChildren(state != SelectionNone);
}

LayoutRect LayoutListMarker::selectionRectForPaintInvalidation(const LayoutBoxModelObject* paintInvalidationContainer) const
{
    ASSERT(!needsLayout());

    if (selectionState() == SelectionNone || !inlineBoxWrapper())
        return LayoutRect();

    RootInlineBox& root = inlineBoxWrapper()->root();
    LayoutRect rect(0, root.selectionTop() - location().y(), size().width(), root.selectionHeight());
    mapRectToPaintInvalidationBacking(paintInvalidationContainer, rect, 0);
    // FIXME: groupedMapping() leaks the squashing abstraction.
    if (paintInvalidationContainer->layer()->groupedMapping())
        PaintLayer::mapRectToPaintBackingCoordinates(paintInvalidationContainer, rect);
    return rect;
}

void LayoutListMarker::listItemStyleDidChange()
{
    RefPtr<ComputedStyle> newStyle = ComputedStyle::create();
    // The marker always inherits from the list item, regardless of where it might end
    // up (e.g., in some deeply nested line box). See CSS3 spec.
    newStyle->inheritFrom(m_listItem->styleRef());
    if (style()) {
        // Reuse the current margins. Otherwise resetting the margins to initial values
        // would trigger unnecessary layout.
        newStyle->setMarginStart(style()->marginStart());
        newStyle->setMarginEnd(style()->marginRight());
    }
    setStyle(newStyle.release());
}

} // namespace blink
