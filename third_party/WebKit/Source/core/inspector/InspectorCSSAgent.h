/*
 * Copyright (C) 2010, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef InspectorCSSAgent_h
#define InspectorCSSAgent_h

#include "core/CoreExport.h"
#include "core/css/CSSSelector.h"
#include "core/dom/SecurityContext.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/inspector/InspectorBaseAgent.h"
#include "core/inspector/InspectorDOMAgent.h"
#include "core/inspector/InspectorStyleSheet.h"
#include "platform/JSONValues.h"
#include "wtf/HashCountedSet.h"
#include "wtf/HashMap.h"
#include "wtf/HashSet.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefPtr.h"
#include "wtf/Vector.h"
#include "wtf/text/WTFString.h"

namespace blink {

class CSSRule;
class CSSRuleList;
class CSSStyleRule;
class CSSStyleSheet;
class Document;
class Element;
class InspectedFrames;
class InspectorFrontend;
class InspectorResourceAgent;
class InspectorResourceContentLoader;
class MediaList;
class Node;
class LayoutObject;

class CORE_EXPORT InspectorCSSAgent final
    : public InspectorBaseAgent<InspectorCSSAgent, InspectorFrontend::CSS>
    , public InspectorDOMAgent::DOMListener
    , public InspectorBackendDispatcher::CSSCommandHandler
    , public InspectorStyleSheetBase::Listener {
    WTF_MAKE_NONCOPYABLE(InspectorCSSAgent);
    WILL_BE_USING_GARBAGE_COLLECTED_MIXIN(InspectorCSSAgent);
public:
    enum MediaListSource {
        MediaListSourceLinkedSheet,
        MediaListSourceInlineSheet,
        MediaListSourceMediaRule,
        MediaListSourceImportRule
    };

    enum StyleSheetsUpdateType {
        InitialFrontendLoad = 0,
        ExistingFrontendRefresh,
    };

    class InlineStyleOverrideScope {
        STACK_ALLOCATED();
    public:
        InlineStyleOverrideScope(SecurityContext* context)
            : m_contentSecurityPolicy(context->contentSecurityPolicy())
        {
            m_contentSecurityPolicy->setOverrideAllowInlineStyle(true);
        }

        ~InlineStyleOverrideScope()
        {
            m_contentSecurityPolicy->setOverrideAllowInlineStyle(false);
        }

    private:
        RawPtrWillBeMember<ContentSecurityPolicy> m_contentSecurityPolicy;
    };

    static CSSStyleRule* asCSSStyleRule(CSSRule*);
    static CSSMediaRule* asCSSMediaRule(CSSRule*);

    static PassOwnPtrWillBeRawPtr<InspectorCSSAgent> create(InspectorDOMAgent* domAgent, InspectedFrames* inspectedFrames, InspectorResourceAgent* resourceAgent, InspectorResourceContentLoader* resourceContentLoader)
    {
        return adoptPtrWillBeNoop(new InspectorCSSAgent(domAgent, inspectedFrames, resourceAgent, resourceContentLoader));
    }

    static void collectAllDocumentStyleSheets(Document*, WillBeHeapVector<RawPtrWillBeMember<CSSStyleSheet> >&);

    ~InspectorCSSAgent() override;
    DECLARE_VIRTUAL_TRACE();

    bool forcePseudoState(Element*, CSSSelector::PseudoType);
    void discardAgent() override;
    void didCommitLoadForLocalFrame(LocalFrame*) override;
    void restore() override;
    void flushPendingProtocolNotifications() override;
    void disable(ErrorString*) override;
    void reset();
    void mediaQueryResultChanged();

    void activeStyleSheetsUpdated(Document*);
    void documentDetached(Document*);

    void addEditedStyleSheet(const String& url, const String& content);
    bool getEditedStyleSheet(const String& url, String* content);

    void addEditedStyleElement(int backendNodeId, const String& content);
    bool getEditedStyleElement(int backendNodeId, String* content);

    void enable(ErrorString*, PassRefPtrWillBeRawPtr<EnableCallback>) override;
    void getComputedStyleForNode(ErrorString*, int nodeId, RefPtr<TypeBuilder::Array<TypeBuilder::CSS::CSSComputedStyleProperty>>&) override;
    void getPlatformFontsForNode(ErrorString*, int nodeId, RefPtr<TypeBuilder::Array<TypeBuilder::CSS::PlatformFontUsage>>&) override;
    void getInlineStylesForNode(ErrorString*, int nodeId, RefPtr<TypeBuilder::CSS::CSSStyle>& inlineStyle, RefPtr<TypeBuilder::CSS::CSSStyle>& attributes) override;
    void getMatchedStylesForNode(ErrorString*, int nodeId, RefPtr<TypeBuilder::CSS::CSSStyle>& inlineStyle, RefPtr<TypeBuilder::CSS::CSSStyle>& attributesStyle, RefPtr<TypeBuilder::Array<TypeBuilder::CSS::RuleMatch>>& matchedCSSRules, RefPtr<TypeBuilder::Array<TypeBuilder::CSS::PseudoElementMatches>>&, RefPtr<TypeBuilder::Array<TypeBuilder::CSS::InheritedStyleEntry>>& inheritedEntries) override;
    void getStyleSheetText(ErrorString*, const String& styleSheetId, String* result) override;
    void setStyleSheetText(ErrorString*, const String& styleSheetId, const String& text) override;
    void setRuleSelector(ErrorString*, const String& styleSheetId, const RefPtr<JSONObject>& range, const String& selector, RefPtr<TypeBuilder::CSS::SelectorList>& result) override;
    void setStyleText(ErrorString*, const String& styleSheetId, const RefPtr<JSONObject>& range, const String& text, RefPtr<TypeBuilder::CSS::CSSStyle>& result) override;
    void setMediaText(ErrorString*, const String& styleSheetId, const RefPtr<JSONObject>& range, const String& text, RefPtr<TypeBuilder::CSS::CSSMedia>& result) override;
    void createStyleSheet(ErrorString*, const String& frameId, TypeBuilder::CSS::StyleSheetId* outStyleSheetId) override;
    void addRule(ErrorString*, const String& styleSheetId, const String& ruleText, const RefPtr<JSONObject>& location, RefPtr<TypeBuilder::CSS::CSSRule>& result) override;
    void forcePseudoState(ErrorString*, int nodeId, const RefPtr<JSONArray>& forcedPseudoClasses) override;
    void getMediaQueries(ErrorString*, RefPtr<TypeBuilder::Array<TypeBuilder::CSS::CSSMedia>>& medias) override;
    void setEffectivePropertyValueForNode(ErrorString*, int nodeId, const String& propertyName, const String& value) override;
    void collectMediaQueriesFromRule(CSSRule*, TypeBuilder::Array<TypeBuilder::CSS::CSSMedia>* mediaArray);
    void collectMediaQueriesFromStyleSheet(CSSStyleSheet*, TypeBuilder::Array<TypeBuilder::CSS::CSSMedia>* mediaArray);
    PassRefPtr<TypeBuilder::CSS::CSSMedia> buildMediaObject(const MediaList*, MediaListSource, const String&, CSSStyleSheet*);
    PassRefPtr<TypeBuilder::Array<TypeBuilder::CSS::CSSMedia> > buildMediaListChain(CSSRule*);

    PassRefPtrWillBeRawPtr<CSSStyleDeclaration> findEffectiveDeclaration(CSSPropertyID, CSSRuleList* matchedRulesList, CSSStyleDeclaration* inlineStyle);
    void setCSSPropertyValue(ErrorString*, Element*, RefPtrWillBeRawPtr<CSSStyleDeclaration>, CSSPropertyID, const String& value, bool forceImportant = false);

    PassRefPtrWillBeRawPtr<CSSRuleList> matchedRulesList(Element*);
    String styleSheetId(CSSStyleSheet*);
private:
    class StyleSheetAction;
    class SetStyleSheetTextAction;
    class ModifyRuleAction;
    class SetElementStyleAction;
    class AddRuleAction;

    static void collectStyleSheets(CSSStyleSheet*, WillBeHeapVector<RawPtrWillBeMember<CSSStyleSheet> >&);

    InspectorCSSAgent(InspectorDOMAgent*, InspectedFrames*, InspectorResourceAgent*, InspectorResourceContentLoader*);

    typedef WillBeHeapHashMap<String, RefPtrWillBeMember<InspectorStyleSheet> > IdToInspectorStyleSheet;
    typedef WillBeHeapHashMap<String, RefPtrWillBeMember<InspectorStyleSheetForInlineStyle> > IdToInspectorStyleSheetForInlineStyle;
    typedef WillBeHeapHashMap<RawPtrWillBeMember<Node>, RefPtrWillBeMember<InspectorStyleSheetForInlineStyle> > NodeToInspectorStyleSheet; // bogus "stylesheets" with elements' inline styles
    typedef HashMap<int, unsigned> NodeIdToForcedPseudoState;

    void resourceContentLoaded(PassRefPtrWillBeRawPtr<EnableCallback>);
    void wasEnabled();
    void resetNonPersistentData();
    InspectorStyleSheetForInlineStyle* asInspectorStyleSheet(Element* element);
    Element* elementForId(ErrorString*, int nodeId);

    void updateActiveStyleSheets(Document*, StyleSheetsUpdateType);
    void setActiveStyleSheets(Document*, const WillBeHeapVector<RawPtrWillBeMember<CSSStyleSheet> >&, StyleSheetsUpdateType);
    CSSStyleDeclaration* setStyleText(ErrorString*, InspectorStyleSheetBase*, const SourceRange&, const String&);

    void collectPlatformFontsForLayoutObject(LayoutObject*, HashCountedSet<String>*);

    InspectorStyleSheet* bindStyleSheet(CSSStyleSheet*);
    String unbindStyleSheet(InspectorStyleSheet*);
    InspectorStyleSheet* inspectorStyleSheetForRule(CSSStyleRule*);

    InspectorStyleSheet* viaInspectorStyleSheet(Document*, bool createIfAbsent);
    InspectorStyleSheet* assertInspectorStyleSheetForId(ErrorString*, const String&);
    InspectorStyleSheetBase* assertStyleSheetForId(ErrorString*, const String&);
    TypeBuilder::CSS::StyleSheetOrigin::Enum detectOrigin(CSSStyleSheet* pageStyleSheet, Document* ownerDocument);

    PassRefPtr<TypeBuilder::CSS::CSSRule> buildObjectForRule(CSSStyleRule*);
    PassRefPtr<TypeBuilder::Array<TypeBuilder::CSS::RuleMatch> > buildArrayForMatchedRuleList(CSSRuleList*, Element*, PseudoId);
    PassRefPtr<TypeBuilder::CSS::CSSStyle> buildObjectForAttributesStyle(Element*);

    // InspectorDOMAgent::DOMListener implementation
    void didRemoveDocument(Document*) override;
    void didRemoveDOMNode(Node*) override;
    void didModifyDOMAttr(Element*) override;

    // InspectorStyleSheet::Listener implementation
    void styleSheetChanged(InspectorStyleSheetBase*) override;
    void willReparseStyleSheet() override;
    void didReparseStyleSheet() override;

    void resetPseudoStates();

    RawPtrWillBeMember<InspectorDOMAgent> m_domAgent;
    InspectedFrames* m_inspectedFrames;
    RawPtrWillBeMember<InspectorResourceAgent> m_resourceAgent;
    RawPtrWillBeMember<InspectorResourceContentLoader> m_resourceContentLoader;

    IdToInspectorStyleSheet m_idToInspectorStyleSheet;
    IdToInspectorStyleSheetForInlineStyle m_idToInspectorStyleSheetForInlineStyle;
    WillBeHeapHashMap<RawPtrWillBeMember<CSSStyleSheet>, RefPtrWillBeMember<InspectorStyleSheet> > m_cssStyleSheetToInspectorStyleSheet;
    typedef WillBeHeapHashMap<RawPtrWillBeMember<Document>, OwnPtrWillBeMember<WillBeHeapHashSet<RawPtrWillBeMember<CSSStyleSheet> > > > DocumentStyleSheets;
    DocumentStyleSheets m_documentToCSSStyleSheets;
    WillBeHeapHashSet<RawPtrWillBeMember<Document> > m_invalidatedDocuments;

    NodeToInspectorStyleSheet m_nodeToInspectorStyleSheet;
    WillBeHeapHashMap<RefPtrWillBeMember<Document>, RefPtrWillBeMember<InspectorStyleSheet> > m_documentToViaInspectorStyleSheet; // "via inspector" stylesheets
    NodeIdToForcedPseudoState m_nodeIdToForcedPseudoState;

    RefPtrWillBeMember<CSSStyleSheet> m_inspectorUserAgentStyleSheet;
    HashMap<String, String> m_editedStyleSheets;
    HashMap<int, String> m_editedStyleElements;

    bool m_creatingViaInspectorStyleSheet;
    bool m_isSettingStyleSheetText;

    friend class InspectorResourceContentLoaderCallback;
    friend class StyleSheetBinder;
};


} // namespace blink

#endif // !defined(InspectorCSSAgent_h)
