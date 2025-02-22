// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "modules/nfc/NavigatorNFC.h"

#include "core/frame/Navigator.h"
#include "modules/nfc/NFC.h"

namespace blink {

NavigatorNFC::NavigatorNFC()
{
}

const char* NavigatorNFC::supplementName()
{
    return "NavigatorNFC";
}

NavigatorNFC& NavigatorNFC::from(Navigator& navigator)
{
    NavigatorNFC* supplement = static_cast<NavigatorNFC*>(HeapSupplement<Navigator>::from(navigator, supplementName()));
    if (!supplement) {
        supplement = new NavigatorNFC();
        provideTo(navigator, supplementName(), supplement);
    }
    return *supplement;
}

NFC* NavigatorNFC::nfc(ExecutionContext* context, Navigator& navigator)
{
    NavigatorNFC& self = NavigatorNFC::from(navigator);
    if (!self.m_nfc) {
        if (!navigator.frame())
            return nullptr;
        self.m_nfc = NFC::create(context, navigator.frame());
    }
    return self.m_nfc.get();
}

DEFINE_TRACE(NavigatorNFC)
{
    visitor->trace(m_nfc);
    HeapSupplement<Navigator>::trace(visitor);
}

} // namespace blink
