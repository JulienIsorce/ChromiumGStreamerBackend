// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/proximity_auth/remote_device_life_cycle_impl.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/thread_task_runner_handle.h"
#include "base/time/default_tick_clock.h"
#include "components/proximity_auth/ble/bluetooth_low_energy_connection.h"
#include "components/proximity_auth/ble/bluetooth_low_energy_connection_finder.h"
#include "components/proximity_auth/bluetooth_connection.h"
#include "components/proximity_auth/bluetooth_connection_finder.h"
#include "components/proximity_auth/bluetooth_throttler_impl.h"
#include "components/proximity_auth/cryptauth/secure_message_delegate.h"
#include "components/proximity_auth/device_to_device_authenticator.h"
#include "components/proximity_auth/logging/logging.h"
#include "components/proximity_auth/messenger_impl.h"
#include "components/proximity_auth/proximity_auth_client.h"
#include "components/proximity_auth/secure_context.h"

namespace proximity_auth {

namespace {

// The UUID of the Smart Lock classic Bluetooth service.
const char kClassicBluetoothServiceUUID[] =
    "704EE561-3782-405A-A14B-2D47A2DDCDDF";

// The UUID of the Bluetooth Low Energy service.
const char kBLESmartLockServiceUUID[] = "b3b7e28e-a000-3e17-bd86-6e97b9e28c11";

// The time to wait, in seconds, after authentication fails, before retrying
// another connection.
const int kAuthenticationRecoveryTimeSeconds = 10;

}  // namespace

RemoteDeviceLifeCycleImpl::RemoteDeviceLifeCycleImpl(
    const RemoteDevice& remote_device,
    ProximityAuthClient* proximity_auth_client)
    : remote_device_(remote_device),
      proximity_auth_client_(proximity_auth_client),
      state_(RemoteDeviceLifeCycle::State::STOPPED),
      observers_(base::ObserverList<Observer>::NOTIFY_EXISTING_ONLY),
      bluetooth_throttler_(new BluetoothThrottlerImpl(
          make_scoped_ptr(new base::DefaultTickClock()))),
      weak_ptr_factory_(this) {}

RemoteDeviceLifeCycleImpl::~RemoteDeviceLifeCycleImpl() {}

void RemoteDeviceLifeCycleImpl::Start() {
  PA_LOG(INFO) << "Life cycle for " << remote_device_.bluetooth_address
               << " started.";
  DCHECK(state_ == RemoteDeviceLifeCycle::State::STOPPED);
  FindConnection();
}

RemoteDeviceLifeCycle::State RemoteDeviceLifeCycleImpl::GetState() const {
  return state_;
}

Messenger* RemoteDeviceLifeCycleImpl::GetMessenger() {
  return messenger_.get();
}

void RemoteDeviceLifeCycleImpl::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void RemoteDeviceLifeCycleImpl::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

scoped_ptr<ConnectionFinder>
RemoteDeviceLifeCycleImpl::CreateConnectionFinder() {
  if (remote_device_.bluetooth_type == RemoteDevice::BLUETOOTH_LE) {
    return make_scoped_ptr(new BluetoothLowEnergyConnectionFinder(
        remote_device_, kBLESmartLockServiceUUID,
        BluetoothLowEnergyConnectionFinder::FinderStrategy::FIND_PAIRED_DEVICE,
        nullptr, bluetooth_throttler_.get(), 3));
  } else {
    return make_scoped_ptr(new BluetoothConnectionFinder(
        remote_device_, device::BluetoothUUID(kClassicBluetoothServiceUUID),
        base::TimeDelta::FromSeconds(3)));
  }
}

scoped_ptr<Authenticator> RemoteDeviceLifeCycleImpl::CreateAuthenticator() {
  return make_scoped_ptr(new DeviceToDeviceAuthenticator(
      connection_.get(), remote_device_.user_id,
      proximity_auth_client_->CreateSecureMessageDelegate()));
}

void RemoteDeviceLifeCycleImpl::TransitionToState(
    RemoteDeviceLifeCycle::State new_state) {
  PA_LOG(INFO) << "Life cycle transition: " << static_cast<int>(state_)
               << " => " << static_cast<int>(new_state);
  RemoteDeviceLifeCycle::State old_state = state_;
  state_ = new_state;
  FOR_EACH_OBSERVER(Observer, observers_,
                    OnLifeCycleStateChanged(old_state, new_state));
}

void RemoteDeviceLifeCycleImpl::FindConnection() {
  connection_finder_ = CreateConnectionFinder();
  connection_finder_->Find(
      base::Bind(&RemoteDeviceLifeCycleImpl::OnConnectionFound,
                 weak_ptr_factory_.GetWeakPtr()));
  TransitionToState(RemoteDeviceLifeCycle::State::FINDING_CONNECTION);
}

void RemoteDeviceLifeCycleImpl::OnConnectionFound(
    scoped_ptr<Connection> connection) {
  DCHECK(state_ == RemoteDeviceLifeCycle::State::FINDING_CONNECTION);
  connection_ = connection.Pass();
  authenticator_ = CreateAuthenticator();
  authenticator_->Authenticate(
      base::Bind(&RemoteDeviceLifeCycleImpl::OnAuthenticationResult,
                 weak_ptr_factory_.GetWeakPtr()));
  TransitionToState(RemoteDeviceLifeCycle::State::AUTHENTICATING);
}

void RemoteDeviceLifeCycleImpl::OnAuthenticationResult(
    Authenticator::Result result,
    scoped_ptr<SecureContext> secure_context) {
  DCHECK(state_ == RemoteDeviceLifeCycle::State::AUTHENTICATING);
  authenticator_.reset();
  if (result != Authenticator::Result::SUCCESS) {
    PA_LOG(WARNING) << "Waiting " << kAuthenticationRecoveryTimeSeconds
                    << " seconds to retry after authentication failure.";
    connection_->Disconnect();
    authentication_recovery_timer_.Start(
        FROM_HERE,
        base::TimeDelta::FromSeconds(kAuthenticationRecoveryTimeSeconds), this,
        &RemoteDeviceLifeCycleImpl::FindConnection);
    TransitionToState(RemoteDeviceLifeCycle::State::AUTHENTICATION_FAILED);
    return;
  }

  // Create the MessengerImpl asynchronously. |messenger_| registers itself as
  // an observer of |connection_|, so creating it synchronously would trigger
  // |OnSendCompleted()| as an observer call for |messenger_|.
  secure_context_ = secure_context.Pass();
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&RemoteDeviceLifeCycleImpl::CreateMessenger,
                            weak_ptr_factory_.GetWeakPtr()));
}

void RemoteDeviceLifeCycleImpl::CreateMessenger() {
  DCHECK(state_ == RemoteDeviceLifeCycle::State::AUTHENTICATING);
  DCHECK(secure_context_);
  messenger_.reset(
      new MessengerImpl(connection_.Pass(), secure_context_.Pass()));
  messenger_->AddObserver(this);

  TransitionToState(RemoteDeviceLifeCycle::State::SECURE_CHANNEL_ESTABLISHED);
}

void RemoteDeviceLifeCycleImpl::OnDisconnected() {
  DCHECK(state_ == RemoteDeviceLifeCycle::State::SECURE_CHANNEL_ESTABLISHED);
  messenger_.reset();
  FindConnection();
}

}  // namespace proximity_auth
