// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_SOUND_EDIT_HPP
#define LCOS_LOGIC_SOUND_EDIT_HPP

#include <cstdint>
#include <set>
#include <string>

namespace lcos {

// Fields Command Center itself changed on the Sound page. Cancel restores
// these and leaves volume keys, the panel, and jack events alone.
struct SoundEdit {
  bool output_device = false;
  bool output_port = false;
  bool output_volume = false;
  bool output_mute = false;
  bool input_device = false;
  bool input_port = false;
  bool input_volume = false;
  bool input_mute = false;
  bool profile = false;
  std::set<std::uint32_t> streams;

  bool any() const {
    return output_device || output_port || output_volume || output_mute || input_device ||
           input_port || input_volume || input_mute || profile || !streams.empty();
  }
};

inline bool sound_revert_touches(const SoundEdit& edit, const std::string& field) {
  if (field == "output-device") {
    return edit.output_device;
  }
  if (field == "output-port") {
    return edit.output_port;
  }
  if (field == "output-volume") {
    return edit.output_volume;
  }
  if (field == "output-mute") {
    return edit.output_mute;
  }
  if (field == "input-device") {
    return edit.input_device;
  }
  if (field == "input-port") {
    return edit.input_port;
  }
  if (field == "input-volume") {
    return edit.input_volume;
  }
  if (field == "input-mute") {
    return edit.input_mute;
  }
  if (field == "profile") {
    return edit.profile;
  }
  return false;
}

}  // namespace lcos

#endif
