// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/screens/user_selection_screen.h"

#include "base/location.h"
#include "base/logging.h"
#include "base/prefs/pref_service.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/chromeos/login/lock/screen_locker.h"
#include "chrome/browser/chromeos/login/reauth_stats.h"
#include "chrome/browser/chromeos/login/ui/login_display_host_impl.h"
#include "chrome/browser/chromeos/login/ui/views/user_board_view.h"
#include "chrome/browser/chromeos/login/users/chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/multi_profile_user_controller.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/signin/easy_unlock_service.h"
#include "chrome/browser/ui/webui/chromeos/login/l10n_util.h"
#include "chrome/browser/ui/webui/chromeos/login/signin_screen_handler.h"
#include "components/proximity_auth/screenlock_bridge.h"
#include "components/user_manager/user_id.h"
#include "components/user_manager/user_manager.h"
#include "components/user_manager/user_type.h"
#include "ui/base/user_activity/user_activity_detector.h"

namespace chromeos {

namespace {

// User dictionary keys.
const char kKeyUsername[] = "username";
const char kKeyGaiaID[] = "gaiaId";
const char kKeyDisplayName[] = "displayName";
const char kKeyEmailAddress[] = "emailAddress";
const char kKeyEnterpriseDomain[] = "enterpriseDomain";
const char kKeyPublicAccount[] = "publicAccount";
const char kKeyLegacySupervisedUser[] = "legacySupervisedUser";
const char kKeyChildUser[] = "childUser";
const char kKeyDesktopUser[] = "isDesktopUser";
const char kKeySignedIn[] = "signedIn";
const char kKeyCanRemove[] = "canRemove";
const char kKeyIsOwner[] = "isOwner";
const char kKeyInitialAuthType[] = "initialAuthType";
const char kKeyMultiProfilesAllowed[] = "isMultiProfilesAllowed";
const char kKeyMultiProfilesPolicy[] = "multiProfilesPolicy";
const char kKeyInitialLocales[] = "initialLocales";
const char kKeyInitialLocale[] = "initialLocale";
const char kKeyInitialMultipleRecommendedLocales[] =
    "initialMultipleRecommendedLocales";
const char kKeyInitialKeyboardLayout[] = "initialKeyboardLayout";

// Max number of users to show.
// Please keep synced with one in signin_userlist_unittest.cc.
const size_t kMaxUsers = 18;

const int kPasswordClearTimeoutSec = 60;

void AddPublicSessionDetailsToUserDictionaryEntry(
    base::DictionaryValue* user_dict,
    const std::vector<std::string>* public_session_recommended_locales) {
  policy::BrowserPolicyConnectorChromeOS* policy_connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();

  if (policy_connector->IsEnterpriseManaged()) {
    user_dict->SetString(kKeyEnterpriseDomain,
                         policy_connector->GetEnterpriseDomain());
  }

  std::vector<std::string> kEmptyRecommendedLocales;
  const std::vector<std::string>& recommended_locales =
      public_session_recommended_locales ?
          *public_session_recommended_locales : kEmptyRecommendedLocales;

  // Construct the list of available locales. This list consists of the
  // recommended locales, followed by all others.
  scoped_ptr<base::ListValue> available_locales =
      GetUILanguageList(&recommended_locales, std::string());

  // Select the the first recommended locale that is actually available or the
  // current UI locale if none of them are available.
  const std::string selected_locale = FindMostRelevantLocale(
      recommended_locales,
      *available_locales.get(),
      g_browser_process->GetApplicationLocale());

  // Set |kKeyInitialLocales| to the list of available locales.
  user_dict->Set(kKeyInitialLocales, available_locales.release());

  // Set |kKeyInitialLocale| to the initially selected locale.
  user_dict->SetString(kKeyInitialLocale, selected_locale);

  // Set |kKeyInitialMultipleRecommendedLocales| to indicate whether the list
  // of recommended locales contains at least two entries. This is used to
  // decide whether the public session pod expands to its basic form (for zero
  // or one recommended locales) or the advanced form (two or more recommended
  // locales).
  user_dict->SetBoolean(kKeyInitialMultipleRecommendedLocales,
                        recommended_locales.size() >= 2);

  // Set |kKeyInitialKeyboardLayout| to the current keyboard layout. This
  // value will be used temporarily only because the UI immediately requests a
  // list of keyboard layouts suitable for the currently selected locale.
  user_dict->Set(kKeyInitialKeyboardLayout,
                 GetCurrentKeyboardLayout().release());
}

}  // namespace

UserSelectionScreen::UserSelectionScreen(const std::string& display_type)
    : handler_(nullptr),
      login_display_delegate_(nullptr),
      view_(nullptr),
      display_type_(display_type),
      weak_factory_(this) {
}

UserSelectionScreen::~UserSelectionScreen() {
  proximity_auth::ScreenlockBridge::Get()->SetLockHandler(nullptr);
  ui::UserActivityDetector* activity_detector = ui::UserActivityDetector::Get();
  if (activity_detector->HasObserver(this))
    activity_detector->RemoveObserver(this);
}

void UserSelectionScreen::InitEasyUnlock() {
  proximity_auth::ScreenlockBridge::Get()->SetLockHandler(this);
}

void UserSelectionScreen::SetLoginDisplayDelegate(
    LoginDisplay::Delegate* login_display_delegate) {
  login_display_delegate_ = login_display_delegate;
}

// static
void UserSelectionScreen::FillUserDictionary(
    user_manager::User* user,
    bool is_owner,
    bool is_signin_to_add,
    AuthType auth_type,
    const std::vector<std::string>* public_session_recommended_locales,
    base::DictionaryValue* user_dict) {
  const user_manager::UserID user_id = user->email();
  const bool is_public_session =
      user->GetType() == user_manager::USER_TYPE_PUBLIC_ACCOUNT;
  const bool is_legacy_supervised_user =
      user->GetType() == user_manager::USER_TYPE_SUPERVISED;
  const bool is_child_user = user->GetType() == user_manager::USER_TYPE_CHILD;

  user_dict->SetString(kKeyUsername, user_id);
  user_dict->SetString(kKeyEmailAddress, user->display_email());
  user_dict->SetString(kKeyDisplayName, user->GetDisplayName());
  user_dict->SetBoolean(kKeyPublicAccount, is_public_session);
  user_dict->SetBoolean(kKeyLegacySupervisedUser, is_legacy_supervised_user);
  user_dict->SetBoolean(kKeyChildUser, is_child_user);
  user_dict->SetBoolean(kKeyDesktopUser, false);
  user_dict->SetInteger(kKeyInitialAuthType, auth_type);
  user_dict->SetBoolean(kKeySignedIn, user->is_logged_in());
  user_dict->SetBoolean(kKeyIsOwner, is_owner);

  FillMultiProfileUserPrefs(user, user_dict, is_signin_to_add);
  FillKnownUserPrefs(user, user_dict);

  if (is_public_session) {
    AddPublicSessionDetailsToUserDictionaryEntry(
        user_dict, public_session_recommended_locales);
  }
}

// static
void UserSelectionScreen::FillKnownUserPrefs(user_manager::User* user,
                                             base::DictionaryValue* user_dict) {
  std::string gaia_id;
  if (user_manager::UserManager::Get()->FindGaiaID(user->email(), &gaia_id)) {
    user_dict->SetString(kKeyGaiaID, gaia_id);
  }
}

// static
void UserSelectionScreen::FillMultiProfileUserPrefs(
    user_manager::User* user,
    base::DictionaryValue* user_dict,
    bool is_signin_to_add) {
  const std::string& user_id = user->email();

  if (is_signin_to_add) {
    MultiProfileUserController* multi_profile_user_controller =
        ChromeUserManager::Get()->GetMultiProfileUserController();
    MultiProfileUserController::UserAllowedInSessionReason isUserAllowedReason;
    bool isUserAllowed = multi_profile_user_controller->IsUserAllowedInSession(
        user_id, &isUserAllowedReason);
    user_dict->SetBoolean(kKeyMultiProfilesAllowed, isUserAllowed);

    std::string behavior;
    switch (isUserAllowedReason) {
      case MultiProfileUserController::NOT_ALLOWED_OWNER_AS_SECONDARY:
        behavior = MultiProfileUserController::kBehaviorOwnerPrimaryOnly;
        break;
      default:
        behavior = multi_profile_user_controller->GetCachedValue(user_id);
    }
    user_dict->SetString(kKeyMultiProfilesPolicy, behavior);
  } else {
    user_dict->SetBoolean(kKeyMultiProfilesAllowed, true);
  }
}

// static
bool UserSelectionScreen::ShouldForceOnlineSignIn(
    const user_manager::User* user) {
  // Public sessions are always allowed to log in offline.
  // Supervised user are allowed to log in offline if their OAuth token status
  // is unknown or valid.
  // For all other users, force online sign in if:
  // * The flag to force online sign-in is set for the user.
  // * The user's OAuth token is invalid.
  // * The user's OAuth token status is unknown (except supervised users,
  //   see above).
  if (user->is_logged_in())
    return false;

  const user_manager::User::OAuthTokenStatus token_status =
      user->oauth_token_status();
  const bool is_supervised_user =
      user->GetType() == user_manager::USER_TYPE_SUPERVISED;
  const bool is_public_session =
      user->GetType() == user_manager::USER_TYPE_PUBLIC_ACCOUNT;

  if (is_supervised_user &&
      token_status == user_manager::User::OAUTH_TOKEN_STATUS_UNKNOWN) {
    return false;
  }

  if (is_public_session)
    return false;

  // At this point the reason for invalid token should be already set. If not,
  // this might be a leftover from an old version.
  if (token_status == user_manager::User::OAUTH2_TOKEN_STATUS_INVALID)
    RecordReauthReason(user->email(), ReauthReason::OTHER);

  return user->force_online_signin() ||
         (token_status == user_manager::User::OAUTH2_TOKEN_STATUS_INVALID) ||
         (token_status == user_manager::User::OAUTH_TOKEN_STATUS_UNKNOWN);
}

void UserSelectionScreen::SetHandler(LoginDisplayWebUIHandler* handler) {
  handler_ = handler;
}

void UserSelectionScreen::SetView(UserBoardView* view) {
  view_ = view;
}

void UserSelectionScreen::Init(const user_manager::UserList& users,
                               bool show_guest) {
  users_ = users;
  show_guest_ = show_guest;

  ui::UserActivityDetector* activity_detector = ui::UserActivityDetector::Get();
  if (!activity_detector->HasObserver(this))
    activity_detector->AddObserver(this);
}

void UserSelectionScreen::OnBeforeUserRemoved(const std::string& username) {
  for (user_manager::UserList::iterator it = users_.begin(); it != users_.end();
       ++it) {
    if ((*it)->email() == username) {
      users_.erase(it);
      break;
    }
  }
}

void UserSelectionScreen::OnUserRemoved(const std::string& username) {
  if (!handler_)
    return;
  handler_->OnUserRemoved(username, users_.empty());
}

void UserSelectionScreen::OnUserImageChanged(const user_manager::User& user) {
  if (!handler_)
    return;
  handler_->OnUserImageChanged(user);
  // TODO(antrim) : updateUserImage(user.email())
}

void UserSelectionScreen::OnPasswordClearTimerExpired() {
  if (handler_)
    handler_->ClearUserPodPassword();
}

void UserSelectionScreen::OnUserActivity(const ui::Event* event) {
  if (!password_clear_timer_.IsRunning()) {
    password_clear_timer_.Start(
        FROM_HERE,
        base::TimeDelta::FromSeconds(kPasswordClearTimeoutSec),
        this,
        &UserSelectionScreen::OnPasswordClearTimerExpired);
  }
  password_clear_timer_.Reset();
}

// static
const user_manager::UserList UserSelectionScreen::PrepareUserListForSending(
    const user_manager::UserList& users,
    std::string owner,
    bool is_signin_to_add) {
  user_manager::UserList users_to_send;
  bool has_owner = owner.size() > 0;
  size_t max_non_owner_users = has_owner ? kMaxUsers - 1 : kMaxUsers;
  size_t non_owner_count = 0;

  for (user_manager::UserList::const_iterator it = users.begin();
       it != users.end();
       ++it) {
    const std::string& user_id = (*it)->email();
    bool is_owner = (user_id == owner);
    bool is_public_account =
        ((*it)->GetType() == user_manager::USER_TYPE_PUBLIC_ACCOUNT);

    if ((is_public_account && !is_signin_to_add) || is_owner ||
        (!is_public_account && non_owner_count < max_non_owner_users)) {

      if (!is_owner)
        ++non_owner_count;
      if (is_owner && users_to_send.size() > kMaxUsers) {
        // Owner is always in the list.
        users_to_send.insert(users_to_send.begin() + (kMaxUsers - 1), *it);
        while (users_to_send.size() > kMaxUsers)
          users_to_send.erase(users_to_send.begin() + kMaxUsers);
      } else if (users_to_send.size() < kMaxUsers) {
        users_to_send.push_back(*it);
      }
    }
  }
  return users_to_send;
}

void UserSelectionScreen::SendUserList() {
  base::ListValue users_list;

  // TODO(nkostylev): Move to a separate method in UserManager.
  // http://crbug.com/230852
  bool single_user = users_.size() == 1;
  bool is_signin_to_add = LoginDisplayHostImpl::default_host() &&
                          user_manager::UserManager::Get()->IsUserLoggedIn();
  std::string owner;
  chromeos::CrosSettings::Get()->GetString(chromeos::kDeviceOwner, &owner);

  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  bool is_enterprise_managed = connector->IsEnterpriseManaged();

  const user_manager::UserList users_to_send =
      PrepareUserListForSending(users_, owner, is_signin_to_add);

  user_auth_type_map_.clear();

  const std::vector<std::string> kEmptyRecommendedLocales;
  for (user_manager::UserList::const_iterator it = users_to_send.begin();
       it != users_to_send.end();
       ++it) {
    const std::string& user_id = (*it)->email();
    bool is_owner = (user_id == owner);
    const bool is_public_account =
        ((*it)->GetType() == user_manager::USER_TYPE_PUBLIC_ACCOUNT);
    const AuthType initial_auth_type =
        is_public_account ? EXPAND_THEN_USER_CLICK
                          : (ShouldForceOnlineSignIn(*it) ? ONLINE_SIGN_IN
                                                          : OFFLINE_PASSWORD);
    user_auth_type_map_[user_id] = initial_auth_type;

    base::DictionaryValue* user_dict = new base::DictionaryValue();
    const std::vector<std::string>* public_session_recommended_locales =
        public_session_recommended_locales_.find(user_id) ==
            public_session_recommended_locales_.end() ?
                &kEmptyRecommendedLocales :
                &public_session_recommended_locales_[user_id];
    FillUserDictionary(*it,
                       is_owner,
                       is_signin_to_add,
                       initial_auth_type,
                       public_session_recommended_locales,
                       user_dict);
    bool signed_in = (*it)->is_logged_in();

    // Single user check here is necessary because owner info might not be
    // available when running into login screen on first boot.
    // See http://crosbug.com/12723
    bool can_remove_user =
        ((!single_user || is_enterprise_managed) && !user_id.empty() &&
         !is_owner && !is_public_account && !signed_in && !is_signin_to_add);
    user_dict->SetBoolean(kKeyCanRemove, can_remove_user);
    users_list.Append(user_dict);
  }

  handler_->LoadUsers(users_list, show_guest_);
}

void UserSelectionScreen::HandleGetUsers() {
  SendUserList();
}

void UserSelectionScreen::CheckUserStatus(const std::string& user_id) {
  // No checks on lock screen.
  if (ScreenLocker::default_screen_locker())
    return;

  if (!token_handle_util_.get()) {
    token_handle_util_.reset(
        new TokenHandleUtil(user_manager::UserManager::Get()));
  }

  if (token_handle_util_->HasToken(user_id)) {
    token_handle_util_->CheckToken(
        user_id, base::Bind(&UserSelectionScreen::OnUserStatusChecked,
                            weak_factory_.GetWeakPtr()));
  }
}

void UserSelectionScreen::OnUserStatusChecked(
    const user_manager::UserID& user_id,
    TokenHandleUtil::TokenHandleStatus status) {
  if (status == TokenHandleUtil::INVALID) {
    RecordReauthReason(user_id, ReauthReason::INVALID_TOKEN_HANDLE);
    token_handle_util_->MarkHandleInvalid(user_id);
    SetAuthType(user_id, ONLINE_SIGN_IN, base::string16());
  }
}

// EasyUnlock stuff

void UserSelectionScreen::SetAuthType(const std::string& user_id,
                                      AuthType auth_type,
                                      const base::string16& initial_value) {
  if (GetAuthType(user_id) == FORCE_OFFLINE_PASSWORD)
    return;
  DCHECK(GetAuthType(user_id) != FORCE_OFFLINE_PASSWORD ||
         auth_type == FORCE_OFFLINE_PASSWORD);
  user_auth_type_map_[user_id] = auth_type;
  view_->SetAuthType(user_id, auth_type, initial_value);
}

proximity_auth::ScreenlockBridge::LockHandler::AuthType
UserSelectionScreen::GetAuthType(const std::string& username) const {
  if (user_auth_type_map_.find(username) == user_auth_type_map_.end())
    return OFFLINE_PASSWORD;
  return user_auth_type_map_.find(username)->second;
}

proximity_auth::ScreenlockBridge::LockHandler::ScreenType
UserSelectionScreen::GetScreenType() const {
  if (display_type_ == OobeUI::kLockDisplay)
    return LOCK_SCREEN;

  if (display_type_ == OobeUI::kLoginDisplay)
    return SIGNIN_SCREEN;

  return OTHER_SCREEN;
}

void UserSelectionScreen::ShowBannerMessage(const base::string16& message) {
  view_->ShowBannerMessage(message);
}

void UserSelectionScreen::ShowUserPodCustomIcon(
    const std::string& user_id,
    const proximity_auth::ScreenlockBridge::UserPodCustomIconOptions&
        icon_options) {
  scoped_ptr<base::DictionaryValue> icon = icon_options.ToDictionaryValue();
  if (!icon || icon->empty())
    return;
  view_->ShowUserPodCustomIcon(user_id, *icon);
}

void UserSelectionScreen::HideUserPodCustomIcon(const std::string& user_id) {
  view_->HideUserPodCustomIcon(user_id);
}

void UserSelectionScreen::EnableInput() {
  // If Easy Unlock fails to unlock the screen, re-enable the password input.
  // This is only necessary on the lock screen, because the error handling for
  // the sign-in screen uses a different code path.
  if (ScreenLocker::default_screen_locker())
    ScreenLocker::default_screen_locker()->EnableInput();
}

void UserSelectionScreen::Unlock(const std::string& user_email) {
  DCHECK_EQ(GetScreenType(), LOCK_SCREEN);
  ScreenLocker::Hide();
}

void UserSelectionScreen::AttemptEasySignin(const std::string& user_id,
                                            const std::string& secret,
                                            const std::string& key_label) {
  DCHECK_EQ(GetScreenType(), SIGNIN_SCREEN);

  UserContext user_context(user_id);
  user_context.SetAuthFlow(UserContext::AUTH_FLOW_EASY_UNLOCK);
  user_context.SetKey(Key(secret));
  user_context.GetKey()->SetLabel(key_label);

  login_display_delegate_->Login(user_context, SigninSpecifics());
}

void UserSelectionScreen::HardLockPod(const std::string& user_id) {
  view_->SetAuthType(user_id, OFFLINE_PASSWORD, base::string16());
  EasyUnlockService* service = GetEasyUnlockServiceForUser(user_id);
  if (!service)
    return;
  service->SetHardlockState(EasyUnlockScreenlockStateHandler::USER_HARDLOCK);
}

void UserSelectionScreen::AttemptEasyUnlock(const std::string& user_id) {
  EasyUnlockService* service = GetEasyUnlockServiceForUser(user_id);
  if (!service)
    return;
  service->AttemptAuth(user_id);
}

void UserSelectionScreen::RecordClickOnLockIcon(const std::string& user_id) {
  EasyUnlockService* service = GetEasyUnlockServiceForUser(user_id);
  if (!service)
    return;
  service->RecordClickOnLockIcon();
}

EasyUnlockService* UserSelectionScreen::GetEasyUnlockServiceForUser(
    const std::string& user_id) const {
  if (GetScreenType() == OTHER_SCREEN)
    return nullptr;

  const user_manager::User* unlock_user = nullptr;
  for (const user_manager::User* user : users_) {
    if (user->email() == user_id) {
      unlock_user = user;
      break;
    }
  }
  if (!unlock_user)
    return nullptr;

  ProfileHelper* profile_helper = ProfileHelper::Get();
  Profile* profile = profile_helper->GetProfileByUser(unlock_user);

  // The user profile should exist if and only if this is the lock screen.
  DCHECK_EQ(!!profile, GetScreenType() == LOCK_SCREEN);

  if (!profile)
    profile = profile_helper->GetSigninProfile();

  return EasyUnlockService::Get(profile);
}

}  // namespace chromeos
