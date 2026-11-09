// SPDX-License-Identifier: GPL-3.0-or-later

#include "themes.hpp"

#include "text.hpp"

#include <glib.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace lcos {
namespace fs = std::filesystem;

IndexTheme parse_index_theme(const std::string& text) {
  IndexTheme theme;
  std::string section;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    line = trim_copy(line);
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      section = line.substr(1, line.size() - 2);
      if (section == "Icon Theme") {
        theme.icon_theme = true;
      } else if (section == "Sound Theme") {
        theme.sound_theme = true;
      }
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = trim_copy(line.substr(0, eq));
    const std::string value = trim_copy(line.substr(eq + 1));
    if (key == "Name" && theme.name.empty()) {
      theme.name = value;
    } else if (key == "Hidden" &&
               (value == "true" || value == "True" || value == "1")) {
      theme.hidden = true;
    }
  }
  return theme;
}

std::string read_text_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::vector<std::string> data_roots(const std::string& subdir, bool dot_dir) {
  std::vector<std::string> roots;
  const char* home = g_get_home_dir();
  if (home != nullptr && home[0] != '\0') {
    if (dot_dir) {
      roots.push_back(std::string(home) + "/." + subdir);
    }
    const char* user = g_get_user_data_dir();
    if (user != nullptr && user[0] != '\0') {
      roots.push_back(std::string(user) + "/" + subdir);
    }
  }
  const char* env = g_getenv("XDG_DATA_DIRS");
  const std::string spec = (env != nullptr && env[0] != '\0')
                               ? env
                               : "/usr/local/share:/usr/share";
  for (const auto& dir : split_char(spec, ':')) {
    if (!dir.empty()) {
      roots.push_back(dir + "/" + subdir);
    }
  }
  return roots;
}

namespace {

bool dir_has(const fs::path& dir, const std::string& child) {
  std::error_code error;
  return fs::is_directory(dir / child, error);
}

void add_unique(std::vector<ThemeEntry>& entries, ThemeEntry entry) {
  for (const auto& existing : entries) {
    if (existing.id == entry.id) {
      return;
    }
  }
  entries.push_back(std::move(entry));
}

std::string display_name(const std::string& id, const IndexTheme& index) {
  if (index.name.empty() || index.name == id) {
    return id;
  }
  return index.name;
}

}  // namespace

std::vector<ThemeEntry> scan_gtk_themes(const std::vector<std::string>& roots) {
  std::vector<ThemeEntry> entries;
  for (const auto& root : roots) {
    std::error_code error;
    if (!fs::is_directory(root, error)) {
      continue;
    }
    for (const auto& item : fs::directory_iterator(root, error)) {
      error.clear();
      if (!item.is_directory()) {
        continue;
      }
      const fs::path path = item.path();
      if (!dir_has(path, "gtk-3.0") && !dir_has(path, "gtk-2.0")) {
        continue;
      }
      const IndexTheme index = parse_index_theme(read_text_file((path / "index.theme").string()));
      if (index.hidden) {
        continue;
      }
      ThemeEntry entry;
      entry.id = path.filename().string();
      entry.name = display_name(entry.id, index);
      entry.path = path.string();
      add_unique(entries, std::move(entry));
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const ThemeEntry& a, const ThemeEntry& b) { return a.name < b.name; });
  return entries;
}

std::vector<ThemeEntry> scan_icon_themes(const std::vector<std::string>& roots) {
  std::vector<ThemeEntry> entries;
  for (const auto& root : roots) {
    std::error_code error;
    if (!fs::is_directory(root, error)) {
      continue;
    }
    for (const auto& item : fs::directory_iterator(root, error)) {
      error.clear();
      if (!item.is_directory()) {
        continue;
      }
      const fs::path path = item.path();
      const fs::path index_path = path / "index.theme";
      error.clear();
      if (!fs::is_regular_file(index_path, error)) {
        continue;
      }
      const IndexTheme index = parse_index_theme(read_text_file(index_path.string()));
      if (index.hidden || !index.icon_theme) {
        continue;
      }
      ThemeEntry entry;
      entry.id = path.filename().string();
      entry.name = display_name(entry.id, index);
      entry.path = path.string();
      add_unique(entries, std::move(entry));
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const ThemeEntry& a, const ThemeEntry& b) { return a.name < b.name; });
  return entries;
}

std::vector<ThemeEntry> scan_cursor_themes(const std::vector<std::string>& roots) {
  std::vector<ThemeEntry> entries;
  for (const auto& root : roots) {
    std::error_code error;
    if (!fs::is_directory(root, error)) {
      continue;
    }
    for (const auto& item : fs::directory_iterator(root, error)) {
      error.clear();
      if (!item.is_directory()) {
        continue;
      }
      const fs::path path = item.path();
      if (!dir_has(path, "cursors")) {
        continue;
      }
      const IndexTheme index = parse_index_theme(read_text_file((path / "index.theme").string()));
      if (index.hidden) {
        continue;
      }
      ThemeEntry entry;
      entry.id = path.filename().string();
      entry.name = display_name(entry.id, index);
      entry.path = path.string();
      add_unique(entries, std::move(entry));
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const ThemeEntry& a, const ThemeEntry& b) { return a.name < b.name; });
  return entries;
}

std::vector<ThemeEntry> scan_xfwm_themes(const std::vector<std::string>& roots) {
  std::vector<ThemeEntry> entries;
  for (const auto& root : roots) {
    std::error_code error;
    if (!fs::is_directory(root, error)) {
      continue;
    }
    for (const auto& item : fs::directory_iterator(root, error)) {
      error.clear();
      if (!item.is_directory()) {
        continue;
      }
      const fs::path path = item.path();
      error.clear();
      if (!fs::is_regular_file(path / "xfwm4" / "themerc", error)) {
        continue;
      }
      const IndexTheme index = parse_index_theme(read_text_file((path / "index.theme").string()));
      if (index.hidden) {
        continue;
      }
      ThemeEntry entry;
      entry.id = path.filename().string();
      entry.name = display_name(entry.id, index);
      entry.path = path.string();
      add_unique(entries, std::move(entry));
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const ThemeEntry& a, const ThemeEntry& b) { return a.name < b.name; });
  return entries;
}

std::vector<ThemeEntry> scan_sound_themes(const std::vector<std::string>& roots) {
  std::vector<ThemeEntry> entries;
  for (const auto& root : roots) {
    std::error_code error;
    if (!fs::is_directory(root, error)) {
      continue;
    }
    for (const auto& item : fs::directory_iterator(root, error)) {
      error.clear();
      if (!item.is_directory()) {
        continue;
      }
      const fs::path path = item.path();
      const IndexTheme index = parse_index_theme(read_text_file((path / "index.theme").string()));
      if (index.hidden || !index.sound_theme) {
        continue;
      }
      ThemeEntry entry;
      entry.id = path.filename().string();
      entry.name = display_name(entry.id, index);
      entry.path = path.string();
      add_unique(entries, std::move(entry));
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const ThemeEntry& a, const ThemeEntry& b) { return a.name < b.name; });
  return entries;
}

}  // namespace lcos
