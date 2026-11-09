// SPDX-License-Identifier: GPL-3.0-or-later

#include "xfconf_store.hpp"

#include <xfconf/xfconf.h>

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
    out.type = PropType::Int;
    out.i = static_cast<int>(g_value_get_uint(value));
  } else if (G_VALUE_HOLDS_DOUBLE(value)) {
    out.type = PropType::Double;
    out.d = g_value_get_double(value);
  } else if (G_VALUE_HOLDS_STRING(value)) {
    out.type = PropType::String;
    const char* text = g_value_get_string(value);
    out.s = text != nullptr ? text : "";
  } else if (G_VALUE_TYPE(value) == G_TYPE_PTR_ARRAY) {
    out.type = PropType::Ints;
    auto* array = static_cast<GPtrArray*>(g_value_get_boxed(value));
    if (array != nullptr) {
      for (guint i = 0; i < array->len; ++i) {
        auto* element = static_cast<GValue*>(g_ptr_array_index(array, i));
        if (element != nullptr && G_VALUE_HOLDS_INT(element)) {
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
    case PropType::Double:
      return std::fabs(d - other.d) < 1e-9;
    case PropType::String:
      return s == other.s;
    case PropType::Ints:
      return ints == other.ints;
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
  if (!ready_ || originals_.empty()) {
    originals_.clear();
    return;
  }
  const bool previous = tracking_;
  tracking_ = false;
  std::vector<Saved> resets;
  std::vector<Saved> restores;
  for (auto& entry : originals_) {
    if (entry.second.value.type == PropType::Absent) {
      resets.push_back(entry.second);
    } else {
      restores.push_back(entry.second);
    }
  }
  originals_.clear();
  for (const auto& saved : resets) {
    auto* xfchannel = static_cast<XfconfChannel*>(channel_of(saved.channel));
    xfconf_channel_reset_property(xfchannel, saved.prop.c_str(), FALSE);
  }
  for (const auto& saved : restores) {
    write(saved.channel, saved.prop, saved.value, false);
  }
  tracking_ = previous;
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
