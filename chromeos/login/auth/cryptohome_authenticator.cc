// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/login/auth/cryptohome_authenticator.h"

#include <vector>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "chromeos/cryptohome/async_method_caller.h"
#include "chromeos/cryptohome/cryptohome_parameters.h"
#include "chromeos/cryptohome/homedir_methods.h"
#include "chromeos/cryptohome/system_salt_getter.h"
#include "chromeos/dbus/cryptohome_client.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/login/auth/auth_status_consumer.h"
#include "chromeos/login/auth/key.h"
#include "chromeos/login/auth/user_context.h"
#include "chromeos/login/login_state.h"
#include "chromeos/login/user_names.h"
#include "chromeos/login_event_recorder.h"
#include "components/device_event_log/device_event_log.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/user_manager/user_type.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

// The label used for the key derived from the user's GAIA credentials.
const char kCryptohomeGAIAKeyLabel[] = "gaia";

// The name under which the type of key generated from the user's GAIA
// credentials is stored.
const char kKeyProviderDataTypeName[] = "type";

// The name under which the salt used to generate a key from the user's GAIA
// credentials is stored.
const char kKeyProviderDataSaltName[] = "salt";

// Hashes |key| with |system_salt| if it its type is KEY_TYPE_PASSWORD_PLAIN.
// Returns the keys unmodified otherwise.
scoped_ptr<Key> TransformKeyIfNeeded(const Key& key,
                                     const std::string& system_salt) {
  scoped_ptr<Key> result(new Key(key));
  if (result->GetKeyType() == Key::KEY_TYPE_PASSWORD_PLAIN)
    result->Transform(Key::KEY_TYPE_SALTED_SHA256_TOP_HALF, system_salt);

  return result.Pass();
}

// Records status and calls resolver->Resolve().
void TriggerResolve(const base::WeakPtr<AuthAttemptState>& attempt,
                    scoped_refptr<CryptohomeAuthenticator> resolver,
                    bool success,
                    cryptohome::MountError return_code) {
  attempt->RecordCryptohomeStatus(success, return_code);
  resolver->Resolve();
}

// Records get hash status and calls resolver->Resolve().
void TriggerResolveHash(const base::WeakPtr<AuthAttemptState>& attempt,
                        scoped_refptr<CryptohomeAuthenticator> resolver,
                        bool success,
                        const std::string& username_hash) {
  if (success)
    attempt->RecordUsernameHash(username_hash);
  else
    attempt->RecordUsernameHashFailed();
  resolver->Resolve();
}

// Calls TriggerResolve while adding login time marker.
void TriggerResolveWithLoginTimeMarker(
    const std::string& marker_name,
    const base::WeakPtr<AuthAttemptState>& attempt,
    scoped_refptr<CryptohomeAuthenticator> resolver,
    bool success,
    cryptohome::MountError return_code) {
  chromeos::LoginEventRecorder::Get()->AddLoginTimeMarker(marker_name, false);
  TriggerResolve(attempt, resolver, success, return_code);
}

// Records an error in accessing the user's cryptohome with the given key and
// calls resolver->Resolve() after adding a login time marker.
void RecordKeyErrorAndResolve(const base::WeakPtr<AuthAttemptState>& attempt,
                              scoped_refptr<CryptohomeAuthenticator> resolver) {
  chromeos::LoginEventRecorder::Get()->AddLoginTimeMarker("CryptohomeMount-End",
                                                          false);
  attempt->RecordCryptohomeStatus(false /* success */,
                                  cryptohome::MOUNT_ERROR_KEY_FAILURE);
  resolver->Resolve();
}

// Callback invoked when cryptohome's MountEx() method has finished.
void OnMount(const base::WeakPtr<AuthAttemptState>& attempt,
             scoped_refptr<CryptohomeAuthenticator> resolver,
             bool success,
             cryptohome::MountError return_code,
             const std::string& mount_hash) {
  chromeos::LoginEventRecorder::Get()->AddLoginTimeMarker("CryptohomeMount-End",
                                                          false);
  attempt->RecordCryptohomeStatus(success, return_code);
  if (success)
    attempt->RecordUsernameHash(mount_hash);
  else
    attempt->RecordUsernameHashFailed();
  resolver->Resolve();
}

// Calls cryptohome's MountEx() method. The key in |attempt->user_context| must
// not be a plain text password. If the user provided a plain text password,
// that password must be transformed to another key type (by salted hashing)
// before calling this method.
void DoMount(const base::WeakPtr<AuthAttemptState>& attempt,
             scoped_refptr<CryptohomeAuthenticator> resolver,
             bool ephemeral,
             bool create_if_nonexistent) {
  const Key* key = attempt->user_context.GetKey();
  // If the |key| is a plain text password, crash rather than attempting to
  // mount the cryptohome with a plain text password.
  CHECK_NE(Key::KEY_TYPE_PASSWORD_PLAIN, key->GetKeyType());

  // Set state that username_hash is requested here so that test implementation
  // that returns directly would not generate 2 OnLoginSucces() calls.
  attempt->UsernameHashRequested();

  // Set the authentication's key label to an empty string, which is a wildcard
  // allowing any key to match. This is necessary because cryptohomes created by
  // Chrome OS M38 and older will have a legacy key with no label while those
  // created by Chrome OS M39 and newer will have a key with the label
  // kCryptohomeGAIAKeyLabel.
  const cryptohome::KeyDefinition auth_key(key->GetSecret(),
                                           std::string(),
                                           cryptohome::PRIV_DEFAULT);
  cryptohome::MountParameters mount(ephemeral);
  if (create_if_nonexistent) {
    mount.create_keys.push_back(cryptohome::KeyDefinition(
        key->GetSecret(),
        kCryptohomeGAIAKeyLabel,
        cryptohome::PRIV_DEFAULT));
  }

  cryptohome::HomedirMethods::GetInstance()->MountEx(
      cryptohome::Identification(attempt->user_context.GetUserID()),
      cryptohome::Authorization(auth_key),
      mount,
      base::Bind(&OnMount, attempt, resolver));
}

// Callback invoked when the system salt has been retrieved. Transforms the key
// in |attempt->user_context| using Chrome's default hashing algorithm and the
// system salt, then calls MountEx().
void OnGetSystemSalt(const base::WeakPtr<AuthAttemptState>& attempt,
                     scoped_refptr<CryptohomeAuthenticator> resolver,
                     bool ephemeral,
                     bool create_if_nonexistent,
                     const std::string& system_salt) {
  DCHECK_EQ(Key::KEY_TYPE_PASSWORD_PLAIN,
            attempt->user_context.GetKey()->GetKeyType());

  attempt->user_context.GetKey()->Transform(
      Key::KEY_TYPE_SALTED_SHA256_TOP_HALF,
      system_salt);

  DoMount(attempt, resolver, ephemeral, create_if_nonexistent);
}

// Callback invoked when cryptohome's GetKeyDataEx() method has finished.
// * If GetKeyDataEx() returned metadata indicating the hashing algorithm and
//   salt that were used to generate the key for this user's cryptohome,
//   transforms the key in |attempt->user_context| with the same parameters.
// * Otherwise, starts the retrieval of the system salt so that the key in
//   |attempt->user_context| can be transformed with Chrome's default hashing
//   algorithm and the system salt.
// The resulting key is then passed to cryptohome's MountEx().
void OnGetKeyDataEx(
    const base::WeakPtr<AuthAttemptState>& attempt,
    scoped_refptr<CryptohomeAuthenticator> resolver,
    bool ephemeral,
    bool create_if_nonexistent,
    bool success,
    cryptohome::MountError return_code,
    const std::vector<cryptohome::KeyDefinition>& key_definitions) {
  if (success) {
    if (key_definitions.size() == 1) {
      const cryptohome::KeyDefinition& key_definition = key_definitions.front();
      DCHECK_EQ(kCryptohomeGAIAKeyLabel, key_definition.label);

      // Extract the key type and salt from |key_definition|, if present.
      scoped_ptr<int64> type;
      scoped_ptr<std::string> salt;
      for (std::vector<cryptohome::KeyDefinition::ProviderData>::
               const_iterator it = key_definition.provider_data.begin();
           it != key_definition.provider_data.end(); ++it) {
        if (it->name == kKeyProviderDataTypeName) {
          if (it->number)
            type.reset(new int64(*it->number));
          else
            NOTREACHED();
        } else if (it->name == kKeyProviderDataSaltName) {
          if (it->bytes)
            salt.reset(new std::string(*it->bytes));
          else
            NOTREACHED();
        }
      }

      if (type) {
        if (*type < 0 || *type >= Key::KEY_TYPE_COUNT) {
          LOGIN_LOG(ERROR) << "Invalid key type: " << *type;
          RecordKeyErrorAndResolve(attempt, resolver);
          return;
        }

        if (!salt) {
          LOGIN_LOG(ERROR) << "Missing salt.";
          RecordKeyErrorAndResolve(attempt, resolver);
          return;
        }

        attempt->user_context.GetKey()->Transform(
            static_cast<Key::KeyType>(*type),
            *salt);
        DoMount(attempt, resolver, ephemeral, create_if_nonexistent);
        return;
      }
    } else {
      LOGIN_LOG(EVENT) << "GetKeyDataEx() returned " << key_definitions.size()
                       << " entries.";
    }
  }

  SystemSaltGetter::Get()->GetSystemSalt(base::Bind(&OnGetSystemSalt,
                                                    attempt,
                                                    resolver,
                                                    ephemeral,
                                                    create_if_nonexistent));
}

// Starts the process that will mount a user's cryptohome.
// * If the key in |attempt->user_context| is not a plain text password,
//   cryptohome's MountEx() method is called directly with the key.
// * Otherwise, the key must be transformed (by salted hashing) before being
//   passed to MountEx(). In that case, cryptohome's GetKeyDataEx() method is
//   called to retrieve metadata indicating the hashing algorithm and salt that
//   were used to generate the key for this user's cryptohome and the key is
//   transformed accordingly before calling MountEx().
void StartMount(const base::WeakPtr<AuthAttemptState>& attempt,
                scoped_refptr<CryptohomeAuthenticator> resolver,
                bool ephemeral,
                bool create_if_nonexistent) {
  chromeos::LoginEventRecorder::Get()->AddLoginTimeMarker(
      "CryptohomeMount-Start", false);

  if (attempt->user_context.GetKey()->GetKeyType() !=
          Key::KEY_TYPE_PASSWORD_PLAIN) {
    DoMount(attempt, resolver, ephemeral, create_if_nonexistent);
    return;
  }

  cryptohome::HomedirMethods::GetInstance()->GetKeyDataEx(
      cryptohome::Identification(attempt->user_context.GetUserID()),
      kCryptohomeGAIAKeyLabel,
      base::Bind(&OnGetKeyDataEx,
                 attempt,
                 resolver,
                 ephemeral,
                 create_if_nonexistent));
}

// Calls cryptohome's mount method for guest and also get the user hash from
// cryptohome.
void MountGuestAndGetHash(const base::WeakPtr<AuthAttemptState>& attempt,
                          scoped_refptr<CryptohomeAuthenticator> resolver) {
  attempt->UsernameHashRequested();
  cryptohome::AsyncMethodCaller::GetInstance()->AsyncMountGuest(
      base::Bind(&TriggerResolveWithLoginTimeMarker,
                 "CryptohomeMount-End",
                 attempt,
                 resolver));
  cryptohome::AsyncMethodCaller::GetInstance()->AsyncGetSanitizedUsername(
      attempt->user_context.GetUserID(),
      base::Bind(&TriggerResolveHash, attempt, resolver));
}

// Calls cryptohome's MountPublic method
void MountPublic(const base::WeakPtr<AuthAttemptState>& attempt,
                 scoped_refptr<CryptohomeAuthenticator> resolver,
                 int flags) {
  cryptohome::AsyncMethodCaller::GetInstance()->AsyncMountPublic(
      attempt->user_context.GetUserID(),
      flags,
      base::Bind(&TriggerResolveWithLoginTimeMarker,
                 "CryptohomeMountPublic-End",
                 attempt,
                 resolver));
  cryptohome::AsyncMethodCaller::GetInstance()->AsyncGetSanitizedUsername(
      attempt->user_context.GetUserID(),
      base::Bind(&TriggerResolveHash, attempt, resolver));
}

// Calls cryptohome's key migration method.
void Migrate(const base::WeakPtr<AuthAttemptState>& attempt,
             scoped_refptr<CryptohomeAuthenticator> resolver,
             bool passing_old_hash,
             const std::string& old_password,
             const std::string& system_salt) {
  chromeos::LoginEventRecorder::Get()->AddLoginTimeMarker(
      "CryptohomeMigrate-Start", false);
  cryptohome::AsyncMethodCaller* caller =
      cryptohome::AsyncMethodCaller::GetInstance();

  // TODO(bartfab): Retrieve the hashing algorithm and salt to use for |old_key|
  // from cryptohomed.
  scoped_ptr<Key> old_key =
      TransformKeyIfNeeded(Key(old_password), system_salt);
  scoped_ptr<Key> new_key =
      TransformKeyIfNeeded(*attempt->user_context.GetKey(), system_salt);
  if (passing_old_hash) {
    caller->AsyncMigrateKey(attempt->user_context.GetUserID(),
                            old_key->GetSecret(),
                            new_key->GetSecret(),
                            base::Bind(&TriggerResolveWithLoginTimeMarker,
                                       "CryptohomeMount-End",
                                       attempt,
                                       resolver));
  } else {
    caller->AsyncMigrateKey(attempt->user_context.GetUserID(),
                            new_key->GetSecret(),
                            old_key->GetSecret(),
                            base::Bind(&TriggerResolveWithLoginTimeMarker,
                                       "CryptohomeMount-End",
                                       attempt,
                                       resolver));
  }
}

// Calls cryptohome's remove method.
void Remove(const base::WeakPtr<AuthAttemptState>& attempt,
            scoped_refptr<CryptohomeAuthenticator> resolver) {
  chromeos::LoginEventRecorder::Get()->AddLoginTimeMarker(
      "CryptohomeRemove-Start", false);
  cryptohome::AsyncMethodCaller::GetInstance()->AsyncRemove(
      attempt->user_context.GetUserID(),
      base::Bind(&TriggerResolveWithLoginTimeMarker,
                 "CryptohomeRemove-End",
                 attempt,
                 resolver));
}

// Calls cryptohome's key check method.
void CheckKey(const base::WeakPtr<AuthAttemptState>& attempt,
              scoped_refptr<CryptohomeAuthenticator> resolver,
              const std::string& system_salt) {
  scoped_ptr<Key> key =
      TransformKeyIfNeeded(*attempt->user_context.GetKey(), system_salt);
  cryptohome::AsyncMethodCaller::GetInstance()->AsyncCheckKey(
      attempt->user_context.GetUserID(),
      key->GetSecret(),
      base::Bind(&TriggerResolve, attempt, resolver));
}

}  // namespace

CryptohomeAuthenticator::CryptohomeAuthenticator(
    scoped_refptr<base::TaskRunner> task_runner,
    AuthStatusConsumer* consumer)
    : Authenticator(consumer),
      task_runner_(task_runner),
      migrate_attempted_(false),
      remove_attempted_(false),
      resync_attempted_(false),
      ephemeral_mount_attempted_(false),
      check_key_attempted_(false),
      already_reported_success_(false),
      owner_is_verified_(false),
      user_can_login_(false),
      remove_user_data_on_failure_(false),
      delayed_login_failure_(NULL) {
}

void CryptohomeAuthenticator::AuthenticateToLogin(
    content::BrowserContext* context,
    const UserContext& user_context) {
  DCHECK_EQ(user_manager::USER_TYPE_REGULAR, user_context.GetUserType());
  authentication_context_ = context;
  current_state_.reset(new AuthAttemptState(user_context,
                                            false,  // unlock
                                            false,  // online_complete
                                            !IsKnownUser(user_context)));
  // Reset the verified flag.
  owner_is_verified_ = false;

  StartMount(current_state_->AsWeakPtr(),
             scoped_refptr<CryptohomeAuthenticator>(this),
             false /* ephemeral */, false /* create_if_nonexistent */);
}

void CryptohomeAuthenticator::CompleteLogin(content::BrowserContext* context,
                                            const UserContext& user_context) {
  DCHECK_EQ(user_manager::USER_TYPE_REGULAR, user_context.GetUserType());
  authentication_context_ = context;
  current_state_.reset(new AuthAttemptState(user_context,
                                            true,   // unlock
                                            false,  // online_complete
                                            !IsKnownUser(user_context)));

  // Reset the verified flag.
  owner_is_verified_ = false;

  StartMount(current_state_->AsWeakPtr(),
             scoped_refptr<CryptohomeAuthenticator>(this),
             false /* ephemeral */, false /* create_if_nonexistent */);

  // For login completion from extension, we just need to resolve the current
  // auth attempt state, the rest of OAuth related tasks will be done in
  // parallel.
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&CryptohomeAuthenticator::ResolveLoginCompletionStatus, this));
}

void CryptohomeAuthenticator::AuthenticateToUnlock(
    const UserContext& user_context) {
  DCHECK_EQ(user_manager::USER_TYPE_REGULAR, user_context.GetUserType());
  current_state_.reset(new AuthAttemptState(user_context,
                                            true,     // unlock
                                            true,     // online_complete
                                            false));  // user_is_new
  remove_user_data_on_failure_ = false;
  check_key_attempted_ = true;
  SystemSaltGetter::Get()->GetSystemSalt(
      base::Bind(&CheckKey, current_state_->AsWeakPtr(),
                 scoped_refptr<CryptohomeAuthenticator>(this)));
}

void CryptohomeAuthenticator::LoginAsSupervisedUser(
    const UserContext& user_context) {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  DCHECK_EQ(user_manager::USER_TYPE_SUPERVISED, user_context.GetUserType());

  // TODO(nkostylev): Pass proper value for |user_is_new| or remove (not used).
  current_state_.reset(new AuthAttemptState(user_context,
                                            false,    // unlock
                                            false,    // online_complete
                                            false));  // user_is_new
  remove_user_data_on_failure_ = false;
  StartMount(current_state_->AsWeakPtr(),
             scoped_refptr<CryptohomeAuthenticator>(this),
             false /* ephemeral */, false /* create_if_nonexistent */);
}

void CryptohomeAuthenticator::LoginOffTheRecord() {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  current_state_.reset(
      new AuthAttemptState(UserContext(user_manager::USER_TYPE_GUEST,
                                       login::GuestAccountId().GetUserEmail()),
                           false,    // unlock
                           false,    // online_complete
                           false));  // user_is_new
  remove_user_data_on_failure_ = false;
  ephemeral_mount_attempted_ = true;
  MountGuestAndGetHash(current_state_->AsWeakPtr(),
                       scoped_refptr<CryptohomeAuthenticator>(this));
}

void CryptohomeAuthenticator::LoginAsPublicSession(
    const UserContext& user_context) {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  DCHECK_EQ(user_manager::USER_TYPE_PUBLIC_ACCOUNT, user_context.GetUserType());

  current_state_.reset(
      new AuthAttemptState(user_context,
                           false,    // unlock
                           false,    // online_complete
                           false));  // user_is_new
  remove_user_data_on_failure_ = false;
  ephemeral_mount_attempted_ = true;
  StartMount(current_state_->AsWeakPtr(),
             scoped_refptr<CryptohomeAuthenticator>(this), true /* ephemeral */,
             true /* create_if_nonexistent */);
}

void CryptohomeAuthenticator::LoginAsKioskAccount(
    const std::string& app_user_id,
    bool use_guest_mount) {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());

  const std::string user_id =
      use_guest_mount ? login::GuestAccountId().GetUserEmail() : app_user_id;
  current_state_.reset(new AuthAttemptState(
      UserContext(user_manager::USER_TYPE_KIOSK_APP, user_id),
      false,    // unlock
      false,    // online_complete
      false));  // user_is_new

  remove_user_data_on_failure_ = true;
  if (!use_guest_mount) {
    MountPublic(current_state_->AsWeakPtr(),
                scoped_refptr<CryptohomeAuthenticator>(this),
                cryptohome::CREATE_IF_MISSING);
  } else {
    ephemeral_mount_attempted_ = true;
    MountGuestAndGetHash(current_state_->AsWeakPtr(),
                         scoped_refptr<CryptohomeAuthenticator>(this));
  }
}

void CryptohomeAuthenticator::OnAuthSuccess() {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  VLOG(1) << "Login success";
  // Send notification of success
  chromeos::LoginEventRecorder::Get()->RecordAuthenticationSuccess();
  {
    base::AutoLock for_this_block(success_lock_);
    already_reported_success_ = true;
  }
  if (consumer_)
    consumer_->OnAuthSuccess(current_state_->user_context);
}

void CryptohomeAuthenticator::OnOffTheRecordAuthSuccess() {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  chromeos::LoginEventRecorder::Get()->RecordAuthenticationSuccess();
  if (consumer_)
    consumer_->OnOffTheRecordAuthSuccess();
}

void CryptohomeAuthenticator::OnPasswordChangeDetected() {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  if (consumer_)
    consumer_->OnPasswordChangeDetected();
}

void CryptohomeAuthenticator::OnAuthFailure(const AuthFailure& error) {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());

  // OnAuthFailure will be called again with the same |error|
  // after the cryptohome has been removed.
  if (remove_user_data_on_failure_) {
    delayed_login_failure_ = &error;
    RemoveEncryptedData();
    return;
  }
  chromeos::LoginEventRecorder::Get()->RecordAuthenticationFailure();
  LOGIN_LOG(ERROR) << "Login failed: " << error.GetErrorString();
  if (consumer_)
    consumer_->OnAuthFailure(error);
}

void CryptohomeAuthenticator::RecoverEncryptedData(
    const std::string& old_password) {
  migrate_attempted_ = true;
  current_state_->ResetCryptohomeStatus();
  SystemSaltGetter::Get()->GetSystemSalt(base::Bind(
      &Migrate, current_state_->AsWeakPtr(),
      scoped_refptr<CryptohomeAuthenticator>(this), true, old_password));
}

void CryptohomeAuthenticator::RemoveEncryptedData() {
  remove_attempted_ = true;
  current_state_->ResetCryptohomeStatus();
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&Remove, current_state_->AsWeakPtr(),
                            scoped_refptr<CryptohomeAuthenticator>(this)));
}

void CryptohomeAuthenticator::ResyncEncryptedData() {
  resync_attempted_ = true;
  current_state_->ResetCryptohomeStatus();
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&Remove, current_state_->AsWeakPtr(),
                            scoped_refptr<CryptohomeAuthenticator>(this)));
}

bool CryptohomeAuthenticator::VerifyOwner() {
  if (owner_is_verified_)
    return true;
  // Check if policy data is fine and continue in safe mode if needed.
  if (!IsSafeMode()) {
    // Now we can continue with the login and report mount success.
    user_can_login_ = true;
    owner_is_verified_ = true;
    return true;
  }

  CheckSafeModeOwnership(
      current_state_->user_context,
      base::Bind(&CryptohomeAuthenticator::OnOwnershipChecked, this));
  return false;
}

void CryptohomeAuthenticator::OnOwnershipChecked(bool is_owner) {
  // Now we can check if this user is the owner.
  user_can_login_ = is_owner;
  owner_is_verified_ = true;
  Resolve();
}

void CryptohomeAuthenticator::Resolve() {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  bool create_if_nonexistent = false;
  CryptohomeAuthenticator::AuthState state = ResolveState();
  VLOG(1) << "Resolved state to: " << state;
  switch (state) {
    case CONTINUE:
    case POSSIBLE_PW_CHANGE:
    case NO_MOUNT:
      // These are intermediate states; we need more info from a request that
      // is still pending.
      break;
    case FAILED_MOUNT:
      // In this case, whether login succeeded or not, we can't log
      // the user in because their data is horked.  So, override with
      // the appropriate failure.
      task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&CryptohomeAuthenticator::OnAuthFailure,
                     this,
                     AuthFailure(AuthFailure::COULD_NOT_MOUNT_CRYPTOHOME)));
      break;
    case FAILED_REMOVE:
      // In this case, we tried to remove the user's old cryptohome at her
      // request, and the remove failed.
      remove_user_data_on_failure_ = false;
      task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&CryptohomeAuthenticator::OnAuthFailure,
                     this,
                     AuthFailure(AuthFailure::DATA_REMOVAL_FAILED)));
      break;
    case FAILED_TMPFS:
      // In this case, we tried to mount a tmpfs for guest and failed.
      task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&CryptohomeAuthenticator::OnAuthFailure,
                     this,
                     AuthFailure(AuthFailure::COULD_NOT_MOUNT_TMPFS)));
      break;
    case FAILED_TPM:
      // In this case, we tried to create/mount cryptohome and failed
      // because of the critical TPM error.
      // Chrome will notify user and request reboot.
      task_runner_->PostTask(FROM_HERE,
                             base::Bind(&CryptohomeAuthenticator::OnAuthFailure,
                                        this,
                                        AuthFailure(AuthFailure::TPM_ERROR)));
      break;
    case FAILED_USERNAME_HASH:
      // In this case, we failed the GetSanitizedUsername request to
      // cryptohomed. This can happen for any login attempt.
      task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&CryptohomeAuthenticator::OnAuthFailure,
                     this,
                     AuthFailure(AuthFailure::USERNAME_HASH_FAILED)));
      break;
    case REMOVED_DATA_AFTER_FAILURE:
      remove_user_data_on_failure_ = false;
      task_runner_->PostTask(FROM_HERE,
                             base::Bind(&CryptohomeAuthenticator::OnAuthFailure,
                                        this,
                                        *delayed_login_failure_));
      break;
    case CREATE_NEW:
      create_if_nonexistent = true;
    case RECOVER_MOUNT:
      current_state_->ResetCryptohomeStatus();
      StartMount(current_state_->AsWeakPtr(),
                 scoped_refptr<CryptohomeAuthenticator>(this),
                 false /*ephemeral*/, create_if_nonexistent);
      break;
    case NEED_OLD_PW:
      task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&CryptohomeAuthenticator::OnPasswordChangeDetected, this));
      break;
    case ONLINE_FAILED:
    case NEED_NEW_PW:
    case HAVE_NEW_PW:
      NOTREACHED() << "Using obsolete ClientLogin code path.";
      break;
    case OFFLINE_LOGIN:
      VLOG(2) << "Offline login";
    // Fall through.
    case UNLOCK:
      VLOG(2) << "Unlock";
    // Fall through.
    case ONLINE_LOGIN:
      VLOG(2) << "Online login";
      task_runner_->PostTask(
          FROM_HERE, base::Bind(&CryptohomeAuthenticator::OnAuthSuccess, this));
      break;
    case GUEST_LOGIN:
      task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&CryptohomeAuthenticator::OnOffTheRecordAuthSuccess,
                     this));
      break;
    case KIOSK_ACCOUNT_LOGIN:
    case PUBLIC_ACCOUNT_LOGIN:
      current_state_->user_context.SetIsUsingOAuth(false);
      task_runner_->PostTask(
          FROM_HERE, base::Bind(&CryptohomeAuthenticator::OnAuthSuccess, this));
      break;
    case SUPERVISED_USER_LOGIN:
      current_state_->user_context.SetIsUsingOAuth(false);
      task_runner_->PostTask(
          FROM_HERE, base::Bind(&CryptohomeAuthenticator::OnAuthSuccess, this));
      break;
    case LOGIN_FAILED:
      current_state_->ResetCryptohomeStatus();
      task_runner_->PostTask(FROM_HERE,
                             base::Bind(&CryptohomeAuthenticator::OnAuthFailure,
                                        this,
                                        current_state_->online_outcome()));
      break;
    case OWNER_REQUIRED: {
      current_state_->ResetCryptohomeStatus();
      bool success = false;
      DBusThreadManager::Get()->GetCryptohomeClient()->Unmount(&success);
      if (!success) {
        // Maybe we should reboot immediately here?
        LOGIN_LOG(ERROR) << "Couldn't unmount users home!";
      }
      task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&CryptohomeAuthenticator::OnAuthFailure,
                     this,
                     AuthFailure(AuthFailure::OWNER_REQUIRED)));
      break;
    }
    default:
      NOTREACHED();
      break;
  }
}

CryptohomeAuthenticator::~CryptohomeAuthenticator() {
}

CryptohomeAuthenticator::AuthState CryptohomeAuthenticator::ResolveState() {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  // If we haven't mounted the user's home dir yet or
  // haven't got sanitized username value, we can't be done.
  // We never get past here if any of these two cryptohome ops is still pending.
  // This is an important invariant.
  if (!current_state_->cryptohome_complete() ||
      !current_state_->username_hash_obtained()) {
    return CONTINUE;
  }

  AuthState state = CONTINUE;

  if (current_state_->cryptohome_outcome() &&
      current_state_->username_hash_valid()) {
    state = ResolveCryptohomeSuccessState();
  } else {
    state = ResolveCryptohomeFailureState();
    LOGIN_LOG(ERROR) << "Cryptohome failure: "
                     << "state=" << state
                     << ", code=" << current_state_->cryptohome_code();
  }

  DCHECK(current_state_->cryptohome_complete());  // Ensure invariant holds.
  migrate_attempted_ = false;
  remove_attempted_ = false;
  resync_attempted_ = false;
  ephemeral_mount_attempted_ = false;
  check_key_attempted_ = false;

  if (state != POSSIBLE_PW_CHANGE && state != NO_MOUNT &&
      state != OFFLINE_LOGIN)
    return state;

  if (current_state_->online_complete()) {
    if (current_state_->online_outcome().reason() == AuthFailure::NONE) {
      // Online attempt succeeded as well, so combine the results.
      return ResolveOnlineSuccessState(state);
    }
    NOTREACHED() << "Using obsolete ClientLogin code path.";
  }
  // if online isn't complete yet, just return the offline result.
  return state;
}

CryptohomeAuthenticator::AuthState
CryptohomeAuthenticator::ResolveCryptohomeFailureState() {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  if (remove_attempted_ || resync_attempted_)
    return FAILED_REMOVE;
  if (ephemeral_mount_attempted_)
    return FAILED_TMPFS;
  if (migrate_attempted_)
    return NEED_OLD_PW;
  if (check_key_attempted_)
    return LOGIN_FAILED;

  if (current_state_->cryptohome_code() ==
      cryptohome::MOUNT_ERROR_TPM_NEEDS_REBOOT) {
    // Critical TPM error detected, reboot needed.
    return FAILED_TPM;
  }

  // Return intermediate states in the following case:
  // when there is an online result to use;
  // This is the case after user finishes Gaia login;
  if (current_state_->online_complete()) {
    if (current_state_->cryptohome_code() ==
        cryptohome::MOUNT_ERROR_KEY_FAILURE) {
      // If we tried a mount but they used the wrong key, we may need to
      // ask the user for her old password.  We'll only know once we've
      // done the online check.
      return POSSIBLE_PW_CHANGE;
    }
    if (current_state_->cryptohome_code() ==
        cryptohome::MOUNT_ERROR_USER_DOES_NOT_EXIST) {
      // If we tried a mount but the user did not exist, then we should wait
      // for online login to succeed and try again with the "create" flag set.
      return NO_MOUNT;
    }
  }

  if (!current_state_->username_hash_valid())
    return FAILED_USERNAME_HASH;

  return FAILED_MOUNT;
}

CryptohomeAuthenticator::AuthState
CryptohomeAuthenticator::ResolveCryptohomeSuccessState() {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  if (resync_attempted_)
    return CREATE_NEW;
  if (remove_attempted_)
    return REMOVED_DATA_AFTER_FAILURE;
  if (migrate_attempted_)
    return RECOVER_MOUNT;
  if (check_key_attempted_)
    return UNLOCK;

  const user_manager::UserType user_type =
      current_state_->user_context.GetUserType();
  if (user_type == user_manager::USER_TYPE_GUEST)
    return GUEST_LOGIN;
  if (user_type == user_manager::USER_TYPE_PUBLIC_ACCOUNT)
    return PUBLIC_ACCOUNT_LOGIN;
  if (user_type == user_manager::USER_TYPE_KIOSK_APP)
    return KIOSK_ACCOUNT_LOGIN;
  if (user_type == user_manager::USER_TYPE_SUPERVISED)
    return SUPERVISED_USER_LOGIN;

  if (!VerifyOwner())
    return CONTINUE;
  return user_can_login_ ? OFFLINE_LOGIN : OWNER_REQUIRED;
}

CryptohomeAuthenticator::AuthState
CryptohomeAuthenticator::ResolveOnlineSuccessState(
    CryptohomeAuthenticator::AuthState offline_state) {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  switch (offline_state) {
    case POSSIBLE_PW_CHANGE:
      return NEED_OLD_PW;
    case NO_MOUNT:
      return CREATE_NEW;
    case OFFLINE_LOGIN:
      return ONLINE_LOGIN;
    default:
      NOTREACHED();
      return offline_state;
  }
}

void CryptohomeAuthenticator::ResolveLoginCompletionStatus() {
  // Shortcut online state resolution process.
  current_state_->RecordOnlineLoginStatus(AuthFailure::AuthFailureNone());
  Resolve();
}

void CryptohomeAuthenticator::SetOwnerState(bool owner_check_finished,
                                            bool check_result) {
  owner_is_verified_ = owner_check_finished;
  user_can_login_ = check_result;
}

}  // namespace chromeos
