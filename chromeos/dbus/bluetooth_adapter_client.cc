// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/bluetooth_adapter_client.h"

#include "base/bind.h"
#include "base/logging.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_manager.h"
#include "dbus/object_proxy.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

BluetoothAdapterClient::DiscoveryFilter::DiscoveryFilter() {}

BluetoothAdapterClient::DiscoveryFilter::~DiscoveryFilter() {}

void BluetoothAdapterClient::DiscoveryFilter::CopyFrom(
    const DiscoveryFilter& filter) {
  if (filter.rssi.get())
    rssi.reset(new int16_t(*filter.rssi));
  else
    rssi.reset();

  if (filter.pathloss.get())
    pathloss.reset(new uint16_t(*filter.pathloss));
  else
    pathloss.reset();

  if (filter.transport.get())
    transport.reset(new std::string(*filter.transport));
  else
    transport.reset();

  if (filter.uuids.get())
    uuids.reset(new std::vector<std::string>(*filter.uuids));
  else
    uuids.reset();
}

const char BluetoothAdapterClient::kNoResponseError[] =
    "org.chromium.Error.NoResponse";
const char BluetoothAdapterClient::kUnknownAdapterError[] =
    "org.chromium.Error.UnknownAdapter";

BluetoothAdapterClient::Properties::Properties(
    dbus::ObjectProxy* object_proxy,
    const std::string& interface_name,
    const PropertyChangedCallback& callback)
    : dbus::PropertySet(object_proxy, interface_name, callback) {
  RegisterProperty(bluetooth_adapter::kAddressProperty, &address);
  RegisterProperty(bluetooth_adapter::kNameProperty, &name);
  RegisterProperty(bluetooth_adapter::kAliasProperty, &alias);
  RegisterProperty(bluetooth_adapter::kClassProperty, &bluetooth_class);
  RegisterProperty(bluetooth_adapter::kPoweredProperty, &powered);
  RegisterProperty(bluetooth_adapter::kDiscoverableProperty, &discoverable);
  RegisterProperty(bluetooth_adapter::kPairableProperty, &pairable);
  RegisterProperty(bluetooth_adapter::kPairableTimeoutProperty,
                   &pairable_timeout);
  RegisterProperty(bluetooth_adapter::kDiscoverableTimeoutProperty,
                   &discoverable_timeout);
  RegisterProperty(bluetooth_adapter::kDiscoveringProperty, &discovering);
  RegisterProperty(bluetooth_adapter::kUUIDsProperty, &uuids);
  RegisterProperty(bluetooth_adapter::kModaliasProperty, &modalias);
}

BluetoothAdapterClient::Properties::~Properties() {}

// The BluetoothAdapterClient implementation used in production.
class BluetoothAdapterClientImpl : public BluetoothAdapterClient,
                                   public dbus::ObjectManager::Interface {
 public:
  BluetoothAdapterClientImpl()
      : object_manager_(NULL), weak_ptr_factory_(this) {}

  ~BluetoothAdapterClientImpl() override {
    object_manager_->UnregisterInterface(
        bluetooth_adapter::kBluetoothAdapterInterface);
  }

  // BluetoothAdapterClient override.
  void AddObserver(BluetoothAdapterClient::Observer* observer) override {
    DCHECK(observer);
    observers_.AddObserver(observer);
  }

  // BluetoothAdapterClient override.
  void RemoveObserver(BluetoothAdapterClient::Observer* observer) override {
    DCHECK(observer);
    observers_.RemoveObserver(observer);
  }

  // Returns the list of adapter object paths known to the system.
  std::vector<dbus::ObjectPath> GetAdapters() override {
    return object_manager_->GetObjectsWithInterface(
        bluetooth_adapter::kBluetoothAdapterInterface);
  }

  // dbus::ObjectManager::Interface override.
  dbus::PropertySet* CreateProperties(
      dbus::ObjectProxy* object_proxy,
      const dbus::ObjectPath& object_path,
      const std::string& interface_name) override {
    Properties* properties = new Properties(
        object_proxy, interface_name,
        base::Bind(&BluetoothAdapterClientImpl::OnPropertyChanged,
                   weak_ptr_factory_.GetWeakPtr(), object_path));
    return static_cast<dbus::PropertySet*>(properties);
  }

  // BluetoothAdapterClient override.
  Properties* GetProperties(const dbus::ObjectPath& object_path) override {
    return static_cast<Properties*>(object_manager_->GetProperties(
        object_path, bluetooth_adapter::kBluetoothAdapterInterface));
  }

  // BluetoothAdapterClient override.
  void StartDiscovery(const dbus::ObjectPath& object_path,
                      const base::Closure& callback,
                      const ErrorCallback& error_callback) override {
    dbus::MethodCall method_call(bluetooth_adapter::kBluetoothAdapterInterface,
                                 bluetooth_adapter::kStartDiscovery);

    dbus::ObjectProxy* object_proxy =
        object_manager_->GetObjectProxy(object_path);
    if (!object_proxy) {
      error_callback.Run(kUnknownAdapterError, "");
      return;
    }

    object_proxy->CallMethodWithErrorCallback(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&BluetoothAdapterClientImpl::OnSuccess,
                   weak_ptr_factory_.GetWeakPtr(), callback),
        base::Bind(&BluetoothAdapterClientImpl::OnError,
                   weak_ptr_factory_.GetWeakPtr(), error_callback));
  }

  // BluetoothAdapterClient override.
  void StopDiscovery(const dbus::ObjectPath& object_path,
                     const base::Closure& callback,
                     const ErrorCallback& error_callback) override {
    dbus::MethodCall method_call(bluetooth_adapter::kBluetoothAdapterInterface,
                                 bluetooth_adapter::kStopDiscovery);

    dbus::ObjectProxy* object_proxy =
        object_manager_->GetObjectProxy(object_path);
    if (!object_proxy) {
      error_callback.Run(kUnknownAdapterError, "");
      return;
    }

    object_proxy->CallMethodWithErrorCallback(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&BluetoothAdapterClientImpl::OnSuccess,
                   weak_ptr_factory_.GetWeakPtr(), callback),
        base::Bind(&BluetoothAdapterClientImpl::OnError,
                   weak_ptr_factory_.GetWeakPtr(), error_callback));
  }

  // BluetoothAdapterClient override.
  void RemoveDevice(const dbus::ObjectPath& object_path,
                    const dbus::ObjectPath& device_path,
                    const base::Closure& callback,
                    const ErrorCallback& error_callback) override {
    dbus::MethodCall method_call(bluetooth_adapter::kBluetoothAdapterInterface,
                                 bluetooth_adapter::kRemoveDevice);

    dbus::MessageWriter writer(&method_call);
    writer.AppendObjectPath(device_path);

    dbus::ObjectProxy* object_proxy =
        object_manager_->GetObjectProxy(object_path);
    if (!object_proxy) {
      error_callback.Run(kUnknownAdapterError, "");
      return;
    }

    object_proxy->CallMethodWithErrorCallback(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&BluetoothAdapterClientImpl::OnSuccess,
                   weak_ptr_factory_.GetWeakPtr(), callback),
        base::Bind(&BluetoothAdapterClientImpl::OnError,
                   weak_ptr_factory_.GetWeakPtr(), error_callback));
  }

  // BluetoothAdapterClient override.
  void SetDiscoveryFilter(const dbus::ObjectPath& object_path,
                          const DiscoveryFilter& discovery_filter,
                          const base::Closure& callback,
                          const ErrorCallback& error_callback) override {
    dbus::MethodCall method_call(bluetooth_adapter::kBluetoothAdapterInterface,
                                 bluetooth_adapter::kSetDiscoveryFilter);

    dbus::MessageWriter writer(&method_call);
    dbus::MessageWriter dict_writer(nullptr);

    dbus::ObjectProxy* object_proxy =
        object_manager_->GetObjectProxy(object_path);
    if (!object_proxy) {
      error_callback.Run(kUnknownAdapterError, "");
      return;
    }

    writer.OpenArray("{sv}", &dict_writer);

    if (discovery_filter.uuids.get()) {
      std::vector<std::string>* uuids = discovery_filter.uuids.get();
      dbus::MessageWriter uuids_entry_writer(nullptr);
      dict_writer.OpenDictEntry(&uuids_entry_writer);
      uuids_entry_writer.AppendString(
          bluetooth_adapter::kDiscoveryFilterParameterUUIDs);

      dbus::MessageWriter uuids_array_variant(nullptr);
      uuids_entry_writer.OpenVariant("as", &uuids_array_variant);
      dbus::MessageWriter uuids_array(nullptr);
      uuids_array_variant.OpenArray("s", &uuids_array);

      for (auto& it : *uuids)
        uuids_array.AppendString(it);

      uuids_array_variant.CloseContainer(&uuids_array);
      uuids_entry_writer.CloseContainer(&uuids_array_variant);
      dict_writer.CloseContainer(&uuids_entry_writer);
    }

    if (discovery_filter.rssi.get()) {
      dbus::MessageWriter rssi_entry_writer(nullptr);
      dict_writer.OpenDictEntry(&rssi_entry_writer);
      rssi_entry_writer.AppendString(
          bluetooth_adapter::kDiscoveryFilterParameterRSSI);
      rssi_entry_writer.AppendVariantOfInt16(*discovery_filter.rssi.get());
      dict_writer.CloseContainer(&rssi_entry_writer);
    }

    if (discovery_filter.pathloss.get()) {
      dbus::MessageWriter pathloss_entry_writer(nullptr);
      dict_writer.OpenDictEntry(&pathloss_entry_writer);
      pathloss_entry_writer.AppendString(
          bluetooth_adapter::kDiscoveryFilterParameterPathloss);
      pathloss_entry_writer.AppendVariantOfUint16(
          *discovery_filter.pathloss.get());
      dict_writer.CloseContainer(&pathloss_entry_writer);
    }

    if (discovery_filter.transport.get()) {
      dbus::MessageWriter transport_entry_writer(nullptr);
      dict_writer.OpenDictEntry(&transport_entry_writer);
      transport_entry_writer.AppendString(
          bluetooth_adapter::kDiscoveryFilterParameterTransport);
      transport_entry_writer.AppendVariantOfString(
          *discovery_filter.transport.get());
      dict_writer.CloseContainer(&transport_entry_writer);
    }

    writer.CloseContainer(&dict_writer);

    object_proxy->CallMethodWithErrorCallback(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&BluetoothAdapterClientImpl::OnSuccess,
                   weak_ptr_factory_.GetWeakPtr(), callback),
        base::Bind(&BluetoothAdapterClientImpl::OnError,
                   weak_ptr_factory_.GetWeakPtr(), error_callback));
  }

 protected:
  void Init(dbus::Bus* bus) override {
    object_manager_ = bus->GetObjectManager(
        bluetooth_object_manager::kBluetoothObjectManagerServiceName,
        dbus::ObjectPath(
            bluetooth_object_manager::kBluetoothObjectManagerServicePath));
    object_manager_->RegisterInterface(
        bluetooth_adapter::kBluetoothAdapterInterface, this);
  }

 private:
  // Called by dbus::ObjectManager when an object with the adapter interface
  // is created. Informs observers.
  void ObjectAdded(const dbus::ObjectPath& object_path,
                   const std::string& interface_name) override {
    FOR_EACH_OBSERVER(BluetoothAdapterClient::Observer, observers_,
                      AdapterAdded(object_path));
  }

  // Called by dbus::ObjectManager when an object with the adapter interface
  // is removed. Informs observers.
  void ObjectRemoved(const dbus::ObjectPath& object_path,
                     const std::string& interface_name) override {
    FOR_EACH_OBSERVER(BluetoothAdapterClient::Observer, observers_,
                      AdapterRemoved(object_path));
  }

  // Called by dbus::PropertySet when a property value is changed,
  // either by result of a signal or response to a GetAll() or Get()
  // call. Informs observers.
  void OnPropertyChanged(const dbus::ObjectPath& object_path,
                         const std::string& property_name) {
    FOR_EACH_OBSERVER(BluetoothAdapterClient::Observer, observers_,
                      AdapterPropertyChanged(object_path, property_name));
  }

  // Called when a response for successful method call is received.
  void OnSuccess(const base::Closure& callback, dbus::Response* response) {
    DCHECK(response);
    callback.Run();
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
  base::ObserverList<BluetoothAdapterClient::Observer> observers_;

  // Weak pointer factory for generating 'this' pointers that might live longer
  // than we do.
  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<BluetoothAdapterClientImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(BluetoothAdapterClientImpl);
};

BluetoothAdapterClient::BluetoothAdapterClient() {}

BluetoothAdapterClient::~BluetoothAdapterClient() {}

BluetoothAdapterClient* BluetoothAdapterClient::Create() {
  return new BluetoothAdapterClientImpl;
}

}  // namespace chromeos
