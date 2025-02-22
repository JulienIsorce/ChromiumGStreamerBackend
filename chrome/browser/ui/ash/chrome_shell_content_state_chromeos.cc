// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/chrome_shell_content_state.h"

#include "ash/session/session_state_delegate.h"
#include "ash/shell.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/browser_context.h"

content::BrowserContext* ChromeShellContentState::GetBrowserContextByIndex(
    ash::UserIndex index) {
  ash::SessionStateDelegate* session_state_delegate =
      ash::Shell::GetInstance()->session_state_delegate();
  DCHECK_LT(index, session_state_delegate->NumberOfLoggedInUsers());
  user_manager::User* user =
      user_manager::UserManager::Get()->GetLRULoggedInUsers()[index];
  CHECK(user);
  return chromeos::ProfileHelper::Get()->GetProfileByUser(user);
}

content::BrowserContext* ChromeShellContentState::GetBrowserContextForWindow(
    aura::Window* window) {
  const std::string& user_id =
      chrome::MultiUserWindowManager::GetInstance()->GetWindowOwner(window);
  return user_id.empty() ? nullptr
                         : multi_user_util::GetProfileFromUserID(user_id);
}

content::BrowserContext*
ChromeShellContentState::GetUserPresentingBrowserContextForWindow(
    aura::Window* window) {
  const std::string& user_id =
      chrome::MultiUserWindowManager::GetInstance()->GetUserPresentingWindow(
          window);
  return user_id.empty() ? nullptr
                         : multi_user_util::GetProfileFromUserID(user_id);
}
