// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_TEST_TEST_SESSION_STATE_DELEGATE_H_
#define ASH_TEST_TEST_SESSION_STATE_DELEGATE_H_

#include <vector>

#include "ash/session/session_state_delegate.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "ui/gfx/image/image_skia.h"

namespace ash {
namespace test {

class MockUserInfo;

class TestSessionStateDelegate : public SessionStateDelegate {
 public:
  TestSessionStateDelegate();
  ~TestSessionStateDelegate() override;

  void set_logged_in_users(int users) { logged_in_users_ = users; }
  void set_session_state(SessionState session_state) {
    session_state_ = session_state;
  }
  void AddUser(const std::string& user_id);
  const user_manager::UserInfo* GetActiveUserInfo() const;

  // SessionStateDelegate:
  int GetMaximumNumberOfLoggedInUsers() const override;
  int NumberOfLoggedInUsers() const override;
  bool IsActiveUserSessionStarted() const override;
  bool CanLockScreen() const override;
  bool IsScreenLocked() const override;
  bool ShouldLockScreenBeforeSuspending() const override;
  void LockScreen() override;
  void UnlockScreen() override;
  bool IsUserSessionBlocked() const override;
  SessionState GetSessionState() const override;
  const user_manager::UserInfo* GetUserInfo(
      ash::UserIndex index) const override;
  bool ShouldShowAvatar(aura::Window* window) const override;
  gfx::ImageSkia GetAvatarImageForWindow(aura::Window* window) const override;
  void SwitchActiveUser(const std::string& user_id) override;
  void CycleActiveUser(CycleUser cycle_user) override;
  bool IsMultiProfileAllowedByPrimaryUserPolicy() const override;
  void AddSessionStateObserver(ash::SessionStateObserver* observer) override;
  void RemoveSessionStateObserver(ash::SessionStateObserver* observer) override;

  // TODO(oshima): Use state machine instead of using boolean variables.

  // Updates the internal state that indicates whether a session is in progress
  // and there is an active user. If |has_active_user| is |false|,
  // |active_user_session_started_| is reset to |false| as well (see below for
  // the difference between these two flags).
  void SetHasActiveUser(bool has_active_user);

  // Updates the internal state that indicates whether the session has been
  // fully started for the active user. If |active_user_session_started| is
  // |true|, |has_active_user_| is set to |true| as well (see below for the
  // difference between these two flags).
  void SetActiveUserSessionStarted(bool active_user_session_started);

  // Updates the internal state that indicates whether the screen can be locked.
  // Locking will only actually be allowed when this value is |true| and there
  // is an active user.
  void SetCanLockScreen(bool can_lock_screen);

  // Updates |should_lock_screen_before_suspending_|.
  void SetShouldLockScreenBeforeSuspending(bool should_lock);

  // Updates the internal state that indicates whether user adding screen is
  // running now.
  void SetUserAddingScreenRunning(bool user_adding_screen_running);

  // Setting non NULL image enables avatar icon.
  void SetUserImage(const gfx::ImageSkia& user_image);

 private:
  class TestUserManager;

  // Whether the screen can be locked. Locking will only actually be allowed
  // when this is |true| and there is an active user.
  bool can_lock_screen_;

  // Return value for ShouldLockScreenBeforeSuspending().
  bool should_lock_screen_before_suspending_;

  // Whether the screen is currently locked.
  bool screen_locked_;

  // Whether user addding screen is running now.
  bool user_adding_screen_running_;

  // The number of users logged in.
  int logged_in_users_;

  // The index for the activated user.
  int active_user_index_;

  std::vector<MockUserInfo*> user_list_;

  // The user manager to be used instead of the system instance.
  scoped_ptr<TestUserManager> user_manager_;

  // The current state of the login screen. |session_state_| becomes active
  // before the profile and browser UI are available.
  SessionState session_state_;

  DISALLOW_COPY_AND_ASSIGN(TestSessionStateDelegate);
};

}  // namespace test
}  // namespace ash

#endif  // ASH_TEST_TEST_SESSION_STATE_DELEGATE_H_
