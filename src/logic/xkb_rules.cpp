// SPDX-License-Identifier: GPL-3.0-or-later

#include "xkb_rules.hpp"

#include "text.hpp"

#include <glib.h>

#include <utility>

namespace lcos {
namespace {

struct Parse {
  XkbRules* rules = nullptr;
  std::vector<std::string> stack;
  std::string text;
  std::string pending_name;
  std::string pending_desc;
  XkbModel model;
  XkbLayout layout;
  XkbVariant variant;
  XkbOptionGroup group;
  XkbOption option;
  GError* error = nullptr;

  std::string parent() const {
    if (stack.size() < 2) {
      return {};
    }
    return stack[stack.size() - 2];
  }

  void flush_config() {
    const std::string owner = parent();
    if (owner == "model") {
      model.name = pending_name;
      model.description = pending_desc.empty() ? pending_name : pending_desc;
    } else if (owner == "layout") {
      layout.name = pending_name;
      layout.description = pending_desc.empty() ? pending_name : pending_desc;
    } else if (owner == "variant") {
      variant.name = pending_name;
      variant.description = pending_desc.empty() ? pending_name : pending_desc;
    } else if (owner == "group") {
      group.name = pending_name;
      group.description = pending_desc.empty() ? pending_name : pending_desc;
    } else if (owner == "option") {
      option.name = pending_name;
      option.description = pending_desc.empty() ? pending_name : pending_desc;
    }
    pending_name.clear();
    pending_desc.clear();
  }

  void on_end(const std::string& element) {
    if (element == "name") {
      pending_name = trim_copy(text);
    } else if (element == "description" && pending_desc.empty()) {
      pending_desc = trim_copy(text);
    } else if (element == "configItem") {
      flush_config();
    } else if (element == "model") {
      if (!model.name.empty()) {
        rules->models.push_back(std::move(model));
      }
      model = {};
    } else if (element == "variant") {
      if (!variant.name.empty()) {
        layout.variants.push_back(std::move(variant));
      }
      variant = {};
    } else if (element == "layout") {
      if (!layout.name.empty()) {
        rules->layouts.push_back(std::move(layout));
      }
      layout = {};
    } else if (element == "option") {
      if (!option.name.empty()) {
        group.options.push_back(std::move(option));
      }
      option = {};
    } else if (element == "group") {
      if (!group.name.empty()) {
        rules->groups.push_back(std::move(group));
      }
      group = {};
    }
    text.clear();
    if (!stack.empty()) {
      stack.pop_back();
    }
  }
};

void start_element(GMarkupParseContext*, const gchar* name, const gchar**,
                   const gchar**, gpointer user, GError**) {
  auto* parse = static_cast<Parse*>(user);
  parse->stack.emplace_back(name);
  parse->text.clear();
}

void end_element(GMarkupParseContext*, const gchar* name, gpointer user,
                 GError**) {
  auto* parse = static_cast<Parse*>(user);
  parse->on_end(name);
}

void text_element(GMarkupParseContext*, const gchar* text, gsize len,
                  gpointer user, GError**) {
  auto* parse = static_cast<Parse*>(user);
  parse->text.append(text, len);
}

std::string strip_doctype(const std::string& xml) {
  const auto start = xml.find("<!DOCTYPE");
  if (start == std::string::npos) {
    return xml;
  }
  const auto end = xml.find('>', start);
  if (end == std::string::npos) {
    return xml;
  }
  std::string out = xml;
  out.erase(start, end - start + 1);
  return out;
}

}  // namespace

bool parse_xkb_rules(const std::string& xml, XkbRules& rules, std::string& error) {
  rules = {};
  Parse parse;
  parse.rules = &rules;
  const std::string cleaned = strip_doctype(xml);
  GMarkupParser parser = {start_element, end_element, text_element, nullptr,
                          nullptr};
  GMarkupParseContext* context = g_markup_parse_context_new(
      &parser, static_cast<GMarkupParseFlags>(0), &parse, nullptr);
  GError* gerror = nullptr;
  const gboolean ok = g_markup_parse_context_parse(
      context, cleaned.data(), static_cast<gssize>(cleaned.size()), &gerror);
  gboolean end_ok = TRUE;
  if (ok) {
    end_ok = g_markup_parse_context_end_parse(context, &gerror);
  }
  g_markup_parse_context_free(context);
  if (!ok || !end_ok) {
    error = gerror != nullptr ? gerror->message : "Could not parse XKB rules";
    if (gerror != nullptr) {
      g_error_free(gerror);
    }
    rules = {};
    return false;
  }
  error.clear();
  return true;
}

std::pair<std::string, std::string> encode_layouts(
    const std::vector<LayoutChoice>& choices) {
  std::vector<std::string> layouts;
  std::vector<std::string> variants;
  layouts.reserve(choices.size());
  variants.reserve(choices.size());
  for (const auto& choice : choices) {
    layouts.push_back(choice.layout);
    variants.push_back(choice.variant);
  }
  return {join_char(layouts, ','), join_char(variants, ',')};
}

std::vector<LayoutChoice> decode_layouts(const std::string& layouts,
                                         const std::string& variants) {
  const auto layout_parts = split_char(layouts, ',');
  const auto variant_parts = split_char(variants, ',');
  std::vector<LayoutChoice> choices;
  choices.reserve(layout_parts.size());
  for (std::size_t i = 0; i < layout_parts.size(); ++i) {
    if (layout_parts[i].empty()) {
      continue;
    }
    LayoutChoice choice;
    choice.layout = layout_parts[i];
    if (i < variant_parts.size()) {
      choice.variant = variant_parts[i];
    }
    choices.push_back(std::move(choice));
  }
  return choices;
}

const XkbLayout* find_layout(const XkbRules& rules, const std::string& name) {
  for (const auto& layout : rules.layouts) {
    if (layout.name == name) {
      return &layout;
    }
  }
  return nullptr;
}

const XkbOptionGroup* find_option_group(const XkbRules& rules,
                                        const std::string& name) {
  for (const auto& group : rules.groups) {
    if (group.name == name) {
      return &group;
    }
  }
  return nullptr;
}

}  // namespace lcos
