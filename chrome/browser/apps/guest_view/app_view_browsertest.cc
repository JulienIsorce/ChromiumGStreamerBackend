// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/stringprintf.h"
#include "chrome/browser/apps/app_browsertest_util.h"
#include "components/guest_view/browser/guest_view_manager.h"
#include "components/guest_view/browser/guest_view_manager_factory.h"
#include "components/guest_view/browser/test_guest_view_manager.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_process_host_observer.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/api/extensions_api_client.h"
#include "extensions/browser/app_window/app_window_registry.h"
#include "extensions/browser/guest_view/app_view/app_view_guest.h"
#include "extensions/browser/guest_view/extensions_guest_view_manager_delegate.h"
#include "extensions/browser/process_manager.h"
#include "extensions/common/switches.h"
#include "extensions/test/extension_test_message_listener.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

using extensions::ExtensionsAPIClient;
using guest_view::GuestViewManager;
using guest_view::TestGuestViewManagerFactory;

namespace {

class RenderProcessHostObserverForExit
    : public content::RenderProcessHostObserver {
 public:
  explicit RenderProcessHostObserverForExit(
      content::RenderProcessHost* observed_host)
      : render_process_host_exited_(false), observed_host_(observed_host) {
    observed_host->AddObserver(this);
  }

  void WaitUntilRenderProcessHostKilled() {
    if (render_process_host_exited_)
      return;
    message_loop_runner_ = new content::MessageLoopRunner;
    message_loop_runner_->Run();
  }

  base::TerminationStatus termination_status() const { return status_; }

 private:
  void RenderProcessExited(content::RenderProcessHost* host,
                           base::TerminationStatus status,
                           int exit_code) override {
    DCHECK(observed_host_ == host);
    render_process_host_exited_ = true;
    status_ = status;
    observed_host_->RemoveObserver(this);
    if (message_loop_runner_.get()) {
      message_loop_runner_->Quit();
    }
  }

  bool render_process_host_exited_;
  content::RenderProcessHost* observed_host_;
  scoped_refptr<content::MessageLoopRunner> message_loop_runner_;
  base::TerminationStatus status_;

  DISALLOW_COPY_AND_ASSIGN(RenderProcessHostObserverForExit);
};

}  // namespace

class AppViewTest : public extensions::PlatformAppBrowserTest {
 public:
  AppViewTest() {
    GuestViewManager::set_factory_for_testing(&factory_);
  }

  enum TestServer {
    NEEDS_TEST_SERVER,
    NO_TEST_SERVER
  };

  void TestHelper(const std::string& test_name,
                  const std::string& app_location,
                  const std::string& app_to_embed,
                  TestServer test_server) {
    // For serving guest pages.
    if (test_server == NEEDS_TEST_SERVER) {
      if (!StartEmbeddedTestServer()) {
        LOG(ERROR) << "FAILED TO START TEST SERVER.";
        return;
      }
    }

    LoadAndLaunchPlatformApp(app_location.c_str(), "Launched");

    // Flush any pending events to make sure we start with a clean slate.
    content::RunAllPendingInMessageLoop();

    content::WebContents* embedder_web_contents =
        GetFirstAppWindowWebContents();
    if (!embedder_web_contents) {
      LOG(ERROR) << "UNABLE TO FIND EMBEDDER WEB CONTENTS.";
      return;
    }

    ExtensionTestMessageListener done_listener("TEST_PASSED", false);
    done_listener.set_failure_message("TEST_FAILED");
    if (!content::ExecuteScript(
            embedder_web_contents,
            base::StringPrintf("runTest('%s', '%s')", test_name.c_str(),
                               app_to_embed.c_str()))) {
      LOG(ERROR) << "UNABLE TO START TEST.";
      return;
    }
    ASSERT_TRUE(done_listener.WaitUntilSatisfied());
  }

  guest_view::TestGuestViewManager* test_guest_view_manager() const {
    return test_guest_view_manager_;
  }

 private:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    extensions::PlatformAppBrowserTest::SetUpCommandLine(command_line);
  }

  void SetUpOnMainThread() override {
    extensions::PlatformAppBrowserTest::SetUpOnMainThread();
    test_guest_view_manager_ = static_cast<guest_view::TestGuestViewManager*>(
        guest_view::GuestViewManager::CreateWithDelegate(
            browser()->profile(),
            scoped_ptr<guest_view::GuestViewManagerDelegate>(
                ExtensionsAPIClient::Get()->CreateGuestViewManagerDelegate(
                    browser()->profile()))));
  }

  TestGuestViewManagerFactory factory_;
  guest_view::TestGuestViewManager* test_guest_view_manager_;

  DISALLOW_COPY_AND_ASSIGN(AppViewTest);
};

// Tests that <appview> is able to navigate to another installed app.
IN_PROC_BROWSER_TEST_F(AppViewTest, TestAppViewWithUndefinedDataShouldSucceed) {
  const extensions::Extension* skeleton_app =
      InstallPlatformApp("app_view/shim/skeleton");
  TestHelper("testAppViewWithUndefinedDataShouldSucceed",
             "app_view/shim",
             skeleton_app->id(),
             NO_TEST_SERVER);
}

// Tests that <appview> correctly processes parameters passed on connect.
IN_PROC_BROWSER_TEST_F(AppViewTest, TestAppViewRefusedDataShouldFail) {
  const extensions::Extension* skeleton_app =
      InstallPlatformApp("app_view/shim/skeleton");
  TestHelper("testAppViewRefusedDataShouldFail",
             "app_view/shim",
             skeleton_app->id(),
             NO_TEST_SERVER);
}

// Tests that <appview> correctly processes parameters passed on connect.
IN_PROC_BROWSER_TEST_F(AppViewTest, TestAppViewGoodDataShouldSucceed) {
  const extensions::Extension* skeleton_app =
      InstallPlatformApp("app_view/shim/skeleton");
  TestHelper("testAppViewGoodDataShouldSucceed",
             "app_view/shim",
             skeleton_app->id(),
             NO_TEST_SERVER);
}

// Tests that <appview> correctly handles multiple successive connects.
IN_PROC_BROWSER_TEST_F(AppViewTest, TestAppViewMultipleConnects) {
  const extensions::Extension* skeleton_app =
      InstallPlatformApp("app_view/shim/skeleton");
  TestHelper("testAppViewMultipleConnects",
             "app_view/shim",
             skeleton_app->id(),
             NO_TEST_SERVER);
}

// Tests that <appview> does not embed self (the app which owns appview).
IN_PROC_BROWSER_TEST_F(AppViewTest, TestAppViewEmbedSelfShouldFail) {
  const extensions::Extension* skeleton_app =
      InstallPlatformApp("app_view/shim/skeleton");
  TestHelper("testAppViewEmbedSelfShouldFail",
             "app_view/shim",
             skeleton_app->id(),
             NO_TEST_SERVER);
}

IN_PROC_BROWSER_TEST_F(AppViewTest, KillGuestWithInvalidInstanceID) {
  const extensions::Extension* mock_bad_app =
      LoadAndLaunchPlatformApp("app_view/bad_app", "AppViewTest.LAUNCHED");

  content::RenderProcessHost* bad_app_render_process_host =
      extensions::AppWindowRegistry::Get(browser()->profile())
          ->GetCurrentAppWindowForApp(mock_bad_app->id())
          ->web_contents()
          ->GetRenderProcessHost();

  // Monitor |mock_bad_app|'s RenderProcessHost for its exiting.
  RenderProcessHostObserverForExit exit_observer(bad_app_render_process_host);

  // Choosing a |guest_instance_id| which does not exist.
  int invalid_guest_instance_id =
      test_guest_view_manager()->GetNextInstanceID();
  // Call the desired function to verify that the |mock_bad_app| gets killed if
  // the provided |guest_instance_id| is not mapped to any "GuestView"'s.
  extensions::AppViewGuest::CompletePendingRequest(
      browser()->profile(), GURL("about:blank"), invalid_guest_instance_id,
      mock_bad_app->id(), bad_app_render_process_host);
  exit_observer.WaitUntilRenderProcessHostKilled();
}

// Failing on msan bot: crbug.com/507940 and Linux dbg http://crbug.com/533318
#if defined(MEMORY_SANITIZER) || (defined(OS_LINUX) && !defined(NDEBUG))
#define MAYBE_KillGuestCommunicatingWithWrongAppView \
        DISABLED_KillGuestCommunicatingWithWrongAppView
#else
#define MAYBE_KillGuestCommunicatingWithWrongAppView \
        KillGuestCommunicatingWithWrongAppView
#endif

IN_PROC_BROWSER_TEST_F(AppViewTest,
                       MAYBE_KillGuestCommunicatingWithWrongAppView) {
  const extensions::Extension* host_app =
      LoadAndLaunchPlatformApp("app_view/host_app", "AppViewTest.LAUNCHED");
  const extensions::Extension* mock_guest_extension =
      InstallPlatformApp("app_view/guest_app");
  const extensions::Extension* mock_bad_app =
      LoadAndLaunchPlatformApp("app_view/bad_app", "AppViewTest.LAUNCHED");

  EXPECT_TRUE(content::ExecuteScript(
      extensions::AppWindowRegistry::Get(browser()->profile())
          ->GetCurrentAppWindowForApp(host_app->id())
          ->web_contents(),
      base::StringPrintf("onAppCommand('%s', '%s');", "EMBED",
                         mock_guest_extension->id().c_str())));
  ExtensionTestMessageListener on_embed_requested_listener(
      "AppViewTest.EmbedRequested", true);
  EXPECT_TRUE(on_embed_requested_listener.WaitUntilSatisfied());
  // Now assume the bad application is somehow sending a message to complete a
  // pending request to attach to <appview>. It should be killed.
  content::RenderProcessHost* bad_app_render_process_host =
      extensions::ProcessManager::Get(browser()->profile())
          ->GetBackgroundHostForExtension(mock_bad_app->id())
          ->render_process_host();
  RenderProcessHostObserverForExit bad_app_obs(bad_app_render_process_host);
  // Make the false request.
  int guest_instance_id =
      extensions::AppViewGuest::GetAllRegisteredInstanceIdsForTesting()[0];
  extensions::AppViewGuest::CompletePendingRequest(
      browser()->profile(), GURL("about:blank"), guest_instance_id,
      mock_bad_app->id(), bad_app_render_process_host);

  on_embed_requested_listener.Reply("continue");
  // Make sure the bad application has been terminated.
  bad_app_obs.WaitUntilRenderProcessHostKilled();
}
