// SPDX-License-Identifier: GPL-3.0-or-later

#include "backends/upower_client.hpp"

#include <gio/gio.h>

#include <cstring>
#include <utility>

namespace lcos {
namespace {

constexpr const char* kService = "org.freedesktop.UPower";
constexpr const char* kManagerPath = "/org/freedesktop/UPower";
constexpr const char* kManagerIface = "org.freedesktop.UPower";
constexpr const char* kDeviceIface = "org.freedesktop.UPower.Device";

std::string variant_string(GVariant* value) {
  if (value == nullptr || !g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
    return {};
  }
  const char* text = g_variant_get_string(value, nullptr);
  return text != nullptr ? text : "";
}

bool variant_bool(GVariant* value) {
  return value != nullptr && g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN) &&
         g_variant_get_boolean(value) != FALSE;
}

unsigned variant_uint(GVariant* value) {
  if (value == nullptr) {
    return 0;
  }
  if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) {
    return g_variant_get_uint32(value);
  }
  if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT16)) {
    return g_variant_get_uint16(value);
  }
  return 0;
}

double variant_double(GVariant* value) {
  if (value != nullptr && g_variant_is_of_type(value, G_VARIANT_TYPE_DOUBLE)) {
    return g_variant_get_double(value);
  }
  return 0;
}

std::int64_t variant_seconds(GVariant* value) {
  if (value == nullptr) {
    return 0;
  }
  if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) {
    return g_variant_get_int64(value);
  }
  if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT64)) {
    return static_cast<std::int64_t>(g_variant_get_uint64(value));
  }
  return 0;
}

GVariant* cached(GDBusProxy* proxy, const char* name) {
  return proxy == nullptr ? nullptr : g_dbus_proxy_get_cached_property(proxy, name);
}

UpDeviceInfo read_device(GDBusProxy* proxy, const std::string& path) {
  UpDeviceInfo info;
  info.path = path;
  GVariant* icon = cached(proxy, "IconName");
  GVariant* model = cached(proxy, "Model");
  GVariant* vendor = cached(proxy, "Vendor");
  GVariant* serial = cached(proxy, "Serial");
  GVariant* kind = cached(proxy, "Type");
  GVariant* state = cached(proxy, "State");
  GVariant* tech = cached(proxy, "Technology");
  GVariant* percent = cached(proxy, "Percentage");
  GVariant* energy = cached(proxy, "Energy");
  GVariant* full = cached(proxy, "EnergyFull");
  GVariant* design = cached(proxy, "EnergyFullDesign");
  GVariant* empty = cached(proxy, "EnergyEmpty");
  GVariant* rate = cached(proxy, "EnergyRate");
  GVariant* voltage = cached(proxy, "Voltage");
  GVariant* capacity = cached(proxy, "Capacity");
  GVariant* empty_time = cached(proxy, "TimeToEmpty");
  GVariant* full_time = cached(proxy, "TimeToFull");
  GVariant* supply = cached(proxy, "PowerSupply");
  GVariant* present = cached(proxy, "IsPresent");
  GVariant* online = cached(proxy, "Online");
  info.icon = variant_string(icon);
  info.model = variant_string(model);
  info.vendor = variant_string(vendor);
  info.serial = variant_string(serial);
  info.kind = variant_uint(kind);
  info.state = variant_uint(state);
  info.technology = variant_uint(tech);
  info.percentage = percent != nullptr ? variant_double(percent) : -1;
  info.energy = variant_double(energy);
  info.energy_full = variant_double(full);
  info.energy_full_design = variant_double(design);
  info.energy_empty = variant_double(empty);
  info.energy_rate = variant_double(rate);
  info.voltage = variant_double(voltage);
  info.capacity = variant_double(capacity);
  info.time_to_empty = variant_seconds(empty_time);
  info.time_to_full = variant_seconds(full_time);
  info.power_supply = variant_bool(supply);
  info.present = present == nullptr || variant_bool(present);
  info.online = variant_bool(online);
  GVariant* values[] = {icon, model, vendor, serial, kind, state, tech, percent, energy, full,
                        design, empty, rate, voltage, capacity, empty_time, full_time, supply,
                        present, online};
  for (GVariant* value : values) {
    if (value != nullptr) {
      g_variant_unref(value);
    }
  }
  return info;
}

}  // namespace

struct UpowerClient::Impl {
  UpowerClient* owner = nullptr;
  bool alive = true;
  GDBusProxy* manager = nullptr;
  std::vector<GDBusProxy*> devices;
  gulong manager_signal = 0;
};

void UpowerClient::manager_signal_cb(void*, const char*, const char* signal_name, void* parameters,
                                     void* user) {
  auto* impl = static_cast<Impl*>(user);
  if (impl == nullptr || !impl->alive || impl->owner == nullptr) {
    return;
  }
  impl->owner->on_manager_signal(signal_name, parameters);
}

void UpowerClient::device_properties_cb(void*, void*, char**, void* user) {
  auto* impl = static_cast<Impl*>(user);
  if (impl == nullptr || !impl->alive || impl->owner == nullptr) {
    return;
  }
  impl->owner->on_device_properties();
}

UpowerClient::UpowerClient() : impl_(new Impl) { impl_->owner = this; }

UpowerClient::~UpowerClient() {
  impl_->alive = false;
  impl_->owner = nullptr;
  for (GDBusProxy* proxy : impl_->devices) {
    g_signal_handlers_disconnect_by_data(proxy, impl_);
    g_object_unref(proxy);
  }
  impl_->devices.clear();
  if (impl_->manager != nullptr) {
    if (impl_->manager_signal != 0) {
      g_signal_handler_disconnect(impl_->manager, impl_->manager_signal);
    }
    g_object_unref(impl_->manager);
    impl_->manager = nullptr;
  }
  delete impl_;
  impl_ = nullptr;
}

void UpowerClient::start() {
  if (impl_->manager != nullptr) {
    return;
  }
  GError* error = nullptr;
  impl_->manager = g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START, nullptr, kService, kManagerPath,
      kManagerIface, nullptr, &error);
  if (impl_->manager == nullptr) {
    available_ = false;
    message_ = error != nullptr ? error->message : "UPower is not available.";
    if (error != nullptr) {
      g_error_free(error);
    }
    changed.emit();
    return;
  }
  gchar* owner = g_dbus_proxy_get_name_owner(impl_->manager);
  if (owner == nullptr) {
    available_ = false;
    message_ = "UPower is not running.";
    changed.emit();
    return;
  }
  g_free(owner);
  available_ = true;
  message_.clear();
  impl_->manager_signal =
      g_signal_connect(impl_->manager, "g-signal", G_CALLBACK(manager_signal_cb), impl_);
  reload();
}

void UpowerClient::reload() {
  for (GDBusProxy* proxy : impl_->devices) {
    g_signal_handlers_disconnect_by_data(proxy, impl_);
    g_object_unref(proxy);
  }
  impl_->devices.clear();
  devices_.clear();
  if (impl_->manager == nullptr) {
    changed.emit();
    return;
  }
  GError* error = nullptr;
  GVariant* result = g_dbus_proxy_call_sync(impl_->manager, "EnumerateDevices", nullptr,
                                            G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
  if (result == nullptr) {
    available_ = false;
    message_ = error != nullptr ? error->message : "UPower did not list any devices.";
    if (error != nullptr) {
      g_error_free(error);
    }
    changed.emit();
    return;
  }
  available_ = true;
  message_.clear();
  GVariant* paths = g_variant_get_child_value(result, 0);
  const gsize count = paths != nullptr ? g_variant_n_children(paths) : 0;
  for (gsize i = 0; i < count; ++i) {
    GVariant* entry = g_variant_get_child_value(paths, i);
    const char* path = entry != nullptr ? g_variant_get_string(entry, nullptr) : nullptr;
    // Object paths use the 'o' type, not 's'.
    if (entry != nullptr && g_variant_is_of_type(entry, G_VARIANT_TYPE_OBJECT_PATH)) {
      path = g_variant_get_string(entry, nullptr);
    }
    if (path != nullptr && path[0] != '\0') {
      GError* device_error = nullptr;
      GDBusProxy* proxy = g_dbus_proxy_new_for_bus_sync(
          G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, nullptr, kService, path, kDeviceIface,
          nullptr, &device_error);
      if (proxy != nullptr) {
        g_signal_connect(proxy, "g-properties-changed", G_CALLBACK(device_properties_cb), impl_);
        impl_->devices.push_back(proxy);
        devices_.push_back(read_device(proxy, path));
      }
      if (device_error != nullptr) {
        g_error_free(device_error);
      }
    }
    if (entry != nullptr) {
      g_variant_unref(entry);
    }
  }
  if (paths != nullptr) {
    g_variant_unref(paths);
  }
  g_variant_unref(result);
  changed.emit();
}

void UpowerClient::on_manager_signal(const char* name, void* parameters) {
  if (!impl_->alive || name == nullptr) {
    return;
  }
  if (std::strcmp(name, "DeviceAdded") == 0 || std::strcmp(name, "DeviceRemoved") == 0) {
    (void)parameters;
    reload();
  }
}

void UpowerClient::on_device_properties() {
  if (!impl_->alive) {
    return;
  }
  devices_.clear();
  for (GDBusProxy* proxy : impl_->devices) {
    const char* path = g_dbus_proxy_get_object_path(proxy);
    devices_.push_back(read_device(proxy, path != nullptr ? path : ""));
  }
  changed.emit();
}

}  // namespace lcos
