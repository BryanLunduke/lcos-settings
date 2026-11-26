// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_SOUND_PAGE_HPP
#define LCOS_SOUND_PAGE_HPP

#include "backends/pulse_client.hpp"
#include "backends/xfconf_store.hpp"
#include "logic/rollback_journal.hpp"
#include "logic/sound_edit.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/scale.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lcos {

class SoundPage : public SettingsPage {
 public:
  SoundPage(XfconfStore& store, PulseClient& pulse);
  ~SoundPage() override;
  Glib::ustring title() const override { return "Sound"; }
  Glib::ustring summary() const override {
    return "Output, input, and application volume.";
  }
  bool has_pending() const override { return sound_dirty_; }
  bool commit_pending() override;
  void revert_page() override;
  bool discard_pending() override;
  std::string pending_detail() const override;
  void on_page_shown() override;
  void reload_from_store() override { refresh(); }
  int output_volume_percent() const;
  void set_output_volume_percent(int percent);
  bool output_muted() const;
  void set_output_muted(bool muted);
  bool output_ready() const;

 private:
  struct StreamSnap {
    std::string application;
    std::string media_name;
    std::uint32_t index = 0;
    std::vector<std::uint32_t> volumes;
    bool mute = false;
  };
  struct DeviceSnap {
    std::string name;
    std::vector<std::uint32_t> volumes;
    bool mute = false;
    std::string port;
  };
  struct AppRow {
    Gtk::Box* row = nullptr;
    Gtk::Scale* scale = nullptr;
    Gtk::CheckButton* mute = nullptr;
    bool dragging = false;
  };

  enum class SoundField {
    OutputDevice,
    OutputPort,
    OutputVolume,
    OutputMute,
    InputDevice,
    InputPort,
    InputVolume,
    InputMute,
    Profile,
    Stream
  };

  void refresh();
  void update_apps();
  void take_snapshot();
  void save_sound_journal() const;
  void restore_sound_journal();
  static void clear_sound_journal();
  static std::string sound_journal_path();
  void touch(SoundField field, std::uint32_t stream = 0);
  static std::string combo_key(const std::vector<std::pair<std::string, std::string>>& rows);
  const PaDevice* selected_sink() const;
  const PaDevice* selected_source() const;
  const PaCard* card_for(std::uint32_t index) const;

  XfconfStore& store_;
  PulseClient& pulse_;
  int freeze_ = 0;
  bool output_dragging_ = false;
  bool input_dragging_ = false;
  bool sound_dirty_ = false;
  bool have_snapshot_ = false;
  SoundEdit edit_;
  std::string output_rows_;
  std::string input_rows_;
  std::string output_port_rows_;
  std::string input_port_rows_;
  std::string profile_rows_;
  DeviceSnap output_snapshot_;
  DeviceSnap input_snapshot_;
  std::string profile_snapshot_;
  std::string profile_card_name_;
  std::uint32_t profile_card_ = 0;
  bool have_profile_card_ = false;
  bool journal_checked_ = false;
  std::vector<StreamSnap> stream_snapshot_;
  std::map<std::uint32_t, AppRow> app_rows_;

  Gtk::ComboBoxText output_;
  Gtk::ComboBoxText output_port_;
  Gtk::Label* output_port_label_ = nullptr;
  Gtk::Label* input_port_label_ = nullptr;
  Gtk::Label* profile_label_ = nullptr;
  Gtk::Scale output_volume_;
  Gtk::CheckButton output_mute_;
  Gtk::ComboBoxText input_;
  Gtk::ComboBoxText input_port_;
  Gtk::Scale input_volume_;
  Gtk::CheckButton input_mute_;
  Gtk::ComboBoxText profile_;
  Gtk::Box* apps_ = nullptr;
  Gtk::Widget* card_group_ = nullptr;
  Gtk::Label* input_note_ = nullptr;
  Gtk::Label* status_ = nullptr;
  Gtk::CheckButton event_sounds_;
  Gtk::CheckButton input_feedback_;
  Gtk::ComboBoxText sound_theme_;
};

}  // namespace lcos

#endif
