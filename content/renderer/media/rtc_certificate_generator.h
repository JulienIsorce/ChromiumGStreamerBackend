// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_RTC_CERTIFICATE_GENERATOR_H_
#define CONTENT_RENDERER_MEDIA_RTC_CERTIFICATE_GENERATOR_H_

#include "base/macros.h"
#include "third_party/WebKit/public/platform/WebRTCCertificate.h"
#include "third_party/WebKit/public/platform/WebRTCCertificateGenerator.h"
#include "third_party/WebKit/public/platform/WebRTCKeyParams.h"

namespace content {

// Chromium's WebRTCCertificateGenerator implementation; uses the
// PeerConnectionIdentityStore/SSLIdentity::Generate to generate the identity,
// rtc::RTCCertificate and content::RTCCertificate.
class RTCCertificateGenerator : public blink::WebRTCCertificateGenerator {
 public:
  RTCCertificateGenerator() {}
  ~RTCCertificateGenerator() override {}

  // blink::WebRTCCertificateGenerator implementation.
  void generateCertificate(
      const blink::WebRTCKeyParams& key_params,
      const blink::WebURL& url,
      const blink::WebURL& first_party_for_cookies,
      blink::WebCallbacks<blink::WebRTCCertificate*, void>* observer) override;
  bool isValidKeyParams(const blink::WebRTCKeyParams& key_params) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(RTCCertificateGenerator);
};

}  // namespace content

#endif  // CONTENT_RENDERER_MEDIA_RTC_CERTIFICATE_GENERATOR_H_
