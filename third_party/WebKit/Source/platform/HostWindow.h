/*
 * Copyright (C) 2008 Apple Inc.  All rights reserved.
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

#ifndef HostWindow_h
#define HostWindow_h

#include "platform/PlatformExport.h"
#include "platform/heap/Handle.h"
#include "wtf/FastAllocBase.h"
#include "wtf/Noncopyable.h"

namespace blink {
class IntRect;
struct WebScreenInfo;

class PLATFORM_EXPORT HostWindow : public NoBaseWillBeGarbageCollectedFinalized<HostWindow> {
    WTF_MAKE_NONCOPYABLE(HostWindow);
    WTF_MAKE_FAST_ALLOCATED_WILL_BE_REMOVED(HostWindow);
public:
    HostWindow() { }
    virtual ~HostWindow() { }
    DEFINE_INLINE_VIRTUAL_TRACE() { }

    // Requests the host invalidate the contents.
    virtual void invalidateRect(const IntRect& updateRect) = 0;

    // Converts from the window coordinates to screen coordinates.
    virtual IntRect viewportToScreen(const IntRect&) const = 0;

    virtual void scheduleAnimation() = 0;
};

} // namespace blink

#endif // HostWindow_h
