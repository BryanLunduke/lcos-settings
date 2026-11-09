// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcuts.hpp"

namespace lcos {
namespace {

constexpr const char* kNotifySuffix = "/startup-notify";

struct Parsed {
  std::string accel;
  bool custom = false;
  bool notify = false;
};

bool starts_with(const std::string& text, const char* prefix) {
  const std::string p(prefix);
  return text.size() >= p.size() && text.compare(0, p.size(), p) == 0;
}

bool parse_path(const std::string& path, Parsed& out) {
  const char* base = nullptr;
  if (starts_with(path, "/commands/custom/")) {
    base = "/commands/custom/";
    out.custom = true;
  } else if (starts_with(path, "/commands/default/")) {
    base = "/commands/default/";
    out.custom = false;
  } else {
    return false;
  }
  std::string rest = path.substr(std::string(base).size());
  if (rest.size() > std::string(kNotifySuffix).size() &&
      rest.compare(rest.size() - std::string(kNotifySuffix).size(),
                   std::string(kNotifySuffix).size(), kNotifySuffix) == 0) {
    rest.erase(rest.size() - std::string(kNotifySuffix).size());
    out.notify = true;
  }
  if (rest.empty() || rest.find('/') != std::string::npos) {
    return false;
  }
  out.accel = std::move(rest);
  return true;
}

}  // namespace

std::string shortcut_command_path(bool custom, const std::string& accel) {
  return std::string(custom ? kShortcutCustomBase : kShortcutDefaultBase) +
         "/" + accel;
}

std::string shortcut_notify_path(bool custom, const std::string& accel) {
  return shortcut_command_path(custom, accel) + kNotifySuffix;
}

std::vector<Shortcut> merge_shortcuts(
    const std::map<std::string, std::string>& values) {
  std::map<std::string, Shortcut> by_accel;
  // Defaults first so a custom entry with the same accelerator replaces them.
  for (int pass = 0; pass < 2; ++pass) {
    const bool want_custom = pass == 1;
    for (const auto& entry : values) {
      Parsed parsed;
      if (!parse_path(entry.first, parsed) || parsed.custom != want_custom) {
        continue;
      }
      Shortcut& row = by_accel[parsed.accel];
      if (row.accel.empty()) {
        row.accel = parsed.accel;
      }
      if (parsed.notify) {
        row.startup_notify = entry.second == "true" || entry.second == "1";
      } else {
        row.command = entry.second;
        row.custom = parsed.custom;
      }
      if (parsed.custom) {
        row.custom = true;
      }
    }
  }

  std::vector<Shortcut> rows;
  rows.reserve(by_accel.size());
  for (auto& entry : by_accel) {
    if (entry.second.command.empty() && !entry.second.custom) {
      continue;
    }
    rows.push_back(std::move(entry.second));
  }
  return rows;
}

}  // namespace lcos
