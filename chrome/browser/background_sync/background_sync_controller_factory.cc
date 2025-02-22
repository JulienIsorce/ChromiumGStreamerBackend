// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "background_sync_controller_factory.h"

#include "chrome/browser/background_sync/background_sync_controller_impl.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"

// static
BackgroundSyncControllerImpl* BackgroundSyncControllerFactory::GetForProfile(
    Profile* profile) {
  return static_cast<BackgroundSyncControllerImpl*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
BackgroundSyncControllerFactory*
BackgroundSyncControllerFactory::GetInstance() {
  return base::Singleton<BackgroundSyncControllerFactory>::get();
}

BackgroundSyncControllerFactory::BackgroundSyncControllerFactory()
    : BrowserContextKeyedServiceFactory(
          "BackgroundSyncService",
          BrowserContextDependencyManager::GetInstance()) {}

BackgroundSyncControllerFactory::~BackgroundSyncControllerFactory() {}

KeyedService* BackgroundSyncControllerFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  return new BackgroundSyncControllerImpl(Profile::FromBrowserContext(context));
}

content::BrowserContext*
BackgroundSyncControllerFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextOwnInstanceInIncognito(context);
}
