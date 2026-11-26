// SPDX-License-Identifier: GPL-3.0-or-later

#include "xfconf_store.hpp"

#include "logic/atomic_file.hpp"

#include <xfconf/xfconf.h>

#include <glib.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <fstream>

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
  restore_journal();
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
  saved.page = edit_page_;
  last_edit_page_ = edit_page_;
  originals_.emplace(key, std::move(saved));
  sync_journal();
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
    sync_journal();
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
  sync_journal();
  changed.emit();
}

void XfconfStore::drop_originals() {
  if (originals_.empty()) {
    return;
  }
  originals_.clear();
  sync_journal();
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
    sync_journal();
    changed.emit();
  }
}

void XfconfStore::revert_page(const std::string& page_id) {
  if (!ready_ || page_id.empty()) {
    return;
  }
  const bool previous = tracking_;
  tracking_ = false;
  std::vector<Saved> resets;
  std::vector<Saved> restores;
  for (auto it = originals_.begin(); it != originals_.end();) {
    if (it->second.page != page_id) {
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
  last_edit_page_ = page_id;
  sync_journal();
  changed.emit();
}

void XfconfStore::drop_page(const std::string& page_id) {
  if (page_id.empty()) {
    return;
  }
  bool removed = false;
  for (auto it = originals_.begin(); it != originals_.end();) {
    if (it->second.page == page_id) {
      it = originals_.erase(it);
      removed = true;
    } else {
      ++it;
    }
  }
  if (removed) {
    last_edit_page_ = page_id;
    sync_journal();
    changed.emit();
  }
}

void XfconfStore::commit_property(const std::string& channel, const std::string& prop) {
  const auto found = originals_.find(key_of(channel, prop));
  if (found == originals_.end()) {
    return;
  }
  last_edit_page_ = found->second.page;
  originals_.erase(found);
  sync_journal();
  changed.emit();
}

bool XfconfStore::page_dirty(const std::string& page_id) const {
  if (page_id.empty()) {
    return false;
  }
  for (const auto& entry : originals_) {
    if (entry.second.page == page_id) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> XfconfStore::dirty_pages() const {
  std::vector<std::string> pages;
  for (const auto& entry : originals_) {
    if (entry.second.page.empty()) {
      continue;
    }
    if (std::find(pages.begin(), pages.end(), entry.second.page) == pages.end()) {
      pages.push_back(entry.second.page);
    }
  }
  return pages;
}

void XfconfStore::set_journal_path(std::string path) { journal_path_ = std::move(path); }

std::string XfconfStore::journal_path() const {
  if (!journal_path_.empty()) {
    return journal_path_;
  }
  const char* dir = g_get_user_config_dir();
  const std::string root = dir != nullptr && dir[0] != '\0' ? dir : "/tmp";
  return root + "/lcos-settings/rollback.journal";
}

JournalEntry XfconfStore::entry_from(const Saved& saved) {
  JournalEntry entry;
  entry.channel = saved.channel;
  entry.property = saved.prop;
  entry.page = saved.page;
  switch (saved.value.type) {
    case PropType::Absent:
      entry.type = JournalType::Absent;
      break;
    case PropType::Bool:
      entry.type = JournalType::Bool;
      entry.b = saved.value.b;
      break;
    case PropType::Int:
      entry.type = JournalType::Int;
      entry.i = saved.value.i;
      break;
    case PropType::Uint:
      entry.type = JournalType::Uint;
      entry.u = saved.value.u;
      break;
    case PropType::Double:
      entry.type = JournalType::Double;
      entry.d = saved.value.d;
      break;
    case PropType::String:
      entry.type = JournalType::String;
      entry.s = saved.value.s;
      break;
    case PropType::Ints:
      entry.type = JournalType::Ints;
      entry.ints = saved.value.ints;
      break;
    case PropType::Doubles:
      entry.type = JournalType::Doubles;
      entry.doubles = saved.value.doubles;
      break;
    case PropType::Strings:
      entry.type = JournalType::Strings;
      entry.strings = saved.value.strings;
      break;
  }
  return entry;
}

XfconfStore::Saved XfconfStore::saved_from(const JournalEntry& entry) {
  Saved saved;
  saved.channel = entry.channel;
  saved.prop = entry.property;
  saved.page = entry.page;
  switch (entry.type) {
    case JournalType::Absent:
      saved.value.type = PropType::Absent;
      break;
    case JournalType::Bool:
      saved.value.type = PropType::Bool;
      saved.value.b = entry.b;
      break;
    case JournalType::Int:
      saved.value.type = PropType::Int;
      saved.value.i = entry.i;
      break;
    case JournalType::Uint:
      saved.value.type = PropType::Uint;
      saved.value.u = entry.u;
      break;
    case JournalType::Double:
      saved.value.type = PropType::Double;
      saved.value.d = entry.d;
      break;
    case JournalType::String:
      saved.value.type = PropType::String;
      saved.value.s = entry.s;
      break;
    case JournalType::Ints:
      saved.value.type = PropType::Ints;
      saved.value.ints = entry.ints;
      break;
    case JournalType::Doubles:
      saved.value.type = PropType::Doubles;
      saved.value.doubles = entry.doubles;
      break;
    case JournalType::Strings:
      saved.value.type = PropType::Strings;
      saved.value.strings = entry.strings;
      break;
  }
  return saved;
}

void XfconfStore::sync_journal() {
  if (journal_quiet_) {
    return;
  }
  const std::string path = journal_path();
  if (originals_.empty()) {
    ::unlink(path.c_str());
    return;
  }
  std::vector<JournalEntry> entries;
  entries.reserve(originals_.size());
  for (const auto& item : originals_) {
    entries.push_back(entry_from(item.second));
  }
  const auto slash = path.find_last_of('/');
  if (slash != std::string::npos) {
    g_mkdir_with_parents(path.substr(0, slash).c_str(), 0700);
  }
  ReplaceRequest request;
  request.mode_if_new = 0600;
  std::string error;
  replace_regular_file(path, format_rollback_journal(entries), request, &error);
}

bool XfconfStore::restore_journal() {
  if (!ready_) {
    return false;
  }
  const std::string path = journal_path();
  std::ifstream in(path);
  if (!in) {
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (text.empty()) {
    ::unlink(path.c_str());
    return false;
  }
  std::vector<JournalEntry> entries;
  std::string error;
  if (!parse_rollback_journal(text, entries, &error)) {
    g_warning("rollback journal: %s", error.c_str());
    const std::string bad = path + ".bad";
    ::rename(path.c_str(), bad.c_str());
    return false;
  }
  if (entries.empty()) {
    ::unlink(path.c_str());
    return false;
  }
  journal_quiet_ = true;
  originals_.clear();
  for (const auto& entry : entries) {
    if (entry.channel.empty() || entry.property.empty()) {
      continue;
    }
    originals_[key_of(entry.channel, entry.property)] = saved_from(entry);
  }
  const bool previous = tracking_;
  tracking_ = false;
  revert_session();
  tracking_ = previous;
  journal_quiet_ = false;
  ::unlink(path.c_str());
  return true;
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
