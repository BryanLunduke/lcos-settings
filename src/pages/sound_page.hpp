// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_SOUND_PAGE_HPP
#define LCOS_SOUND_PAGE_HPP

#include "backends/pulse_client.hpp"
#include "backends/xfconf_store.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/scale.h>

namespace lcos {

class SoundPage : public SettingsPage {
 public:
  SoundPage(XfconfStore& store, PulseClient& pulse);
  Glib::ustring title() const override { return "Sound"; }
  Glib::ustring summary() const override {
    return "Output, input, application volumes, and card profiles. PulseAudio stores the levels.";
  }
  void on_page_shown() override;

 private:
  void refresh();
  void build_apps();
  const PaDevice* selected_sink() const;
  const PaDevice* selected_source() const;
  const PaCard* card_for(std::uint32_t index) const;

  XfconfStore& store_;
  PulseClient& pulse_;
  int freeze_ = 0;
  bool dragging_ = false;

  Gtk::ComboBoxText output_;
  Gtk::ComboBoxText output_port_;
  Gtk::Scale output_volume_;
  Gtk::CheckButton output_mute_;
  Gtk::ComboBoxText input_;
  Gtk::ComboBoxText input_port_;
  Gtk::Scale input_volume_;
  Gtk::CheckButton input_mute_;
  Gtk::ComboBoxText profile_;
  Gtk::Box* apps_ = nullptr;
  Gtk::Label* status_ = nullptr;
  Gtk::CheckButton event_sounds_;
  Gtk::CheckButton input_feedback_;
  Gtk::ComboBoxText sound_theme_;
};

}  // namespace lcos

#endif
