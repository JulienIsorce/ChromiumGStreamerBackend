/*
 * Copyright (C) 2003 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010 Apple Inc. All rights reserved.
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

#ifndef CSSParserSelector_h
#define CSSParserSelector_h

#include "core/css/CSSSelector.h"

namespace blink {

class CSSParserSelector {
    WTF_MAKE_NONCOPYABLE(CSSParserSelector); WTF_MAKE_FAST_ALLOCATED(CSSParserSelector);
public:
    CSSParserSelector();
    explicit CSSParserSelector(const QualifiedName&, bool isImplicit = false);

    static PassOwnPtr<CSSParserSelector> create() { return adoptPtr(new CSSParserSelector); }
    static PassOwnPtr<CSSParserSelector> create(const QualifiedName& name, bool isImplicit = false) { return adoptPtr(new CSSParserSelector(name, isImplicit)); }

    ~CSSParserSelector();

    PassOwnPtr<CSSSelector> releaseSelector() { return m_selector.release(); }

    CSSSelector::Relation relation() const { return m_selector->relation(); }
    void setValue(const AtomicString& value) { m_selector->setValue(value); }
    void setAttribute(const QualifiedName& value, CSSSelector::AttributeMatchType matchType) { m_selector->setAttribute(value, matchType); }
    void setArgument(const AtomicString& value) { m_selector->setArgument(value); }
    void setNth(int a, int b) { m_selector->setNth(a, b); }
    void setMatch(CSSSelector::Match value) { m_selector->setMatch(value); }
    void setRelation(CSSSelector::Relation value) { m_selector->setRelation(value); }
    void setForPage() { m_selector->setForPage(); }
    void setRelationIsAffectedByPseudoContent() { m_selector->setRelationIsAffectedByPseudoContent(); }
    bool relationIsAffectedByPseudoContent() const { return m_selector->relationIsAffectedByPseudoContent(); }

    void updatePseudoType(const AtomicString& value, bool hasArguments = false) const { m_selector->updatePseudoType(value, hasArguments); }

    void adoptSelectorVector(Vector<OwnPtr<CSSParserSelector>>& selectorVector);
    void setSelectorList(PassOwnPtr<CSSSelectorList>);

    bool hasHostPseudoSelector() const;

    CSSSelector::PseudoType pseudoType() const { return m_selector->pseudoType(); }

    // TODO(esprehn): This set of cases doesn't make sense, why PseudoShadow but not a check for ::content or /deep/ ?
    bool crossesTreeScopes() const { return pseudoType() == CSSSelector::PseudoWebKitCustomElement || pseudoType() == CSSSelector::PseudoCue || pseudoType() == CSSSelector::PseudoShadow; }

    bool isSimple() const;
    bool hasShadowPseudo() const;

    CSSParserSelector* tagHistory() const { return m_tagHistory.get(); }
    void setTagHistory(PassOwnPtr<CSSParserSelector> selector) { m_tagHistory = selector; }
    void clearTagHistory() { m_tagHistory.clear(); }
    void insertTagHistory(CSSSelector::Relation before, PassOwnPtr<CSSParserSelector>, CSSSelector::Relation after);
    void appendTagHistory(CSSSelector::Relation, PassOwnPtr<CSSParserSelector>);
    void prependTagSelector(const QualifiedName&, bool tagIsImplicit = false);

private:
    OwnPtr<CSSSelector> m_selector;
    OwnPtr<CSSParserSelector> m_tagHistory;
};

inline bool CSSParserSelector::hasShadowPseudo() const
{
    return m_selector->relation() == CSSSelector::ShadowPseudo;
}

} // namespace blink

#endif
