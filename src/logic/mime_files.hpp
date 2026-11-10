// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_MIME_FILES_HPP
#define LCOS_LOGIC_MIME_FILES_HPP

#include <string>
#include <utility>
#include <vector>

namespace lcos {

// helpers.rc is an xfce rc file. Category keys live in the default group,
// the lines before the first [Group] header.
std::string rc_lookup(const std::string& text, const std::string& key);
std::string rc_assign(const std::string& text, const std::string& key,
                      const std::string& value);

// mimeapps.list groups, such as "Default Applications".
std::string ini_lookup(const std::string& text, const std::string& group,
                       const std::string& key);
std::string ini_assign(const std::string& text, const std::string& group,
                       const std::string& key, const std::string& value);
std::vector<std::pair<std::string, std::string>> ini_entries(const std::string& text,
                                                             const std::string& group);

struct DesktopFile {
  std::string id;
  std::string name;
  std::string category;
  std::vector<std::string> mime_types;
  bool hidden = false;
  bool no_display = false;
  bool helper = false;
  bool application = false;
};

DesktopFile parse_desktop_file(const std::string& text, const std::string& id);

std::vector<std::string> split_desktop_list(const std::string& text);

}  // namespace lcos

#endif
