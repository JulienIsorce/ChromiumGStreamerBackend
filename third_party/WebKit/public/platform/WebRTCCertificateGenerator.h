/*
 * Copyright (C) 2015 Google Inc. All rights reserved.
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

#ifndef WebRTCCertificateGenerator_h
#define WebRTCCertificateGenerator_h

#include "public/platform/WebCallbacks.h"
#include "public/platform/WebRTCCertificate.h"
#include "public/platform/WebRTCKeyParams.h"
#include "public/platform/WebURL.h"

namespace blink {

// Interface defining a class that can generate WebRTCCertificates asynchronously.
class WebRTCCertificateGenerator {
public:
    virtual ~WebRTCCertificateGenerator() {}

    // Start generating a certificate asynchronously. The |observer| is invoked on the
    // same thread that called generateCertificate when the operation is completed.
    virtual void generateCertificate(
        const WebRTCKeyParams&,
        const WebURL&,
        const WebURL& firstPartyForCookies,
        WebCallbacks<WebRTCCertificate*, void>* observer) = 0;

    // Determines if the parameters should be considered valid for certificate generation.
    // For example, if the number of bits of some parameter is too small or too large we
    // may want to reject it for security or performance reasons.
    virtual bool isValidKeyParams(const WebRTCKeyParams&) = 0;
};

} // namespace blink

#endif // WebRTCCertificateGenerator_h
