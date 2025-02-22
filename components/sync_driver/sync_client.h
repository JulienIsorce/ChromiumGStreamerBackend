// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_DRIVER_SYNC_CLIENT_H_
#define COMPONENTS_SYNC_DRIVER_SYNC_CLIENT_H_

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/engine/model_safe_worker.h"
#include "sync/util/extensions_activity.h"

class BookmarkUndoService;
class PrefService;

namespace autofill {
class AutofillWebDataService;
class AutocompleteSyncableService;
class PersonalDataManager;
}  // namespace autofill

namespace bookmarks {
class BookmarkModel;
}  // namespace bookmarks

namespace favicon {
class FaviconService;
}  // namespace favicon

namespace history {
class HistoryService;
}  // namespace history

namespace password_manager {
class PasswordStore;
}  // namespace password_manager

namespace syncer {
class SyncableService;
}  // namespace syncer

namespace sync_sessions {
class SyncSessionsClient;
}  // namespace sync_sessions

namespace sync_driver {

class SyncApiComponentFactory;
class SyncService;

// Interface for clients of the Sync API to plumb through necessary dependent
// components. This interface is purely for abstracting dependencies, and
// should not contain any non-trivial functional logic.
//
// Note: on some platforms, getters might return nullptr. Callers are expected
// to handle these scenarios gracefully.
class SyncClient {
 public:
  SyncClient();
  virtual ~SyncClient();

  // Initializes the sync client with the specified sync service. This will also
  // register data type controllers with |service| (via
  // SyncApiComponentFactory::RegisterDataTypes).
  virtual void Initialize(SyncService* service) = 0;

  // Returns the current SyncService instance.
  virtual SyncService* GetSyncService() = 0;

  // Returns the current profile's preference service.
  virtual PrefService* GetPrefService() = 0;

  // DataType specific service getters.
  virtual bookmarks::BookmarkModel* GetBookmarkModel() = 0;
  virtual favicon::FaviconService* GetFaviconService() = 0;
  virtual history::HistoryService* GetHistoryService() = 0;
  virtual scoped_refptr<password_manager::PasswordStore> GetPasswordStore() = 0;

  // Returns a callback that will be invoked when password sync state has
  // potentially been changed.
  virtual base::Closure GetPasswordStateChangedCallback() = 0;

  virtual autofill::PersonalDataManager* GetPersonalDataManager() = 0;
  virtual scoped_refptr<autofill::AutofillWebDataService>
  GetWebDataService() = 0;
  virtual BookmarkUndoService* GetBookmarkUndoServiceIfExists() = 0;
  virtual scoped_refptr<syncer::ExtensionsActivity> GetExtensionsActivity() = 0;
  virtual sync_sessions::SyncSessionsClient* GetSyncSessionsClient() = 0;

  // Returns a weak pointer to the syncable service specified by |type|.
  // Weak pointer may be unset if service is already destroyed.
  // Note: Should only be called from the model type thread.
  virtual base::WeakPtr<syncer::SyncableService> GetSyncableServiceForType(
      syncer::ModelType type) = 0;

  // Creates and returns a new ModelSafeWorker for the group, or null if one
  // cannot be created.
  // TODO(maxbogue): Move this inside SyncApiComponentFactory.
  virtual scoped_refptr<syncer::ModelSafeWorker> CreateModelWorkerForGroup(
      syncer::ModelSafeGroup group,
      syncer::WorkerLoopDestructionObserver* observer) = 0;

  // Returns the current SyncApiComponentFactory instance.
  virtual SyncApiComponentFactory* GetSyncApiComponentFactory() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(SyncClient);
};

}  // namespace sync_driver

#endif  // COMPONENTS_SYNC_DRIVER_SYNC_CLIENT_H_
