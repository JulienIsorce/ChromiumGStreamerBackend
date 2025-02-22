// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/input_method_persistence.h"

#include "base/command_line.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/chromeos/input_method/mock_input_method_manager.h"
#include "chrome/browser/chromeos/language_preferences.h"
#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/scoped_user_manager_enabler.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "chromeos/chromeos_switches.h"
#include "components/syncable_prefs/testing_pref_service_syncable.h"
#include "content/public/test/test_browser_thread_bundle.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {
namespace input_method {

namespace {
const char kInputId1[] = "xkb:us:dvorak:eng";
const char kInputId2[] = "xkb:us:colemak:eng";
}

class InputMethodPersistenceTest : public testing::Test {
 protected:
  InputMethodPersistenceTest()
      : mock_profile_manager_(TestingBrowserProcess::GetGlobal()),
        fake_user_manager_(new chromeos::FakeChromeUserManager()),
        user_manager_enabler_(fake_user_manager_) {}

  void SetUp() override {
    ASSERT_TRUE(mock_profile_manager_.SetUp());

    // Add a user.
    const char kTestUserName[] = "test-user@example.com";
    fake_user_manager_->AddUser(kTestUserName);
    fake_user_manager_->LoginUser(kTestUserName);

    // Create a valid profile for the user.
    TestingProfile* mock_profile =
        mock_profile_manager_.CreateTestingProfile(kTestUserName);
    mock_profile_manager_.SetLoggedIn(true);
    EXPECT_TRUE(ProfileManager::GetActiveUserProfile() == mock_profile);

    mock_user_prefs_ = mock_profile->GetTestingPrefService();
  }

  // Verifies that the user and system prefs contain the expected values.
  void VerifyPrefs(const std::string& current_input_method,
                   const std::string& previous_input_method,
                   const std::string& preferred_keyboard_layout) {
    EXPECT_EQ(current_input_method,
              mock_user_prefs_->GetString(prefs::kLanguageCurrentInputMethod));
    EXPECT_EQ(previous_input_method,
              mock_user_prefs_->GetString(prefs::kLanguagePreviousInputMethod));
    EXPECT_EQ(preferred_keyboard_layout,
              g_browser_process->local_state()->GetString(
                  language_prefs::kPreferredKeyboardLayout));
  }

  content::TestBrowserThreadBundle thread_bundle_;
  syncable_prefs::TestingPrefServiceSyncable* mock_user_prefs_;
  MockInputMethodManager mock_manager_;
  TestingProfileManager mock_profile_manager_;
  chromeos::FakeChromeUserManager* fake_user_manager_;
  chromeos::ScopedUserManagerEnabler user_manager_enabler_;
};

TEST_F(InputMethodPersistenceTest, TestLifetime) {
  {
    InputMethodPersistence persistence(&mock_manager_);
    EXPECT_EQ(1, mock_manager_.add_observer_count_);
  }
  EXPECT_EQ(1, mock_manager_.remove_observer_count_);
}

TEST_F(InputMethodPersistenceTest, TestPrefPersistenceByState) {
  InputMethodPersistence persistence(&mock_manager_);

  persistence.OnSessionStateChange(InputMethodManager::STATE_LOGIN_SCREEN);
  mock_manager_.SetCurrentInputMethodId(kInputId1);
  persistence.InputMethodChanged(&mock_manager_,
                                 ProfileManager::GetActiveUserProfile(), false);
  VerifyPrefs("", "", kInputId1);

  persistence.OnSessionStateChange(InputMethodManager::STATE_BROWSER_SCREEN);
  mock_manager_.SetCurrentInputMethodId(kInputId2);
  persistence.InputMethodChanged(&mock_manager_,
                                 ProfileManager::GetActiveUserProfile(), false);
  VerifyPrefs(kInputId2, "", kInputId1);

  persistence.OnSessionStateChange(InputMethodManager::STATE_LOCK_SCREEN);
  mock_manager_.SetCurrentInputMethodId(kInputId1);
  persistence.InputMethodChanged(&mock_manager_,
                                 ProfileManager::GetActiveUserProfile(), false);
  VerifyPrefs(kInputId2, "", kInputId1);

  persistence.OnSessionStateChange(InputMethodManager::STATE_TERMINATING);
  mock_manager_.SetCurrentInputMethodId(kInputId1);
  persistence.InputMethodChanged(&mock_manager_,
                                 ProfileManager::GetActiveUserProfile(), false);
  VerifyPrefs(kInputId2, "", kInputId1);

  persistence.OnSessionStateChange(InputMethodManager::STATE_LOGIN_SCREEN);
  mock_manager_.SetCurrentInputMethodId(kInputId2);
  persistence.InputMethodChanged(&mock_manager_,
                                 ProfileManager::GetActiveUserProfile(), false);
  VerifyPrefs(kInputId2, "", kInputId2);

  persistence.OnSessionStateChange(InputMethodManager::STATE_BROWSER_SCREEN);
  mock_manager_.SetCurrentInputMethodId(kInputId1);
  persistence.InputMethodChanged(&mock_manager_,
                                 ProfileManager::GetActiveUserProfile(), false);
  VerifyPrefs(kInputId1, kInputId2, kInputId2);
}

}  // namespace input_method
}  // namespace chromeos
