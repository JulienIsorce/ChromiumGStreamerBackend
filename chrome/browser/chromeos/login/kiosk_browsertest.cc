// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "ash/desktop_background/desktop_background_controller.h"
#include "ash/desktop_background/desktop_background_controller_observer.h"
#include "ash/shell.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/prefs/pref_service.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/app_mode/fake_cws.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_launch_error.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_manager.h"
#include "chrome/browser/chromeos/file_manager/fake_disk_mount_manager.h"
#include "chrome/browser/chromeos/login/app_launch_controller.h"
#include "chrome/browser/chromeos/login/startup_utils.h"
#include "chrome/browser/chromeos/login/test/app_window_waiter.h"
#include "chrome/browser/chromeos/login/test/oobe_base_test.h"
#include "chrome/browser/chromeos/login/test/oobe_screen_waiter.h"
#include "chrome/browser/chromeos/login/ui/login_display_host.h"
#include "chrome/browser/chromeos/login/ui/login_display_host_impl.h"
#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/mock_user_manager.h"
#include "chrome/browser/chromeos/login/users/scoped_user_manager_enabler.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/chromeos/ownership/fake_owner_settings_service.h"
#include "chrome/browser/chromeos/policy/device_local_account.h"
#include "chrome/browser/chromeos/policy/device_policy_cros_browser_test.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/chromeos/settings/device_oauth2_token_service.h"
#include "chrome/browser/chromeos/settings/device_oauth2_token_service_factory.h"
#include "chrome/browser/chromeos/settings/scoped_cros_settings_test_helper.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/updater/local_extension_cache.h"
#include "chrome/browser/profiles/profile_impl.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/ui/webui/chromeos/login/kiosk_app_menu_handler.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "chromeos/chromeos_paths.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/cryptohome_client.h"
#include "chromeos/disks/disk_mount_manager.h"
#include "chromeos/settings/cros_settings_provider.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/signin/core/common/signin_pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_ui.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/app_window/app_window.h"
#include "extensions/browser/app_window/app_window_registry.h"
#include "extensions/browser/app_window/native_app_window.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/manifest.h"
#include "extensions/components/native_app_window/native_app_window_views.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/result_catcher.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/gaia_switches.h"
#include "google_apis/gaia/gaia_urls.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/accelerators/accelerator.h"

namespace em = enterprise_management;

namespace chromeos {

namespace {

// This is a simple test app that creates an app window and immediately closes
// it again. Webstore data json is in
//   chrome/test/data/chromeos/app_mode/webstore/inlineinstall/
//       detail/ggbflgnkafappblpkiflbgpmkfdpnhhe
const char kTestKioskApp[] = "ggbflgnkafappblpkiflbgpmkfdpnhhe";

// This app creates a window and declares usage of the identity API in its
// manifest, so we can test device robot token minting via the identity API.
// Webstore data json is in
//   chrome/test/data/chromeos/app_mode/webstore/inlineinstall/
//       detail/ibjkkfdnfcaoapcpheeijckmpcfkifob
const char kTestEnterpriseKioskApp[] = "ibjkkfdnfcaoapcpheeijckmpcfkifob";

// An offline enable test app. Webstore data json is in
//   chrome/test/data/chromeos/app_mode/webstore/inlineinstall/
//       detail/ajoggoflpgplnnjkjamcmbepjdjdnpdp
// An app profile with version 1.0.0 installed is in
//   chrome/test/data/chromeos/app_mode/offline_enabled_app_profile
// The version 2.0.0 crx is in
//   chrome/test/data/chromeos/app_mode/webstore/downloads/
const char kTestOfflineEnabledKioskApp[] = "ajoggoflpgplnnjkjamcmbepjdjdnpdp";

// An app to test local fs data persistence across app update. V1 app writes
// data into local fs. V2 app reads and verifies the data.
// Webstore data json is in
//   chrome/test/data/chromeos/app_mode/webstore/inlineinstall/
//       detail/bmbpicmpniaclbbpdkfglgipkkebnbjf
const char kTestLocalFsKioskApp[] = "bmbpicmpniaclbbpdkfglgipkkebnbjf";

// An app to test local access to file systems via the
// chrome.fileSystem.requestFileSystem API.
// Webstore data json is in
//   chrome/test/data/chromeos/app_mode/webstore/inlineinstall/
//       detail/aaedpojejpghjkedenggihopfhfijcko
const char kTestGetVolumeListKioskApp[] = "aaedpojejpghjkedenggihopfhfijcko";

// Testing apps for testing kiosk multi-app feature.
const char kTestPrimaryKioskApp[] = "ceobkcclegcliomogfoeoheahogoecgl";
const char kTestSecondaryApp1[] = "ihplaomghjbeafnpnjkhppmfpnmdihgd";
const char kTestSecondaryApp2[] = "fiehokkcgaojmbhfhlpiheggjhaedjoc";
const char kTestSecondaryApp3[] = "aabnpdpieclcikafhdkkpldcaodmfoai";
const char kTestSecondaryExtension[] = "imlgadjgphbjkaceoiapiephhgeofhic";
const char kTestSharedModulePrimaryApp[] = "ofmeihgcmabfalhhgooajcijiaoekhkg";
const char kTestSecondaryApp[] = "bbmaiojbgkkmfaglfhaplfomobgojhke";
const char kTestSharedModuleId[] = "biebhpdepndljbnkadldcbjkiedldnmn";
const char kTestSecondaryExt[] = "kcoobopfcjmbfeppibolpaolbgbmkcjd";

// Fake usb stick mount path.
const char kFakeUsbMountPathUpdatePass[] =
    "chromeos/app_mode/external_update/update_pass";
const char kFakeUsbMountPathNoManifest[] =
    "chromeos/app_mode/external_update/no_manifest";
const char kFakeUsbMountPathBadManifest[] =
    "chromeos/app_mode/external_update/bad_manifest";
const char kFakeUsbMountPathLowerAppVersion[] =
    "chromeos/app_mode/external_update/lower_app_version";
const char kFakeUsbMountPathLowerCrxVersion[] =
    "chromeos/app_mode/external_update/lower_crx_version";
const char kFakeUsbMountPathBadCrx[] =
    "chromeos/app_mode/external_update/bad_crx";

// Timeout while waiting for network connectivity during tests.
const int kTestNetworkTimeoutSeconds = 1;

// Email of owner account for test.
const char kTestOwnerEmail[] = "owner@example.com";

const char kTestEnterpriseAccountId[] = "enterprise-kiosk-app@localhost";
const char kTestEnterpriseServiceAccountId[] = "service_account@example.com";
const char kTestRefreshToken[] = "fake-refresh-token";
const char kTestUserinfoToken[] = "fake-userinfo-token";
const char kTestLoginToken[] = "fake-login-token";
const char kTestAccessToken[] = "fake-access-token";
const char kTestClientId[] = "fake-client-id";
const char kTestAppScope[] =
    "https://www.googleapis.com/auth/userinfo.profile";

// Test JS API.
const char kLaunchAppForTestNewAPI[] =
    "login.AccountPickerScreen.runAppForTesting";
const char kLaunchAppForTestOldAPI[] =
    "login.AppsMenuButton.runAppForTesting";
const char kCheckDiagnosticModeNewAPI[] =
    "$('oobe').confirmDiagnosticMode_";
const char kCheckDiagnosticModeOldAPI[] =
    "$('show-apps-button').confirmDiagnosticMode_";

// Helper function for GetConsumerKioskAutoLaunchStatusCallback.
void ConsumerKioskAutoLaunchStatusCheck(
    KioskAppManager::ConsumerKioskAutoLaunchStatus* out_status,
    const base::Closure& runner_quit_task,
    KioskAppManager::ConsumerKioskAutoLaunchStatus in_status) {
  LOG(INFO) << "KioskAppManager::ConsumerKioskModeStatus = " << in_status;
  *out_status = in_status;
  runner_quit_task.Run();
}

// Helper KioskAppManager::EnableKioskModeCallback implementation.
void ConsumerKioskModeAutoStartLockCheck(
    bool* out_locked,
    const base::Closure& runner_quit_task,
    bool in_locked) {
  LOG(INFO) << "kiosk locked  = " << in_locked;
  *out_locked = in_locked;
  runner_quit_task.Run();
}

// Helper function for WaitForNetworkTimeOut.
void OnNetworkWaitTimedOut(const base::Closure& runner_quit_task) {
  runner_quit_task.Run();
}

// Helper function for LockFileThread.
void LockAndUnlock(scoped_ptr<base::Lock> lock) {
  lock->Acquire();
  lock->Release();
}

bool IsAppInstalled(const std::string& app_id) {
  Profile* app_profile = ProfileManager::GetPrimaryUserProfile();
  DCHECK(app_profile);
  const extensions::Extension* app =
      extensions::ExtensionSystem::Get(app_profile)
          ->extension_service()
          ->GetInstalledExtension(app_id);
  return app != nullptr;
}

extensions::Manifest::Type GetAppType(const std::string& app_id) {
  Profile* app_profile = ProfileManager::GetPrimaryUserProfile();
  DCHECK(app_profile);
  const extensions::Extension* app =
      extensions::ExtensionSystem::Get(app_profile)
          ->extension_service()
          ->GetInstalledExtension(app_id);
  DCHECK(app);
  return app->GetType();
}

// Helper functions for CanConfigureNetwork mock.
class ScopedCanConfigureNetwork {
 public:
  ScopedCanConfigureNetwork(bool can_configure, bool needs_owner_auth)
      : can_configure_(can_configure),
        needs_owner_auth_(needs_owner_auth),
        can_configure_network_callback_(
            base::Bind(&ScopedCanConfigureNetwork::CanConfigureNetwork,
                       base::Unretained(this))),
        needs_owner_auth_callback_(base::Bind(
            &ScopedCanConfigureNetwork::NeedsOwnerAuthToConfigureNetwork,
            base::Unretained(this))) {
    AppLaunchController::SetCanConfigureNetworkCallbackForTesting(
        &can_configure_network_callback_);
    AppLaunchController::SetNeedOwnerAuthToConfigureNetworkCallbackForTesting(
        &needs_owner_auth_callback_);
  }
  ~ScopedCanConfigureNetwork() {
    AppLaunchController::SetCanConfigureNetworkCallbackForTesting(NULL);
    AppLaunchController::SetNeedOwnerAuthToConfigureNetworkCallbackForTesting(
        NULL);
  }

  bool CanConfigureNetwork() {
    return can_configure_;
  }

  bool NeedsOwnerAuthToConfigureNetwork() {
    return needs_owner_auth_;
  }

 private:
  bool can_configure_;
  bool needs_owner_auth_;
  AppLaunchController::ReturnBoolCallback can_configure_network_callback_;
  AppLaunchController::ReturnBoolCallback needs_owner_auth_callback_;
  DISALLOW_COPY_AND_ASSIGN(ScopedCanConfigureNetwork);
};

// Helper class to wait until a js condition becomes true.
class JsConditionWaiter {
 public:
  JsConditionWaiter(content::WebContents* web_contents,
                    const std::string& js)
      : web_contents_(web_contents),
        js_(js) {
  }

  void Wait() {
    if (CheckJs())
      return;

    base::RepeatingTimer check_timer;
    check_timer.Start(
        FROM_HERE,
        base::TimeDelta::FromMilliseconds(10),
        this,
        &JsConditionWaiter::OnTimer);

    runner_ = new content::MessageLoopRunner;
    runner_->Run();
  }

 private:
  bool CheckJs() {
    bool result;
    CHECK(content::ExecuteScriptAndExtractBool(
        web_contents_,
        "window.domAutomationController.send(!!(" + js_ + "));",
        &result));
    return result;
  }

  void OnTimer() {
    DCHECK(runner_.get());
    if (CheckJs())
      runner_->Quit();
  }

  content::WebContents* web_contents_;
  const std::string js_;
  scoped_refptr<content::MessageLoopRunner> runner_;

  DISALLOW_COPY_AND_ASSIGN(JsConditionWaiter);
};

class KioskFakeDiskMountManager : public file_manager::FakeDiskMountManager {
 public:
  KioskFakeDiskMountManager() {}

  ~KioskFakeDiskMountManager() override {}

  void set_usb_mount_path(const std::string& usb_mount_path) {
    usb_mount_path_ = usb_mount_path;
  }

  void MountUsbStick() {
    DCHECK(!usb_mount_path_.empty());
    MountPath(usb_mount_path_, "", "", chromeos::MOUNT_TYPE_DEVICE);
  }

  void UnMountUsbStick() {
    DCHECK(!usb_mount_path_.empty());
    UnmountPath(usb_mount_path_,
                UNMOUNT_OPTIONS_NONE,
                disks::DiskMountManager::UnmountPathCallback());
  }

 private:
  std::string usb_mount_path_;

  DISALLOW_COPY_AND_ASSIGN(KioskFakeDiskMountManager);
};

class AppDataLoadWaiter : public KioskAppManagerObserver {
 public:
  AppDataLoadWaiter(KioskAppManager* manager,
                    const std::string& app_id,
                    const std::string& version)
      : runner_(NULL),
        manager_(manager),
        wait_type_(WAIT_FOR_CRX_CACHE),
        loaded_(false),
        quit_(false),
        app_id_(app_id),
        version_(version) {
    manager_->AddObserver(this);
  }

  ~AppDataLoadWaiter() override { manager_->RemoveObserver(this); }

  void Wait() {
    wait_type_ = WAIT_FOR_CRX_CACHE;
    if (quit_)
      return;
    runner_ = new content::MessageLoopRunner;
    runner_->Run();
  }

  void WaitForAppData() {
    wait_type_ = WAIT_FOR_APP_DATA;
    if (quit_ || IsAppDataLoaded())
      return;
    runner_ = new content::MessageLoopRunner;
    runner_->Run();
  }

  bool loaded() const { return loaded_; }

 private:
  enum WaitType {
    WAIT_FOR_CRX_CACHE,
    WAIT_FOR_APP_DATA,
  };

  // KioskAppManagerObserver overrides:
  void OnKioskAppDataChanged(const std::string& app_id) override {
    if (wait_type_ != WAIT_FOR_APP_DATA ||
        app_id != app_id_ ||
        !IsAppDataLoaded()) {
      return;
    }

    loaded_ = true;
    quit_ = true;
    if (runner_.get())
      runner_->Quit();
  }

  void OnKioskAppDataLoadFailure(const std::string& app_id) override {
    if (wait_type_ != WAIT_FOR_APP_DATA || app_id != app_id_)
      return;

    loaded_ = false;
    quit_ = true;
    if (runner_.get())
      runner_->Quit();
  }

  void OnKioskExtensionLoadedInCache(const std::string& app_id) override {
    if (wait_type_ != WAIT_FOR_CRX_CACHE)
      return;

    std::string cached_version;
    base::FilePath file_path;
    if (!manager_->GetCachedCrx(app_id_, &file_path, &cached_version))
      return;
    if (version_ != cached_version)
      return;
    loaded_ = true;
    quit_ = true;
    if (runner_.get())
      runner_->Quit();
  }

  void OnKioskExtensionDownloadFailed(const std::string& app_id) override {
    if (wait_type_ != WAIT_FOR_CRX_CACHE)
      return;

    loaded_ = false;
    quit_ = true;
    if (runner_.get())
      runner_->Quit();
  }

  bool IsAppDataLoaded() {
    KioskAppManager::App app;
    return manager_->GetApp(app_id_, &app) && !app.is_loading;
  }

  scoped_refptr<content::MessageLoopRunner> runner_;
  KioskAppManager* manager_;
  WaitType wait_type_;
  bool loaded_;
  bool quit_;
  std::string app_id_;
  std::string version_;

  DISALLOW_COPY_AND_ASSIGN(AppDataLoadWaiter);
};

}  // namespace

class KioskTest : public OobeBaseTest {
 public:
  KioskTest()
      : settings_helper_(false),
        use_consumer_kiosk_mode_(true),
        fake_cws_(new FakeCWS) {
    set_exit_when_last_browser_closes(false);
  }

  ~KioskTest() override {}

 protected:
  void SetUp() override {
    test_app_id_ = kTestKioskApp;
    set_test_app_version("1.0.0");
    set_test_crx_file(test_app_id() + ".crx");
    needs_background_networking_ = true;
    mock_user_manager_.reset(new MockUserManager);
    ProfileHelper::SetAlwaysReturnPrimaryUserForTesting(true);
    AppLaunchController::SkipSplashWaitForTesting();
    AppLaunchController::SetNetworkWaitForTesting(kTestNetworkTimeoutSeconds);

    OobeBaseTest::SetUp();
  }

  void TearDown() override {
    ProfileHelper::SetAlwaysReturnPrimaryUserForTesting(false);
    OobeBaseTest::TearDown();
  }

  void SetUpOnMainThread() override {
    OobeBaseTest::SetUpOnMainThread();
    // Needed to avoid showing Gaia screen instead of owner signin for
    // consumer network down test cases.
    StartupUtils::MarkDeviceRegistered(base::Closure());
    settings_helper_.ReplaceProvider(kAccountsPrefDeviceLocalAccounts);
    owner_settings_service_ = settings_helper_.CreateOwnerSettingsService(
        ProfileManager::GetPrimaryUserProfile());
  }

  void TearDownOnMainThread() override {
    settings_helper_.RestoreProvider();
    AppLaunchController::SetNetworkTimeoutCallbackForTesting(NULL);
    AppLaunchSigninScreen::SetUserManagerForTesting(NULL);

    OobeBaseTest::TearDownOnMainThread();

    // Clean up while main thread still runs.
    // See http://crbug.com/176659.
    KioskAppManager::Get()->CleanUp();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    OobeBaseTest::SetUpCommandLine(command_line);
    fake_cws_->Init(embedded_test_server());
  }

  void LaunchApp(const std::string& app_id, bool diagnostic_mode) {
    bool new_kiosk_ui = KioskAppMenuHandler::EnableNewKioskUI();
    GetLoginUI()->CallJavascriptFunction(new_kiosk_ui ?
        kLaunchAppForTestNewAPI : kLaunchAppForTestOldAPI,
        base::StringValue(app_id),
        base::FundamentalValue(diagnostic_mode));
  }

  void ReloadKioskApps() {
    SetupTestAppUpdateCheck();

    // Remove then add to ensure NOTIFICATION_KIOSK_APPS_LOADED fires.
    KioskAppManager::Get()->RemoveApp(test_app_id_,
                                      owner_settings_service_.get());
    KioskAppManager::Get()->AddApp(test_app_id_, owner_settings_service_.get());
  }

  void FireKioskAppSettingsChanged() {
    KioskAppManager::Get()->UpdateAppData();
  }

  void SetupTestAppUpdateCheck() {
    if (!test_app_version().empty()) {
      fake_cws_->SetUpdateCrx(
          test_app_id(), test_crx_file(), test_app_version());
    }
  }

  void ReloadAutolaunchKioskApps() {
    SetupTestAppUpdateCheck();

    KioskAppManager::Get()->AddApp(test_app_id_, owner_settings_service_.get());
    KioskAppManager::Get()->SetAutoLaunchApp(test_app_id_,
                                             owner_settings_service_.get());
  }

  void StartUIForAppLaunch() {
    if (use_consumer_kiosk_mode_)
      EnableConsumerKioskMode();

    // Start UI
    chromeos::WizardController::SkipPostLoginScreensForTesting();
    chromeos::WizardController* wizard_controller =
        chromeos::WizardController::default_controller();
    if (wizard_controller) {
      wizard_controller->SkipToLoginForTesting(LoginScreenContext());
      OobeScreenWaiter(OobeDisplay::SCREEN_GAIA_SIGNIN).Wait();
    } else {
      // No wizard and running with an existing profile and it should land
      // on account picker when new kiosk UI is enabled. Otherwise, just
      // wait for the login signal from Gaia.
      if (KioskAppMenuHandler::EnableNewKioskUI())
        OobeScreenWaiter(OobeDisplay::SCREEN_ACCOUNT_PICKER).Wait();
      else
        OobeScreenWaiter(OobeDisplay::SCREEN_GAIA_SIGNIN).Wait();
    }
  }

  void PrepareAppLaunch() {
    // Start UI
    StartUIForAppLaunch();

    // Wait for the Kiosk App configuration to reload.
    content::WindowedNotificationObserver apps_loaded_signal(
        chrome::NOTIFICATION_KIOSK_APPS_LOADED,
        content::NotificationService::AllSources());
    ReloadKioskApps();
    apps_loaded_signal.Wait();
  }

  void StartAppLaunchFromLoginScreen(const base::Closure& network_setup_cb) {
    PrepareAppLaunch();

    if (!network_setup_cb.is_null())
      network_setup_cb.Run();

    LaunchApp(test_app_id(), false);
  }

  const extensions::Extension* GetInstalledApp() {
    Profile* app_profile = ProfileManager::GetPrimaryUserProfile();
    return extensions::ExtensionSystem::Get(app_profile)->
        extension_service()->GetInstalledExtension(test_app_id_);
  }

  const Version& GetInstalledAppVersion() {
    return *GetInstalledApp()->version();
  }

  extensions::Manifest::Location GetInstalledAppLocation() {
    return GetInstalledApp()->location();
  }

  void WaitForAppLaunchWithOptions(bool check_launch_data, bool terminate_app) {
    ExtensionTestMessageListener
        launch_data_check_listener("launchData.isKioskSession = true", false);

    // Wait for the Kiosk App to launch.
    content::WindowedNotificationObserver(
        chrome::NOTIFICATION_KIOSK_APP_LAUNCHED,
        content::NotificationService::AllSources()).Wait();

    // Default profile switches to app profile after app is launched.
    Profile* app_profile = ProfileManager::GetPrimaryUserProfile();
    ASSERT_TRUE(app_profile);

    // Check ChromeOS preference is initialized.
    EXPECT_TRUE(
        static_cast<ProfileImpl*>(app_profile)->chromeos_preferences_);

    // Check installer status.
    EXPECT_EQ(chromeos::KioskAppLaunchError::NONE,
              chromeos::KioskAppLaunchError::Get());

    // Check if the kiosk webapp is really installed for the default profile.
    const extensions::Extension* app =
        extensions::ExtensionSystem::Get(app_profile)->
        extension_service()->GetInstalledExtension(test_app_id_);
    EXPECT_TRUE(app);

    // App should appear with its window.
    extensions::AppWindowRegistry* app_window_registry =
        extensions::AppWindowRegistry::Get(app_profile);
    extensions::AppWindow* window =
        AppWindowWaiter(app_window_registry, test_app_id_).Wait();
    EXPECT_TRUE(window);

    // Login screen should be gone or fading out.
    chromeos::LoginDisplayHost* login_display_host =
        chromeos::LoginDisplayHostImpl::default_host();
    EXPECT_TRUE(
        login_display_host == NULL ||
        login_display_host->GetNativeWindow()->layer()->GetTargetOpacity() ==
            0.0f);

    // Terminate the app.
    if (terminate_app)
      window->GetBaseWindow()->Close();

    // Wait until the app terminates if it is still running.
    if (!app_window_registry->GetAppWindowsForApp(test_app_id_).empty())
      content::RunMessageLoop();

    // Check that the app had been informed that it is running in a kiosk
    // session.
    if (check_launch_data)
      EXPECT_TRUE(launch_data_check_listener.was_satisfied());
  }

  void WaitForAppLaunchSuccess() {
    WaitForAppLaunchWithOptions(true /* check_launch_data */,
                                true /* terminate_app */);
  }

  void WaitForAppLaunchNetworkTimeout() {
    if (GetAppLaunchController()->network_wait_timedout())
      return;

    scoped_refptr<content::MessageLoopRunner> runner =
        new content::MessageLoopRunner;

    base::Closure callback = base::Bind(
        &OnNetworkWaitTimedOut, runner->QuitClosure());
    AppLaunchController::SetNetworkTimeoutCallbackForTesting(&callback);

    runner->Run();

    CHECK(GetAppLaunchController()->network_wait_timedout());
    AppLaunchController::SetNetworkTimeoutCallbackForTesting(NULL);
  }

  void EnableConsumerKioskMode() {
    scoped_ptr<bool> locked(new bool(false));
    scoped_refptr<content::MessageLoopRunner> runner =
        new content::MessageLoopRunner;
    KioskAppManager::Get()->EnableConsumerKioskAutoLaunch(
        base::Bind(&ConsumerKioskModeAutoStartLockCheck,
                   locked.get(),
                   runner->QuitClosure()));
    runner->Run();
    EXPECT_TRUE(*locked.get());
  }

  KioskAppManager::ConsumerKioskAutoLaunchStatus
  GetConsumerKioskModeStatus() {
    KioskAppManager::ConsumerKioskAutoLaunchStatus status =
        static_cast<KioskAppManager::ConsumerKioskAutoLaunchStatus>(-1);
    scoped_refptr<content::MessageLoopRunner> runner =
        new content::MessageLoopRunner;
    KioskAppManager::Get()->GetConsumerKioskAutoLaunchStatus(
        base::Bind(&ConsumerKioskAutoLaunchStatusCheck,
                   &status,
                   runner->QuitClosure()));
    runner->Run();
    CHECK_NE(status,
             static_cast<KioskAppManager::ConsumerKioskAutoLaunchStatus>(-1));
    return status;
  }

  void RunAppLaunchNetworkDownTest() {
    mock_user_manager()->SetActiveUser(kTestOwnerEmail);
    AppLaunchSigninScreen::SetUserManagerForTesting(mock_user_manager());

    // Mock network could be configured with owner's password.
    ScopedCanConfigureNetwork can_configure_network(true, true);

    // Start app launch and wait for network connectivity timeout.
    StartAppLaunchFromLoginScreen(SimulateNetworkOfflineClosure());
    OobeScreenWaiter splash_waiter(OobeDisplay::SCREEN_APP_LAUNCH_SPLASH);
    splash_waiter.Wait();
    WaitForAppLaunchNetworkTimeout();

    // Configure network link should be visible.
    JsExpect("$('splash-config-network').hidden == false");

    // Set up fake user manager with an owner for the test.
    static_cast<LoginDisplayHostImpl*>(LoginDisplayHostImpl::default_host())
        ->GetOobeUI()->ShowOobeUI(false);

    // Configure network should bring up lock screen for owner.
    OobeScreenWaiter lock_screen_waiter(OobeDisplay::SCREEN_ACCOUNT_PICKER);
    static_cast<AppLaunchSplashScreenActor::Delegate*>(GetAppLaunchController())
        ->OnConfigureNetwork();
    lock_screen_waiter.Wait();

    // There should be only one owner pod on this screen.
    JsExpect("$('pod-row').alwaysFocusSinglePod");

    // A network error screen should be shown after authenticating.
    OobeScreenWaiter error_screen_waiter(OobeDisplay::SCREEN_ERROR_MESSAGE);
    static_cast<AppLaunchSigninScreen::Delegate*>(GetAppLaunchController())
        ->OnOwnerSigninSuccess();
    error_screen_waiter.Wait();

    ASSERT_TRUE(GetAppLaunchController()->showing_network_dialog());

    SimulateNetworkOnline();
    WaitForAppLaunchSuccess();
  }

  AppLaunchController* GetAppLaunchController() {
    return chromeos::LoginDisplayHostImpl::default_host()
        ->GetAppLaunchController();
  }

  // Returns a lock that is holding a task on the FILE thread. Any tasks posted
  // to the FILE thread after this call will be blocked until the returned
  // lock is released.
  // This can be used to prevent app installation from completing until some
  // other conditions are checked and triggered. For example, this can be used
  // to trigger the network screen during app launch without racing with the
  // app launching process itself.
  scoped_ptr<base::AutoLock> LockFileThread() {
    scoped_ptr<base::Lock> lock(new base::Lock);
    scoped_ptr<base::AutoLock> auto_lock(new base::AutoLock(*lock));
    content::BrowserThread::PostTask(
        content::BrowserThread::FILE, FROM_HERE,
        base::Bind(&LockAndUnlock, base::Passed(&lock)));
    return auto_lock.Pass();
  }

  MockUserManager* mock_user_manager() { return mock_user_manager_.get(); }

  void set_test_app_id(const std::string& test_app_id) {
    test_app_id_ = test_app_id;
  }
  const std::string& test_app_id() const { return test_app_id_; }
  void set_test_app_version(const std::string& version) {
    test_app_version_ = version;
  }
  const std::string& test_app_version() const { return test_app_version_; }
  void set_test_crx_file(const std::string& filename) {
    test_crx_file_ = filename;
  }
  const std::string& test_crx_file() const { return test_crx_file_; }
  FakeCWS* fake_cws() { return fake_cws_.get(); }

  void set_use_consumer_kiosk_mode(bool use) {
    use_consumer_kiosk_mode_ = use;
  }

  ScopedCrosSettingsTestHelper settings_helper_;
  scoped_ptr<FakeOwnerSettingsService> owner_settings_service_;

 private:
  bool use_consumer_kiosk_mode_;
  std::string test_app_id_;
  std::string test_app_version_;
  std::string test_crx_file_;
  scoped_ptr<FakeCWS> fake_cws_;
  scoped_ptr<MockUserManager> mock_user_manager_;

  DISALLOW_COPY_AND_ASSIGN(KioskTest);
};

IN_PROC_BROWSER_TEST_F(KioskTest, InstallAndLaunchApp) {
  StartAppLaunchFromLoginScreen(SimulateNetworkOnlineClosure());
  WaitForAppLaunchSuccess();
  KioskAppManager::App app;
  ASSERT_TRUE(KioskAppManager::Get()->GetApp(test_app_id(), &app));
  EXPECT_FALSE(app.was_auto_launched_with_zero_delay);
  EXPECT_EQ(extensions::Manifest::EXTERNAL_PREF, GetInstalledAppLocation());
}

IN_PROC_BROWSER_TEST_F(KioskTest, ZoomSupport) {
  ExtensionTestMessageListener
      app_window_loaded_listener("appWindowLoaded", false);
  StartAppLaunchFromLoginScreen(SimulateNetworkOnlineClosure());
  app_window_loaded_listener.WaitUntilSatisfied();

  Profile* app_profile = ProfileManager::GetPrimaryUserProfile();
  ASSERT_TRUE(app_profile);

  extensions::AppWindowRegistry* app_window_registry =
      extensions::AppWindowRegistry::Get(app_profile);
  extensions::AppWindow* window =
      AppWindowWaiter(app_window_registry, test_app_id()).Wait();
  ASSERT_TRUE(window);

  // Gets the original width of the app window.
  int original_width;
  EXPECT_TRUE(content::ExecuteScriptAndExtractInt(
      window->web_contents(),
      "window.domAutomationController.setAutomationId(0);"
      "window.domAutomationController.send(window.innerWidth);",
      &original_width));

  native_app_window::NativeAppWindowViews* native_app_window_views =
      static_cast<native_app_window::NativeAppWindowViews*>(
          window->GetBaseWindow());
  ui::AcceleratorTarget* accelerator_target =
      static_cast<ui::AcceleratorTarget*>(native_app_window_views);

  // Zoom in. Text is bigger and content window width becomes smaller.
  accelerator_target->AcceleratorPressed(ui::Accelerator(
      ui::VKEY_ADD, ui::EF_CONTROL_DOWN));
  int width_zoomed_in;
  EXPECT_TRUE(content::ExecuteScriptAndExtractInt(
      window->web_contents(),
      "window.domAutomationController.setAutomationId(0);"
      "window.domAutomationController.send(window.innerWidth);",
      &width_zoomed_in));
  DCHECK_LT(width_zoomed_in, original_width);

  // Go back to normal. Window width is restored.
  accelerator_target->AcceleratorPressed(ui::Accelerator(
      ui::VKEY_0, ui::EF_CONTROL_DOWN));
  int width_zoom_normal;
  EXPECT_TRUE(content::ExecuteScriptAndExtractInt(
      window->web_contents(),
      "window.domAutomationController.setAutomationId(0);"
      "window.domAutomationController.send(window.innerWidth);",
      &width_zoom_normal));
  DCHECK_EQ(width_zoom_normal, original_width);

  // Zoom out. Text is smaller and content window width becomes larger.
  accelerator_target->AcceleratorPressed(ui::Accelerator(
      ui::VKEY_SUBTRACT, ui::EF_CONTROL_DOWN));
  int width_zoomed_out;
  EXPECT_TRUE(content::ExecuteScriptAndExtractInt(
      window->web_contents(),
      "window.domAutomationController.setAutomationId(0);"
      "window.domAutomationController.send(window.innerWidth);",
      &width_zoomed_out));
  DCHECK_GT(width_zoomed_out, original_width);

  // Terminate the app.
  window->GetBaseWindow()->Close();
  content::RunAllPendingInMessageLoop();
}

IN_PROC_BROWSER_TEST_F(KioskTest, NotSignedInWithGAIAAccount) {
  // Tests that the kiosk session is not considered to be logged in with a GAIA
  // account.
  StartAppLaunchFromLoginScreen(SimulateNetworkOnlineClosure());
  WaitForAppLaunchSuccess();
  EXPECT_EQ(extensions::Manifest::EXTERNAL_PREF, GetInstalledAppLocation());

  Profile* app_profile = ProfileManager::GetPrimaryUserProfile();
  ASSERT_TRUE(app_profile);
  EXPECT_FALSE(
      SigninManagerFactory::GetForProfile(app_profile)->IsAuthenticated());
}

IN_PROC_BROWSER_TEST_F(KioskTest, PRE_LaunchAppNetworkDown) {
  // Tests the network down case for the initial app download and launch.
  RunAppLaunchNetworkDownTest();
}

IN_PROC_BROWSER_TEST_F(KioskTest, LaunchAppNetworkDown) {
  // Tests the network down case for launching an existing app that is
  // installed in PRE_LaunchAppNetworkDown.
  RunAppLaunchNetworkDownTest();
}

IN_PROC_BROWSER_TEST_F(KioskTest, LaunchAppWithNetworkConfigAccelerator) {
  ScopedCanConfigureNetwork can_configure_network(true, false);

  // Block app loading until the network screen is shown.
  scoped_ptr<base::AutoLock> lock = LockFileThread();

  // Start app launch and wait for network connectivity timeout.
  StartAppLaunchFromLoginScreen(SimulateNetworkOnlineClosure());
  OobeScreenWaiter splash_waiter(OobeDisplay::SCREEN_APP_LAUNCH_SPLASH);
  splash_waiter.Wait();

  // A network error screen should be shown after authenticating.
  OobeScreenWaiter error_screen_waiter(OobeDisplay::SCREEN_ERROR_MESSAGE);
  // Simulate Ctrl+Alt+N accelerator.
  GetLoginUI()->CallJavascriptFunction(
      "cr.ui.Oobe.handleAccelerator",
      base::StringValue("app_launch_network_config"));
  error_screen_waiter.Wait();
  ASSERT_TRUE(GetAppLaunchController()->showing_network_dialog());

  // Continue button should be visible since we are online.
  JsExpect("$('continue-network-config-btn').hidden == false");

  // Click on [Continue] button.
  ASSERT_TRUE(content::ExecuteScript(
      GetLoginUI()->GetWebContents(),
      "(function() {"
      "var e = new Event('click');"
      "$('continue-network-config-btn').dispatchEvent(e);"
      "})();"));

  // Let app launching resume.
  lock.reset();

  WaitForAppLaunchSuccess();
}

IN_PROC_BROWSER_TEST_F(KioskTest, LaunchAppNetworkDownConfigureNotAllowed) {
  // Mock network could not be configured.
  ScopedCanConfigureNetwork can_configure_network(false, true);

  // Start app launch and wait for network connectivity timeout.
  StartAppLaunchFromLoginScreen(SimulateNetworkOfflineClosure());
  OobeScreenWaiter splash_waiter(OobeDisplay::SCREEN_APP_LAUNCH_SPLASH);
  splash_waiter.Wait();
  WaitForAppLaunchNetworkTimeout();

  // Configure network link should not be visible.
  JsExpect("$('splash-config-network').hidden == true");

  // Network becomes online and app launch is resumed.
  SimulateNetworkOnline();
  WaitForAppLaunchSuccess();
}

// http://crbug.com/543332
IN_PROC_BROWSER_TEST_F(KioskTest, DISABLED_LaunchAppNetworkPortal) {
  // Mock network could be configured without the owner password.
  ScopedCanConfigureNetwork can_configure_network(true, false);

  // Start app launch with network portal state.
  StartAppLaunchFromLoginScreen(SimulateNetworkPortalClosure());
  OobeScreenWaiter(OobeDisplay::SCREEN_APP_LAUNCH_SPLASH)
      .WaitNoAssertCurrentScreen();
  WaitForAppLaunchNetworkTimeout();

  // Network error should show up automatically since this test does not
  // require owner auth to configure network.
  OobeScreenWaiter(OobeDisplay::SCREEN_ERROR_MESSAGE).Wait();

  ASSERT_TRUE(GetAppLaunchController()->showing_network_dialog());
  SimulateNetworkOnline();
  WaitForAppLaunchSuccess();
}

IN_PROC_BROWSER_TEST_F(KioskTest, LaunchAppUserCancel) {
  // Make fake_cws_ return empty update response.
  set_test_app_version("");
  StartAppLaunchFromLoginScreen(SimulateNetworkOfflineClosure());
  OobeScreenWaiter splash_waiter(OobeDisplay::SCREEN_APP_LAUNCH_SPLASH);
  splash_waiter.Wait();

  settings_helper_.SetBoolean(
      kAccountsPrefDeviceLocalAccountAutoLoginBailoutEnabled, true);
  content::WindowedNotificationObserver signal(
      chrome::NOTIFICATION_APP_TERMINATING,
      content::NotificationService::AllSources());
  GetLoginUI()->CallJavascriptFunction("cr.ui.Oobe.handleAccelerator",
                                       base::StringValue("app_launch_bailout"));
  signal.Wait();
  EXPECT_EQ(chromeos::KioskAppLaunchError::USER_CANCEL,
            chromeos::KioskAppLaunchError::Get());
}

IN_PROC_BROWSER_TEST_F(KioskTest, LaunchInDiagnosticMode) {
  PrepareAppLaunch();
  SimulateNetworkOnline();

  LaunchApp(kTestKioskApp, true);

  content::WebContents* login_contents = GetLoginUI()->GetWebContents();

  bool new_kiosk_ui = KioskAppMenuHandler::EnableNewKioskUI();
  JsConditionWaiter(login_contents, new_kiosk_ui ?
      kCheckDiagnosticModeNewAPI : kCheckDiagnosticModeOldAPI).Wait();

  std::string diagnosticMode(new_kiosk_ui ?
      kCheckDiagnosticModeNewAPI : kCheckDiagnosticModeOldAPI);
  ASSERT_TRUE(content::ExecuteScript(
      login_contents,
      "(function() {"
         "var e = new Event('click');" +
         diagnosticMode + "."
             "okButton_.dispatchEvent(e);"
      "})();"));

  WaitForAppLaunchSuccess();
  EXPECT_EQ(extensions::Manifest::EXTERNAL_PREF, GetInstalledAppLocation());
}

IN_PROC_BROWSER_TEST_F(KioskTest, AutolaunchWarningCancel) {
  EnableConsumerKioskMode();

  chromeos::WizardController::SkipPostLoginScreensForTesting();
  chromeos::WizardController* wizard_controller =
      chromeos::WizardController::default_controller();
  CHECK(wizard_controller);

  // Start login screen after configuring auto launch app since the warning
  // is triggered when switching to login screen.
  wizard_controller->AdvanceToScreen(WizardController::kNetworkScreenName);
  ReloadAutolaunchKioskApps();
  EXPECT_FALSE(KioskAppManager::Get()->GetAutoLaunchApp().empty());
  EXPECT_FALSE(KioskAppManager::Get()->IsAutoLaunchEnabled());
  wizard_controller->SkipToLoginForTesting(LoginScreenContext());

  // Wait for the auto launch warning come up.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_AUTOLAUNCH_WARNING_VISIBLE,
      content::NotificationService::AllSources()).Wait();
  GetLoginUI()->CallJavascriptFunction(
      "login.AutolaunchScreen.confirmAutoLaunchForTesting",
      base::FundamentalValue(false));

  // Wait for the auto launch warning to go away.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_AUTOLAUNCH_WARNING_COMPLETED,
      content::NotificationService::AllSources()).Wait();

  EXPECT_FALSE(KioskAppManager::Get()->IsAutoLaunchEnabled());
}

IN_PROC_BROWSER_TEST_F(KioskTest, AutolaunchWarningConfirm) {
  EnableConsumerKioskMode();

  chromeos::WizardController::SkipPostLoginScreensForTesting();
  chromeos::WizardController* wizard_controller =
      chromeos::WizardController::default_controller();
  CHECK(wizard_controller);

  // Start login screen after configuring auto launch app since the warning
  // is triggered when switching to login screen.
  wizard_controller->AdvanceToScreen(WizardController::kNetworkScreenName);
  ReloadAutolaunchKioskApps();
  EXPECT_FALSE(KioskAppManager::Get()->GetAutoLaunchApp().empty());
  EXPECT_FALSE(KioskAppManager::Get()->IsAutoLaunchEnabled());
  wizard_controller->SkipToLoginForTesting(LoginScreenContext());

  // Wait for the auto launch warning come up.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_AUTOLAUNCH_WARNING_VISIBLE,
      content::NotificationService::AllSources()).Wait();
  GetLoginUI()->CallJavascriptFunction(
      "login.AutolaunchScreen.confirmAutoLaunchForTesting",
      base::FundamentalValue(true));

  // Wait for the auto launch warning to go away.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_AUTOLAUNCH_WARNING_COMPLETED,
      content::NotificationService::AllSources()).Wait();

  EXPECT_FALSE(KioskAppManager::Get()->GetAutoLaunchApp().empty());
  EXPECT_TRUE(KioskAppManager::Get()->IsAutoLaunchEnabled());

  WaitForAppLaunchSuccess();

  KioskAppManager::App app;
  ASSERT_TRUE(KioskAppManager::Get()->GetApp(test_app_id(), &app));
  EXPECT_TRUE(app.was_auto_launched_with_zero_delay);
  EXPECT_EQ(extensions::Manifest::EXTERNAL_PREF, GetInstalledAppLocation());
}

IN_PROC_BROWSER_TEST_F(KioskTest, KioskEnableCancel) {
  chromeos::WizardController::SkipPostLoginScreensForTesting();
  chromeos::WizardController* wizard_controller =
      chromeos::WizardController::default_controller();
  CHECK(wizard_controller);

  // Check Kiosk mode status.
  EXPECT_EQ(KioskAppManager::CONSUMER_KIOSK_AUTO_LAUNCH_CONFIGURABLE,
            GetConsumerKioskModeStatus());

  // Wait for the login UI to come up and switch to the kiosk_enable screen.
  wizard_controller->SkipToLoginForTesting(LoginScreenContext());
  OobeScreenWaiter(OobeDisplay::SCREEN_GAIA_SIGNIN).Wait();
  GetLoginUI()->CallJavascriptFunction("cr.ui.Oobe.handleAccelerator",
                                       base::StringValue("kiosk_enable"));

  // Wait for the kiosk_enable screen to show and cancel the screen.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_ENABLE_WARNING_VISIBLE,
      content::NotificationService::AllSources()).Wait();
  GetLoginUI()->CallJavascriptFunction(
      "login.KioskEnableScreen.enableKioskForTesting",
      base::FundamentalValue(false));

  // Wait for the kiosk_enable screen to disappear.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_ENABLE_WARNING_COMPLETED,
      content::NotificationService::AllSources()).Wait();

  // Check that the status still says configurable.
  EXPECT_EQ(KioskAppManager::CONSUMER_KIOSK_AUTO_LAUNCH_CONFIGURABLE,
            GetConsumerKioskModeStatus());
}

IN_PROC_BROWSER_TEST_F(KioskTest, KioskEnableConfirmed) {
  // Start UI, find menu entry for this app and launch it.
  chromeos::WizardController::SkipPostLoginScreensForTesting();
  chromeos::WizardController* wizard_controller =
      chromeos::WizardController::default_controller();
  CHECK(wizard_controller);

  // Check Kiosk mode status.
  EXPECT_EQ(KioskAppManager::CONSUMER_KIOSK_AUTO_LAUNCH_CONFIGURABLE,
            GetConsumerKioskModeStatus());

  // Wait for the login UI to come up and switch to the kiosk_enable screen.
  wizard_controller->SkipToLoginForTesting(LoginScreenContext());
  OobeScreenWaiter(OobeDisplay::SCREEN_GAIA_SIGNIN).Wait();
  GetLoginUI()->CallJavascriptFunction("cr.ui.Oobe.handleAccelerator",
                                       base::StringValue("kiosk_enable"));

  // Wait for the kiosk_enable screen to show and cancel the screen.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_ENABLE_WARNING_VISIBLE,
      content::NotificationService::AllSources()).Wait();
  GetLoginUI()->CallJavascriptFunction(
      "login.KioskEnableScreen.enableKioskForTesting",
      base::FundamentalValue(true));

  // Wait for the signal that indicates Kiosk Mode is enabled.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_ENABLED,
      content::NotificationService::AllSources()).Wait();
  EXPECT_EQ(KioskAppManager::CONSUMER_KIOSK_AUTO_LAUNCH_ENABLED,
            GetConsumerKioskModeStatus());
}

IN_PROC_BROWSER_TEST_F(KioskTest, KioskEnableAfter2ndSigninScreen) {
  chromeos::WizardController::SkipPostLoginScreensForTesting();
  chromeos::WizardController* wizard_controller =
      chromeos::WizardController::default_controller();
  CHECK(wizard_controller);

  // Check Kiosk mode status.
  EXPECT_EQ(KioskAppManager::CONSUMER_KIOSK_AUTO_LAUNCH_CONFIGURABLE,
            GetConsumerKioskModeStatus());

  // Wait for the login UI to come up and switch to the kiosk_enable screen.
  wizard_controller->SkipToLoginForTesting(LoginScreenContext());
  OobeScreenWaiter(OobeDisplay::SCREEN_GAIA_SIGNIN).Wait();
  GetLoginUI()->CallJavascriptFunction("cr.ui.Oobe.handleAccelerator",
                                       base::StringValue("kiosk_enable"));

  // Wait for the kiosk_enable screen to show and cancel the screen.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_ENABLE_WARNING_VISIBLE,
      content::NotificationService::AllSources()).Wait();
  GetLoginUI()->CallJavascriptFunction(
      "login.KioskEnableScreen.enableKioskForTesting",
      base::FundamentalValue(false));

  // Wait for the kiosk_enable screen to disappear.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_ENABLE_WARNING_COMPLETED,
      content::NotificationService::AllSources()).Wait();

  // Show signin screen again.
  chromeos::LoginDisplayHostImpl::default_host()->StartSignInScreen(
      LoginScreenContext());
  OobeScreenWaiter(OobeDisplay::SCREEN_GAIA_SIGNIN).Wait();

  // Show kiosk enable screen again.
  GetLoginUI()->CallJavascriptFunction("cr.ui.Oobe.handleAccelerator",
                                       base::StringValue("kiosk_enable"));

  // And it should show up.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_ENABLE_WARNING_VISIBLE,
      content::NotificationService::AllSources()).Wait();
}

IN_PROC_BROWSER_TEST_F(KioskTest, DoNotLaunchWhenUntrusted) {
  PrepareAppLaunch();
  SimulateNetworkOnline();

  // Make cros settings untrusted.
  settings_helper_.SetTrustedStatus(
      CrosSettingsProvider::PERMANENTLY_UNTRUSTED);

  // Check that the attempt to start a kiosk app fails with an error.
  LaunchApp(test_app_id(), false);
  bool ignored = false;
  EXPECT_TRUE(content::ExecuteScriptAndExtractBool(
      GetLoginUI()->GetWebContents(),
      "if (cr.ui.Oobe.getInstance().errorMessageWasShownForTesting_) {"
      "  window.domAutomationController.send(true);"
      "} else {"
      "  cr.ui.Oobe.showSignInError = function("
      "      loginAttempts, message, link, helpId) {"
      "    window.domAutomationController.send(true);"
      "  };"
      "}",
      &ignored));
}

// Verifies that a consumer device does not auto-launch kiosk mode when cros
// settings are untrusted.
IN_PROC_BROWSER_TEST_F(KioskTest, NoConsumerAutoLaunchWhenUntrusted) {
  EnableConsumerKioskMode();

  // Wait for and confirm the auto-launch warning.
  chromeos::WizardController::SkipPostLoginScreensForTesting();
  chromeos::WizardController* wizard_controller =
      chromeos::WizardController::default_controller();
  ASSERT_TRUE(wizard_controller);
  wizard_controller->AdvanceToScreen(WizardController::kNetworkScreenName);
  ReloadAutolaunchKioskApps();
  wizard_controller->SkipToLoginForTesting(LoginScreenContext());
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_AUTOLAUNCH_WARNING_VISIBLE,
      content::NotificationService::AllSources()).Wait();
  GetLoginUI()->CallJavascriptFunction(
      "login.AutolaunchScreen.confirmAutoLaunchForTesting",
      base::FundamentalValue(true));

  // Make cros settings untrusted.
  settings_helper_.SetTrustedStatus(
      CrosSettingsProvider::PERMANENTLY_UNTRUSTED);

  // Check that the attempt to auto-launch a kiosk app fails with an error.
  OobeScreenWaiter(OobeDisplay::SCREEN_ERROR_MESSAGE).Wait();
}

// Verifies available volumes for kiosk apps in kiosk session.
IN_PROC_BROWSER_TEST_F(KioskTest, GetVolumeList) {
  set_test_app_id(kTestGetVolumeListKioskApp);
  set_test_app_version("0.1");
  set_test_crx_file(test_app_id() + ".crx");

  extensions::ResultCatcher catcher;
  StartAppLaunchFromLoginScreen(SimulateNetworkOnlineClosure());
  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

// Verifies that an enterprise device does not auto-launch kiosk mode when cros
// settings are untrusted.
IN_PROC_BROWSER_TEST_F(KioskTest, NoEnterpriseAutoLaunchWhenUntrusted) {
  PrepareAppLaunch();
  SimulateNetworkOnline();

  // Make cros settings untrusted.
  settings_helper_.SetTrustedStatus(
      CrosSettingsProvider::PERMANENTLY_UNTRUSTED);

  // Trigger the code that handles auto-launch on enterprise devices. This would
  // normally be called from ShowLoginWizard(), which runs so early that it is
  // not possible to inject an auto-launch policy before it runs.
  LoginDisplayHost* login_display_host = LoginDisplayHostImpl::default_host();
  ASSERT_TRUE(login_display_host);
  login_display_host->StartAppLaunch(test_app_id(), false, true);

  // Check that no launch has started.
  EXPECT_FALSE(login_display_host->GetAppLaunchController());
}

class KioskUpdateTest : public KioskTest {
 public:
  KioskUpdateTest() {}
  ~KioskUpdateTest() override {}

  struct TestAppInfo {
    std::string id;
    std::string version;
    std::string crx_filename;
    extensions::Manifest::Type type;
    TestAppInfo() {}
    TestAppInfo(const std::string& id,
                const std::string& version,
                const std::string& crx_filename,
                extensions::Manifest::Type type)
        : id(id), version(version), crx_filename(crx_filename), type(type) {}
    ~TestAppInfo() {}
  };

 protected:
  void SetUp() override {
    fake_disk_mount_manager_ = new KioskFakeDiskMountManager();
    disks::DiskMountManager::InitializeForTesting(fake_disk_mount_manager_);

    KioskTest::SetUp();
  }

  void TearDown() override {
    disks::DiskMountManager::Shutdown();

    KioskTest::TearDown();
  }

  void SetUpOnMainThread() override {
    // For update tests, we cache the app in the PRE part, and then we load it
    // in the test, so we need to both store the apps list on teardown (so that
    // the app manager would accept existing files in its extension cache on the
    // next startup) and copy the list to our stub settings provider as well.
    settings_helper_.CopyStoredValue(kAccountsPrefDeviceLocalAccounts);

    CreateAndInitializeLocalCache();

    KioskTest::SetUpOnMainThread();
  }

  void TearDownOnMainThread() override {
    settings_helper_.StoreCachedDeviceSetting(kAccountsPrefDeviceLocalAccounts);
    KioskTest::TearDownOnMainThread();
  }

  void PreCacheApp(const std::string& app_id,
                   const std::string& version,
                   const std::string& crx_file) {
    set_test_app_id(app_id);
    set_test_app_version(version);
    set_test_crx_file(crx_file);

    KioskAppManager* manager = KioskAppManager::Get();
    AppDataLoadWaiter waiter(manager, app_id, version);
    ReloadKioskApps();
    waiter.Wait();
    EXPECT_TRUE(waiter.loaded());
    std::string cached_version;
    base::FilePath file_path;
    EXPECT_TRUE(manager->GetCachedCrx(app_id, &file_path, &cached_version));
    EXPECT_EQ(version, cached_version);
  }

  void UpdateExternalCache(const std::string& version,
                           const std::string& crx_file) {
    set_test_app_version(version);
    set_test_crx_file(crx_file);
    SetupTestAppUpdateCheck();

    KioskAppManager* manager = KioskAppManager::Get();
    AppDataLoadWaiter waiter(manager, test_app_id(), version);
    KioskAppManager::Get()->UpdateExternalCache();
    waiter.Wait();
    EXPECT_TRUE(waiter.loaded());
    std::string cached_version;
    base::FilePath file_path;
    EXPECT_TRUE(
        manager->GetCachedCrx(test_app_id(), &file_path, &cached_version));
    EXPECT_EQ(version, cached_version);
  }

  void SetupFakeDiskMountManagerMountPath(const std::string& mount_path) {
    base::FilePath test_data_dir;
    PathService::Get(chrome::DIR_TEST_DATA, &test_data_dir);
    test_data_dir = test_data_dir.AppendASCII(mount_path);
    fake_disk_mount_manager_->set_usb_mount_path(test_data_dir.value());
  }

  void SimulateUpdateAppFromUsbStick(const std::string& usb_mount_path,
                                     bool* app_update_notified,
                                     bool* update_success) {
    SetupFakeDiskMountManagerMountPath(usb_mount_path);
    KioskAppExternalUpdateWaiter waiter(KioskAppManager::Get(), test_app_id());
    fake_disk_mount_manager_->MountUsbStick();
    waiter.Wait();
    fake_disk_mount_manager_->UnMountUsbStick();
    *update_success = waiter.update_success();
    *app_update_notified = waiter.app_update_notified();
  }

  void PreCacheAndLaunchApp(const std::string& app_id,
                            const std::string& version,
                            const std::string& crx_file) {
    set_test_app_id(app_id);
    set_test_app_version(version);
    set_test_crx_file(crx_file);
    PrepareAppLaunch();
    SimulateNetworkOnline();
    LaunchApp(test_app_id(), false);
    WaitForAppLaunchSuccess();
    EXPECT_EQ(version, GetInstalledAppVersion().GetString());
  }

  void LaunchKioskWithSecondaryApps(
      const TestAppInfo& primary_app,
      const std::vector<TestAppInfo>& secondary_apps) {
    // Pre-cache the primary app.
    PreCacheApp(primary_app.id, primary_app.version, primary_app.crx_filename);

    set_test_app_id(primary_app.id);
    fake_cws()->SetNoUpdate(primary_app.id);
    for (const auto& app : secondary_apps) {
      fake_cws()->SetUpdateCrx(app.id, app.crx_filename, app.version);
    }

    // Launch the primary app.
    StartUIForAppLaunch();
    SimulateNetworkOnline();
    LaunchApp(test_app_id(), false);
    WaitForAppLaunchWithOptions(false, true);

    // Verify the primary app and the secondary apps are all installed.
    EXPECT_EQ(primary_app.version, GetInstalledAppVersion().GetString());
    for (const auto& app : secondary_apps) {
      EXPECT_TRUE(IsAppInstalled(app.id));
      EXPECT_EQ(GetAppType(app.id), app.type);
    }
  }

  void LaunchTestKioskAppWithTwoSecondaryApps() {
    TestAppInfo primary_app(kTestPrimaryKioskApp, "1.0.0",
                            std::string(kTestPrimaryKioskApp) + "-1.0.0.crx",
                            extensions::Manifest::TYPE_PLATFORM_APP);

    std::vector<TestAppInfo> secondary_apps;
    TestAppInfo secondary_app_1(kTestSecondaryApp1, "1.0.0",
                                std::string(kTestSecondaryApp1) + "-1.0.0.crx",
                                extensions::Manifest::TYPE_PLATFORM_APP);
    secondary_apps.push_back(secondary_app_1);
    TestAppInfo secondary_app_2(kTestSecondaryApp2, "1.0.0",
                                std::string(kTestSecondaryApp2) + "-1.0.0.crx",
                                extensions::Manifest::TYPE_PLATFORM_APP);
    secondary_apps.push_back(secondary_app_2);

    LaunchKioskWithSecondaryApps(primary_app, secondary_apps);
  }

  void LaunchTestKioskAppWithSeconadayExtension() {
    TestAppInfo primary_app(kTestPrimaryKioskApp, "24.0.0",
                            std::string(kTestPrimaryKioskApp) + "-24.0.0.crx",
                            extensions::Manifest::TYPE_PLATFORM_APP);

    std::vector<TestAppInfo> secondary_apps;
    TestAppInfo secondary_extension(
        kTestSecondaryExtension, "1.0.0",
        std::string(kTestSecondaryExtension) + "-1.0.0.crx",
        extensions::Manifest::TYPE_EXTENSION);
    secondary_apps.push_back(secondary_extension);

    LaunchKioskWithSecondaryApps(primary_app, secondary_apps);
  }

  void LaunchAppWithSharedModuleAndSecondaryApp() {
    TestAppInfo primary_app(
        kTestSharedModulePrimaryApp, "1.0.0",
        std::string(kTestSharedModulePrimaryApp) + "-1.0.0.crx",
        extensions::Manifest::TYPE_PLATFORM_APP);

    std::vector<TestAppInfo> secondary_apps;
    TestAppInfo secondary_app(kTestSecondaryApp, "1.0.0",
                              std::string(kTestSecondaryApp) + "-1.0.0.crx",
                              extensions::Manifest::TYPE_PLATFORM_APP);
    secondary_apps.push_back(secondary_app);
    // Setting up FakeCWS for shared module is the same for shared module as
    // for kiosk secondary apps.
    TestAppInfo shared_module(kTestSharedModuleId, "1.0.0",
                              std::string(kTestSharedModuleId) + "-1.0.0.crx",
                              extensions::Manifest::TYPE_SHARED_MODULE);
    secondary_apps.push_back(shared_module);

    LaunchKioskWithSecondaryApps(primary_app, secondary_apps);
    EXPECT_TRUE(IsAppInstalled(kTestSharedModuleId));
  }

 private:
  class KioskAppExternalUpdateWaiter : public KioskAppManagerObserver {
   public:
    KioskAppExternalUpdateWaiter(KioskAppManager* manager,
                                 const std::string& app_id)
        : runner_(NULL),
          manager_(manager),
          app_id_(app_id),
          quit_(false),
          update_success_(false),
          app_update_notified_(false) {
      manager_->AddObserver(this);
    }

    ~KioskAppExternalUpdateWaiter() override { manager_->RemoveObserver(this); }

    void Wait() {
      if (quit_)
        return;
      runner_ = new content::MessageLoopRunner;
      runner_->Run();
    }

    bool update_success() const { return update_success_; }

    bool app_update_notified() const { return app_update_notified_; }

   private:
    // KioskAppManagerObserver overrides:
    void OnKioskAppCacheUpdated(const std::string& app_id) override {
      if (app_id_ != app_id)
        return;
      app_update_notified_ = true;
    }

    void OnKioskAppExternalUpdateComplete(bool success) override {
      quit_ = true;
      update_success_ = success;
      if (runner_.get())
        runner_->Quit();
    }

    scoped_refptr<content::MessageLoopRunner> runner_;
    KioskAppManager* manager_;
    const std::string app_id_;
    bool quit_;
    bool update_success_;
    bool app_update_notified_;

    DISALLOW_COPY_AND_ASSIGN(KioskAppExternalUpdateWaiter);
  };

  // The local cache is supposed to be initialized on chromeos device, and a
  // ready flag file will be pre-created to mark the ready state, before chrome
  // starts. In order for the tests to run without being on real chromeos
  // device, we need to manually create this file.
  void CreateAndInitializeLocalCache() {
    base::FilePath extension_cache_dir;
    CHECK(PathService::Get(chromeos::DIR_DEVICE_EXTENSION_LOCAL_CACHE,
                           &extension_cache_dir));
    base::FilePath cache_init_file = extension_cache_dir.Append(
        extensions::LocalExtensionCache::kCacheReadyFlagFileName);
    EXPECT_EQ(base::WriteFile(cache_init_file, "", 0), 0);
  }

  // Owned by DiskMountManager.
  KioskFakeDiskMountManager* fake_disk_mount_manager_;

  DISALLOW_COPY_AND_ASSIGN(KioskUpdateTest);
};

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, PRE_LaunchOfflineEnabledAppNoNetwork) {
  PreCacheAndLaunchApp(kTestOfflineEnabledKioskApp,
                       "1.0.0",
                       std::string(kTestOfflineEnabledKioskApp) + "_v1.crx");
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, LaunchOfflineEnabledAppNoNetwork) {
  set_test_app_id(kTestOfflineEnabledKioskApp);
  StartUIForAppLaunch();
  SimulateNetworkOffline();
  LaunchApp(test_app_id(), false);
  WaitForAppLaunchSuccess();

  EXPECT_EQ("1.0.0", GetInstalledAppVersion().GetString());
  EXPECT_EQ(extensions::Manifest::EXTERNAL_PREF, GetInstalledAppLocation());
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest,
                       PRE_LaunchCachedOfflineEnabledAppNoNetwork) {
  PreCacheApp(kTestOfflineEnabledKioskApp,
              "1.0.0",
              std::string(kTestOfflineEnabledKioskApp) + "_v1.crx");
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest,
                       LaunchCachedOfflineEnabledAppNoNetwork) {
  set_test_app_id(kTestOfflineEnabledKioskApp);
  EXPECT_TRUE(
      KioskAppManager::Get()->HasCachedCrx(kTestOfflineEnabledKioskApp));
  StartUIForAppLaunch();
  SimulateNetworkOffline();
  LaunchApp(test_app_id(), false);
  WaitForAppLaunchSuccess();

  EXPECT_EQ("1.0.0", GetInstalledAppVersion().GetString());
  EXPECT_EQ(extensions::Manifest::EXTERNAL_PREF, GetInstalledAppLocation());
}

// Network offline, app v1.0 has run before, has cached v2.0 crx and v2.0 should
// be installed and launched during next launch.
IN_PROC_BROWSER_TEST_F(KioskUpdateTest,
                       PRE_LaunchCachedNewVersionOfflineEnabledAppNoNetwork) {
  // Install and launch v1 app.
  PreCacheAndLaunchApp(kTestOfflineEnabledKioskApp,
                       "1.0.0",
                       std::string(kTestOfflineEnabledKioskApp) + "_v1.crx");
  // Update cache for v2 app.
  UpdateExternalCache("2.0.0",
                      std::string(kTestOfflineEnabledKioskApp) + ".crx");
  // The installed app is still in v1.
  EXPECT_EQ("1.0.0", GetInstalledAppVersion().GetString());
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest,
                       LaunchCachedNewVersionOfflineEnabledAppNoNetwork) {
  set_test_app_id(kTestOfflineEnabledKioskApp);
  EXPECT_TRUE(KioskAppManager::Get()->HasCachedCrx(test_app_id()));

  StartUIForAppLaunch();
  SimulateNetworkOffline();
  LaunchApp(test_app_id(), false);
  WaitForAppLaunchSuccess();

  // v2 app should have been installed.
  EXPECT_EQ("2.0.0", GetInstalledAppVersion().GetString());
  EXPECT_EQ(extensions::Manifest::EXTERNAL_PREF, GetInstalledAppLocation());
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, PRE_LaunchOfflineEnabledAppNoUpdate) {
  PreCacheAndLaunchApp(kTestOfflineEnabledKioskApp,
                       "1.0.0",
                       std::string(kTestOfflineEnabledKioskApp) + "_v1.crx");
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, LaunchOfflineEnabledAppNoUpdate) {
  set_test_app_id(kTestOfflineEnabledKioskApp);
  fake_cws()->SetNoUpdate(test_app_id());

  StartUIForAppLaunch();
  SimulateNetworkOnline();
  LaunchApp(test_app_id(), false);
  WaitForAppLaunchSuccess();

  EXPECT_EQ("1.0.0", GetInstalledAppVersion().GetString());
  EXPECT_EQ(extensions::Manifest::EXTERNAL_PREF, GetInstalledAppLocation());
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, PRE_LaunchOfflineEnabledAppHasUpdate) {
  PreCacheAndLaunchApp(kTestOfflineEnabledKioskApp,
                       "1.0.0",
                       std::string(kTestOfflineEnabledKioskApp) + "_v1.crx");
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, LaunchOfflineEnabledAppHasUpdate) {
  set_test_app_id(kTestOfflineEnabledKioskApp);
  fake_cws()->SetUpdateCrx(
      test_app_id(), "ajoggoflpgplnnjkjamcmbepjdjdnpdp.crx", "2.0.0");

  StartUIForAppLaunch();
  SimulateNetworkOnline();
  LaunchApp(test_app_id(), false);
  WaitForAppLaunchSuccess();

  EXPECT_EQ("2.0.0", GetInstalledAppVersion().GetString());
  EXPECT_EQ(extensions::Manifest::EXTERNAL_PREF, GetInstalledAppLocation());
}

// Pre-cache v1 kiosk app, then launch the app without network,
// plug in usb stick with a v2 app for offline updating.
IN_PROC_BROWSER_TEST_F(KioskUpdateTest, PRE_UsbStickUpdateAppNoNetwork) {
  PreCacheApp(kTestOfflineEnabledKioskApp,
              "1.0.0",
              std::string(kTestOfflineEnabledKioskApp) + "_v1.crx");

  set_test_app_id(kTestOfflineEnabledKioskApp);
  StartUIForAppLaunch();
  SimulateNetworkOffline();
  LaunchApp(test_app_id(), false);
  WaitForAppLaunchSuccess();
  EXPECT_EQ("1.0.0", GetInstalledAppVersion().GetString());

  // Simulate mounting of usb stick with v2 app on the stick.
  bool update_success;
  bool app_update_notified;
  SimulateUpdateAppFromUsbStick(
      kFakeUsbMountPathUpdatePass, &app_update_notified, &update_success);
  EXPECT_TRUE(update_success);
  EXPECT_TRUE(app_update_notified);

  // The v2 kiosk app is loaded into external cache, but won't be installed
  // until next time the device is started.
  base::FilePath crx_path;
  std::string cached_version;
  EXPECT_TRUE(KioskAppManager::Get()->GetCachedCrx(
      test_app_id(), &crx_path, &cached_version));
  EXPECT_EQ("2.0.0", cached_version);
  EXPECT_EQ("1.0.0", GetInstalledAppVersion().GetString());
}

// Restart the device, verify the app has been updated to v2.
IN_PROC_BROWSER_TEST_F(KioskUpdateTest, UsbStickUpdateAppNoNetwork) {
  // Verify the kiosk app has been updated to v2.
  set_test_app_id(kTestOfflineEnabledKioskApp);
  StartUIForAppLaunch();
  SimulateNetworkOffline();
  LaunchApp(test_app_id(), false);
  WaitForAppLaunchSuccess();
  EXPECT_EQ("2.0.0", GetInstalledAppVersion().GetString());
}

// Usb stick is plugged in without a manifest file on it.
IN_PROC_BROWSER_TEST_F(KioskUpdateTest, UsbStickUpdateAppNoManifest) {
  PreCacheAndLaunchApp(kTestOfflineEnabledKioskApp,
                       "1.0.0",
                       std::string(kTestOfflineEnabledKioskApp) + "_v1.crx");
  EXPECT_EQ("1.0.0", GetInstalledAppVersion().GetString());

  // Simulate mounting of usb stick with v2 app on the stick.
  bool update_success;
  bool app_update_notified;
  SimulateUpdateAppFromUsbStick(
      kFakeUsbMountPathNoManifest, &app_update_notified, &update_success);
  EXPECT_FALSE(update_success);

  // Kiosk app is not updated.
  base::FilePath crx_path;
  std::string cached_version;
  EXPECT_TRUE(KioskAppManager::Get()->GetCachedCrx(
      test_app_id(), &crx_path, &cached_version));
  EXPECT_EQ("1.0.0", cached_version);
}

// Usb stick is plugged in with a bad manifest file on it.
IN_PROC_BROWSER_TEST_F(KioskUpdateTest, UsbStickUpdateAppBadManifest) {
  PreCacheAndLaunchApp(kTestOfflineEnabledKioskApp,
                       "1.0.0",
                       std::string(kTestOfflineEnabledKioskApp) + "_v1.crx");
  EXPECT_EQ("1.0.0", GetInstalledAppVersion().GetString());

  // Simulate mounting of usb stick with v2 app on the stick.
  bool update_success;
  bool app_update_notified;
  SimulateUpdateAppFromUsbStick(
      kFakeUsbMountPathBadManifest, &app_update_notified, &update_success);
  EXPECT_FALSE(update_success);

  // Kiosk app is not updated.
  base::FilePath crx_path;
  std::string cached_version;
  EXPECT_TRUE(KioskAppManager::Get()->GetCachedCrx(
      test_app_id(), &crx_path, &cached_version));
  EXPECT_EQ("1.0.0", cached_version);
}

// Usb stick is plugged in with a lower version of crx file specified in
// manifest.
IN_PROC_BROWSER_TEST_F(KioskUpdateTest, UsbStickUpdateAppLowerAppVersion) {
  // Precache v2 version of app.
  PreCacheAndLaunchApp(kTestOfflineEnabledKioskApp,
                       "2.0.0",
                       std::string(kTestOfflineEnabledKioskApp) + ".crx");
  EXPECT_EQ("2.0.0", GetInstalledAppVersion().GetString());

  // Simulate mounting of usb stick with v1 app on the stick.
  bool update_success;
  bool app_update_notified;
  SimulateUpdateAppFromUsbStick(
      kFakeUsbMountPathLowerAppVersion, &app_update_notified, &update_success);
  EXPECT_FALSE(update_success);

  // Kiosk app is NOT updated to the lower version.
  base::FilePath crx_path;
  std::string cached_version;
  EXPECT_TRUE(KioskAppManager::Get()->GetCachedCrx(
      test_app_id(), &crx_path, &cached_version));
  EXPECT_EQ("2.0.0", cached_version);
}

// Usb stick is plugged in with a v1 crx file, although the manifest says
// this is a v3 version.
IN_PROC_BROWSER_TEST_F(KioskUpdateTest, UsbStickUpdateAppLowerCrxVersion) {
  PreCacheAndLaunchApp(kTestOfflineEnabledKioskApp,
                       "2.0.0",
                       std::string(kTestOfflineEnabledKioskApp) + ".crx");
  EXPECT_EQ("2.0.0", GetInstalledAppVersion().GetString());

  // Simulate mounting of usb stick with v1 crx file on the stick, although
  // the manifest says it is v3 app.
  bool update_success;
  bool app_update_notified;
  SimulateUpdateAppFromUsbStick(
      kFakeUsbMountPathLowerCrxVersion, &app_update_notified, &update_success);
  EXPECT_FALSE(update_success);

  // Kiosk app is NOT updated to the lower version.
  base::FilePath crx_path;
  std::string cached_version;
  EXPECT_TRUE(KioskAppManager::Get()->GetCachedCrx(
      test_app_id(), &crx_path, &cached_version));
  EXPECT_EQ("2.0.0", cached_version);
}

// Usb stick is plugged in with a bad crx file.
IN_PROC_BROWSER_TEST_F(KioskUpdateTest, UsbStickUpdateAppBadCrx) {
  PreCacheAndLaunchApp(kTestOfflineEnabledKioskApp,
                       "1.0.0",
                       std::string(kTestOfflineEnabledKioskApp) + "_v1.crx");
  EXPECT_EQ("1.0.0", GetInstalledAppVersion().GetString());

  // Simulate mounting of usb stick with v1 crx file on the stick, although
  // the manifest says it is v3 app.
  bool update_success;
  bool app_update_notified;
  SimulateUpdateAppFromUsbStick(
      kFakeUsbMountPathBadCrx, &app_update_notified, &update_success);
  EXPECT_FALSE(update_success);

  // Kiosk app is NOT updated.
  base::FilePath crx_path;
  std::string cached_version;
  EXPECT_TRUE(KioskAppManager::Get()->GetCachedCrx(
      test_app_id(), &crx_path, &cached_version));
  EXPECT_EQ("1.0.0", cached_version);
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, PRE_PermissionChange) {
  PreCacheAndLaunchApp(kTestOfflineEnabledKioskApp,
                       "2.0.0",
                       std::string(kTestOfflineEnabledKioskApp) + ".crx");
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, PermissionChange) {
  set_test_app_id(kTestOfflineEnabledKioskApp);
  set_test_app_version("2.0.0");
  set_test_crx_file(test_app_id() + "_v2_permission_change.crx");

  StartUIForAppLaunch();
  SimulateNetworkOnline();
  LaunchApp(test_app_id(), false);
  WaitForAppLaunchSuccess();

  EXPECT_EQ("2.0.0", GetInstalledAppVersion().GetString());
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, PRE_PreserveLocalData) {
  // Installs v1 app and writes some local data.
  set_test_app_id(kTestLocalFsKioskApp);
  set_test_app_version("1.0.0");
  set_test_crx_file(test_app_id() + ".crx");

  extensions::ResultCatcher catcher;
  StartAppLaunchFromLoginScreen(SimulateNetworkOnlineClosure());
  WaitForAppLaunchWithOptions(true /* check_launch_data */,
                              false /* terminate_app */);
  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, PreserveLocalData) {
  // Update existing v1 app installed in PRE_PreserveLocalData to v2
  // that reads and verifies the local data.
  set_test_app_id(kTestLocalFsKioskApp);
  set_test_app_version("2.0.0");
  set_test_crx_file(test_app_id() + "_v2_read_and_verify_data.crx");
  extensions::ResultCatcher catcher;
  StartAppLaunchFromLoginScreen(SimulateNetworkOnlineClosure());
  WaitForAppLaunchWithOptions(true /* check_launch_data */,
                              false /* terminate_app */);

  EXPECT_EQ("2.0.0", GetInstalledAppVersion().GetString());
  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

/* ***** Test Kiosk multi-app feature ***** */

// Launch a primary kiosk app which has two secondary apps.
IN_PROC_BROWSER_TEST_F(KioskUpdateTest,
                       LaunchTestKioskAppWithTwoSecondaryApps) {
  LaunchTestKioskAppWithTwoSecondaryApps();
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, PRE_UpdateMultiAppKioskRemoveOneApp) {
  LaunchTestKioskAppWithTwoSecondaryApps();
}

// Update the primary app to version 2 which removes one of the secondary app
// from its manifest.
IN_PROC_BROWSER_TEST_F(KioskUpdateTest, UpdateMultiAppKioskRemoveOneApp) {
  set_test_app_id(kTestPrimaryKioskApp);
  fake_cws()->SetUpdateCrx(
      kTestPrimaryKioskApp,
      std::string(kTestPrimaryKioskApp) + "-2.0.0-1app.crx", "2.0.0");
  fake_cws()->SetNoUpdate(kTestSecondaryApp1);
  fake_cws()->SetNoUpdate(kTestSecondaryApp2);

  StartUIForAppLaunch();
  SimulateNetworkOnline();
  LaunchApp(test_app_id(), false);
  WaitForAppLaunchWithOptions(false, true);

  // Verify the secondary app kTestSecondaryApp1 is removed.
  EXPECT_EQ("2.0.0", GetInstalledAppVersion().GetString());
  EXPECT_FALSE(IsAppInstalled(kTestSecondaryApp1));
  EXPECT_TRUE(IsAppInstalled(kTestSecondaryApp2));
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, PRE_UpdateMultiAppKioskAddOneApp) {
  LaunchTestKioskAppWithTwoSecondaryApps();
}

// Update the primary app to version 3 which adds a new secondary app in its
// manifest.
IN_PROC_BROWSER_TEST_F(KioskUpdateTest, UpdateMultiAppKioskAddOneApp) {
  set_test_app_id(kTestPrimaryKioskApp);
  fake_cws()->SetUpdateCrx(
      kTestPrimaryKioskApp,
      std::string(kTestPrimaryKioskApp) + "-3.0.0-3app.crx", "3.0.0");
  fake_cws()->SetNoUpdate(kTestSecondaryApp1);
  fake_cws()->SetNoUpdate(kTestSecondaryApp2);
  fake_cws()->SetUpdateCrx(kTestSecondaryApp3,
                           std::string(kTestSecondaryApp3) + "-1.0.0.crx",
                           "1.0.0");

  StartUIForAppLaunch();
  SimulateNetworkOnline();
  LaunchApp(test_app_id(), false);
  WaitForAppLaunchWithOptions(false, true);

  // Verify the secondary app kTestSecondaryApp3 is installed.
  EXPECT_EQ("3.0.0", GetInstalledAppVersion().GetString());
  EXPECT_TRUE(IsAppInstalled(kTestSecondaryApp1));
  EXPECT_TRUE(IsAppInstalled(kTestSecondaryApp2));
  EXPECT_TRUE(IsAppInstalled(kTestSecondaryApp3));
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest, LaunchKioskAppWithSecondaryExtension) {
  LaunchTestKioskAppWithSeconadayExtension();
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest,
                       LaunchAppWithSharedModuleAndSecondaryApp) {
  LaunchAppWithSharedModuleAndSecondaryApp();
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest,
                       PRE_UpdateAppWithSharedModuleRemoveAllSecondaryApps) {
  LaunchAppWithSharedModuleAndSecondaryApp();
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest,
                       UpdateAppWithSharedModuleRemoveAllSecondaryApps) {
  set_test_app_id(kTestSharedModulePrimaryApp);
  fake_cws()->SetUpdateCrx(
      kTestSharedModulePrimaryApp,
      std::string(kTestSharedModulePrimaryApp) + "-2.0.0.crx", "2.0.0");
  fake_cws()->SetNoUpdate(kTestSecondaryApp1);
  fake_cws()->SetNoUpdate(kTestSharedModuleId);

  StartUIForAppLaunch();
  SimulateNetworkOnline();
  LaunchApp(test_app_id(), false);
  WaitForAppLaunchWithOptions(false, true);

  // Verify the secondary app is removed.
  EXPECT_TRUE(IsAppInstalled(kTestSharedModuleId));
  EXPECT_FALSE(IsAppInstalled(kTestSecondaryApp1));
}

// This simulates the stand-alone ARC kiosk app case. The primary app has a
// shared ARC runtime but no secondary apps.
IN_PROC_BROWSER_TEST_F(KioskUpdateTest, LaunchAppWithSharedModuleNoSecondary) {
  TestAppInfo primary_app(
      kTestSharedModulePrimaryApp, "2.0.0",
      std::string(kTestSharedModulePrimaryApp) + "-2.0.0.crx",
      extensions::Manifest::TYPE_PLATFORM_APP);

  std::vector<TestAppInfo> secondary_apps;
  // Setting up FakeCWS for shared module is the same for shared module as
  // for kiosk secondary apps.
  TestAppInfo shared_module(kTestSharedModuleId, "1.0.0",
                            std::string(kTestSharedModuleId) + "-1.0.0.crx",
                            extensions::Manifest::TYPE_SHARED_MODULE);
  secondary_apps.push_back(shared_module);

  LaunchKioskWithSecondaryApps(primary_app, secondary_apps);
}

IN_PROC_BROWSER_TEST_F(KioskUpdateTest,
                       LaunchAppWithSecondaryArcLikeAppAndExtension) {
  TestAppInfo primary_app(
      kTestSharedModulePrimaryApp, "3.0.0",
      std::string(kTestSharedModulePrimaryApp) + "-3.0.0.crx",
      extensions::Manifest::TYPE_PLATFORM_APP);

  std::vector<TestAppInfo> secondary_apps;
  // Setting up FakeCWS for shared module is the same for shared module as
  // for kiosk secondary apps.
  TestAppInfo shared_module(kTestSharedModuleId, "1.0.0",
                            std::string(kTestSharedModuleId) + "-1.0.0.crx",
                            extensions::Manifest::TYPE_SHARED_MODULE);
  secondary_apps.push_back(shared_module);
  // The secondary app has a shared module, which is similar to an ARC app.
  TestAppInfo secondary_app(kTestSecondaryApp, "2.0.0",
                            std::string(kTestSecondaryApp) + "-2.0.0.crx",
                            extensions::Manifest::TYPE_PLATFORM_APP);
  secondary_apps.push_back(secondary_app);
  TestAppInfo secondary_ext(kTestSecondaryExt, "1.0.0",
                            std::string(kTestSecondaryExt) + "-1.0.0.crx",
                            extensions::Manifest::TYPE_EXTENSION);
  secondary_apps.push_back(secondary_ext);

  LaunchKioskWithSecondaryApps(primary_app, secondary_apps);
}

class KioskEnterpriseTest : public KioskTest {
 protected:
  KioskEnterpriseTest() {
    set_use_consumer_kiosk_mode(false);
  }

  void SetUpInProcessBrowserTestFixture() override {
    policy::DevicePolicyCrosTestHelper::MarkAsEnterpriseOwnedBy(
        kTestOwnerEmail);
    settings_helper_.SetCurrentUserIsOwner(false);

    KioskTest::SetUpInProcessBrowserTestFixture();
  }

  void SetUpOnMainThread() override {
    KioskTest::SetUpOnMainThread();

    // Configure OAuth authentication.
    GaiaUrls* gaia_urls = GaiaUrls::GetInstance();

    // This token satisfies the userinfo.email request from
    // DeviceOAuth2TokenService used in token validation.
    FakeGaia::AccessTokenInfo userinfo_token_info;
    userinfo_token_info.token = kTestUserinfoToken;
    userinfo_token_info.scopes.insert(
        "https://www.googleapis.com/auth/userinfo.email");
    userinfo_token_info.audience = gaia_urls->oauth2_chrome_client_id();
    userinfo_token_info.email = kTestEnterpriseServiceAccountId;
    fake_gaia_->IssueOAuthToken(kTestRefreshToken, userinfo_token_info);

    // The any-api access token for accessing the token minting endpoint.
    FakeGaia::AccessTokenInfo login_token_info;
    login_token_info.token = kTestLoginToken;
    login_token_info.scopes.insert(GaiaConstants::kAnyApiOAuth2Scope);
    login_token_info.audience = gaia_urls->oauth2_chrome_client_id();
    fake_gaia_->IssueOAuthToken(kTestRefreshToken, login_token_info);

    // This is the access token requested by the app via the identity API.
    FakeGaia::AccessTokenInfo access_token_info;
    access_token_info.token = kTestAccessToken;
    access_token_info.scopes.insert(kTestAppScope);
    access_token_info.audience = kTestClientId;
    access_token_info.email = kTestEnterpriseServiceAccountId;
    fake_gaia_->IssueOAuthToken(kTestLoginToken, access_token_info);

    DeviceOAuth2TokenService* token_service =
        DeviceOAuth2TokenServiceFactory::Get();
    token_service->SetAndSaveRefreshToken(
        kTestRefreshToken, DeviceOAuth2TokenService::StatusCallback());
    base::RunLoop().RunUntilIdle();
  }

  void ConfigureKioskAppInPolicy(const std::string& account_id,
                                 const std::string& app_id,
                                 const std::string& update_url) {
    settings_helper_.SetCurrentUserIsOwner(true);
    std::vector<policy::DeviceLocalAccount> accounts;
    accounts.push_back(
        policy::DeviceLocalAccount(policy::DeviceLocalAccount::TYPE_KIOSK_APP,
                                   account_id, app_id, update_url));
    policy::SetDeviceLocalAccounts(owner_settings_service_.get(), accounts);
    settings_helper_.SetString(kAccountsPrefDeviceLocalAccountAutoLoginId,
                               account_id);
    settings_helper_.SetString(kServiceAccountIdentity,
                               kTestEnterpriseServiceAccountId);
    settings_helper_.SetCurrentUserIsOwner(false);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(KioskEnterpriseTest);
};

IN_PROC_BROWSER_TEST_F(KioskEnterpriseTest, EnterpriseKioskApp) {
  // Prepare Fake CWS to serve app crx.
  set_test_app_id(kTestEnterpriseKioskApp);
  set_test_app_version("1.0.0");
  set_test_crx_file(test_app_id() + ".crx");
  SetupTestAppUpdateCheck();

  // Configure kTestEnterpriseKioskApp in device policy.
  ConfigureKioskAppInPolicy(kTestEnterpriseAccountId,
                            kTestEnterpriseKioskApp,
                            "");

  PrepareAppLaunch();
  LaunchApp(kTestEnterpriseKioskApp, false);

  // Wait for the Kiosk App to launch.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_APP_LAUNCHED,
      content::NotificationService::AllSources()).Wait();

  // Check installer status.
  EXPECT_EQ(chromeos::KioskAppLaunchError::NONE,
            chromeos::KioskAppLaunchError::Get());
  EXPECT_EQ(extensions::Manifest::EXTERNAL_POLICY, GetInstalledAppLocation());

  // Wait for the window to appear.
  extensions::AppWindow* window =
      AppWindowWaiter(
          extensions::AppWindowRegistry::Get(
              ProfileManager::GetPrimaryUserProfile()),
          kTestEnterpriseKioskApp).Wait();
  ASSERT_TRUE(window);

  // Check whether the app can retrieve an OAuth2 access token.
  std::string result;
  EXPECT_TRUE(content::ExecuteScriptAndExtractString(
      window->web_contents(),
      "chrome.identity.getAuthToken({ 'interactive': false }, function(token) {"
      "    window.domAutomationController.setAutomationId(0);"
      "    window.domAutomationController.send(token);"
      "});",
      &result));
  EXPECT_EQ(kTestAccessToken, result);

  // Verify that the session is not considered to be logged in with a GAIA
  // account.
  Profile* app_profile = ProfileManager::GetPrimaryUserProfile();
  ASSERT_TRUE(app_profile);
  EXPECT_FALSE(
      SigninManagerFactory::GetForProfile(app_profile)->IsAuthenticated());

  // Terminate the app.
  window->GetBaseWindow()->Close();
  content::RunAllPendingInMessageLoop();
}

IN_PROC_BROWSER_TEST_F(KioskEnterpriseTest, PrivateStore) {
  set_test_app_id(kTestEnterpriseKioskApp);

  const char kPrivateStoreUpdate[] = "/private_store_update";
  net::test_server::EmbeddedTestServer private_server;
  ASSERT_TRUE(private_server.InitializeAndWaitUntilReady());

  // |private_server| serves crx from test data dir.
  base::FilePath test_data_dir;
  PathService::Get(chrome::DIR_TEST_DATA, &test_data_dir);
  private_server.ServeFilesFromDirectory(test_data_dir);

  FakeCWS private_store;
  private_store.InitAsPrivateStore(&private_server, kPrivateStoreUpdate);
  private_store.SetUpdateCrx(kTestEnterpriseKioskApp,
                             std::string(kTestEnterpriseKioskApp) + ".crx",
                             "1.0.0");

  // Configure kTestEnterpriseKioskApp in device policy.
  ConfigureKioskAppInPolicy(kTestEnterpriseAccountId,
                            kTestEnterpriseKioskApp,
                            private_server.GetURL(kPrivateStoreUpdate).spec());

  // Meta should be able to be extracted from crx before launching.
  KioskAppManager* manager = KioskAppManager::Get();
  AppDataLoadWaiter waiter(manager, kTestEnterpriseKioskApp, std::string());
  waiter.WaitForAppData();

  PrepareAppLaunch();
  LaunchApp(kTestEnterpriseKioskApp, false);
  WaitForAppLaunchWithOptions(false /* check_launch_data */,
                              true /* terminate_app */);

  // Private store should serve crx and CWS should not.
  DCHECK_GT(private_store.GetUpdateCheckCountAndReset(), 0);
  DCHECK_EQ(0, fake_cws()->GetUpdateCheckCountAndReset());
  EXPECT_EQ(extensions::Manifest::EXTERNAL_POLICY, GetInstalledAppLocation());
}

// Specialized test fixture for testing kiosk mode on the
// hidden WebUI initialization flow for slow hardware.
class KioskHiddenWebUITest : public KioskTest,
                             public ash::DesktopBackgroundControllerObserver {
 public:
  KioskHiddenWebUITest() : wallpaper_loaded_(false) {}

  // KioskTest overrides:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    KioskTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kDisableBootAnimation);
  }

  void SetUpOnMainThread() override {
    LoginDisplayHostImpl::DisableRestrictiveProxyCheckForTest();

    KioskTest::SetUpOnMainThread();
    ash::Shell::GetInstance()->desktop_background_controller()
        ->AddObserver(this);
  }

  void TearDownOnMainThread() override {
    ash::Shell::GetInstance()->desktop_background_controller()
        ->RemoveObserver(this);
    KioskTest::TearDownOnMainThread();
  }

  void WaitForWallpaper() {
    if (!wallpaper_loaded_) {
      runner_ = new content::MessageLoopRunner;
      runner_->Run();
    }
  }

  bool wallpaper_loaded() const { return wallpaper_loaded_; }

  // ash::DesktopBackgroundControllerObserver overrides:
  void OnWallpaperDataChanged() override {
    wallpaper_loaded_ = true;
    if (runner_.get())
      runner_->Quit();
  }

  bool wallpaper_loaded_;
  scoped_refptr<content::MessageLoopRunner> runner_;

  DISALLOW_COPY_AND_ASSIGN(KioskHiddenWebUITest);
};

IN_PROC_BROWSER_TEST_F(KioskHiddenWebUITest, AutolaunchWarning) {
  // Add a device owner.
  FakeChromeUserManager* user_manager = new FakeChromeUserManager();
  user_manager->AddUser(kTestOwnerEmail);
  ScopedUserManagerEnabler enabler(user_manager);

  // Set kiosk app to autolaunch.
  EnableConsumerKioskMode();
  WizardController::SkipPostLoginScreensForTesting();
  WizardController* wizard_controller = WizardController::default_controller();
  CHECK(wizard_controller);

  // Start login screen after configuring auto launch app since the warning
  // is triggered when switching to login screen.
  wizard_controller->AdvanceToScreen(WizardController::kNetworkScreenName);
  ReloadAutolaunchKioskApps();
  wizard_controller->SkipToLoginForTesting(LoginScreenContext());

  EXPECT_FALSE(KioskAppManager::Get()->GetAutoLaunchApp().empty());
  EXPECT_FALSE(KioskAppManager::Get()->IsAutoLaunchEnabled());

  // Wait for the auto launch warning come up.
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_KIOSK_AUTOLAUNCH_WARNING_VISIBLE,
      content::NotificationService::AllSources()).Wait();

  // Wait for the wallpaper to load.
  WaitForWallpaper();
  EXPECT_TRUE(wallpaper_loaded());
}

}  // namespace chromeos
