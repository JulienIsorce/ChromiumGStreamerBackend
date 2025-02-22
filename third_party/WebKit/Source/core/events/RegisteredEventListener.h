/*
 * Copyright (C) 2001 Peter Kelly (pmk@post.com)
 * Copyright (C) 2001 Tobias Anton (anton@stud.fbi.fh-darmstadt.de)
 * Copyright (C) 2006 Samuel Weinig (sam.weinig@gmail.com)
 * Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Apple Inc. All rights reserved.
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

#ifndef RegisteredEventListener_h
#define RegisteredEventListener_h

#include "core/events/EventListener.h"
#include "wtf/RefPtr.h"

namespace blink {

class RegisteredEventListener {
    ALLOW_ONLY_INLINE_ALLOCATION();
public:
    RegisteredEventListener(PassRefPtrWillBeRawPtr<EventListener> listener, const EventListenerOptions& options)
        : listener(listener)
        , useCapture(options.capture())
    {
    }

    DEFINE_INLINE_TRACE()
    {
        visitor->trace(listener);
    }

    EventListenerOptions options() const
    {
        EventListenerOptions result;
        result.setCapture(useCapture);
        return result;
    }

    RefPtrWillBeMember<EventListener> listener;
    unsigned useCapture : 1;
};

inline bool operator==(const RegisteredEventListener& a, const RegisteredEventListener& b)
{

    ASSERT(a.listener);
    ASSERT(b.listener);
    return *a.listener == *b.listener && a.useCapture == b.useCapture;
}

} // namespace blink

WTF_ALLOW_CLEAR_UNUSED_SLOTS_WITH_MEM_FUNCTIONS(blink::RegisteredEventListener);

#endif // RegisteredEventListener_h
