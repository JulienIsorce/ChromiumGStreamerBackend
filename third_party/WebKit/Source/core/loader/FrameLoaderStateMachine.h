/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Google, Inc. nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef FrameLoaderStateMachine_h
#define FrameLoaderStateMachine_h

#include "core/CoreExport.h"
#include "wtf/Allocator.h"
#include "wtf/Noncopyable.h"

namespace blink {

// Encapsulates a state machine for FrameLoader. Note that this is different from FrameState,
// which stores the state of the current load that FrameLoader is executing.
class CORE_EXPORT FrameLoaderStateMachine {
    DISALLOW_ALLOCATION();
    WTF_MAKE_NONCOPYABLE(FrameLoaderStateMachine);
public:
    FrameLoaderStateMachine();

    // Once a load has been committed, the state may
    // alternate between CommittedFirstRealLoad and FirstLayoutDone.
    // Otherwise, the states only go down the list.
    enum State {
        CreatingInitialEmptyDocument,
        DisplayingInitialEmptyDocument,
        CommittedFirstRealLoad,
        CommittedMultipleRealLoads
    };

    bool committedFirstRealDocumentLoad() const;
    bool creatingInitialEmptyDocument() const;
    bool isDisplayingInitialEmptyDocument() const;
    bool committedMultipleRealLoads() const;
    void advanceTo(State);

private:
    State m_state;
};

} // namespace blink

#endif // FrameLoaderStateMachine_h
