/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WebDevToolsFrontendImpl_h
#define WebDevToolsFrontendImpl_h

#include "core/inspector/InspectorFrontendClient.h"
#include "platform/heap/Handle.h"
#include "public/web/WebDevToolsFrontend.h"
#include "wtf/HashMap.h"
#include "wtf/Noncopyable.h"
#include "wtf/text/WTFString.h"

namespace blink {

class DevToolsHost;
class WebLocalFrameImpl;

class WebDevToolsFrontendImpl final : public WebDevToolsFrontend, public InspectorFrontendClient {
    WTF_MAKE_NONCOPYABLE(WebDevToolsFrontendImpl);
public:
    WebDevToolsFrontendImpl(WebLocalFrameImpl*, WebDevToolsFrontendClient*);
    ~WebDevToolsFrontendImpl() override;

    void didClearWindowObject(WebLocalFrameImpl*);

    void sendMessageToBackend(const WTF::String&) override;

    void sendMessageToEmbedder(const WTF::String&) override;

    bool isUnderTest() override;

    void showContextMenu(LocalFrame*, float x, float y, PassRefPtrWillBeRawPtr<ContextMenuProvider>) override;

    void setInjectedScriptForOrigin(const String& origin, const String& source) override;

private:
    // TODO(Oilpan): Make the lifetime of m_webFrame clear to remove this GC_PLUGIN_IGNORE.
    GC_PLUGIN_IGNORE("http://crbug.com/509911")
    WebLocalFrameImpl* m_webFrame;
    WebDevToolsFrontendClient* m_client;
    RefPtrWillBePersistent<DevToolsHost> m_devtoolsHost;
    typedef HashMap<String, String> InjectedScriptForOriginMap;
    InjectedScriptForOriginMap m_injectedScriptForOrigin;
};

} // namespace blink

#endif
