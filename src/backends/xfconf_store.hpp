// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_XFCONF_STORE_HPP
#define LCOS_XFCONF_STORE_HPP

#include "logic/rollback_journal.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <sigc++/signal.h>

namespace lcos {

enum class PropType { Absent, Bool, Int, Uint, Double, String, Ints, Doubles, Strings };

struct PropValue {
  PropType type = PropType::Absent;
  bool b = false;
  int i = 0;
  unsigned u = 0;
  double d = 0;
  std::string s;
  std::vector<int> ints;
  std::vector<double> doubles;
  std::vector<std::string> strings;

  bool same_as(const PropValue& other) const;
};

// Reads and writes the xfconf channels the XFCE settings dialogs use.
// The first write of a property during this visit remembers the previous
// value so Cancel can put it back. Writes that do not change the stored
// value are ignored.
class XfconfStore {
 public:
  XfconfStore() = default;
  ~XfconfStore();

  XfconfStore(const XfconfStore&) = delete;
  XfconfStore& operator=(const XfconfStore&) = delete;

  bool init(std::string* error);
  bool available() const { return ready_; }

  bool has(const std::string& channel, const std::string& prop) const;
  PropValue get(const std::string& channel, const std::string& prop) const;
  std::string get_string(const std::string& channel, const std::string& prop,
                         const std::string& fallback) const;
  bool get_bool(const std::string& channel, const std::string& prop,
                bool fallback) const;
  int get_int(const std::string& channel, const std::string& prop,
              int fallback) const;
  unsigned get_uint(const std::string& channel, const std::string& prop,
                    unsigned fallback) const;
  double get_double(const std::string& channel, const std::string& prop,
                    double fallback) const;
  std::vector<int> get_ints(const std::string& channel, const std::string& prop) const;
  std::vector<double> get_doubles(const std::string& channel,
                                  const std::string& prop) const;
  std::vector<std::string> get_strings(const std::string& channel,
                                       const std::string& prop) const;

  bool set_string(const std::string& channel, const std::string& prop,
                  const std::string& value);
  bool set_bool(const std::string& channel, const std::string& prop, bool value);
  bool set_int(const std::string& channel, const std::string& prop, int value);
  bool set_uint(const std::string& channel, const std::string& prop, unsigned value);
  bool set_double(const std::string& channel, const std::string& prop, double value);
  bool set_ints(const std::string& channel, const std::string& prop,
                const std::vector<int>& values);
  bool set_doubles(const std::string& channel, const std::string& prop,
                   const std::vector<double>& values);
  bool set_strings(const std::string& channel, const std::string& prop,
                   const std::vector<std::string>& values);
  bool reset(const std::string& channel, const std::string& prop, bool recursive);

  // Writes that should survive Cancel (the display preview profile, and the
  // apply trigger the daemon consumes).
  bool set_string_untracked(const std::string& channel, const std::string& prop,
                            const std::string& value);
  bool reset_untracked(const std::string& channel, const std::string& prop,
                       bool recursive);

  std::vector<std::pair<std::string, PropValue>> list(
      const std::string& channel, const std::string& base) const;

  void set_tracking(bool on) { tracking_ = on; }
  bool tracking() const { return tracking_; }
  // Page id stamped on originals remembered while this is set.
  void set_edit_page(const std::string& page_id) { edit_page_ = page_id; }
  const std::string& edit_page() const { return edit_page_; }
  const std::string& last_edit_page() const { return last_edit_page_; }
  void revert_session();
  // Puts back only the listed channels and leaves the rest of the visit.
  void revert_channels(const std::vector<std::string>& channels);
  // Puts back only the properties first written by this page.
  void revert_page(const std::string& page_id);
  // Apply and OK keep what has been written. Later Cancel only restores
  // changes made after the checkpoint.
  void drop_originals();
  void drop_channel(const std::string& channel);
  void drop_page(const std::string& page_id);
  // The current value of one property becomes the baseline, so Cancel will
  // not restore whatever it was at the start of the visit.
  void commit_property(const std::string& channel, const std::string& prop);
  bool session_dirty() const { return !originals_.empty(); }
  // Live writes are journaled so a kill, SIGTERM, or the next start can put
  // them back. Tests can point this at a temporary file. An empty path uses
  // ~/.config/lcos-settings/rollback.journal.
  void set_journal_path(std::string path);
  std::string journal_path() const;
  // Puts a journal left by a killed process back into xfconf.
  bool restore_journal();
  bool channel_touched(const std::string& channel) const;
  bool page_dirty(const std::string& page_id) const;
  std::vector<std::string> dirty_pages() const;
  int pending_count() const { return static_cast<int>(originals_.size()); }
  sigc::signal<void> changed;

 private:
  struct Saved {
    std::string channel;
    std::string prop;
    PropValue value;
    std::string page;
  };

  void remember(const std::string& channel, const std::string& prop);
  bool write(const std::string& channel, const std::string& prop,
             const PropValue& value, bool track);
  void* channel_of(const std::string& name) const;
  void sync_journal();
  static JournalEntry entry_from(const Saved& saved);
  static Saved saved_from(const JournalEntry& entry);

  bool ready_ = false;
  bool tracking_ = true;
  bool journal_quiet_ = false;
  std::string journal_path_;
  std::string edit_page_;
  std::string last_edit_page_;
  mutable std::map<std::string, void*> channels_;
  std::map<std::string, Saved> originals_;
};

}  // namespace lcos

#endif
