// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_garbage_collector_chromeos.h"

#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/prefs/scoped_user_pref_update.h"
#include "base/prefs/testing_pref_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/values.h"
#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/scoped_user_manager_enabler.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/extensions/extension_assets_manager_chromeos.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_service_test_base.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chromeos/login/user_names.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/install_flag.h"
#include "extensions/common/manifest_constants.h"

namespace {
const char kExtensionId1[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char kExtensionId2[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
}  // namespace

namespace extensions {

class ExtensionGarbageCollectorChromeOSUnitTest
    : public ExtensionServiceTestBase {
 protected:
  const base::FilePath& cache_dir() { return cache_dir_.path(); }

  void SetUp() override {
#if defined(ENABLE_PLUGINS)
    content::PluginService::GetInstance()->Init();
#endif
    InitializeGoodInstalledExtensionService();

    // Need real IO thread.
    service_->SetFileTaskRunnerForTesting(
        content::BrowserThread::GetBlockingPool()
            ->GetSequencedTaskRunnerWithShutdownBehavior(
                content::BrowserThread::GetBlockingPool()
                    ->GetNamedSequenceToken("ext_install-"),
                base::SequencedWorkerPool::SKIP_ON_SHUTDOWN));

    CHECK(cache_dir_.CreateUniqueTempDir());
    ExtensionAssetsManagerChromeOS::SetSharedInstallDirForTesting(cache_dir());
    ExtensionGarbageCollectorChromeOS::ClearGarbageCollectedForTesting();

    // Initialize the UserManager singleton to a fresh FakeChromeUserManager
    // instance.
    user_manager_enabler_.reset(new chromeos::ScopedUserManagerEnabler(
        new chromeos::FakeChromeUserManager));

    GetFakeUserManager()->AddUser(chromeos::login::kStubUser);
    GetFakeUserManager()->LoginUser(chromeos::login::kStubUser);
    chromeos::ProfileHelper::Get()->SetUserToProfileMappingForTesting(
        GetFakeUserManager()->GetActiveUser(), profile_.get());
  }

  void GarbageCollectExtensions() {
    ExtensionGarbageCollector::Get(profile_.get())
        ->GarbageCollectExtensionsForTest();
    // Wait for GarbageCollectExtensions task to complete.
    content::RunAllBlockingPoolTasksUntilIdle();
  }

  base::FilePath CreateSharedExtensionDir(const std::string& id,
                                          const std::string& version,
                                          const base::FilePath& shared_dir) {
    base::FilePath path = shared_dir.AppendASCII(id).AppendASCII(version);
    CreateDirectory(path);
    return path;
  }

  void CreateSharedExtensionPrefs(const std::string& id,
                                  const std::string& version,
                                  const std::string& users_string,
                                  const base::FilePath& path) {
    DictionaryPrefUpdate shared_extensions(testing_local_state_.Get(),
        ExtensionAssetsManagerChromeOS::kSharedExtensions);

    base::DictionaryValue* extension_info = NULL;
    if (!shared_extensions->GetDictionary(id, &extension_info)) {
      extension_info = new base::DictionaryValue;
      shared_extensions->Set(id, extension_info);
    }

    base::DictionaryValue* version_info = new base::DictionaryValue;
    extension_info->SetWithoutPathExpansion(version, version_info);
    version_info->SetString(
        ExtensionAssetsManagerChromeOS::kSharedExtensionPath, path.value());

    base::ListValue* users = new base::ListValue;
    version_info->Set(ExtensionAssetsManagerChromeOS::kSharedExtensionUsers,
                      users);
    for (const std::string& user :
         base::SplitString(users_string, ",",
                           base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY))
      users->AppendString(user);
  }

  scoped_refptr<Extension> CreateExtension(const std::string& id,
                                           const std::string& version,
                                           const base::FilePath& path) {
    base::DictionaryValue manifest;
    manifest.SetString(manifest_keys::kName, "test");
    manifest.SetString(manifest_keys::kVersion, version);

    std::string error;
    scoped_refptr<Extension> extension = Extension::Create(
        path, Manifest::INTERNAL, manifest, Extension::NO_FLAGS, id, &error);
    CHECK(extension.get()) << error;
    CHECK_EQ(id, extension->id());

    return extension;
  }

  ExtensionPrefs* GetExtensionPrefs() {
    return ExtensionPrefs::Get(profile_.get());
  }

  chromeos::FakeChromeUserManager* GetFakeUserManager() {
    return static_cast<chromeos::FakeChromeUserManager*>(
        user_manager::UserManager::Get());
  }

 private:
  scoped_ptr<chromeos::ScopedUserManagerEnabler> user_manager_enabler_;
  base::ScopedTempDir cache_dir_;
};

// Test shared extensions clean up.
TEST_F(ExtensionGarbageCollectorChromeOSUnitTest, SharedExtensions) {
  // Version for non-existing user.
  base::FilePath path_id1_1 = CreateSharedExtensionDir(
      kExtensionId1, "1.0", cache_dir());
  CreateSharedExtensionPrefs(kExtensionId1, "1.0", "test@test.com", path_id1_1);
  EXPECT_TRUE(base::PathExists(path_id1_1));

  // Version for current user but the extension is not installed.
  base::FilePath path_id1_2 = CreateSharedExtensionDir(
      kExtensionId1, "2.0", cache_dir());
  CreateSharedExtensionPrefs(kExtensionId1, "2.0",
                             chromeos::login::StubAccountId().GetUserEmail(),
                             path_id1_2);
  EXPECT_TRUE(base::PathExists(path_id1_2));

  // Version for current user that delayed install.
  base::FilePath path_id2_1 = CreateSharedExtensionDir(
      kExtensionId2, "1.0", cache_dir());
  CreateSharedExtensionPrefs(kExtensionId2, "1.0",
                             chromeos::login::StubAccountId().GetUserEmail(),
                             path_id2_1);
  scoped_refptr<Extension> extension2 = CreateExtension(kExtensionId2, "1.0",
                                                        path_id2_1);
  GetExtensionPrefs()->SetDelayedInstallInfo(
      extension2.get(),
      Extension::ENABLED,
      kInstallFlagNone,
      ExtensionPrefs::DELAY_REASON_WAIT_FOR_IDLE,
      syncer::StringOrdinal(),
      std::string());
  EXPECT_TRUE(base::PathExists(path_id2_1));

  GarbageCollectExtensions();

  EXPECT_FALSE(base::PathExists(path_id1_1));
  EXPECT_FALSE(base::PathExists(path_id1_2));
  EXPECT_FALSE(base::PathExists(cache_dir().AppendASCII(kExtensionId1)));

  EXPECT_TRUE(base::PathExists(path_id2_1));

  const base::DictionaryValue* shared_extensions = testing_local_state_.Get()->
      GetDictionary(ExtensionAssetsManagerChromeOS::kSharedExtensions);
  ASSERT_TRUE(shared_extensions);

  EXPECT_FALSE(shared_extensions->HasKey(kExtensionId1));
  EXPECT_TRUE(shared_extensions->HasKey(kExtensionId2));
}

}  // namespace extensions
