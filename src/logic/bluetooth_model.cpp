// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/bluetooth_model.hpp"

namespace lcos {

std::string bluetooth_device_icon(const std::string& icon) {
  static const char* known[] = {
      "phone",           "smartphone",      "computer",         "audio-card",
      "audio-headphones","audio-headset",   "audio-speakers",   "input-keyboard",
      "input-mouse",     "input-gaming",    "input-tablet",     "camera-photo",
      "camera-video",    "printer",         "modem",            "network-wireless",
      "multimedia-player","scanner",        "video-display"};
  for (const char* name : known) {
    if (icon == name) {
      return icon == std::string("audio-headset") ? "audio-headphones" : icon;
    }
  }
  return "bluetooth";
}

std::string bluetooth_device_status(const BtDevice& device) {
  std::string text;
  if (device.connected) {
    text = "Connected";
  } else if (device.paired) {
    text = "Paired";
  } else {
    text = "Not paired";
  }
  if (device.trusted) {
    text += " · Trusted";
  }
  if (!device.address.empty()) {
    text += " · " + device.address;
  }
  return text;
}

}  // namespace lcos
