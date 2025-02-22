// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/web_state/ui/crw_wk_script_message_router.h"

#include "base/mac/scoped_block.h"
#include "base/mac/scoped_nsobject.h"
#include "ios/web/public/test/test_browser_state.h"
#import "ios/web/public/test/test_web_client.h"
#include "ios/web/public/test/web_test_util.h"
#include "ios/web/test/web_test.h"
#include "third_party/ocmock/OCMock/OCMock.h"
#include "third_party/ocmock/gtest_support.h"

namespace {

// Returns WKScriptMessage mock.
id GetScriptMessageMock(WKWebView* web_view, NSString* name) {
  id result = [OCMockObject mockForClass:[WKScriptMessage class]];
  [[[result stub] andReturn:web_view] webView];
  [[[result stub] andReturn:name] name];
  return result;
}

// Test fixture for CRWWKScriptMessageRouter.
class CRWWKScriptMessageRouterTest : public web::WebTest {
 protected:
  void SetUp() override {
    CR_TEST_REQUIRES_WK_WEB_VIEW();
    web::SetWebClient(&web_client_);
    // Mock WKUserContentController object.
    controller_mock_.reset(
        [[OCMockObject mockForClass:[WKUserContentController class]] retain]);
    [controller_mock_ setExpectationOrderMatters:YES];

    // Create testable CRWWKScriptMessageRouter.
    router_.reset(static_cast<id<WKScriptMessageHandler>>(
        [[CRWWKScriptMessageRouter alloc]
            initWithUserContentController:controller_mock_]));

    // Prepare test data.
    handler1_.reset([^{
    } copy]);
    handler2_.reset([^{
    } copy]);
    handler3_.reset([^{
    } copy]);
    name1_.reset([@"name1" copy]);
    name2_.reset([@"name2" copy]);
    name3_.reset([@"name3" copy]);
    web_view1_.reset(web::CreateWKWebView(CGRectZero, &browser_state_));
    web_view2_.reset(web::CreateWKWebView(CGRectZero, &browser_state_));
    web_view3_.reset(web::CreateWKWebView(CGRectZero, &browser_state_));
  }
  void TearDown() override {
    EXPECT_OCMOCK_VERIFY(controller_mock_);
    web::SetWebClient(nullptr);
  }

  // WKUserContentController mock used to create testable router.
  base::scoped_nsobject<id> controller_mock_;

  // CRWWKScriptMessageRouter set up for testing.
  base::scoped_nsobject<id> router_;

  // Tests data.
  typedef void (^WKScriptMessageHandler)(WKScriptMessage*);
  base::mac::ScopedBlock<WKScriptMessageHandler> handler1_;
  base::mac::ScopedBlock<WKScriptMessageHandler> handler2_;
  base::mac::ScopedBlock<WKScriptMessageHandler> handler3_;
  base::scoped_nsobject<NSString> name1_;
  base::scoped_nsobject<NSString> name2_;
  base::scoped_nsobject<NSString> name3_;
  base::scoped_nsobject<WKWebView> web_view1_;
  base::scoped_nsobject<WKWebView> web_view2_;
  base::scoped_nsobject<WKWebView> web_view3_;

 private:
  // WebClient and BrowserState for testing.
  web::TestWebClient web_client_;
  web::TestBrowserState browser_state_;
};

// Tests CRWWKScriptMessageRouter designated initializer.
TEST_F(CRWWKScriptMessageRouterTest, Initialization) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  EXPECT_TRUE(router_);
}

// Tests registration/deregistation of message handlers.
TEST_F(CRWWKScriptMessageRouterTest, HandlerRegistration) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  [[controller_mock_ expect] addScriptMessageHandler:router_ name:name1_];
  [[controller_mock_ expect] addScriptMessageHandler:router_ name:name2_];

  [[controller_mock_ expect] removeScriptMessageHandlerForName:name1_];
  [[controller_mock_ expect] removeScriptMessageHandlerForName:name2_];

  [router_ setScriptMessageHandler:handler1_ name:name1_ webView:web_view1_];
  [router_ setScriptMessageHandler:handler2_ name:name2_ webView:web_view2_];
  [router_ setScriptMessageHandler:handler3_ name:name2_ webView:web_view3_];

  [router_ removeScriptMessageHandlerForName:name1_ webView:web_view1_];
  [router_ removeScriptMessageHandlerForName:name2_ webView:web_view2_];
  [router_ removeScriptMessageHandlerForName:name2_ webView:web_view3_];
}

// Tests registration of message handlers. Test ensures that
// WKScriptMessageHandler is not removed if CRWWKScriptMessageRouter has valid
// message handlers.
TEST_F(CRWWKScriptMessageRouterTest, HandlerRegistrationLeak) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  [[controller_mock_ expect] addScriptMessageHandler:router_ name:name1_];

  // -removeScriptMessageHandlerForName must not be called.

  [router_ setScriptMessageHandler:handler1_ name:name1_ webView:web_view1_];
  [router_ setScriptMessageHandler:handler2_ name:name1_ webView:web_view2_];

  [router_ removeScriptMessageHandlerForName:name1_ webView:web_view1_];
}

// Tests deregistation of all message handlers.
TEST_F(CRWWKScriptMessageRouterTest, RemoveAllHandlers) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  [[controller_mock_ expect] addScriptMessageHandler:router_ name:name1_];
  [[controller_mock_ expect] addScriptMessageHandler:router_ name:name2_];

  [[controller_mock_ expect] removeScriptMessageHandlerForName:name2_];
  [[controller_mock_ expect] removeScriptMessageHandlerForName:name1_];

  [router_ setScriptMessageHandler:handler1_ name:name1_ webView:web_view1_];
  [router_ setScriptMessageHandler:handler2_ name:name2_ webView:web_view1_];
  [router_ setScriptMessageHandler:handler3_ name:name1_ webView:web_view2_];

  [router_ removeAllScriptMessageHandlersForWebView:web_view1_];
  [router_ removeAllScriptMessageHandlersForWebView:web_view2_];
}

// Tests deregistation of all message handlers. Test ensures that
// WKScriptMessageHandler is not removed if CRWWKScriptMessageRouter has valid
// message handlers.
TEST_F(CRWWKScriptMessageRouterTest, RemoveAllHandlersLeak) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  [[controller_mock_ expect] addScriptMessageHandler:router_ name:name1_];
  [[controller_mock_ expect] addScriptMessageHandler:router_ name:name2_];
  [[controller_mock_ expect] addScriptMessageHandler:router_ name:name3_];

  [[controller_mock_ expect] removeScriptMessageHandlerForName:name2_];
  // -removeScriptMessageHandlerForName:name1_ must not be called.

  [router_ setScriptMessageHandler:handler1_ name:name1_ webView:web_view1_];
  [router_ setScriptMessageHandler:handler2_ name:name2_ webView:web_view1_];
  [router_ setScriptMessageHandler:handler2_ name:name3_ webView:web_view2_];
  [router_ setScriptMessageHandler:handler3_ name:name1_ webView:web_view2_];

  [router_ removeAllScriptMessageHandlersForWebView:web_view1_];
}

// Tests proper routing of WKScriptMessage object depending on message name and
// web view.
TEST_F(CRWWKScriptMessageRouterTest, Routing) {
  CR_TEST_REQUIRES_WK_WEB_VIEW();

  // It's expected that messages handlers will be called once and in order.
  __block NSInteger last_called_handler = 0;
  id message1 = GetScriptMessageMock(web_view1_, name1_);
  id handler1 = ^(WKScriptMessage* message) {
    EXPECT_EQ(0, last_called_handler);
    EXPECT_EQ(message1, message);
    last_called_handler = 1;
  };
  id message2 = GetScriptMessageMock(web_view2_, name2_);
  id handler2 = ^(WKScriptMessage* message) {
    EXPECT_EQ(1, last_called_handler);
    EXPECT_EQ(message2, message);
    last_called_handler = 2;
  };
  id message3 = GetScriptMessageMock(web_view3_, name2_);
  id handler3 = ^(WKScriptMessage* message) {
    EXPECT_EQ(2, last_called_handler);
    EXPECT_EQ(message3, message);
    last_called_handler = 3;
  };

  [[controller_mock_ expect] addScriptMessageHandler:router_ name:name1_];
  [[controller_mock_ expect] addScriptMessageHandler:router_ name:name2_];

  [router_ setScriptMessageHandler:handler1 name:name1_ webView:web_view1_];
  [router_ setScriptMessageHandler:handler2 name:name2_ webView:web_view2_];
  [router_ setScriptMessageHandler:handler3 name:name2_ webView:web_view3_];

  [router_ userContentController:controller_mock_
         didReceiveScriptMessage:message1];
  [router_ userContentController:controller_mock_
         didReceiveScriptMessage:message2];
  [router_ userContentController:controller_mock_
         didReceiveScriptMessage:message3];

  EXPECT_EQ(3, last_called_handler);
}

}  // namespace
