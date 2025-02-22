// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/fake_bluetooth_gatt_descriptor_client.h"

#include "base/bind.h"
#include "base/logging.h"
#include "chromeos/dbus/bluetooth_gatt_characteristic_client.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

const char FakeBluetoothGattDescriptorClient::
    kClientCharacteristicConfigurationPathComponent[] = "desc0000";
const char FakeBluetoothGattDescriptorClient::
    kClientCharacteristicConfigurationUUID[] =
        "00002902-0000-1000-8000-00805f9b34fb";

FakeBluetoothGattDescriptorClient::Properties::Properties(
    const PropertyChangedCallback& callback)
    : BluetoothGattDescriptorClient::Properties(
          NULL,
          bluetooth_gatt_descriptor::kBluetoothGattDescriptorInterface,
          callback) {}

FakeBluetoothGattDescriptorClient::Properties::~Properties() {}

void FakeBluetoothGattDescriptorClient::Properties::Get(
    dbus::PropertyBase* property,
    dbus::PropertySet::GetCallback callback) {
  VLOG(1) << "Get " << property->name();
  callback.Run(true);
}

void FakeBluetoothGattDescriptorClient::Properties::GetAll() {
  VLOG(1) << "GetAll";
}

void FakeBluetoothGattDescriptorClient::Properties::Set(
    dbus::PropertyBase* property,
    dbus::PropertySet::SetCallback callback) {
  VLOG(1) << "Set " << property->name();
  callback.Run(false);
}

FakeBluetoothGattDescriptorClient::DescriptorData::DescriptorData() {}

FakeBluetoothGattDescriptorClient::DescriptorData::~DescriptorData() {}

FakeBluetoothGattDescriptorClient::FakeBluetoothGattDescriptorClient()
    : weak_ptr_factory_(this) {}

FakeBluetoothGattDescriptorClient::~FakeBluetoothGattDescriptorClient() {
  for (PropertiesMap::iterator iter = properties_.begin();
       iter != properties_.end(); iter++)
    delete iter->second;
}

void FakeBluetoothGattDescriptorClient::Init(dbus::Bus* bus) {}

void FakeBluetoothGattDescriptorClient::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void FakeBluetoothGattDescriptorClient::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

std::vector<dbus::ObjectPath>
FakeBluetoothGattDescriptorClient::GetDescriptors() {
  std::vector<dbus::ObjectPath> descriptors;
  for (PropertiesMap::const_iterator iter = properties_.begin();
       iter != properties_.end(); ++iter) {
    descriptors.push_back(iter->first);
  }
  return descriptors;
}

FakeBluetoothGattDescriptorClient::Properties*
FakeBluetoothGattDescriptorClient::GetProperties(
    const dbus::ObjectPath& object_path) {
  PropertiesMap::const_iterator iter = properties_.find(object_path);
  if (iter == properties_.end())
    return NULL;
  return iter->second->properties.get();
}

void FakeBluetoothGattDescriptorClient::ReadValue(
    const dbus::ObjectPath& object_path,
    const ValueCallback& callback,
    const ErrorCallback& error_callback) {
  PropertiesMap::iterator iter = properties_.find(object_path);
  if (iter == properties_.end()) {
    error_callback.Run(kUnknownDescriptorError, "");
    return;
  }

  // Assign the value of the descriptor as necessary
  Properties* properties = iter->second->properties.get();
  if (properties->uuid.value() == kClientCharacteristicConfigurationUUID) {
    BluetoothGattCharacteristicClient::Properties* chrc_props =
        DBusThreadManager::Get()
            ->GetBluetoothGattCharacteristicClient()
            ->GetProperties(properties->characteristic.value());
    DCHECK(chrc_props);

    uint8_t value_byte = chrc_props->notifying.value() ? 0x01 : 0x00;
    const std::vector<uint8_t>& cur_value = properties->value.value();

    if (!cur_value.size() || cur_value[0] != value_byte) {
      std::vector<uint8_t> value = {value_byte, 0x00};
      properties->value.ReplaceValue(value);
    }
  }

  callback.Run(iter->second->properties->value.value());
}

void FakeBluetoothGattDescriptorClient::WriteValue(
    const dbus::ObjectPath& object_path,
    const std::vector<uint8>& value,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  if (properties_.find(object_path) == properties_.end()) {
    error_callback.Run(kUnknownDescriptorError, "");
    return;
  }

  // Since the only fake descriptor is "Client Characteristic Configuration"
  // and BlueZ doesn't allow writing to it, return failure.
  error_callback.Run("org.bluez.Error.NotPermitted",
                     "Writing to the Client Characteristic Configuration "
                     "descriptor not allowed");
}

dbus::ObjectPath FakeBluetoothGattDescriptorClient::ExposeDescriptor(
    const dbus::ObjectPath& characteristic_path,
    const std::string& uuid) {
  if (uuid != kClientCharacteristicConfigurationUUID) {
    VLOG(2) << "Unsupported UUID: " << uuid;
    return dbus::ObjectPath();
  }

  // CCC descriptor is the only one supported at the moment.
  DCHECK(characteristic_path.IsValid());
  dbus::ObjectPath object_path(characteristic_path.value() + "/" +
                               kClientCharacteristicConfigurationPathComponent);
  DCHECK(object_path.IsValid());
  PropertiesMap::const_iterator iter = properties_.find(object_path);
  if (iter != properties_.end()) {
    VLOG(1) << "Descriptor already exposed: " << object_path.value();
    return dbus::ObjectPath();
  }

  Properties* properties = new Properties(
      base::Bind(&FakeBluetoothGattDescriptorClient::OnPropertyChanged,
                 weak_ptr_factory_.GetWeakPtr(), object_path));
  properties->uuid.ReplaceValue(uuid);
  properties->characteristic.ReplaceValue(characteristic_path);

  DescriptorData* data = new DescriptorData();
  data->properties.reset(properties);

  properties_[object_path] = data;

  NotifyDescriptorAdded(object_path);

  return object_path;
}

void FakeBluetoothGattDescriptorClient::HideDescriptor(
    const dbus::ObjectPath& descriptor_path) {
  PropertiesMap::iterator iter = properties_.find(descriptor_path);
  if (iter == properties_.end()) {
    VLOG(1) << "Descriptor not exposed: " << descriptor_path.value();
    return;
  }

  NotifyDescriptorRemoved(descriptor_path);

  delete iter->second;
  properties_.erase(iter);
}

void FakeBluetoothGattDescriptorClient::OnPropertyChanged(
    const dbus::ObjectPath& object_path,
    const std::string& property_name) {
  VLOG(2) << "Descriptor property changed: " << object_path.value() << ": "
          << property_name;

  FOR_EACH_OBSERVER(BluetoothGattDescriptorClient::Observer, observers_,
                    GattDescriptorPropertyChanged(object_path, property_name));
}

void FakeBluetoothGattDescriptorClient::NotifyDescriptorAdded(
    const dbus::ObjectPath& object_path) {
  FOR_EACH_OBSERVER(BluetoothGattDescriptorClient::Observer, observers_,
                    GattDescriptorAdded(object_path));
}

void FakeBluetoothGattDescriptorClient::NotifyDescriptorRemoved(
    const dbus::ObjectPath& object_path) {
  FOR_EACH_OBSERVER(BluetoothGattDescriptorClient::Observer, observers_,
                    GattDescriptorRemoved(object_path));
}

}  // namespace chromeos
