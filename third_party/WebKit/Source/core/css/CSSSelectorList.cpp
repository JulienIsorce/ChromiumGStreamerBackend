/*
 * Copyright (C) 2008, 2012 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "core/css/CSSSelectorList.h"

#include "core/css/parser/CSSParserSelector.h"
#include "wtf/text/StringBuilder.h"

namespace blink {

CSSSelectorList::~CSSSelectorList()
{
    deleteSelectors();
}

CSSSelectorList::CSSSelectorList(const CSSSelectorList& other)
{
    unsigned otherLength = other.length();
    m_selectorArray = reinterpret_cast<CSSSelector*>(fastMalloc(sizeof(CSSSelector) * otherLength));
    for (unsigned i = 0; i < otherLength; ++i)
        new (&m_selectorArray[i]) CSSSelector(other.m_selectorArray[i]);
}

void CSSSelectorList::adopt(CSSSelectorList& list)
{
    deleteSelectors();
    m_selectorArray = list.m_selectorArray;
    list.m_selectorArray = 0;
}

void CSSSelectorList::adoptSelectorVector(Vector<OwnPtr<CSSParserSelector>>& selectorVector)
{
    deleteSelectors();
    size_t flattenedSize = 0;
    for (size_t i = 0; i < selectorVector.size(); ++i) {
        for (CSSParserSelector* selector = selectorVector[i].get(); selector; selector = selector->tagHistory())
            ++flattenedSize;
    }
    ASSERT(flattenedSize);
    m_selectorArray = reinterpret_cast<CSSSelector*>(fastMalloc(sizeof(CSSSelector) * flattenedSize));
    size_t arrayIndex = 0;
    for (size_t i = 0; i < selectorVector.size(); ++i) {
        CSSParserSelector* current = selectorVector[i].get();
        while (current) {
            // Move item from the parser selector vector into m_selectorArray without invoking destructor (Ugh.)
            CSSSelector* currentSelector = current->releaseSelector().leakPtr();
            memcpy(&m_selectorArray[arrayIndex], currentSelector, sizeof(CSSSelector));
            fastFree(currentSelector);

            current = current->tagHistory();
            ASSERT(!m_selectorArray[arrayIndex].isLastInSelectorList());
            if (current)
                m_selectorArray[arrayIndex].setNotLastInTagHistory();
            ++arrayIndex;
        }
        ASSERT(m_selectorArray[arrayIndex - 1].isLastInTagHistory());
    }
    ASSERT(flattenedSize == arrayIndex);
    m_selectorArray[arrayIndex - 1].setLastInSelectorList();
    selectorVector.clear();
}

unsigned CSSSelectorList::length() const
{
    if (!m_selectorArray)
        return 0;
    CSSSelector* current = m_selectorArray;
    while (!current->isLastInSelectorList())
        ++current;
    return (current - m_selectorArray) + 1;
}

void CSSSelectorList::deleteSelectors()
{
    if (!m_selectorArray)
        return;

    bool finished = false;
    for (CSSSelector* s = m_selectorArray; !finished; ++s) {
        finished = s->isLastInSelectorList();
        s->~CSSSelector();
    }

    fastFree(m_selectorArray);
}

String CSSSelectorList::selectorsText() const
{
    StringBuilder result;

    for (const CSSSelector* s = first(); s; s = next(*s)) {
        if (s != first())
            result.appendLiteral(", ");
        result.append(s->selectorText());
    }

    return result.toString();
}

template <typename Functor>
static bool forEachTagSelector(Functor& functor, const CSSSelector& selector)
{
    for (const CSSSelector* current = &selector; current; current = current->tagHistory()) {
        if (functor(*current))
            return true;
        if (const CSSSelectorList* selectorList = current->selectorList()) {
            for (const CSSSelector* subSelector = selectorList->first(); subSelector; subSelector = CSSSelectorList::next(*subSelector)) {
                if (forEachTagSelector(functor, *subSelector))
                    return true;
            }
        }
    }

    return false;
}

template <typename Functor>
static bool forEachSelector(Functor& functor, const CSSSelectorList* selectorList)
{
    for (const CSSSelector* selector = selectorList->first(); selector; selector = CSSSelectorList::next(*selector)) {
        if (forEachTagSelector(functor, *selector))
            return true;
    }

    return false;
}

class SelectorNeedsNamespaceResolutionFunctor {
public:
    bool operator()(const CSSSelector& selector)
    {
        if (selector.match() != CSSSelector::Tag && !selector.isAttributeSelector())
            return false;
        const AtomicString& prefix = selector.isAttributeSelector() ? selector.attribute().prefix() : selector.tagQName().prefix();
        return prefix != nullAtom && prefix != emptyAtom && prefix != starAtom;
    }
};

bool CSSSelectorList::selectorsNeedNamespaceResolution()
{
    SelectorNeedsNamespaceResolutionFunctor functor;
    return forEachSelector(functor, this);
}

class SelectorHasShadowDistributed {
public:
    bool operator()(const CSSSelector& selector)
    {
        return selector.relationIsAffectedByPseudoContent();
    }
};

bool CSSSelectorList::hasShadowDistributedAt(size_t index) const
{
    SelectorHasShadowDistributed functor;
    return forEachTagSelector(functor, selectorAt(index));
}

class SelectorCrossesTreeScopes {
public:
    bool operator()(const CSSSelector& selector)
    {
        return selector.relation() == CSSSelector::ShadowDeep || selector.pseudoType() == CSSSelector::PseudoShadow;
    }
};

bool CSSSelectorList::selectorCrossesTreeScopes(size_t index) const
{
    SelectorCrossesTreeScopes functor;
    return forEachTagSelector(functor, selectorAt(index));
}

class SelectorNeedsUpdatedDistribution {
public:
    bool operator()(const CSSSelector& selector)
    {
        return selector.relationIsAffectedByPseudoContent() || selector.pseudoType() == CSSSelector::PseudoHostContext;
    }
};

bool CSSSelectorList::selectorNeedsUpdatedDistribution(size_t index) const
{
    SelectorNeedsUpdatedDistribution functor;
    return forEachTagSelector(functor, selectorAt(index));
}

} // namespace blink
