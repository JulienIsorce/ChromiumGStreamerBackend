// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metrics/variations/chrome_variations_service_client.h"

#include "base/bind.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/channel_info.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_thread.h"

#if !defined(OS_ANDROID) && !defined(OS_IOS) && !defined(OS_CHROMEOS)
#include "chrome/browser/upgrade_detector_impl.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/settings/cros_settings.h"
#endif

namespace {

// Gets the version number to use for variations seed simulation. Must be called
// on a thread where IO is allowed.
base::Version GetVersionForSimulation() {
#if !defined(OS_ANDROID) && !defined(OS_IOS) && !defined(OS_CHROMEOS)
  const base::Version installed_version =
      UpgradeDetectorImpl::GetCurrentlyInstalledVersion();
  if (installed_version.IsValid())
    return installed_version;
#endif  // !defined(OS_ANDROID) && !defined(OS_IOS) && !defined(OS_CHROMEOS)

  // TODO(asvitkine): Get the version that will be used on restart instead of
  // the current version on Android, iOS and ChromeOS.
  return base::Version(version_info::GetVersionNumber());
}

}  // namespace

ChromeVariationsServiceClient::ChromeVariationsServiceClient() {}

ChromeVariationsServiceClient::~ChromeVariationsServiceClient() {}

std::string ChromeVariationsServiceClient::GetApplicationLocale() {
  return g_browser_process->GetApplicationLocale();
}

base::SequencedWorkerPool* ChromeVariationsServiceClient::GetBlockingPool() {
  return content::BrowserThread::GetBlockingPool();
}

base::Callback<base::Version(void)>
ChromeVariationsServiceClient::GetVersionForSimulationCallback() {
  return base::Bind(&GetVersionForSimulation);
}

net::URLRequestContextGetter*
ChromeVariationsServiceClient::GetURLRequestContext() {
  return g_browser_process->system_request_context();
}

network_time::NetworkTimeTracker*
ChromeVariationsServiceClient::GetNetworkTimeTracker() {
  return g_browser_process->network_time_tracker();
}

version_info::Channel ChromeVariationsServiceClient::GetChannel() {
  return chrome::GetChannel();
}

bool ChromeVariationsServiceClient::OverridesRestrictParameter(
    std::string* parameter) {
#if defined(OS_CHROMEOS)
  chromeos::CrosSettings::Get()->GetString(
      chromeos::kVariationsRestrictParameter, parameter);
  return true;
#else
  return false;
#endif
}

void ChromeVariationsServiceClient::OnInitialStartup() {
#if defined(OS_WIN)
  StartGoogleUpdateRegistrySync();
#endif
}

#if defined(OS_WIN)
void ChromeVariationsServiceClient::StartGoogleUpdateRegistrySync() {
  registry_syncer_.RequestRegistrySync();
}
#endif
