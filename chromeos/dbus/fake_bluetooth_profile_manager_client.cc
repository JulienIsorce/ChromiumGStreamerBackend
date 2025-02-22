// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/fake_bluetooth_profile_manager_client.h"

#include "base/location.h"
#include "base/logging.h"
#include "base/single_thread_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "chromeos/dbus/fake_bluetooth_profile_service_provider.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_proxy.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

const char FakeBluetoothProfileManagerClient::kL2capUuid[] =
    "4d995052-33cc-4fdf-b446-75f32942a076";
const char FakeBluetoothProfileManagerClient::kRfcommUuid[] =
    "3f6d6dbf-a6ad-45fc-9653-47dc912ef70e";
const char FakeBluetoothProfileManagerClient::kUnregisterableUuid[] =
    "00000000-0000-0000-0000-000000000000";

FakeBluetoothProfileManagerClient::FakeBluetoothProfileManagerClient() {}

FakeBluetoothProfileManagerClient::~FakeBluetoothProfileManagerClient() {}

void FakeBluetoothProfileManagerClient::Init(dbus::Bus* bus) {}

void FakeBluetoothProfileManagerClient::RegisterProfile(
    const dbus::ObjectPath& profile_path,
    const std::string& uuid,
    const Options& options,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  VLOG(1) << "RegisterProfile: " << profile_path.value() << ": " << uuid;

  if (uuid == kUnregisterableUuid) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(error_callback,
                              bluetooth_profile_manager::kErrorInvalidArguments,
                              "Can't register this UUID"));
    return;
  }

  // TODO(jamuraa): check options for channel & psm

  ServiceProviderMap::iterator iter = service_provider_map_.find(profile_path);
  if (iter == service_provider_map_.end()) {
    error_callback.Run(bluetooth_profile_manager::kErrorInvalidArguments,
                       "No profile created");
  } else {
    ProfileMap::iterator piter = profile_map_.find(uuid);
    if (piter != profile_map_.end()) {
      error_callback.Run(bluetooth_profile_manager::kErrorAlreadyExists,
                         "Profile already registered");
    } else {
      profile_map_[uuid] = profile_path;
      base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, callback);
    }
  }
}

void FakeBluetoothProfileManagerClient::UnregisterProfile(
    const dbus::ObjectPath& profile_path,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  VLOG(1) << "UnregisterProfile: " << profile_path.value();

  ServiceProviderMap::iterator iter = service_provider_map_.find(profile_path);
  if (iter == service_provider_map_.end()) {
    error_callback.Run(bluetooth_profile_manager::kErrorInvalidArguments,
                       "Profile not registered");
  } else {
    for (ProfileMap::iterator piter = profile_map_.begin();
         piter != profile_map_.end(); ++piter) {
      if (piter->second == profile_path) {
        profile_map_.erase(piter);
        break;
      }
    }

    base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, callback);
  }
}

void FakeBluetoothProfileManagerClient::RegisterProfileServiceProvider(
    FakeBluetoothProfileServiceProvider* service_provider) {
  service_provider_map_[service_provider->object_path_] = service_provider;
}

void FakeBluetoothProfileManagerClient::UnregisterProfileServiceProvider(
    FakeBluetoothProfileServiceProvider* service_provider) {
  ServiceProviderMap::iterator iter =
      service_provider_map_.find(service_provider->object_path_);
  if (iter != service_provider_map_.end() && iter->second == service_provider)
    service_provider_map_.erase(iter);
}

FakeBluetoothProfileServiceProvider*
FakeBluetoothProfileManagerClient::GetProfileServiceProvider(
    const std::string& uuid) {
  ProfileMap::iterator iter = profile_map_.find(uuid);
  if (iter == profile_map_.end())
    return nullptr;
  return service_provider_map_[iter->second];
}

}  // namespace chromeos
