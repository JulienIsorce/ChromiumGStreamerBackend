// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/test/test_bluetooth_adapter_observer.h"

#include "base/message_loop/message_loop.h"
#include "device/bluetooth/bluetooth_gatt_characteristic.h"
#include "device/bluetooth/bluetooth_gatt_descriptor.h"
#include "device/bluetooth/bluetooth_gatt_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace device {

TestBluetoothAdapterObserver::TestBluetoothAdapterObserver(
    scoped_refptr<BluetoothAdapter> adapter)
    : adapter_(adapter) {
  Reset();
  adapter_->AddObserver(this);
}

TestBluetoothAdapterObserver::~TestBluetoothAdapterObserver() {
  adapter_->RemoveObserver(this);
}

void TestBluetoothAdapterObserver::Reset() {
  present_changed_count_ = 0;
  powered_changed_count_ = 0;
  discoverable_changed_count_ = 0;
  discovering_changed_count_ = 0;
  last_present_ = false;
  last_powered_ = false;
  last_discovering_ = false;
  device_added_count_ = 0;
  device_changed_count_ = 0;
  device_address_changed_count_ = 0;
  device_removed_count_ = 0;
  last_device_ = NULL;
  last_device_address_.clear();
  gatt_service_added_count_ = 0;
  gatt_service_removed_count_ = 0;
  gatt_service_changed_count_ = 0;
  gatt_discovery_complete_count_ = 0;
  gatt_characteristic_added_count_ = 0;
  gatt_characteristic_removed_count_ = 0;
  gatt_characteristic_value_changed_count_ = 0;
  gatt_descriptor_added_count_ = 0;
  gatt_descriptor_removed_count_ = 0;
  gatt_descriptor_value_changed_count_ = 0;
  last_gatt_service_id_.clear();
  last_gatt_service_uuid_ = BluetoothUUID();
  last_gatt_characteristic_id_.clear();
  last_gatt_characteristic_uuid_ = BluetoothUUID();
  last_changed_characteristic_value_.clear();
  last_gatt_descriptor_id_.clear();
  last_gatt_descriptor_uuid_ = BluetoothUUID();
  last_changed_descriptor_value_.clear();
}

void TestBluetoothAdapterObserver::AdapterPresentChanged(
    BluetoothAdapter* adapter,
    bool present) {
  EXPECT_EQ(adapter_.get(), adapter);

  ++present_changed_count_;
  last_present_ = present;
}

void TestBluetoothAdapterObserver::AdapterPoweredChanged(
    BluetoothAdapter* adapter,
    bool powered) {
  EXPECT_EQ(adapter_.get(), adapter);

  ++powered_changed_count_;
  last_powered_ = powered;
}

void TestBluetoothAdapterObserver::AdapterDiscoverableChanged(
    BluetoothAdapter* adapter,
    bool discoverable) {
  EXPECT_EQ(adapter_.get(), adapter);

  ++discoverable_changed_count_;
}

void TestBluetoothAdapterObserver::AdapterDiscoveringChanged(
    BluetoothAdapter* adapter,
    bool discovering) {
  EXPECT_EQ(adapter_.get(), adapter);

  ++discovering_changed_count_;
  last_discovering_ = discovering;
}

void TestBluetoothAdapterObserver::DeviceAdded(BluetoothAdapter* adapter,
                                               BluetoothDevice* device) {
  EXPECT_EQ(adapter_.get(), adapter);

  ++device_added_count_;
  last_device_ = device;
  last_device_address_ = device->GetAddress();

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::DeviceChanged(BluetoothAdapter* adapter,
                                                 BluetoothDevice* device) {
  EXPECT_EQ(adapter_.get(), adapter);

  ++device_changed_count_;
  last_device_ = device;
  last_device_address_ = device->GetAddress();

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::DeviceAddressChanged(
    device::BluetoothAdapter* adapter,
    device::BluetoothDevice* device,
    const std::string& old_address) {
  ++device_address_changed_count_;
  last_device_ = device;
  last_device_address_ = device->GetAddress();

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::DeviceRemoved(BluetoothAdapter* adapter,
                                                 BluetoothDevice* device) {
  EXPECT_EQ(adapter_.get(), adapter);

  ++device_removed_count_;
  // Can't save device, it may be freed
  last_device_address_ = device->GetAddress();

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::GattServiceAdded(
    BluetoothAdapter* adapter,
    BluetoothDevice* device,
    BluetoothGattService* service) {
  ASSERT_EQ(adapter_.get(), adapter);
  ASSERT_EQ(service->GetDevice(), device);

  ++gatt_service_added_count_;
  last_gatt_service_id_ = service->GetIdentifier();
  last_gatt_service_uuid_ = service->GetUUID();

  EXPECT_FALSE(service->IsLocal());
  EXPECT_TRUE(service->IsPrimary());

  EXPECT_EQ(device->GetGattService(last_gatt_service_id_), service);

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::GattServiceRemoved(
    BluetoothAdapter* adapter,
    BluetoothDevice* device,
    BluetoothGattService* service) {
  ASSERT_EQ(adapter_.get(), adapter);
  ASSERT_EQ(service->GetDevice(), device);

  ++gatt_service_removed_count_;
  last_gatt_service_id_ = service->GetIdentifier();
  last_gatt_service_uuid_ = service->GetUUID();

  EXPECT_FALSE(service->IsLocal());
  EXPECT_TRUE(service->IsPrimary());

  // The device should return NULL for this service.
  EXPECT_FALSE(device->GetGattService(last_gatt_service_id_));

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::GattDiscoveryCompleteForService(
    BluetoothAdapter* adapter,
    BluetoothGattService* service) {
  ASSERT_EQ(adapter_.get(), adapter);
  ++gatt_discovery_complete_count_;

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::GattServiceChanged(
    BluetoothAdapter* adapter,
    BluetoothGattService* service) {
  ASSERT_EQ(adapter_.get(), adapter);
  ++gatt_service_changed_count_;

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::GattCharacteristicAdded(
    BluetoothAdapter* adapter,
    BluetoothGattCharacteristic* characteristic) {
  ASSERT_EQ(adapter_.get(), adapter);

  ++gatt_characteristic_added_count_;
  last_gatt_characteristic_id_ = characteristic->GetIdentifier();
  last_gatt_characteristic_uuid_ = characteristic->GetUUID();

  ASSERT_TRUE(characteristic->GetService());
  EXPECT_EQ(characteristic->GetService()->GetCharacteristic(
                last_gatt_characteristic_id_),
            characteristic);

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::GattCharacteristicRemoved(
    BluetoothAdapter* adapter,
    BluetoothGattCharacteristic* characteristic) {
  ASSERT_EQ(adapter_.get(), adapter);

  ++gatt_characteristic_removed_count_;
  last_gatt_characteristic_id_ = characteristic->GetIdentifier();
  last_gatt_characteristic_uuid_ = characteristic->GetUUID();

  // The service should return NULL for this characteristic.
  ASSERT_TRUE(characteristic->GetService());
  EXPECT_FALSE(characteristic->GetService()->GetCharacteristic(
      last_gatt_characteristic_id_));

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::GattDescriptorAdded(
    BluetoothAdapter* adapter,
    BluetoothGattDescriptor* descriptor) {
  ASSERT_EQ(adapter_.get(), adapter);

  ++gatt_descriptor_added_count_;
  last_gatt_descriptor_id_ = descriptor->GetIdentifier();
  last_gatt_descriptor_uuid_ = descriptor->GetUUID();

  ASSERT_TRUE(descriptor->GetCharacteristic());
  EXPECT_EQ(
      descriptor->GetCharacteristic()->GetDescriptor(last_gatt_descriptor_id_),
      descriptor);

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::GattDescriptorRemoved(
    BluetoothAdapter* adapter,
    BluetoothGattDescriptor* descriptor) {
  ASSERT_EQ(adapter_.get(), adapter);

  ++gatt_descriptor_removed_count_;
  last_gatt_descriptor_id_ = descriptor->GetIdentifier();
  last_gatt_descriptor_uuid_ = descriptor->GetUUID();

  // The characteristic should return NULL for this descriptor..
  ASSERT_TRUE(descriptor->GetCharacteristic());
  EXPECT_FALSE(
      descriptor->GetCharacteristic()->GetDescriptor(last_gatt_descriptor_id_));

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::GattCharacteristicValueChanged(
    BluetoothAdapter* adapter,
    BluetoothGattCharacteristic* characteristic,
    const std::vector<uint8>& value) {
  ASSERT_EQ(adapter_.get(), adapter);

  ++gatt_characteristic_value_changed_count_;
  last_gatt_characteristic_id_ = characteristic->GetIdentifier();
  last_gatt_characteristic_uuid_ = characteristic->GetUUID();
  last_changed_characteristic_value_ = value;

  ASSERT_TRUE(characteristic->GetService());
  EXPECT_EQ(characteristic->GetService()->GetCharacteristic(
                last_gatt_characteristic_id_),
            characteristic);

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::GattDescriptorValueChanged(
    BluetoothAdapter* adapter,
    BluetoothGattDescriptor* descriptor,
    const std::vector<uint8>& value) {
  ASSERT_EQ(adapter_.get(), adapter);

  ++gatt_descriptor_value_changed_count_;
  last_gatt_descriptor_id_ = descriptor->GetIdentifier();
  last_gatt_descriptor_uuid_ = descriptor->GetUUID();
  last_changed_descriptor_value_ = value;

  ASSERT_TRUE(descriptor->GetCharacteristic());
  EXPECT_EQ(
      descriptor->GetCharacteristic()->GetDescriptor(last_gatt_descriptor_id_),
      descriptor);

  QuitMessageLoop();
}

void TestBluetoothAdapterObserver::QuitMessageLoop() {
  if (base::MessageLoop::current() &&
      base::MessageLoop::current()->is_running())
    base::MessageLoop::current()->QuitWhenIdle();
}

}  // namespace device
