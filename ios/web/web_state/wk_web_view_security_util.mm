// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/web_state/wk_web_view_security_util.h"

#include "base/mac/scoped_cftyperef.h"
#include "base/strings/sys_string_conversions.h"
#include "net/cert/x509_certificate.h"
#include "net/ssl/ssl_info.h"

namespace web {

// This key was determined by inspecting userInfo dict of an SSL error.
NSString* const kNSErrorPeerCertificateChainKey =
    @"NSErrorPeerCertificateChainKey";

}

namespace {

// Maps NSError code to net::CertStatus.
net::CertStatus GetCertStatusFromNSErrorCode(NSInteger code) {
  switch (code) {
    // Regardless of real certificate problem the system always returns
    // NSURLErrorServerCertificateUntrusted. The mapping is done in case this
    // bug is fixed (rdar://18517043).
    case NSURLErrorServerCertificateUntrusted:
    case NSURLErrorSecureConnectionFailed:
    case NSURLErrorServerCertificateHasUnknownRoot:
    case NSURLErrorClientCertificateRejected:
    case NSURLErrorClientCertificateRequired:
      return net::CERT_STATUS_INVALID;
    case NSURLErrorServerCertificateHasBadDate:
    case NSURLErrorServerCertificateNotYetValid:
      return net::CERT_STATUS_DATE_INVALID;
  }
  NOTREACHED();
  return 0;
}

}  // namespace


namespace web {

scoped_refptr<net::X509Certificate> CreateCertFromChain(NSArray* certs) {
  if (certs.count == 0)
    return nullptr;
  net::X509Certificate::OSCertHandles intermediates;
  for (NSUInteger i = 1; i < certs.count; i++) {
    intermediates.push_back(reinterpret_cast<SecCertificateRef>(certs[i]));
  }
  return net::X509Certificate::CreateFromHandle(
      reinterpret_cast<SecCertificateRef>(certs[0]), intermediates);
}

scoped_refptr<net::X509Certificate> CreateCertFromTrust(SecTrustRef trust) {
  if (!trust)
    return nullptr;

  CFIndex cert_count = SecTrustGetCertificateCount(trust);
  if (cert_count == 0) {
    // At the moment there is no API which allows trust creation w/o certs.
    return nullptr;
  }

  net::X509Certificate::OSCertHandles intermediates;
  for (CFIndex i = 1; i < cert_count; i++) {
    intermediates.push_back(SecTrustGetCertificateAtIndex(trust, i));
  }
  return net::X509Certificate::CreateFromHandle(
      SecTrustGetCertificateAtIndex(trust, 0), intermediates);
}

base::ScopedCFTypeRef<SecTrustRef> CreateServerTrustFromChain(NSArray* certs,
                                                              NSString* host) {
  base::ScopedCFTypeRef<SecTrustRef> scoped_result;
  if (certs.count == 0)
    return scoped_result;

  base::ScopedCFTypeRef<SecPolicyRef> policy(
      SecPolicyCreateSSL(TRUE, static_cast<CFStringRef>(host)));
  SecTrustRef ref_result = nullptr;
  if (SecTrustCreateWithCertificates(certs, policy, &ref_result) ==
      errSecSuccess) {
    scoped_result.reset(ref_result);
  }
  return scoped_result;
}

void EnsureFutureTrustEvaluationSucceeds(SecTrustRef trust) {
  base::ScopedCFTypeRef<CFDataRef> exceptions(SecTrustCopyExceptions(trust));
  SecTrustSetExceptions(trust, exceptions);
}

BOOL IsWKWebViewSSLCertError(NSError* error) {
  if (![error.domain isEqualToString:NSURLErrorDomain]) {
    return NO;
  }

  switch (error.code) {
    case NSURLErrorServerCertificateHasBadDate:
    case NSURLErrorServerCertificateUntrusted:
    case NSURLErrorServerCertificateHasUnknownRoot:
    case NSURLErrorServerCertificateNotYetValid:
      return YES;
    case NSURLErrorSecureConnectionFailed:
      // Although the finer-grained errors above exist, iOS never uses them
      // and instead signals NSURLErrorSecureConnectionFailed for both
      // certificate failures and other SSL connection failures. Instead, check
      // if the error has a certificate attached (crbug.com/539735).
      return [error.userInfo[web::kNSErrorPeerCertificateChainKey] count] > 0;
    default:
      return NO;
  }
}

void GetSSLInfoFromWKWebViewSSLCertError(NSError* error,
                                         net::SSLInfo* ssl_info) {
  DCHECK(IsWKWebViewSSLCertError(error));
  ssl_info->cert_status = GetCertStatusFromNSErrorCode(error.code);
  ssl_info->cert = web::CreateCertFromChain(
      error.userInfo[web::kNSErrorPeerCertificateChainKey]);
}

SecurityStyle GetSecurityStyleFromTrustResult(SecTrustResultType result) {
  switch (result) {
    case kSecTrustResultInvalid:
      return SECURITY_STYLE_UNKNOWN;
    case kSecTrustResultProceed:
    case kSecTrustResultUnspecified:
      return SECURITY_STYLE_AUTHENTICATED;
    case kSecTrustResultDeny:
    case kSecTrustResultRecoverableTrustFailure:
    case kSecTrustResultFatalTrustFailure:
    case kSecTrustResultOtherError:
      return SECURITY_STYLE_AUTHENTICATION_BROKEN;
  }
  NOTREACHED();
  return SECURITY_STYLE_UNKNOWN;
}

}  // namespace web
