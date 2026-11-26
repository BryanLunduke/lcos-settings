// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_ROLLBACK_JOURNAL_HPP
#define LCOS_LOGIC_ROLLBACK_JOURNAL_HPP

#include <string>
#include <vector>

namespace lcos {

// One xfconf property as it was before this visit changed it. Absent means
// the property did not exist and Cancel (or the next start) removes it.
enum class JournalType { Absent, Bool, Int, Uint, Double, String, Ints, Doubles, Strings };

struct JournalEntry {
  std::string channel;
  std::string property;
  std::string page;
  JournalType type = JournalType::Absent;
  bool b = false;
  int i = 0;
  unsigned u = 0;
  double d = 0;
  std::string s;
  std::vector<int> ints;
  std::vector<double> doubles;
  std::vector<std::string> strings;
};

// Text form of the visit snapshot. Empty entries are a valid journal that
// restores nothing. parse returns false on a truncated or unknown file.
std::string format_rollback_journal(const std::vector<JournalEntry>& entries);
bool parse_rollback_journal(const std::string& text, std::vector<JournalEntry>& entries,
                            std::string* error);

// PulseAudio levels Command Center changed and has not kept. Device names
// are stable across a restart; stream indexes are not, so streams match on
// application and media name.
struct SoundRollbackStream {
  std::string application;
  std::string media;
  bool mute = false;
  std::vector<unsigned> volumes;
};

struct SoundRollback {
  bool output_device = false;
  std::string output_name;
  bool output_volume = false;
  std::vector<unsigned> output_volumes;
  bool output_mute_set = false;
  bool output_mute = false;
  bool output_port = false;
  std::string output_port_name;

  bool input_device = false;
  std::string input_name;
  bool input_volume = false;
  std::vector<unsigned> input_volumes;
  bool input_mute_set = false;
  bool input_mute = false;
  bool input_port = false;
  std::string input_port_name;

  bool profile = false;
  std::string profile_card;
  std::string profile_name;
  std::vector<SoundRollbackStream> streams;

  bool any() const;
};

std::string format_sound_rollback(const SoundRollback& rollback);
bool parse_sound_rollback(const std::string& text, SoundRollback& rollback, std::string* error);

}  // namespace lcos

#endif
