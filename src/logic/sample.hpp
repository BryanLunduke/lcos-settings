// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_SAMPLE_HPP
#define LCOS_LOGIC_SAMPLE_HPP

#include <glib.h>

namespace lcos {

// Screenshots and logic checks can show a page with data when the service
// that normally fills it is not running.
inline bool settings_sample() {
  const char* value = g_getenv("LCOS_SETTINGS_SAMPLE");
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

}  // namespace lcos

#endif
