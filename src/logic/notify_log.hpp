// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_NOTIFY_LOG_HPP
#define LCOS_LOGIC_NOTIFY_LOG_HPP

#include <string>

namespace lcos {

// xfce4-notifyd stores the log as a key file. Each notification is one
// group. Counts those groups and ignores blank lines and comments.
int notify_log_entry_count(const std::string& text);

}  // namespace lcos

#endif
