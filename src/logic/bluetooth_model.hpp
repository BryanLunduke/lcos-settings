// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_BLUETOOTH_MODEL_HPP
#define LCOS_LOGIC_BLUETOOTH_MODEL_HPP

#include <string>
#include <vector>

namespace lcos {

struct BtAdapter {
  std::string path;
  std::string name;
  std::string address;
  bool powered = false;
  bool discoverable = false;
};

struct BtDevice {
  std::string path;
  std::string name;
  std::string address;
  std::string icon;
  bool paired = false;
  bool connected = false;
  bool trusted = false;
};

struct BtSnapshot {
  bool service = false;
  std::string error;
  bool adapter = false;
  BtAdapter adapter_info;
  std::vector<BtDevice> devices;
};

// BlueZ Icon values are freedesktop icon names. Unknown values use bluetooth.
std::string bluetooth_device_icon(const std::string& icon);
std::string bluetooth_device_status(const BtDevice& device);

}  // namespace lcos

#endif
