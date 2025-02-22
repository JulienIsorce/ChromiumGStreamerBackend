// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_pairing_chromeos.h"

#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_device_chromeos.h"

using device::BluetoothDevice;

namespace {

// Histogram enumerations for pairing methods.
enum UMAPairingMethod {
  UMA_PAIRING_METHOD_NONE,
  UMA_PAIRING_METHOD_REQUEST_PINCODE,
  UMA_PAIRING_METHOD_REQUEST_PASSKEY,
  UMA_PAIRING_METHOD_DISPLAY_PINCODE,
  UMA_PAIRING_METHOD_DISPLAY_PASSKEY,
  UMA_PAIRING_METHOD_CONFIRM_PASSKEY,
  // NOTE: Add new pairing methods immediately above this line. Make sure to
  // update the enum list in tools/histogram/histograms.xml accordingly.
  UMA_PAIRING_METHOD_COUNT
};

// Number of keys that will be entered for a passkey, six digits plus the
// final enter.
const uint16 kPasskeyMaxKeysEntered = 7;

}  // namespace

namespace chromeos {

BluetoothPairingChromeOS::BluetoothPairingChromeOS(
    BluetoothDeviceChromeOS* device,
    BluetoothDevice::PairingDelegate* pairing_delegate)
    : device_(device),
      pairing_delegate_(pairing_delegate),
      pairing_delegate_used_(false) {
  VLOG(1) << "Created BluetoothPairingChromeOS for "
          << device_->GetAddress();
}

BluetoothPairingChromeOS::~BluetoothPairingChromeOS() {
  VLOG(1) << "Destroying BluetoothPairingChromeOS for "
          << device_->GetAddress();

  if (!pairing_delegate_used_) {
    UMA_HISTOGRAM_ENUMERATION("Bluetooth.PairingMethod",
                              UMA_PAIRING_METHOD_NONE,
                              UMA_PAIRING_METHOD_COUNT);
  }

  if (!pincode_callback_.is_null()) {
    pincode_callback_.Run(BluetoothAgentServiceProvider::Delegate::CANCELLED,
                          "");
  }

  if (!passkey_callback_.is_null()) {
    passkey_callback_.Run(BluetoothAgentServiceProvider::Delegate::CANCELLED,
                          0);
  }

  if (!confirmation_callback_.is_null()) {
    confirmation_callback_.Run(
        BluetoothAgentServiceProvider::Delegate::CANCELLED);
  }

  pairing_delegate_ = NULL;
}

void BluetoothPairingChromeOS::RequestPinCode(
    const BluetoothAgentServiceProvider::Delegate::PinCodeCallback& callback) {
  UMA_HISTOGRAM_ENUMERATION("Bluetooth.PairingMethod",
                            UMA_PAIRING_METHOD_REQUEST_PINCODE,
                            UMA_PAIRING_METHOD_COUNT);

  ResetCallbacks();
  pincode_callback_ = callback;
  pairing_delegate_used_ = true;
  pairing_delegate_->RequestPinCode(device_);
}

bool BluetoothPairingChromeOS::ExpectingPinCode() const {
  return !pincode_callback_.is_null();
}

void BluetoothPairingChromeOS::SetPinCode(const std::string& pincode) {
  if (pincode_callback_.is_null())
    return;

  pincode_callback_.Run(BluetoothAgentServiceProvider::Delegate::SUCCESS,
                        pincode);
  pincode_callback_.Reset();

  // If this is not an outgoing connection to the device, clean up the pairing
  // context since the pairing is done. The outgoing connection case is cleaned
  // up in the callback for the underlying Pair() call.
  if (!device_->IsConnecting())
    device_->EndPairing();
}

void BluetoothPairingChromeOS::DisplayPinCode(const std::string& pincode) {
  UMA_HISTOGRAM_ENUMERATION("Bluetooth.PairingMethod",
                            UMA_PAIRING_METHOD_DISPLAY_PINCODE,
                            UMA_PAIRING_METHOD_COUNT);

  ResetCallbacks();
  pairing_delegate_used_ = true;
  pairing_delegate_->DisplayPinCode(device_, pincode);

  // If this is not an outgoing connection to the device, the pairing context
  // needs to be cleaned up again as there's no reliable indication of
  // completion of incoming pairing.
  if (!device_->IsConnecting())
    device_->EndPairing();
}

void BluetoothPairingChromeOS::RequestPasskey(
    const BluetoothAgentServiceProvider::Delegate::PasskeyCallback& callback) {
  UMA_HISTOGRAM_ENUMERATION("Bluetooth.PairingMethod",
                            UMA_PAIRING_METHOD_REQUEST_PASSKEY,
                            UMA_PAIRING_METHOD_COUNT);

  ResetCallbacks();
  passkey_callback_ = callback;
  pairing_delegate_used_ = true;
  pairing_delegate_->RequestPasskey(device_);
}

bool BluetoothPairingChromeOS::ExpectingPasskey() const {
  return !passkey_callback_.is_null();
}

void BluetoothPairingChromeOS::SetPasskey(uint32 passkey) {
  if (passkey_callback_.is_null())
    return;

  passkey_callback_.Run(BluetoothAgentServiceProvider::Delegate::SUCCESS,
                        passkey);
  passkey_callback_.Reset();

  // If this is not an outgoing connection to the device, clean up the pairing
  // context since the pairing is done. The outgoing connection case is cleaned
  // up in the callback for the underlying Pair() call.
  if (!device_->IsConnecting())
    device_->EndPairing();
}

void BluetoothPairingChromeOS::DisplayPasskey(uint32 passkey) {
  UMA_HISTOGRAM_ENUMERATION("Bluetooth.PairingMethod",
                            UMA_PAIRING_METHOD_DISPLAY_PASSKEY,
                            UMA_PAIRING_METHOD_COUNT);

  ResetCallbacks();
  pairing_delegate_used_ = true;
  pairing_delegate_->DisplayPasskey(device_, passkey);

}

void BluetoothPairingChromeOS::KeysEntered(uint16 entered) {
  pairing_delegate_used_ = true;
  pairing_delegate_->KeysEntered(device_, entered);

  // If this is not an outgoing connection to the device, the pairing context
  // needs to be cleaned up again as there's no reliable indication of
  // completion of incoming pairing.
  if (entered >= kPasskeyMaxKeysEntered && !device_->IsConnecting())
    device_->EndPairing();
}

void BluetoothPairingChromeOS::RequestConfirmation(
    uint32 passkey,
    const BluetoothAgentServiceProvider::Delegate::ConfirmationCallback&
        callback) {
  UMA_HISTOGRAM_ENUMERATION("Bluetooth.PairingMethod",
                            UMA_PAIRING_METHOD_CONFIRM_PASSKEY,
                            UMA_PAIRING_METHOD_COUNT);

  ResetCallbacks();
  confirmation_callback_ = callback;
  pairing_delegate_used_ = true;
  pairing_delegate_->ConfirmPasskey(device_, passkey);
}

void BluetoothPairingChromeOS::RequestAuthorization(
    const BluetoothAgentServiceProvider::Delegate::ConfirmationCallback&
        callback) {
  UMA_HISTOGRAM_ENUMERATION("Bluetooth.PairingMethod",
                            UMA_PAIRING_METHOD_NONE,
                            UMA_PAIRING_METHOD_COUNT);

  ResetCallbacks();
  confirmation_callback_ = callback;
  pairing_delegate_used_ = true;
  pairing_delegate_->AuthorizePairing(device_);
}

bool BluetoothPairingChromeOS::ExpectingConfirmation() const {
  return !confirmation_callback_.is_null();
}

void BluetoothPairingChromeOS::ConfirmPairing() {
  if (confirmation_callback_.is_null())
    return;

  confirmation_callback_.Run(BluetoothAgentServiceProvider::Delegate::SUCCESS);
  confirmation_callback_.Reset();

  // If this is not an outgoing connection to the device, clean up the pairing
  // context since the pairing is done. The outgoing connection case is cleaned
  // up in the callback for the underlying Pair() call.
  if (!device_->IsConnecting())
    device_->EndPairing();
}

bool BluetoothPairingChromeOS::RejectPairing() {
  return RunPairingCallbacks(BluetoothAgentServiceProvider::Delegate::REJECTED);
}

bool BluetoothPairingChromeOS::CancelPairing() {
  return RunPairingCallbacks(
      BluetoothAgentServiceProvider::Delegate::CANCELLED);
}

BluetoothDevice::PairingDelegate*
BluetoothPairingChromeOS::GetPairingDelegate() const {
  return pairing_delegate_;
}

void BluetoothPairingChromeOS::ResetCallbacks() {
  pincode_callback_.Reset();
  passkey_callback_.Reset();
  confirmation_callback_.Reset();
}

bool BluetoothPairingChromeOS::RunPairingCallbacks(
    BluetoothAgentServiceProvider::Delegate::Status status) {
  pairing_delegate_used_ = true;

  bool callback_run = false;
  if (!pincode_callback_.is_null()) {
    pincode_callback_.Run(status, "");
    pincode_callback_.Reset();
    callback_run = true;
  }

  if (!passkey_callback_.is_null()) {
    passkey_callback_.Run(status, 0);
    passkey_callback_.Reset();
    callback_run = true;
  }

  if (!confirmation_callback_.is_null()) {
    confirmation_callback_.Run(status);
    confirmation_callback_.Reset();
    callback_run = true;
  }

  // If this is not an outgoing connection to the device, clean up the pairing
  // context since the pairing is done. The outgoing connection case is cleaned
  // up in the callback for the underlying Pair() call.
  if (!device_->IsConnecting())
    device_->EndPairing();

  return callback_run;
}

}  // namespace chromeos
