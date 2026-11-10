// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_XKB_RULES_HPP
#define LCOS_LOGIC_XKB_RULES_HPP

#include <string>
#include <utility>
#include <vector>

namespace lcos {

struct XkbModel {
  std::string name;
  std::string description;
};

struct XkbVariant {
  std::string name;
  std::string description;
};

struct XkbLayout {
  std::string name;
  std::string description;
  std::vector<XkbVariant> variants;
};

struct XkbOption {
  std::string name;
  std::string description;
};

struct XkbOptionGroup {
  std::string name;
  std::string description;
  std::vector<XkbOption> options;
};

struct XkbRules {
  std::vector<XkbModel> models;
  std::vector<XkbLayout> layouts;
  std::vector<XkbOptionGroup> groups;
};

struct LayoutChoice {
  std::string layout;
  std::string variant;
};

// Parses XKB rules XML (evdev.xml). The DOCTYPE line is ignored.
bool parse_xkb_rules(const std::string& xml, XkbRules& rules, std::string& error);

std::pair<std::string, std::string> encode_layouts(
    const std::vector<LayoutChoice>& choices);
std::vector<LayoutChoice> decode_layouts(const std::string& layouts,
                                         const std::string& variants);

const XkbLayout* find_layout(const XkbRules& rules, const std::string& name);
const XkbOptionGroup* find_option_group(const XkbRules& rules,
                                        const std::string& name);

}  // namespace lcos

#endif
