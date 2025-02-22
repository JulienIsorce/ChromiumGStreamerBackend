// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/web_state/wk_web_view_security_util.h"

#import <Foundation/Foundation.h>
#include <Security/Security.h>

#include "base/mac/scoped_cftyperef.h"
#include "base/memory/scoped_ptr.h"
#include "crypto/rsa_private_key.h"
#include "ios/web/public/test/web_test_util.h"
#include "net/cert/x509_cert_types.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util.h"
#include "net/ssl/ssl_info.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/gtest_mac.h"
#include "testing/platform_test.h"

namespace web {
namespace {
// Subject for testing self-signed certificate.
const char kTestSubject[] = "self-signed";
// Hostname for testing SecTrustRef objects.
NSString* const kTestHost = @"www.example.com";

// Returns an autoreleased certificate chain for testing. Chain will contain a
// single self-signed cert with |subject| as a subject.
NSArray* MakeTestCertChain(const std::string& subject) {
  scoped_ptr<crypto::RSAPrivateKey> private_key;
  std::string der_cert;
  net::x509_util::CreateKeyAndSelfSignedCert(
      "CN=" + subject, 1, base::Time::Now(),
      base::Time::Now() + base::TimeDelta::FromDays(1), &private_key,
      &der_cert);

  base::ScopedCFTypeRef<SecCertificateRef> cert(
      net::X509Certificate::CreateOSCertHandleFromBytes(der_cert.data(),
                                                        der_cert.size()));
  NSArray* result = @[ reinterpret_cast<id>(cert.get()) ];
  return result;
}

// Returns an autoreleased dictionary, which represents NSError's user info for
// testing.
NSDictionary* MakeTestSSLCertErrorUserInfo() {
  return @{
    web::kNSErrorPeerCertificateChainKey : MakeTestCertChain(kTestSubject),
  };
}

// Returns SecTrustRef object for testing.
base::ScopedCFTypeRef<SecTrustRef> CreateTestTrust(NSArray* cert_chain) {
  base::ScopedCFTypeRef<SecPolicyRef> policy(SecPolicyCreateBasicX509());
  SecTrustRef trust = nullptr;
  SecTrustCreateWithCertificates(cert_chain, policy, &trust);
  return base::ScopedCFTypeRef<SecTrustRef>(trust);
}

}  // namespace

// Test class for wk_web_view_security_util functions.
typedef PlatformTest WKWebViewSecurityUtilTest;

// Tests CreateCertFromChain with self-signed cert.
TEST_F(WKWebViewSecurityUtilTest, CreationCertFromChain) {
  scoped_refptr<net::X509Certificate> cert =
      CreateCertFromChain(MakeTestCertChain(kTestSubject));
  EXPECT_TRUE(cert->subject().GetDisplayName() == kTestSubject);
}

// Tests CreateCertFromChain with nil chain.
TEST_F(WKWebViewSecurityUtilTest, CreationCertFromNilChain) {
  EXPECT_FALSE(CreateCertFromChain(nil));
}

// Tests CreateCertFromChain with empty chain.
TEST_F(WKWebViewSecurityUtilTest, CreationCertFromEmptyChain) {
  EXPECT_FALSE(CreateCertFromChain(@[]));
}

// Tests MakeTrustValid with self-signed cert.
TEST_F(WKWebViewSecurityUtilTest, MakingTrustValid) {
  // Create invalid trust object.
  base::ScopedCFTypeRef<SecTrustRef> trust =
      CreateTestTrust(MakeTestCertChain(kTestSubject));

  SecTrustResultType result = -1;
  SecTrustEvaluate(trust, &result);
  EXPECT_EQ(kSecTrustResultRecoverableTrustFailure, result);

  // Make sure that trust becomes valid after
  // |EnsureFutureTrustEvaluationSucceeds| call.
  EnsureFutureTrustEvaluationSucceeds(trust);
  SecTrustEvaluate(trust, &result);
  EXPECT_EQ(kSecTrustResultProceed, result);
}

// Tests CreateCertFromTrust.
TEST_F(WKWebViewSecurityUtilTest, CreationCertFromTrust) {
  base::ScopedCFTypeRef<SecTrustRef> trust =
      CreateTestTrust(MakeTestCertChain(kTestSubject));
  scoped_refptr<net::X509Certificate> cert = CreateCertFromTrust(trust);
  EXPECT_TRUE(cert->subject().GetDisplayName() == kTestSubject);
}

// Tests CreateCertFromTrust with nil trust.
TEST_F(WKWebViewSecurityUtilTest, CreationCertFromNilTrust) {
  EXPECT_FALSE(CreateCertFromTrust(nil));
}

// Tests CreateServerTrustFromChain with valid input.
TEST_F(WKWebViewSecurityUtilTest, CreationServerTrust) {
  // Create server trust.
  NSArray* chain = MakeTestCertChain(kTestSubject);
  base::ScopedCFTypeRef<SecTrustRef> server_trust(
      CreateServerTrustFromChain(chain, kTestHost));
  EXPECT_TRUE(server_trust);

  // Verify chain.
  EXPECT_EQ(static_cast<CFIndex>(chain.count),
            SecTrustGetCertificateCount(server_trust));
  [chain enumerateObjectsUsingBlock:^(id expected_cert, NSUInteger i, BOOL*) {
    id actual_cert = static_cast<id>(SecTrustGetCertificateAtIndex(
        server_trust.get(), static_cast<CFIndex>(i)));
    EXPECT_EQ(expected_cert, actual_cert);
  }];

  // Verify policies.
  CFArrayRef policies = nullptr;
  EXPECT_EQ(errSecSuccess, SecTrustCopyPolicies(server_trust.get(), &policies));
  EXPECT_EQ(1, CFArrayGetCount(policies));
  SecPolicyRef policy = (SecPolicyRef)CFArrayGetValueAtIndex(policies, 0);
  base::ScopedCFTypeRef<CFDictionaryRef> properties(
      SecPolicyCopyProperties(policy));
  NSString* name = static_cast<NSString*>(
      CFDictionaryGetValue(properties.get(), kSecPolicyName));
  EXPECT_NSEQ(kTestHost, name);
  CFRelease(policies);
}

// Tests CreateServerTrustFromChain with nil chain.
TEST_F(WKWebViewSecurityUtilTest, CreationServerTrustFromNilChain) {
  EXPECT_FALSE(CreateServerTrustFromChain(nil, kTestHost));
}

// Tests CreateServerTrustFromChain with empty chain.
TEST_F(WKWebViewSecurityUtilTest, CreationServerTrustFromEmptyChain) {
  EXPECT_FALSE(CreateServerTrustFromChain(@[], kTestHost));
}

// Tests that IsWKWebViewSSLCertError returns YES for NSError with
// NSURLErrorDomain domain, NSURLErrorSecureConnectionFailed error code and
// certificate chain.
TEST_F(WKWebViewSecurityUtilTest, CheckSecureConnectionFailedWithCertError) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  EXPECT_TRUE(IsWKWebViewSSLCertError([NSError
      errorWithDomain:NSURLErrorDomain
                 code:NSURLErrorSecureConnectionFailed
             userInfo:MakeTestSSLCertErrorUserInfo()]));
}

// Tests that IsWKWebViewSSLCertError returns NO for NSError with
// NSURLErrorDomain domain, NSURLErrorSecureConnectionFailed error code and no
// certificate chain.
TEST_F(WKWebViewSecurityUtilTest, CheckSecureConnectionFailedWithoutCertError) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  EXPECT_FALSE(IsWKWebViewSSLCertError([NSError
      errorWithDomain:NSURLErrorDomain
                 code:NSURLErrorSecureConnectionFailed
             userInfo:nil]));
}

// Tests that IsWKWebViewSSLCertError returns YES for NSError with
// NSURLErrorDomain domain and certificates error codes.
TEST_F(WKWebViewSecurityUtilTest, CheckCertificateSSLError) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  EXPECT_TRUE(IsWKWebViewSSLCertError([NSError
      errorWithDomain:NSURLErrorDomain
                 code:NSURLErrorServerCertificateHasBadDate
             userInfo:nil]));
  EXPECT_TRUE(IsWKWebViewSSLCertError([NSError
      errorWithDomain:NSURLErrorDomain
                 code:NSURLErrorServerCertificateUntrusted
             userInfo:nil]));
  EXPECT_TRUE(IsWKWebViewSSLCertError([NSError
      errorWithDomain:NSURLErrorDomain
                 code:NSURLErrorServerCertificateHasUnknownRoot
             userInfo:nil]));
  EXPECT_TRUE(IsWKWebViewSSLCertError([NSError
      errorWithDomain:NSURLErrorDomain
                 code:NSURLErrorServerCertificateNotYetValid
             userInfo:nil]));
}

// Tests that IsWKWebViewSSLCertError returns NO for NSError with
// NSURLErrorDomain domain and non cert SSL error codes.
TEST_F(WKWebViewSecurityUtilTest, CheckNonCertificateSSLError) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  EXPECT_FALSE(IsWKWebViewSSLCertError([NSError
      errorWithDomain:NSURLErrorDomain
                 code:NSURLErrorClientCertificateRejected
             userInfo:nil]));
  EXPECT_FALSE(IsWKWebViewSSLCertError([NSError
      errorWithDomain:NSURLErrorDomain
                 code:NSURLErrorClientCertificateRequired
             userInfo:nil]));
}

// Tests that IsWKWebViewSSLCertError returns NO for NSError with
// NSURLErrorDomain domain and NSURLErrorDataLengthExceedsMaximum error code.
TEST_F(WKWebViewSecurityUtilTest, CheckDataLengthExceedsMaximumError) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  EXPECT_FALSE(IsWKWebViewSSLCertError([NSError
      errorWithDomain:NSURLErrorDomain
                 code:NSURLErrorDataLengthExceedsMaximum
             userInfo:nil]));
}

// Tests that IsWKWebViewSSLCertError returns NO for NSError with
// NSURLErrorDomain domain and NSURLErrorCannotLoadFromNetwork error code.
TEST_F(WKWebViewSecurityUtilTest, CheckCannotLoadFromNetworkError) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  EXPECT_FALSE(IsWKWebViewSSLCertError([NSError
      errorWithDomain:NSURLErrorDomain
                 code:NSURLErrorCannotLoadFromNetwork
             userInfo:nil]));
}

// Tests GetSSLInfoFromWKWebViewSSLCertError with NSError and self-signed cert.
TEST_F(WKWebViewSecurityUtilTest, SSLInfoFromErrorWithCert) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  NSError* unknownCertError =
      [NSError errorWithDomain:NSURLErrorDomain
                          code:NSURLErrorServerCertificateHasUnknownRoot
                      userInfo:MakeTestSSLCertErrorUserInfo()];

  net::SSLInfo info;
  GetSSLInfoFromWKWebViewSSLCertError(unknownCertError, &info);
  EXPECT_TRUE(info.is_valid());
  EXPECT_EQ(net::CERT_STATUS_INVALID, info.cert_status);
  EXPECT_TRUE(info.cert->subject().GetDisplayName() == kTestSubject);
}

// Tests GetSecurityStyleFromTrustResult with bad SecTrustResultType result.
TEST_F(WKWebViewSecurityUtilTest, GetSecurityStyleFromBadResult) {
  EXPECT_EQ(SECURITY_STYLE_AUTHENTICATION_BROKEN,
            GetSecurityStyleFromTrustResult(kSecTrustResultDeny));
  EXPECT_EQ(
      SECURITY_STYLE_AUTHENTICATION_BROKEN,
      GetSecurityStyleFromTrustResult(kSecTrustResultRecoverableTrustFailure));
  EXPECT_EQ(SECURITY_STYLE_AUTHENTICATION_BROKEN,
            GetSecurityStyleFromTrustResult(kSecTrustResultFatalTrustFailure));
  EXPECT_EQ(SECURITY_STYLE_AUTHENTICATION_BROKEN,
            GetSecurityStyleFromTrustResult(kSecTrustResultOtherError));
}

// Tests GetSecurityStyleFromTrustResult with good SecTrustResultType result.
TEST_F(WKWebViewSecurityUtilTest, GetSecurityStyleFromGoodResult) {
  EXPECT_EQ(SECURITY_STYLE_AUTHENTICATED,
            GetSecurityStyleFromTrustResult(kSecTrustResultProceed));
  EXPECT_EQ(SECURITY_STYLE_AUTHENTICATED,
            GetSecurityStyleFromTrustResult(kSecTrustResultUnspecified));
}

// Tests GetSecurityStyleFromTrustResult with invalid SecTrustResultType result.
TEST_F(WKWebViewSecurityUtilTest, GetSecurityStyleFromInvalidResult) {
  EXPECT_EQ(SECURITY_STYLE_UNKNOWN,
            GetSecurityStyleFromTrustResult(kSecTrustResultInvalid));
}

}  // namespace web
