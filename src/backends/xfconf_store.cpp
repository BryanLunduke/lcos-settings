// SPDX-License-Identifier: GPL-3.0-or-later

#include "xfconf_store.hpp"

#include <xfconf/xfconf.h>

#include <algorithm>
#include <cmath>

namespace lcos {
namespace {

std::string key_of(const std::string& channel, const std::string& prop) {
  return channel + "\n" + prop;
}

PropValue from_gvalue(const GValue* value) {
  PropValue out;
  if (value == nullptr) {
    return out;
  }
  if (G_VALUE_HOLDS_BOOLEAN(value)) {
    out.type = PropType::Bool;
    out.b = g_value_get_boolean(value) != FALSE;
  } else if (G_VALUE_HOLDS_INT(value)) {
    out.type = PropType::Int;
    out.i = g_value_get_int(value);
  } else if (G_VALUE_HOLDS_UINT(value)) {
    out.type = PropType::Uint;
    out.u = g_value_get_uint(value);
  } else if (G_VALUE_HOLDS_DOUBLE(value)) {
    out.type = PropType::Double;
    out.d = g_value_get_double(value);
  } else if (G_VALUE_HOLDS_STRING(value)) {
    out.type = PropType::String;
    const char* text = g_value_get_string(value);
    out.s = text != nullptr ? text : "";
  } else if (G_VALUE_TYPE(value) == G_TYPE_PTR_ARRAY) {
    auto* array = static_cast<GPtrArray*>(g_value_get_boxed(value));
    if (array != nullptr && array->len > 0) {
      auto* first = static_cast<GValue*>(g_ptr_array_index(array, 0));
      const bool as_double = first != nullptr && G_VALUE_HOLDS_DOUBLE(first);
      const bool as_string = first != nullptr && G_VALUE_HOLDS_STRING(first);
      out.type = as_double ? PropType::Doubles : as_string ? PropType::Strings : PropType::Ints;
      for (guint i = 0; i < array->len; ++i) {
        auto* element = static_cast<GValue*>(g_ptr_array_index(array, i));
        if (element == nullptr) {
          continue;
        }
        if (as_double && G_VALUE_HOLDS_DOUBLE(element)) {
          out.doubles.push_back(g_value_get_double(element));
        } else if (as_string && G_VALUE_HOLDS_STRING(element)) {
          const char* text = g_value_get_string(element);
          out.strings.emplace_back(text != nullptr ? text : "");
        } else if (!as_double && !as_string && G_VALUE_HOLDS_INT(element)) {
          out.ints.push_back(g_value_get_int(element));
        }
      }
    }
  }
  return out;
}

std::string full_path(const std::string& base, const char* key) {
  if (key == nullptr) {
    return base;
  }
  if (key[0] == '/') {
    return key;
  }
  if (base.empty() || base == "/") {
    return std::string("/") + key;
  }
  if (!base.empty() && base.back() == '/') {
    return base + key;
  }
  return base + "/" + key;
}

}  // namespace

bool PropValue::same_as(const PropValue& other) const {
  if (type != other.type) {
    return false;
  }
  switch (type) {
    case PropType::Absent:
      return true;
    case PropType::Bool:
      return b == other.b;
    case PropType::Int:
      return i == other.i;
    case PropType::Uint:
      return u == other.u;
    case PropType::Double:
      return std::fabs(d - other.d) < 1e-9;
    case PropType::String:
      return s == other.s;
    case PropType::Ints:
      return ints == other.ints;
    case PropType::Doubles: {
      if (doubles.size() != other.doubles.size()) {
        return false;
      }
      for (std::size_t i = 0; i < doubles.size(); ++i) {
        if (std::fabs(doubles[i] - other.doubles[i]) > 1e-6) {
          return false;
        }
      }
      return true;
    }
    case PropType::Strings:
      return strings == other.strings;
  }
  return false;
}

XfconfStore::~XfconfStore() {
  if (ready_) {
    xfconf_shutdown();
    ready_ = false;
  }
}

bool XfconfStore::init(std::string* error) {
  if (ready_) {
    return true;
  }
  GError* gerror = nullptr;
  if (!xfconf_init(&gerror)) {
    if (error != nullptr) {
      *error = gerror != nullptr ? gerror->message
                                 : "xfconf_init failed. Is a session bus available?";
    }
    if (gerror != nullptr) {
      g_error_free(gerror);
    }
    return false;
  }
  ready_ = true;
  return true;
}

void* XfconfStore::channel_of(const std::string& name) const {
  const auto found = channels_.find(name);
  if (found != channels_.end()) {
    return found->second;
  }
  XfconfChannel* channel = xfconf_channel_get(name.c_str());
  channels_[name] = channel;
  return channel;
}

bool XfconfStore::has(const std::string& channel, const std::string& prop) const {
  if (!ready_) {
    return false;
  }
  auto* xfchannel = static_cast<XfconfChannel*>(channel_of(channel));
  return xfconf_channel_has_property(xfchannel, prop.c_str()) != FALSE;
}

PropValue XfconfStore::get(const std::string& channel, const std::string& prop) const {
  PropValue value;
  if (!ready_ || !has(channel, prop)) {
    return value;
  }
  GValue raw = G_VALUE_INIT;
  auto* xfchannel = static_cast<XfconfChannel*>(channel_of(channel));
  if (!xfconf_channel_get_property(xfchannel, prop.c_str(), &raw)) {
    return value;
  }
  value = from_gvalue(&raw);
  g_value_unset(&raw);
  return value;
}

std::string XfconfStore::get_string(const std::string& channel, const std::string& prop,
                                   const std::string& fallback) const {
  const PropValue value = get(channel, prop);
  if (value.type == PropType::String) {
    return value.s;
  }
  return fallback;
}

bool XfconfStore::get_bool(const std::string& channel, const std::string& prop,
                           bool fallback) const {
  const PropValue value = get(channel, prop);
  if (value.type == PropType::Bool) {
    return value.b;
  }
  if (value.type == PropType::Int) {
    return value.i != 0;
  }
  return fallback;
}

int XfconfStore::get_int(const std::string& channel, const std::string& prop,
                         int fallback) const {
  const PropValue value = get(channel, prop);
  if (value.type == PropType::Int) {
    return value.i;
  }
  if (value.type == PropType::Uint) {
    return static_cast<int>(value.u);
  }
  return fallback;
}

unsigned XfconfStore::get_uint(const std::string& channel, const std::string& prop,
                               unsigned fallback) const {
  const PropValue value = get(channel, prop);
  if (value.type == PropType::Uint) {
    return value.u;
  }
  if (value.type == PropType::Int && value.i >= 0) {
    return static_cast<unsigned>(value.i);
  }
  return fallback;
}

double XfconfStore::get_double(const std::string& channel, const std::string& prop,
                               double fallback) const {
  const PropValue value = get(channel, prop);
  if (value.type == PropType::Double) {
    return value.d;
  }
  if (value.type == PropType::Int) {
    return value.i;
  }
  return fallback;
}

std::vector<int> XfconfStore::get_ints(const std::string& channel,
                                       const std::string& prop) const {
  const PropValue value = get(channel, prop);
  if (value.type == PropType::Ints) {
    return value.ints;
  }
  if (value.type == PropType::Int) {
    return {value.i};
  }
  return {};
}

std::vector<double> XfconfStore::get_doubles(const std::string& channel,
                                             const std::string& prop) const {
  const PropValue value = get(channel, prop);
  if (value.type == PropType::Doubles) {
    return value.doubles;
  }
  if (value.type == PropType::Double) {
    return {value.d};
  }
  return {};
}

std::vector<std::string> XfconfStore::get_strings(const std::string& channel,
                                                  const std::string& prop) const {
  const PropValue value = get(channel, prop);
  if (value.type == PropType::Strings) {
    return value.strings;
  }
  if (value.type == PropType::String) {
    return {value.s};
  }
  return {};
}

void XfconfStore::remember(const std::string& channel, const std::string& prop) {
  if (!tracking_) {
    return;
  }
  const std::string key = key_of(channel, prop);
  if (originals_.count(key) != 0) {
    return;
  }
  Saved saved;
  saved.channel = channel;
  saved.prop = prop;
  saved.value = get(channel, prop);
  originals_.emplace(key, std::move(saved));
  changed.emit();
}

bool XfconfStore::write(const std::string& channel, const std::string& prop,
                        const PropValue& value, bool track) {
  if (!ready_) {
    return false;
  }
  const PropValue current = get(channel, prop);
  if (current.same_as(value)) {
    return true;
  }
  // An empty string list is the same as a property xfconf has not stored.
  if (value.type == PropType::Strings && value.strings.empty() &&
      current.type == PropType::Absent) {
    return true;
  }
  if (track) {
    remember(channel, prop);
  }
  auto* xfchannel = static_cast<XfconfChannel*>(channel_of(channel));
  gboolean ok = FALSE;
  switch (value.type) {
    case PropType::Absent:
      ok = TRUE;
      break;
    case PropType::Bool:
      ok = xfconf_channel_set_bool(xfchannel, prop.c_str(), value.b ? TRUE : FALSE);
      break;
    case PropType::Int:
      ok = xfconf_channel_set_int(xfchannel, prop.c_str(), value.i);
      break;
    case PropType::Uint:
      ok = xfconf_channel_set_uint(xfchannel, prop.c_str(), value.u);
      break;
    case PropType::Double:
      ok = xfconf_channel_set_double(xfchannel, prop.c_str(), value.d);
      break;
    case PropType::String:
      ok = xfconf_channel_set_string(xfchannel, prop.c_str(), value.s.c_str());
      break;
    case PropType::Ints: {
      GPtrArray* array = g_ptr_array_sized_new(static_cast<guint>(value.ints.size()));
      for (int number : value.ints) {
        auto* element = g_new0(GValue, 1);
        g_value_init(element, G_TYPE_INT);
        g_value_set_int(element, number);
        g_ptr_array_add(array, element);
      }
      ok = xfconf_channel_set_arrayv(xfchannel, prop.c_str(), array);
      xfconf_array_free(array);
      break;
    }
    case PropType::Doubles: {
      GPtrArray* array = g_ptr_array_sized_new(static_cast<guint>(value.doubles.size()));
      for (double number : value.doubles) {
        auto* element = g_new0(GValue, 1);
        g_value_init(element, G_TYPE_DOUBLE);
        g_value_set_double(element, number);
        g_ptr_array_add(array, element);
      }
      ok = xfconf_channel_set_arrayv(xfchannel, prop.c_str(), array);
      xfconf_array_free(array);
      break;
    }
    case PropType::Strings: {
      GPtrArray* array = g_ptr_array_sized_new(static_cast<guint>(value.strings.size()));
      for (const auto& text : value.strings) {
        auto* element = g_new0(GValue, 1);
        g_value_init(element, G_TYPE_STRING);
        g_value_set_string(element, text.c_str());
        g_ptr_array_add(array, element);
      }
      ok = xfconf_channel_set_arrayv(xfchannel, prop.c_str(), array);
      xfconf_array_free(array);
      break;
    }
  }
  return ok != FALSE;
}

bool XfconfStore::set_string(const std::string& channel, const std::string& prop,
                             const std::string& value) {
  PropValue stored;
  stored.type = PropType::String;
  stored.s = value;
  return write(channel, prop, stored, true);
}

bool XfconfStore::set_bool(const std::string& channel, const std::string& prop, bool value) {
  PropValue stored;
  stored.type = PropType::Bool;
  stored.b = value;
  return write(channel, prop, stored, true);
}

bool XfconfStore::set_int(const std::string& channel, const std::string& prop, int value) {
  PropValue stored;
  stored.type = PropType::Int;
  stored.i = value;
  return write(channel, prop, stored, true);
}

bool XfconfStore::set_uint(const std::string& channel, const std::string& prop, unsigned value) {
  PropValue stored;
  stored.type = PropType::Uint;
  stored.u = value;
  return write(channel, prop, stored, true);
}

bool XfconfStore::set_double(const std::string& channel, const std::string& prop,
                             double value) {
  PropValue stored;
  stored.type = PropType::Double;
  stored.d = value;
  return write(channel, prop, stored, true);
}

bool XfconfStore::set_ints(const std::string& channel, const std::string& prop,
                           const std::vector<int>& values) {
  PropValue stored;
  stored.type = PropType::Ints;
  stored.ints = values;
  return write(channel, prop, stored, true);
}

bool XfconfStore::set_doubles(const std::string& channel, const std::string& prop,
                              const std::vector<double>& values) {
  PropValue stored;
  stored.type = PropType::Doubles;
  stored.doubles = values;
  return write(channel, prop, stored, true);
}

bool XfconfStore::set_strings(const std::string& channel, const std::string& prop,
                              const std::vector<std::string>& values) {
  PropValue stored;
  stored.type = PropType::Strings;
  stored.strings = values;
  return write(channel, prop, stored, true);
}

bool XfconfStore::set_string_untracked(const std::string& channel, const std::string& prop,
                                       const std::string& value) {
  PropValue stored;
  stored.type = PropType::String;
  stored.s = value;
  return write(channel, prop, stored, false);
}

bool XfconfStore::reset(const std::string& channel, const std::string& prop, bool recursive) {
  if (!ready_) {
    return false;
  }
  if (recursive) {
    for (const auto& entry : list(channel, prop)) {
      remember(channel, entry.first);
    }
  }
  remember(channel, prop);
  auto* xfchannel = static_cast<XfconfChannel*>(channel_of(channel));
  xfconf_channel_reset_property(xfchannel, prop.c_str(), recursive ? TRUE : FALSE);
  return true;
}

bool XfconfStore::reset_untracked(const std::string& channel, const std::string& prop,
                                  bool recursive) {
  if (!ready_) {
    return false;
  }
  const bool previous = tracking_;
  tracking_ = false;
  auto* xfchannel = static_cast<XfconfChannel*>(channel_of(channel));
  xfconf_channel_reset_property(xfchannel, prop.c_str(), recursive ? TRUE : FALSE);
  tracking_ = previous;
  return true;
}

std::vector<std::pair<std::string, PropValue>> XfconfStore::list(
    const std::string& channel, const std::string& base) const {
  std::vector<std::pair<std::string, PropValue>> rows;
  if (!ready_) {
    return rows;
  }
  auto* xfchannel = static_cast<XfconfChannel*>(channel_of(channel));
  GHashTable* table = xfconf_channel_get_properties(xfchannel, base.c_str());
  if (table == nullptr) {
    return rows;
  }
  GHashTableIter iter;
  g_hash_table_iter_init(&iter, table);
  gpointer key = nullptr;
  gpointer value = nullptr;
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    const std::string path = full_path(base, static_cast<const char*>(key));
    rows.emplace_back(path, from_gvalue(static_cast<const GValue*>(value)));
  }
  g_hash_table_destroy(table);
  return rows;
}

void XfconfStore::revert_session() {
  std::vector<std::string> all;
  for (const auto& entry : originals_) {
    all.push_back(entry.second.channel);
  }
  revert_channels(all);
}

void XfconfStore::revert_channels(const std::vector<std::string>& channels) {
  if (!ready_ || originals_.empty() || channels.empty()) {
    if (channels.empty()) {
      return;
    }
    originals_.clear();
    return;
  }
  const bool previous = tracking_;
  tracking_ = false;
  std::vector<Saved> resets;
  std::vector<Saved> restores;
  for (auto it = originals_.begin(); it != originals_.end();) {
    const bool match =
        std::find(channels.begin(), channels.end(), it->second.channel) != channels.end();
    if (!match) {
      ++it;
      continue;
    }
    if (it->second.value.type == PropType::Absent) {
      resets.push_back(it->second);
    } else {
      restores.push_back(it->second);
    }
    it = originals_.erase(it);
  }
  for (const auto& saved : resets) {
    auto* xfchannel = static_cast<XfconfChannel*>(channel_of(saved.channel));
    xfconf_channel_reset_property(xfchannel, saved.prop.c_str(), FALSE);
  }
  for (const auto& saved : restores) {
    write(saved.channel, saved.prop, saved.value, false);
  }
  tracking_ = previous;
  changed.emit();
}

void XfconfStore::drop_originals() {
  if (originals_.empty()) {
    return;
  }
  originals_.clear();
  changed.emit();
}

void XfconfStore::drop_channel(const std::string& channel) {
  bool removed = false;
  for (auto it = originals_.begin(); it != originals_.end();) {
    if (it->second.channel == channel) {
      it = originals_.erase(it);
      removed = true;
    } else {
      ++it;
    }
  }
  if (removed) {
    changed.emit();
  }
}

bool XfconfStore::channel_touched(const std::string& channel) const {
  for (const auto& entry : originals_) {
    if (entry.second.channel == channel) {
      return true;
    }
  }
  return false;
}

}  // namespace lcos
