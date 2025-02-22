/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 * Copyright (C) 2010 Google, Inc. All Rights Reserved.
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

#ifndef HTMLTokenizer_h
#define HTMLTokenizer_h

#include "core/html/parser/HTMLParserOptions.h"
#include "core/html/parser/HTMLToken.h"
#include "core/html/parser/InputStreamPreprocessor.h"
#include "platform/text/SegmentedString.h"

namespace blink {

class HTMLTokenizer {
    WTF_MAKE_NONCOPYABLE(HTMLTokenizer);
    WTF_MAKE_FAST_ALLOCATED(HTMLTokenizer);
public:
    static PassOwnPtr<HTMLTokenizer> create(const HTMLParserOptions& options) { return adoptPtr(new HTMLTokenizer(options)); }
    ~HTMLTokenizer();

    void reset();

    enum State {
        DataState,
        CharacterReferenceInDataState,
        RCDATAState,
        CharacterReferenceInRCDATAState,
        RAWTEXTState,
        ScriptDataState,
        PLAINTEXTState,
        TagOpenState,
        EndTagOpenState,
        TagNameState,
        RCDATALessThanSignState,
        RCDATAEndTagOpenState,
        RCDATAEndTagNameState,
        RAWTEXTLessThanSignState,
        RAWTEXTEndTagOpenState,
        RAWTEXTEndTagNameState,
        ScriptDataLessThanSignState,
        ScriptDataEndTagOpenState,
        ScriptDataEndTagNameState,
        ScriptDataEscapeStartState,
        ScriptDataEscapeStartDashState,
        ScriptDataEscapedState,
        ScriptDataEscapedDashState,
        ScriptDataEscapedDashDashState,
        ScriptDataEscapedLessThanSignState,
        ScriptDataEscapedEndTagOpenState,
        ScriptDataEscapedEndTagNameState,
        ScriptDataDoubleEscapeStartState,
        ScriptDataDoubleEscapedState,
        ScriptDataDoubleEscapedDashState,
        ScriptDataDoubleEscapedDashDashState,
        ScriptDataDoubleEscapedLessThanSignState,
        ScriptDataDoubleEscapeEndState,
        BeforeAttributeNameState,
        AttributeNameState,
        AfterAttributeNameState,
        BeforeAttributeValueState,
        AttributeValueDoubleQuotedState,
        AttributeValueSingleQuotedState,
        AttributeValueUnquotedState,
        CharacterReferenceInAttributeValueState,
        AfterAttributeValueQuotedState,
        SelfClosingStartTagState,
        BogusCommentState,
        // The ContinueBogusCommentState is not in the HTML5 spec, but we use
        // it internally to keep track of whether we've started the bogus
        // comment token yet.
        ContinueBogusCommentState,
        MarkupDeclarationOpenState,
        CommentStartState,
        CommentStartDashState,
        CommentState,
        CommentEndDashState,
        CommentEndState,
        CommentEndBangState,
        DOCTYPEState,
        BeforeDOCTYPENameState,
        DOCTYPENameState,
        AfterDOCTYPENameState,
        AfterDOCTYPEPublicKeywordState,
        BeforeDOCTYPEPublicIdentifierState,
        DOCTYPEPublicIdentifierDoubleQuotedState,
        DOCTYPEPublicIdentifierSingleQuotedState,
        AfterDOCTYPEPublicIdentifierState,
        BetweenDOCTYPEPublicAndSystemIdentifiersState,
        AfterDOCTYPESystemKeywordState,
        BeforeDOCTYPESystemIdentifierState,
        DOCTYPESystemIdentifierDoubleQuotedState,
        DOCTYPESystemIdentifierSingleQuotedState,
        AfterDOCTYPESystemIdentifierState,
        BogusDOCTYPEState,
        CDATASectionState,
        // These CDATA states are not in the HTML5 spec, but we use them internally.
        CDATASectionRightSquareBracketState,
        CDATASectionDoubleRightSquareBracketState,
    };

    // This function returns true if it emits a token. Otherwise, callers
    // must provide the same (in progress) token on the next call (unless
    // they call reset() first).
    bool nextToken(SegmentedString&, HTMLToken&);

    // Returns a copy of any characters buffered internally by the tokenizer.
    // The tokenizer buffers characters when searching for the </script> token
    // that terminates a script element.
    String bufferedCharacters() const;

    size_t numberOfBufferedCharacters() const
    {
        // Notice that we add 2 to the length of the m_temporaryBuffer to
        // account for the "</" characters, which are effecitvely buffered in
        // the tokenizer's state machine.
        return m_temporaryBuffer.size() ? m_temporaryBuffer.size() + 2 : 0;
    }

    // Updates the tokenizer's state according to the given tag name. This is
    // an approximation of how the tree builder would update the tokenizer's
    // state. This method is useful for approximating HTML tokenization. To
    // get exactly the correct tokenization, you need the real tree builder.
    //
    // The main failures in the approximation are as follows:
    //
    //  * The first set of character tokens emitted for a <pre> element might
    //    contain an extra leading newline.
    //  * The replacement of U+0000 with U+FFFD will not be sensitive to the
    //    tree builder's insertion mode.
    //  * CDATA sections in foreign content will be tokenized as bogus comments
    //    instead of as character tokens.
    //
    void updateStateFor(const String& tagName);

    bool forceNullCharacterReplacement() const { return m_forceNullCharacterReplacement; }
    void setForceNullCharacterReplacement(bool value) { m_forceNullCharacterReplacement = value; }

    bool shouldAllowCDATA() const { return m_shouldAllowCDATA; }
    void setShouldAllowCDATA(bool value) { m_shouldAllowCDATA = value; }

    State state() const { return m_state; }
    void setState(State state) { m_state = state; }

    inline bool shouldSkipNullCharacters() const
    {
        return !m_forceNullCharacterReplacement
            && (m_state == HTMLTokenizer::DataState
                || m_state == HTMLTokenizer::RCDATAState
                || m_state == HTMLTokenizer::RAWTEXTState);
    }

private:
    explicit HTMLTokenizer(const HTMLParserOptions&);

    inline bool processEntity(SegmentedString&);

    inline void parseError();

    inline void bufferCharacter(UChar character)
    {
        ASSERT(character != kEndOfFileMarker);
        m_token->ensureIsCharacterToken();
        m_token->appendToCharacter(character);
    }

    inline bool emitAndResumeIn(SegmentedString& source, State state)
    {
        saveEndTagNameIfNeeded();
        m_state = state;
        source.advanceAndUpdateLineNumber();
        return true;
    }

    inline bool emitAndReconsumeIn(SegmentedString&, State state)
    {
        saveEndTagNameIfNeeded();
        m_state = state;
        return true;
    }

    inline bool emitEndOfFile(SegmentedString& source)
    {
        if (haveBufferedCharacterToken())
            return true;
        m_state = HTMLTokenizer::DataState;
        source.advanceAndUpdateLineNumber();
        m_token->clear();
        m_token->makeEndOfFile();
        return true;
    }

    inline bool flushEmitAndResumeIn(SegmentedString&, State);

    // Return whether we need to emit a character token before dealing with
    // the buffered end tag.
    inline bool flushBufferedEndTag(SegmentedString&);
    inline bool temporaryBufferIs(const String&);

    // Sometimes we speculatively consume input characters and we don't
    // know whether they represent end tags or RCDATA, etc. These
    // functions help manage these state.
    inline void addToPossibleEndTag(LChar cc);

    inline void saveEndTagNameIfNeeded()
    {
        ASSERT(m_token->type() != HTMLToken::Uninitialized);
        if (m_token->type() == HTMLToken::StartTag)
            m_appropriateEndTagName = m_token->name();
    }
    inline bool isAppropriateEndTag();


    inline bool haveBufferedCharacterToken()
    {
        return m_token->type() == HTMLToken::Character;
    }

    State m_state;
    bool m_forceNullCharacterReplacement;
    bool m_shouldAllowCDATA;

    // m_token is owned by the caller. If nextToken is not on the stack,
    // this member might be pointing to unallocated memory.
    HTMLToken* m_token;

    // http://www.whatwg.org/specs/web-apps/current-work/#additional-allowed-character
    UChar m_additionalAllowedCharacter;

    // http://www.whatwg.org/specs/web-apps/current-work/#preprocessing-the-input-stream
    InputStreamPreprocessor<HTMLTokenizer> m_inputStreamPreprocessor;

    Vector<UChar, 32> m_appropriateEndTagName;

    // http://www.whatwg.org/specs/web-apps/current-work/#temporary-buffer
    Vector<LChar, 32> m_temporaryBuffer;

    // We occationally want to emit both a character token and an end tag
    // token (e.g., when lexing script). We buffer the name of the end tag
    // token here so we remember it next time we re-enter the tokenizer.
    Vector<LChar, 32> m_bufferedEndTagName;

    HTMLParserOptions m_options;
};

}

#endif
