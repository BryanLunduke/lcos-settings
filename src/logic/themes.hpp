// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_THEMES_HPP
#define LCOS_LOGIC_THEMES_HPP

#include <string>
#include <vector>

namespace lcos {

struct IndexTheme {
  std::string name;
  bool hidden = false;
  bool icon_theme = false;
  bool sound_theme = false;
};

struct ThemeEntry {
  std::string id;
  std::string name;
  std::string path;
};

IndexTheme parse_index_theme(const std::string& text);

std::vector<std::string> data_roots(const std::string& subdir,
                                    bool dot_dir);

std::vector<ThemeEntry> scan_gtk_themes(const std::vector<std::string>& roots);
std::vector<ThemeEntry> scan_icon_themes(const std::vector<std::string>& roots);
std::vector<ThemeEntry> scan_cursor_themes(const std::vector<std::string>& roots);
std::vector<ThemeEntry> scan_xfwm_themes(const std::vector<std::string>& roots);
std::vector<ThemeEntry> scan_sound_themes(const std::vector<std::string>& roots);

std::string read_text_file(const std::string& path);

}  // namespace lcos

#endif
