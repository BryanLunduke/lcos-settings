// SPDX-License-Identifier: GPL-3.0-or-later

#include "backends/bluez_client.hpp"

#include <gio/gio.h>

#include <utility>

namespace lcos {
namespace {

constexpr const char* kService = "org.bluez";
constexpr const char* kAdapter = "org.bluez.Adapter1";
constexpr const char* kDevice = "org.bluez.Device1";
constexpr const char* kProperties = "org.freedesktop.DBus.Properties";

std::string take_error(GError* error, const char* fallback) {
  std::string text = fallback != nullptr ? fallback : "Bluetooth request failed.";
  if (error != nullptr) {
    if (error->message != nullptr && error->message[0] != '\0') {
      text = error->message;
    }
    g_error_free(error);
  }
  return text;
}

GDBusConnection* system_bus(std::string* error) {
  GError* gerror = nullptr;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &gerror);
  if (bus == nullptr && error != nullptr) {
    *error = take_error(gerror, "The system bus is not available.");
  }
  return bus;
}

std::string call(const std::string& path, const char* interface, const char* method, GVariant* params,
                 int timeout_ms) {
  std::string error;
  GDBusConnection* bus = system_bus(&error);
  if (bus == nullptr) {
    if (params != nullptr) {
      g_variant_unref(params);
    }
    return error;
  }
  GError* gerror = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(bus, kService, path.c_str(), interface, method, params,
                                                nullptr, G_DBUS_CALL_FLAGS_NONE, timeout_ms, nullptr,
                                                &gerror);
  g_object_unref(bus);
  if (reply == nullptr) {
    return take_error(gerror, "Bluetooth refused that request.");
  }
  g_variant_unref(reply);
  return {};
}

std::string set_property(const std::string& path, const char* interface, const char* name,
                         GVariant* value) {
  return call(path, kProperties, "Set", g_variant_new("(ssv)", interface, name, value), 4000);
}

std::string variant_string(GVariant* dictionary, const char* key) {
  GVariant* value = g_variant_lookup_value(dictionary, key, G_VARIANT_TYPE_STRING);
  std::string text;
  if (value != nullptr) {
    const char* string = g_variant_get_string(value, nullptr);
    text = string != nullptr ? string : "";
    g_variant_unref(value);
  }
  return text;
}

bool variant_bool(GVariant* dictionary, const char* key) {
  GVariant* value = g_variant_lookup_value(dictionary, key, G_VARIANT_TYPE_BOOLEAN);
  const bool on = value != nullptr && g_variant_get_boolean(value) != FALSE;
  if (value != nullptr) {
    g_variant_unref(value);
  }
  return on;
}

BtSnapshot sample_snapshot() {
  BtSnapshot snapshot;
  snapshot.service = true;
  snapshot.adapter = true;
  snapshot.adapter_info.path = "/org/bluez/hci0";
  snapshot.adapter_info.name = "LCOS Computer";
  snapshot.adapter_info.address = "00:11:22:33:44:55";
  snapshot.adapter_info.powered = true;
  snapshot.adapter_info.discoverable = false;
  BtDevice phone;
  phone.path = "/org/bluez/hci0/dev_AA";
  phone.name = "Pocket Phone";
  phone.address = "AA:BB:CC:DD:EE:01";
  phone.icon = "phone";
  phone.paired = true;
  phone.connected = true;
  phone.trusted = true;
  BtDevice headset;
  headset.path = "/org/bluez/hci0/dev_BB";
  headset.name = "Headset";
  headset.address = "AA:BB:CC:DD:EE:02";
  headset.icon = "audio-headset";
  headset.paired = true;
  snapshot.devices.push_back(phone);
  snapshot.devices.push_back(headset);
  return snapshot;
}

}  // namespace

BtSnapshot BluezClient::snapshot(bool sample) const {
  if (sample) {
    return sample_snapshot();
  }
  BtSnapshot snapshot;
  std::string error;
  GDBusConnection* bus = system_bus(&error);
  if (bus == nullptr) {
    snapshot.error = "Bluetooth is not available.";
    return snapshot;
  }
  GError* gerror = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(
      bus, kService, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", nullptr,
      G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, 2500, nullptr, &gerror);
  g_object_unref(bus);
  if (reply == nullptr) {
    const std::string message = take_error(gerror, "Bluetooth is not available.");
    const bool missing = message.find("refused") != std::string::npos ||
                         message.find("not provided") != std::string::npos ||
                         message.find("not found") != std::string::npos ||
                         message.find("No such") != std::string::npos ||
                         message.find("ServiceUnknown") != std::string::npos;
    snapshot.error = missing ? "Bluetooth is not available." : message;
    return snapshot;
  }
  snapshot.service = true;
  GVariant* objects = g_variant_get_child_value(reply, 0);
  GVariantIter iter;
  g_variant_iter_init(&iter, objects);
  const char* path = nullptr;
  GVariant* interfaces = nullptr;
  while (g_variant_iter_loop(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
    GVariant* adapter = g_variant_lookup_value(interfaces, kAdapter, G_VARIANT_TYPE("a{sv}"));
    if (adapter != nullptr && !snapshot.adapter) {
      snapshot.adapter = true;
      snapshot.adapter_info.path = path != nullptr ? path : "";
      snapshot.adapter_info.name = variant_string(adapter, "Alias");
      if (snapshot.adapter_info.name.empty()) {
        snapshot.adapter_info.name = variant_string(adapter, "Name");
      }
      snapshot.adapter_info.address = variant_string(adapter, "Address");
      snapshot.adapter_info.powered = variant_bool(adapter, "Powered");
      snapshot.adapter_info.discoverable = variant_bool(adapter, "Discoverable");
      g_variant_unref(adapter);
    } else if (adapter != nullptr) {
      g_variant_unref(adapter);
    }
    GVariant* device = g_variant_lookup_value(interfaces, kDevice, G_VARIANT_TYPE("a{sv}"));
    if (device != nullptr) {
      BtDevice row;
      row.path = path != nullptr ? path : "";
      row.name = variant_string(device, "Alias");
      if (row.name.empty()) {
        row.name = variant_string(device, "Name");
      }
      if (row.name.empty()) {
        row.name = variant_string(device, "Address");
      }
      row.address = variant_string(device, "Address");
      row.icon = variant_string(device, "Icon");
      row.paired = variant_bool(device, "Paired");
      row.connected = variant_bool(device, "Connected");
      row.trusted = variant_bool(device, "Trusted");
      snapshot.devices.push_back(std::move(row));
      g_variant_unref(device);
    }
  }
  g_variant_unref(objects);
  g_variant_unref(reply);
  if (!snapshot.adapter) {
    snapshot.error = "No Bluetooth adapter is available.";
  }
  return snapshot;
}

std::string BluezClient::set_powered(const std::string& adapter, bool on) {
  if (adapter.empty()) {
    return "No Bluetooth adapter is available.";
  }
  return set_property(adapter, kAdapter, "Powered", g_variant_new_boolean(on));
}

std::string BluezClient::set_discoverable(const std::string& adapter, bool on) {
  if (adapter.empty()) {
    return "No Bluetooth adapter is available.";
  }
  return set_property(adapter, kAdapter, "Discoverable", g_variant_new_boolean(on));
}

std::string BluezClient::set_name(const std::string& adapter, const std::string& name) {
  if (adapter.empty()) {
    return "No Bluetooth adapter is available.";
  }
  if (name.empty()) {
    return "The adapter needs a name.";
  }
  return set_property(adapter, kAdapter, "Alias", g_variant_new_string(name.c_str()));
}

std::string BluezClient::start_discovery(const std::string& adapter) {
  if (adapter.empty()) {
    return "No Bluetooth adapter is available.";
  }
  return call(adapter, kAdapter, "StartDiscovery", nullptr, 4000);
}

std::string BluezClient::stop_discovery(const std::string& adapter) {
  if (adapter.empty()) {
    return {};
  }
  return call(adapter, kAdapter, "StopDiscovery", nullptr, 4000);
}

std::string BluezClient::pair_device(const std::string& device) {
  return call(device, kDevice, "Pair", nullptr, 25000);
}

std::string BluezClient::connect_device(const std::string& device) {
  return call(device, kDevice, "Connect", nullptr, 20000);
}

std::string BluezClient::disconnect_device(const std::string& device) {
  return call(device, kDevice, "Disconnect", nullptr, 8000);
}

std::string BluezClient::set_trusted(const std::string& device, bool trusted) {
  return set_property(device, kDevice, "Trusted", g_variant_new_boolean(trusted));
}

std::string BluezClient::remove_device(const std::string& adapter, const std::string& device) {
  if (adapter.empty()) {
    return "No Bluetooth adapter is available.";
  }
  return call(adapter, kAdapter, "RemoveDevice", g_variant_new("(o)", device.c_str()), 8000);
}

}  // namespace lcos
