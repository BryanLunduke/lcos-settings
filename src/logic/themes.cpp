// SPDX-License-Identifier: GPL-3.0-or-later

#include "themes.hpp"

#include "text.hpp"

#include <glib.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
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
    } else if (key == "Directories") {
      std::string item;
      auto push = [&]() {
        const std::string trimmed = trim_copy(item);
        if (!trimmed.empty()) {
          theme.directories.push_back(trimmed);
        }
        item.clear();
      };
      for (char c : value) {
        if (c == ',') {
          push();
        } else {
          item.push_back(c);
        }
      }
      push();
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

bool regular_file(const fs::path& path) {
  std::error_code error;
  return fs::is_regular_file(path, error);
}

bool nonempty_file(const fs::path& path) {
  std::error_code error;
  return fs::is_regular_file(path, error) && fs::file_size(path, error) > 0;
}

std::string without_comments(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '#') {
      while (i < text.size() && text[i] != '\n') {
        ++i;
      }
    } else if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      i += 2;
      while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
        ++i;
      }
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

bool word_at(const std::string& text, std::size_t i, const char* word) {
  const std::size_t length = std::char_traits<char>::length(word);
  if (i + length > text.size() || text.compare(i, length, word) != 0) {
    return false;
  }
  if (i > 0) {
    const unsigned char before = static_cast<unsigned char>(text[i - 1]);
    if (std::isalnum(before) || before == '_' || before == '-') {
      return false;
    }
  }
  if (i + length < text.size()) {
    const unsigned char after = static_cast<unsigned char>(text[i + length]);
    if (std::isalnum(after) || after == '_' || after == '-') {
      return false;
    }
  }
  return true;
}

bool gtkrc_has_body(const std::string& text) {
  const char* words[] = {"style", "widget", "class", "include", "binding", "pixmap_path",
                         "module_path"};
  for (std::size_t i = 0; i < text.size(); ++i) {
    for (const char* word : words) {
      if (word_at(text, i, word)) {
        return true;
      }
    }
  }
  return false;
}

std::vector<std::string> gtkrc_engines(const std::string& text) {
  std::vector<std::string> names;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (!word_at(text, i, "engine")) {
      continue;
    }
    std::size_t j = i + 6;
    while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) {
      ++j;
    }
    if (j >= text.size() || (text[j] != '"' && text[j] != '\'')) {
      continue;
    }
    const char quote = text[j++];
    const auto end = text.find(quote, j);
    if (end == std::string::npos || end == j) {
      continue;
    }
    names.push_back(text.substr(j, end - j));
    i = end;
  }
  return names;
}

void collect_engine_modules(const fs::path& dir, std::set<std::string>& engines) {
  std::error_code error;
  if (!fs::is_directory(dir, error)) {
    return;
  }
  for (const auto& item : fs::directory_iterator(dir, error)) {
    error.clear();
    if (!item.is_regular_file()) {
      continue;
    }
    const std::string name = item.path().filename().string();
    if (name.size() > 6 && name.compare(0, 3, "lib") == 0 &&
        name.compare(name.size() - 3, 3, ".so") == 0) {
      engines.insert(name.substr(3, name.size() - 6));
    }
  }
}

const std::set<std::string>& installed_gtk2_engines() {
  static std::set<std::string> engines = [] {
    std::set<std::string> found;
    const fs::path roots[] = {"/usr/lib", "/usr/lib64", "/lib"};
    for (const auto& root : roots) {
      std::error_code error;
      collect_engine_modules(root / "gtk-2.0" / "2.10.0" / "engines", found);
      if (!fs::is_directory(root, error)) {
        continue;
      }
      for (const auto& arch : fs::directory_iterator(root, error)) {
        error.clear();
        if (!arch.is_directory()) {
          continue;
        }
        const fs::path gtk2 = arch.path() / "gtk-2.0";
        std::error_code nested;
        if (!fs::is_directory(gtk2, nested)) {
          continue;
        }
        for (const auto& version : fs::directory_iterator(gtk2, nested)) {
          nested.clear();
          collect_engine_modules(version.path() / "engines", found);
        }
      }
    }
    return found;
  }();
  return engines;
}

// A widget theme GTK can actually load. gtk-3.0/gtk-keys.css and gtk-2.0-key
// are keybinding snippets (Emacs, Default), not styles. A gtk-2.0/gtkrc that
// only names a missing engine (Crux, Emacs) cannot be selected either.
bool gtk_theme_usable(const fs::path& path) {
  if (regular_file(path / "gtk-3.0" / "gtk.css") || regular_file(path / "gtk-3.0" / "gtk-dark.css")) {
    return true;
  }
  const fs::path gtkrc = path / "gtk-2.0" / "gtkrc";
  if (!regular_file(gtkrc)) {
    return false;
  }
  const std::string body = without_comments(read_text_file(gtkrc.string()));
  const auto engines = gtkrc_engines(body);
  if (engines.empty()) {
    return gtkrc_has_body(body);
  }
  const auto& installed = installed_gtk2_engines();
  for (const auto& engine : engines) {
    if (installed.count(engine) == 0) {
      return false;
    }
  }
  return true;
}

bool icon_directory_has_file(const fs::path& dir) {
  std::error_code error;
  if (!fs::is_directory(dir, error)) {
    return false;
  }
  for (const auto& item : fs::directory_iterator(dir, error)) {
    error.clear();
    if (item.is_regular_file()) {
      return true;
    }
  }
  return false;
}

bool xfwm_has_art(const fs::path& dir) {
  std::error_code error;
  if (!fs::is_directory(dir, error)) {
    return false;
  }
  for (const auto& item : fs::directory_iterator(dir, error)) {
    error.clear();
    const std::string ext = item.path().extension().string();
    if (ext == ".png" || ext == ".xpm" || ext == ".svg") {
      return true;
    }
  }
  return false;
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
      if (!gtk_theme_usable(path)) {
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
      bool has_icons = false;
      for (const auto& relative : index.directories) {
        if (icon_directory_has_file(path / relative)) {
          has_icons = true;
          break;
        }
      }
      if (!has_icons) {
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
      const fs::path xfwm = path / "xfwm4";
      if (!nonempty_file(xfwm / "themerc") || !xfwm_has_art(xfwm)) {
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
