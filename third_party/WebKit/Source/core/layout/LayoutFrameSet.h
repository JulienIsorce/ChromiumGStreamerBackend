/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 2000 Simon Hausmann <hausmann@kde.org>
 * Copyright (C) 2006, 2008 Apple Inc. All rights reserved.
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

#ifndef LayoutFrameSet_h
#define LayoutFrameSet_h

#include "core/layout/LayoutBox.h"

namespace blink {

class HTMLDimension;
class HTMLFrameSetElement;
class MouseEvent;

enum FrameEdge { LeftFrameEdge, RightFrameEdge, TopFrameEdge, BottomFrameEdge };

struct FrameEdgeInfo {
    STACK_ALLOCATED();
    FrameEdgeInfo(bool preventResize = false, bool allowBorder = true)
        : m_preventResize(4)
        , m_allowBorder(4)
    {
        m_preventResize.fill(preventResize);
        m_allowBorder.fill(allowBorder);
    }

    bool preventResize(FrameEdge edge) const { return m_preventResize[edge]; }
    bool allowBorder(FrameEdge edge) const { return m_allowBorder[edge]; }

    void setPreventResize(FrameEdge edge, bool preventResize) { m_preventResize[edge] = preventResize; }
    void setAllowBorder(FrameEdge edge, bool allowBorder) { m_allowBorder[edge] = allowBorder; }

private:
    Vector<bool> m_preventResize;
    Vector<bool> m_allowBorder;
};

class LayoutFrameSet final : public LayoutBox {
public:
    LayoutFrameSet(HTMLFrameSetElement*);
    ~LayoutFrameSet() override;

    LayoutObject* firstChild() const { ASSERT(children() == virtualChildren()); return children()->firstChild(); }
    LayoutObject* lastChild() const { ASSERT(children() == virtualChildren()); return children()->lastChild(); }

    // If you have a LayoutFrameSet, use firstChild or lastChild instead.
    void slowFirstChild() const = delete;
    void slowLastChild() const = delete;

    const LayoutObjectChildList* children() const { return &m_children; }
    LayoutObjectChildList* children() { return &m_children; }

    FrameEdgeInfo edgeInfo() const;

    bool userResize(MouseEvent*);

    bool canResizeRow(const IntPoint&) const;
    bool canResizeColumn(const IntPoint&) const;

    void notifyFrameEdgeInfoChanged();
    HTMLFrameSetElement* frameSet() const;

    class GridAxis {
        DISALLOW_ALLOCATION();
        WTF_MAKE_NONCOPYABLE(GridAxis);
    public:
        GridAxis();
        void resize(int);

        Vector<int> m_sizes;
        Vector<int> m_deltas;
        Vector<bool> m_preventResize;
        Vector<bool> m_allowBorder;
        int m_splitBeingResized;
        int m_splitResizeOffset;
    };

    const GridAxis& rows() const { return m_rows; }
    const GridAxis& columns() const { return m_cols; }

    const char* name() const override { return "LayoutFrameSet"; }

private:
    static const int noSplit = -1;

    LayoutObjectChildList* virtualChildren() override { return children(); }
    const LayoutObjectChildList* virtualChildren() const override { return children(); }

    bool isOfType(LayoutObjectType type) const override { return type == LayoutObjectFrameSet || LayoutBox::isOfType(type); }

    void layout() override;
    void paint(const PaintInfo&, const LayoutPoint&) const override;
    void computePreferredLogicalWidths() override;
    bool isChildAllowed(LayoutObject*, const ComputedStyle&) const override;
    CursorDirective getCursor(const LayoutPoint&, Cursor&) const override;

    void setIsResizing(bool);

    void layOutAxis(GridAxis&, const Vector<HTMLDimension>&, int availableSpace);
    void computeEdgeInfo();
    void fillFromEdgeInfo(const FrameEdgeInfo&, int r, int c);
    void positionFrames();

    int splitPosition(const GridAxis&, int split) const;
    int hitTestSplit(const GridAxis&, int position) const;

    void startResizing(GridAxis&, int position);
    void continueResizing(GridAxis&, int position);

    LayoutObjectChildList m_children;

    GridAxis m_rows;
    GridAxis m_cols;

    bool m_isResizing;
    bool m_isChildResizing;
};

DEFINE_LAYOUT_OBJECT_TYPE_CASTS(LayoutFrameSet, isFrameSet());

} // namespace blink

#endif // LayoutFrameSet_h
