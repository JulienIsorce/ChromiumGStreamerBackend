// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/bluetooth_gatt_descriptor_client.h"

#include "base/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "dbus/bus.h"
#include "dbus/object_manager.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

// TODO(armansito): Move this constant to cros_system_api.
const char kValueProperty[] = "Value";

}  // namespace

// static
const char BluetoothGattDescriptorClient::kNoResponseError[] =
    "org.chromium.Error.NoResponse";
// static
const char BluetoothGattDescriptorClient::kUnknownDescriptorError[] =
    "org.chromium.Error.UnknownDescriptor";

BluetoothGattDescriptorClient::Properties::Properties(
    dbus::ObjectProxy* object_proxy,
    const std::string& interface_name,
    const PropertyChangedCallback& callback)
    : dbus::PropertySet(object_proxy, interface_name, callback) {
  RegisterProperty(bluetooth_gatt_descriptor::kUUIDProperty, &uuid);
  RegisterProperty(bluetooth_gatt_descriptor::kCharacteristicProperty,
                   &characteristic);
  RegisterProperty(kValueProperty, &value);
}

BluetoothGattDescriptorClient::Properties::~Properties() {}

// The BluetoothGattDescriptorClient implementation used in production.
class BluetoothGattDescriptorClientImpl
    : public BluetoothGattDescriptorClient,
      public dbus::ObjectManager::Interface {
 public:
  BluetoothGattDescriptorClientImpl()
      : object_manager_(NULL), weak_ptr_factory_(this) {}

  ~BluetoothGattDescriptorClientImpl() override {
    object_manager_->UnregisterInterface(
        bluetooth_gatt_descriptor::kBluetoothGattDescriptorInterface);
  }

  // BluetoothGattDescriptorClientImpl override.
  void AddObserver(BluetoothGattDescriptorClient::Observer* observer) override {
    DCHECK(observer);
    observers_.AddObserver(observer);
  }

  // BluetoothGattDescriptorClientImpl override.
  void RemoveObserver(
      BluetoothGattDescriptorClient::Observer* observer) override {
    DCHECK(observer);
    observers_.RemoveObserver(observer);
  }

  // BluetoothGattDescriptorClientImpl override.
  std::vector<dbus::ObjectPath> GetDescriptors() override {
    DCHECK(object_manager_);
    return object_manager_->GetObjectsWithInterface(
        bluetooth_gatt_descriptor::kBluetoothGattDescriptorInterface);
  }

  // BluetoothGattDescriptorClientImpl override.
  Properties* GetProperties(const dbus::ObjectPath& object_path) override {
    DCHECK(object_manager_);
    return static_cast<Properties*>(object_manager_->GetProperties(
        object_path,
        bluetooth_gatt_descriptor::kBluetoothGattDescriptorInterface));
  }

  // BluetoothGattDescriptorClientImpl override.
  void ReadValue(const dbus::ObjectPath& object_path,
                 const ValueCallback& callback,
                 const ErrorCallback& error_callback) override {
    dbus::ObjectProxy* object_proxy =
        object_manager_->GetObjectProxy(object_path);
    if (!object_proxy) {
      error_callback.Run(kUnknownDescriptorError, "");
      return;
    }

    dbus::MethodCall method_call(
        bluetooth_gatt_descriptor::kBluetoothGattDescriptorInterface,
        bluetooth_gatt_descriptor::kReadValue);

    object_proxy->CallMethodWithErrorCallback(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&BluetoothGattDescriptorClientImpl::OnValueSuccess,
                   weak_ptr_factory_.GetWeakPtr(), callback),
        base::Bind(&BluetoothGattDescriptorClientImpl::OnError,
                   weak_ptr_factory_.GetWeakPtr(), error_callback));
  }

  // BluetoothGattDescriptorClientImpl override.
  void WriteValue(const dbus::ObjectPath& object_path,
                  const std::vector<uint8>& value,
                  const base::Closure& callback,
                  const ErrorCallback& error_callback) override {
    dbus::ObjectProxy* object_proxy =
        object_manager_->GetObjectProxy(object_path);
    if (!object_proxy) {
      error_callback.Run(kUnknownDescriptorError, "");
      return;
    }

    dbus::MethodCall method_call(
        bluetooth_gatt_descriptor::kBluetoothGattDescriptorInterface,
        bluetooth_gatt_descriptor::kWriteValue);
    dbus::MessageWriter writer(&method_call);
    writer.AppendArrayOfBytes(value.data(), value.size());

    object_proxy->CallMethodWithErrorCallback(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&BluetoothGattDescriptorClientImpl::OnSuccess,
                   weak_ptr_factory_.GetWeakPtr(), callback),
        base::Bind(&BluetoothGattDescriptorClientImpl::OnError,
                   weak_ptr_factory_.GetWeakPtr(), error_callback));
  }

  // dbus::ObjectManager::Interface override.
  dbus::PropertySet* CreateProperties(
      dbus::ObjectProxy* object_proxy,
      const dbus::ObjectPath& object_path,
      const std::string& interface_name) override {
    Properties* properties = new Properties(
        object_proxy, interface_name,
        base::Bind(&BluetoothGattDescriptorClientImpl::OnPropertyChanged,
                   weak_ptr_factory_.GetWeakPtr(), object_path));
    return static_cast<dbus::PropertySet*>(properties);
  }

  // dbus::ObjectManager::Interface override.
  void ObjectAdded(const dbus::ObjectPath& object_path,
                   const std::string& interface_name) override {
    VLOG(2) << "Remote GATT descriptor added: " << object_path.value();
    FOR_EACH_OBSERVER(BluetoothGattDescriptorClient::Observer, observers_,
                      GattDescriptorAdded(object_path));
  }

  // dbus::ObjectManager::Interface override.
  void ObjectRemoved(const dbus::ObjectPath& object_path,
                     const std::string& interface_name) override {
    VLOG(2) << "Remote GATT descriptor removed: " << object_path.value();
    FOR_EACH_OBSERVER(BluetoothGattDescriptorClient::Observer, observers_,
                      GattDescriptorRemoved(object_path));
  }

 protected:
  // chromeos::DBusClient override.
  void Init(dbus::Bus* bus) override {
    object_manager_ = bus->GetObjectManager(
        bluetooth_object_manager::kBluetoothObjectManagerServiceName,
        dbus::ObjectPath(
            bluetooth_object_manager::kBluetoothObjectManagerServicePath));
    object_manager_->RegisterInterface(
        bluetooth_gatt_descriptor::kBluetoothGattDescriptorInterface, this);
  }

 private:
  // Called by dbus::PropertySet when a property value is changed, either by
  // result of a signal or response to a GetAll() or Get() call. Informs
  // observers.
  virtual void OnPropertyChanged(const dbus::ObjectPath& object_path,
                                 const std::string& property_name) {
    VLOG(2) << "Remote GATT descriptor property changed: "
            << object_path.value() << ": " << property_name;
    FOR_EACH_OBSERVER(
        BluetoothGattDescriptorClient::Observer, observers_,
        GattDescriptorPropertyChanged(object_path, property_name));
  }

  // Called when a response for a successful method call is received.
  void OnSuccess(const base::Closure& callback, dbus::Response* response) {
    DCHECK(response);
    callback.Run();
  }

  // Called when a descriptor value response for a successful method call is
  // received.
  void OnValueSuccess(const ValueCallback& callback, dbus::Response* response) {
    DCHECK(response);
    dbus::MessageReader reader(response);

    const uint8* bytes = NULL;
    size_t length = 0;

    if (!reader.PopArrayOfBytes(&bytes, &length))
      VLOG(2) << "Error reading array of bytes in ValueCallback";

    std::vector<uint8> value;

    if (bytes)
      value.assign(bytes, bytes + length);

    callback.Run(value);
  }

  // Called when a response for a failed method call is received.
  void OnError(const ErrorCallback& error_callback,
               dbus::ErrorResponse* response) {
    // Error response has optional error message argument.
    std::string error_name;
    std::string error_message;
    if (response) {
      dbus::MessageReader reader(response);
      error_name = response->GetErrorName();
      reader.PopString(&error_message);
    } else {
      error_name = kNoResponseError;
      error_message = "";
    }
    error_callback.Run(error_name, error_message);
  }

  dbus::ObjectManager* object_manager_;

  // List of observers interested in event notifications from us.
  base::ObserverList<BluetoothGattDescriptorClient::Observer> observers_;

  // Weak pointer factory for generating 'this' pointers that might live longer
  // than we do.
  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<BluetoothGattDescriptorClientImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(BluetoothGattDescriptorClientImpl);
};

BluetoothGattDescriptorClient::BluetoothGattDescriptorClient() {}

BluetoothGattDescriptorClient::~BluetoothGattDescriptorClient() {}

// static
BluetoothGattDescriptorClient* BluetoothGattDescriptorClient::Create() {
  return new BluetoothGattDescriptorClientImpl();
}

}  // namespace chromeos
