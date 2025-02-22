/*
 * Copyright (C) Research In Motion Limited 2010-2011. All rights reserved.
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
 */

#ifndef SVGTextLayoutAttributes_h
#define SVGTextLayoutAttributes_h

#include "core/layout/svg/SVGTextMetrics.h"
#include "wtf/Allocator.h"
#include "wtf/HashMap.h"
#include "wtf/MathExtras.h"
#include "wtf/Noncopyable.h"
#include "wtf/Vector.h"

namespace blink {

class LayoutSVGInlineText;

struct SVGCharacterData {
    ALLOW_ONLY_INLINE_ALLOCATION();
    SVGCharacterData();

    float x;
    float y;
    float dx;
    float dy;
    float rotate;
};

typedef HashMap<unsigned, SVGCharacterData> SVGCharacterDataMap;

class SVGTextLayoutAttributes {
    DISALLOW_ALLOCATION();
    WTF_MAKE_NONCOPYABLE(SVGTextLayoutAttributes);
public:
    SVGTextLayoutAttributes(LayoutSVGInlineText*);

    void clear();
    static float emptyValue() { return std::numeric_limits<float>::quiet_NaN(); }
    static bool isEmptyValue(float value) { return std::isnan(value); }

    LayoutSVGInlineText* context() const { return m_context; }

    SVGCharacterDataMap& characterDataMap() { return m_characterDataMap; }
    const SVGCharacterDataMap& characterDataMap() const { return m_characterDataMap; }

    Vector<SVGTextMetrics>& textMetricsValues() { return m_textMetricsValues; }
    const Vector<SVGTextMetrics>& textMetricsValues() const { return m_textMetricsValues; }

private:
    LayoutSVGInlineText* m_context;
    SVGCharacterDataMap m_characterDataMap;
    Vector<SVGTextMetrics> m_textMetricsValues;
};

inline SVGCharacterData::SVGCharacterData()
    : x(SVGTextLayoutAttributes::emptyValue())
    , y(SVGTextLayoutAttributes::emptyValue())
    , dx(SVGTextLayoutAttributes::emptyValue())
    , dy(SVGTextLayoutAttributes::emptyValue())
    , rotate(SVGTextLayoutAttributes::emptyValue())
{
}

} // namespace blink

#endif
