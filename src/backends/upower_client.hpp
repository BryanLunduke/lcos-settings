// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_UPOWER_CLIENT_HPP
#define LCOS_UPOWER_CLIENT_HPP

#include <sigc++/signal.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lcos {

// One org.freedesktop.UPower.Device, as the Devices tab of
// xfce4-power-manager-settings shows it.
struct UpDeviceInfo {
  std::string path;
  std::string icon;
  std::string model;
  std::string vendor;
  std::string serial;
  unsigned kind = 0;
  unsigned state = 0;
  unsigned technology = 0;
  double percentage = -1;
  double energy = 0;
  double energy_full = 0;
  double energy_full_design = 0;
  double energy_empty = 0;
  double energy_rate = 0;
  double voltage = 0;
  double capacity = 0;
  std::int64_t time_to_empty = 0;
  std::int64_t time_to_full = 0;
  bool power_supply = false;
  bool present = false;
  bool online = false;
};

// Live view of UPower on the system bus. The client does not start UPower.
class UpowerClient {
 public:
  UpowerClient();
  ~UpowerClient();

  UpowerClient(const UpowerClient&) = delete;
  UpowerClient& operator=(const UpowerClient&) = delete;

  void start();
  bool available() const { return available_; }
  const std::string& message() const { return message_; }
  const std::vector<UpDeviceInfo>& devices() const { return devices_; }

  sigc::signal<void> changed;

 private:
  struct Impl;
  Impl* impl_ = nullptr;
  bool available_ = false;
  std::string message_;
  std::vector<UpDeviceInfo> devices_;

  void reload();
  void on_manager_signal(const char* name, void* parameters);
  void on_device_properties();
  static void manager_signal_cb(void* proxy, const char* sender, const char* signal_name,
                                void* parameters, void* user);
  static void device_properties_cb(void* proxy, void* changed, char** invalidated, void* user);
};

}  // namespace lcos

#endif
