// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_BLUEZ_CLIENT_HPP
#define LCOS_BLUEZ_CLIENT_HPP

#include "logic/bluetooth_model.hpp"

#include <string>

namespace lcos {

// BlueZ over the system D-Bus. A missing service or adapter is a snapshot flag.
class BluezClient {
 public:
  BtSnapshot snapshot(bool sample) const;
  std::string set_powered(const std::string& adapter, bool on);
  std::string set_discoverable(const std::string& adapter, bool on);
  std::string set_name(const std::string& adapter, const std::string& name);
  std::string start_discovery(const std::string& adapter);
  std::string stop_discovery(const std::string& adapter);
  std::string pair_device(const std::string& device);
  std::string connect_device(const std::string& device);
  std::string disconnect_device(const std::string& device);
  std::string set_trusted(const std::string& device, bool trusted);
  std::string remove_device(const std::string& adapter, const std::string& device);
};

}  // namespace lcos

#endif
