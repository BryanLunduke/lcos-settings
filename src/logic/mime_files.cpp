// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/mime_files.hpp"

#include <cctype>
#include <cstddef>

namespace lcos {
namespace {

std::string trim(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return text.substr(begin, end - begin);
}

bool is_comment(const std::string& line) {
  const std::string text = trim(line);
  return text.empty() || text[0] == '#' || text[0] == ';';
}

bool is_group_line(const std::string& line) {
  const std::string text = trim(line);
  return text.size() >= 2 && text.front() == '[' && text.back() == ']';
}

std::string group_name(const std::string& line) {
  const std::string text = trim(line);
  if (text.size() < 2) {
    return {};
  }
  return text.substr(1, text.size() - 2);
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::string current;
  for (char character : text) {
    if (character == '\n') {
      if (!current.empty() && current.back() == '\r') {
        current.pop_back();
      }
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(character);
    }
  }
  if (!current.empty()) {
    if (current.back() == '\r') {
      current.pop_back();
    }
    lines.push_back(current);
  }
  return lines;
}

std::string join_lines(const std::vector<std::string>& lines) {
  std::string out;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i != 0) {
      out.push_back('\n');
    }
    out += lines[i];
  }
  if (!out.empty() && out.back() != '\n') {
    out.push_back('\n');
  }
  return out;
}

bool key_equals(const std::string& line, const std::string& key, std::string* value) {
  const std::string text = trim(line);
  const auto eq = text.find('=');
  if (eq == std::string::npos) {
    return false;
  }
  if (trim(text.substr(0, eq)) != key) {
    return false;
  }
  if (value != nullptr) {
    *value = trim(text.substr(eq + 1));
  }
  return true;
}

bool truthy(const std::string& value) {
  std::string lower = value;
  for (char& character : lower) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return lower == "true" || lower == "1";
}

}  // namespace

std::string rc_lookup(const std::string& text, const std::string& key) {
  for (const auto& line : split_lines(text)) {
    if (is_group_line(line)) {
      break;
    }
    if (is_comment(line)) {
      continue;
    }
    std::string value;
    if (key_equals(line, key, &value)) {
      return value;
    }
  }
  return {};
}

std::string rc_assign(const std::string& text, const std::string& key,
                      const std::string& value) {
  auto lines = split_lines(text);
  bool in_group = false;
  bool found = false;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (is_group_line(lines[i])) {
      in_group = true;
    }
    if (in_group || is_comment(lines[i])) {
      continue;
    }
    if (key_equals(lines[i], key, nullptr)) {
      found = true;
      if (value.empty()) {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
        --i;
      } else {
        lines[i] = key + "=" + value;
      }
    }
  }
  if (!found && !value.empty()) {
    std::size_t insert_at = 0;
    for (; insert_at < lines.size(); ++insert_at) {
      if (is_group_line(lines[insert_at])) {
        break;
      }
      if (!is_comment(lines[insert_at])) {
        break;
      }
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_at), key + "=" + value);
  }
  return join_lines(lines);
}

std::string ini_lookup(const std::string& text, const std::string& group,
                       const std::string& key) {
  bool in_group = false;
  for (const auto& line : split_lines(text)) {
    if (is_group_line(line)) {
      in_group = group_name(line) == group;
      continue;
    }
    if (!in_group || is_comment(line)) {
      continue;
    }
    std::string value;
    if (key_equals(line, key, &value)) {
      return value;
    }
  }
  return {};
}

std::string ini_assign(const std::string& text, const std::string& group,
                       const std::string& key, const std::string& value) {
  auto lines = split_lines(text);
  int group_at = -1;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (is_group_line(lines[i]) && group_name(lines[i]) == group) {
      group_at = static_cast<int>(i);
      break;
    }
  }
  if (group_at < 0) {
    if (value.empty()) {
      return join_lines(lines);
    }
    if (!lines.empty() && !lines.back().empty()) {
      lines.emplace_back();
    }
    lines.push_back("[" + group + "]");
    lines.push_back(key + "=" + value);
    return join_lines(lines);
  }
  bool found = false;
  std::size_t insert_at = static_cast<std::size_t>(group_at) + 1;
  for (std::size_t i = insert_at; i < lines.size(); ++i) {
    if (is_group_line(lines[i])) {
      break;
    }
    insert_at = i + 1;
    if (is_comment(lines[i])) {
      continue;
    }
    if (key_equals(lines[i], key, nullptr)) {
      found = true;
      if (value.empty()) {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        lines[i] = key + "=" + value;
      }
      break;
    }
  }
  if (!found && !value.empty()) {
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_at), key + "=" + value);
  }
  return join_lines(lines);
}

std::vector<std::pair<std::string, std::string>> ini_entries(const std::string& text,
                                                             const std::string& group) {
  std::vector<std::pair<std::string, std::string>> rows;
  bool in_group = false;
  for (const auto& line : split_lines(text)) {
    if (is_group_line(line)) {
      in_group = group_name(line) == group;
      continue;
    }
    if (!in_group || is_comment(line)) {
      continue;
    }
    const std::string trimmed = trim(line);
    const auto eq = trimmed.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = trim(trimmed.substr(0, eq));
    const std::string value = trim(trimmed.substr(eq + 1));
    if (!key.empty()) {
      rows.emplace_back(key, value);
    }
  }
  return rows;
}

std::vector<std::string> split_desktop_list(const std::string& text) {
  std::vector<std::string> items;
  std::string current;
  for (char character : text) {
    if (character == ';') {
      const std::string item = trim(current);
      if (!item.empty()) {
        items.push_back(item);
      }
      current.clear();
    } else {
      current.push_back(character);
    }
  }
  const std::string item = trim(current);
  if (!item.empty()) {
    items.push_back(item);
  }
  return items;
}

DesktopFile parse_desktop_file(const std::string& text, const std::string& id) {
  DesktopFile file;
  file.id = id;
  bool in_entry = false;
  bool seen_group = false;
  for (const auto& line : split_lines(text)) {
    if (is_group_line(line)) {
      const std::string name = group_name(line);
      if (!seen_group && name == "Desktop Entry") {
        in_entry = true;
      } else if (seen_group) {
        break;
      } else {
        in_entry = false;
      }
      seen_group = true;
      continue;
    }
    if (!in_entry || is_comment(line)) {
      continue;
    }
    const std::string trimmed = trim(line);
    const auto eq = trimmed.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = trim(trimmed.substr(0, eq));
    if (key.find('[') != std::string::npos) {
      continue;
    }
    const std::string value = trim(trimmed.substr(eq + 1));
    if (key == "Name" && file.name.empty()) {
      file.name = value;
    } else if (key == "Type") {
      file.application = value == "Application";
      file.helper = value == "X-XFCE-Helper";
    } else if (key == "X-XFCE-Category") {
      file.category = value;
    } else if (key == "MimeType" || key == "X-XFCE-MimeType") {
      for (const auto& mime : split_desktop_list(value)) {
        file.mime_types.push_back(mime);
      }
    } else if (key == "Hidden") {
      file.hidden = truthy(value);
    } else if (key == "NoDisplay") {
      file.no_display = truthy(value);
    }
  }
  if (file.name.empty()) {
    file.name = id;
  }
  return file;
}

std::string prepend_desktop_id(const std::string& list, const std::string& desktop_id) {
  const std::string id = trim(desktop_id);
  if (id.empty()) {
    return list;
  }
  std::string out = id;
  for (const auto& item : split_desktop_list(list)) {
    if (item != id) {
      out += ";";
      out += item;
    }
  }
  out += ";";
  return out;
}

}  // namespace lcos
