// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/fake_bluetooth_agent_manager_client.h"

#include "base/logging.h"
#include "chromeos/dbus/fake_bluetooth_agent_service_provider.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

FakeBluetoothAgentManagerClient::FakeBluetoothAgentManagerClient()
    : service_provider_(NULL) {}

FakeBluetoothAgentManagerClient::~FakeBluetoothAgentManagerClient() {}

void FakeBluetoothAgentManagerClient::Init(dbus::Bus* bus) {}

void FakeBluetoothAgentManagerClient::RegisterAgent(
    const dbus::ObjectPath& agent_path,
    const std::string& capability,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  VLOG(1) << "RegisterAgent: " << agent_path.value();

  if (service_provider_ == NULL) {
    error_callback.Run(bluetooth_agent_manager::kErrorInvalidArguments,
                       "No agent created");
  } else if (service_provider_->object_path_ != agent_path) {
    error_callback.Run(bluetooth_agent_manager::kErrorAlreadyExists,
                       "Agent already registered");
  } else {
    callback.Run();
  }
}

void FakeBluetoothAgentManagerClient::UnregisterAgent(
    const dbus::ObjectPath& agent_path,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  VLOG(1) << "UnregisterAgent: " << agent_path.value();
  if (service_provider_ == NULL) {
    error_callback.Run(bluetooth_agent_manager::kErrorDoesNotExist,
                       "No agent registered");
  } else if (service_provider_->object_path_ != agent_path) {
    error_callback.Run(bluetooth_agent_manager::kErrorDoesNotExist,
                       "Agent still registered");
  } else {
    callback.Run();
  }
}

void FakeBluetoothAgentManagerClient::RequestDefaultAgent(
    const dbus::ObjectPath& agent_path,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  VLOG(1) << "RequestDefaultAgent: " << agent_path.value();
  callback.Run();
}

void FakeBluetoothAgentManagerClient::RegisterAgentServiceProvider(
    FakeBluetoothAgentServiceProvider* service_provider) {
  service_provider_ = service_provider;
}

void FakeBluetoothAgentManagerClient::UnregisterAgentServiceProvider(
    FakeBluetoothAgentServiceProvider* service_provider) {
  if (service_provider_ == service_provider)
    service_provider_ = NULL;
}

FakeBluetoothAgentServiceProvider*
FakeBluetoothAgentManagerClient::GetAgentServiceProvider() {
  return service_provider_;
}

}  // namespace chromeos
