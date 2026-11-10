// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/power_page.hpp"

#include "logic/power.hpp"
#include "ui_util.hpp"

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/stack.h>
#include <gtkmm/stackswitcher.h>

#include <glib.h>

#include <fstream>
#include <string>

namespace lcos {
namespace {

constexpr const char* kChannel = "xfce4-power-manager";

bool directory_exists(const char* path) {
  return g_file_test(path, G_FILE_TEST_IS_DIR) != 0;
}

bool machine_has_battery() {
  GDir* dir = g_dir_open("/sys/class/power_supply", 0, nullptr);
  if (dir == nullptr) {
    return false;
  }
  bool found = false;
  while (const char* name = g_dir_read_name(dir)) {
    const std::string path =
        std::string("/sys/class/power_supply/") + name + "/type";
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (power_supply_type_is_battery(text)) {
      found = true;
      break;
    }
  }
  g_dir_close(dir);
  return found;
}

bool machine_has_lid() { return directory_exists("/proc/acpi/button/lid"); }

void fill_button_actions(Gtk::ComboBoxText& combo) {
  combo.append("0", "Do nothing");
  combo.append("1", "Suspend");
  combo.append("2", "Hibernate");
  combo.append("3", "Ask");
  combo.append("4", "Shut down");
  combo.append("5", "Hybrid sleep");
}

void fill_lid_actions(Gtk::ComboBoxText& combo) {
  combo.append("0", "Switch off display");
  combo.append("1", "Suspend");
  combo.append("2", "Hibernate");
  combo.append("3", "Lock screen");
  combo.append("4", "Do nothing");
  combo.append("5", "Hybrid sleep");
  combo.append("6", "Shut down");
}

void fill_profiles(Gtk::ComboBoxText& combo) {
  combo.append("power-saver", "Power saver");
  combo.append("balanced", "Balanced");
  combo.append("performance", "Performance");
}

Gtk::SpinButton* minutes_spin() {
  auto* spin = Gtk::manage(new Gtk::SpinButton());
  spin->set_range(0, 120);
  spin->set_increments(1, 5);
  spin->set_digits(0);
  spin->set_numeric(true);
  return spin;
}

void hide_without_show_all(Gtk::Widget& widget) {
  widget.set_no_show_all(true);
  widget.hide();
}

}  // namespace

PowerPage::PowerPage(XfconfStore& store) : store_(store) {
  const bool battery = machine_has_battery();
  const bool lid = machine_has_lid();
  pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);

  auto* general = make_vbox(8);
  general->set_border_width(12);
  auto* buttons = make_vbox(8);
  FormGrid button_form(220);
  auto* power = Gtk::manage(new Gtk::ComboBoxText());
  auto* sleep = Gtk::manage(new Gtk::ComboBoxText());
  auto* hibernate = Gtk::manage(new Gtk::ComboBoxText());
  Gtk::ComboBoxText* battery_button = nullptr;
  fill_button_actions(*power);
  fill_button_actions(*sleep);
  fill_button_actions(*hibernate);
  button_form.add("When power button is pressed", *power);
  button_form.add("When sleep button is pressed", *sleep);
  button_form.add("When hibernate button is pressed", *hibernate);
  if (battery) {
    battery_button = Gtk::manage(new Gtk::ComboBoxText());
    fill_button_actions(*battery_button);
    button_form.add("When battery button is pressed", *battery_button);
  }
  buttons->pack_start(button_form.grid(), Gtk::PACK_SHRINK);
  auto* handle_keys = Gtk::manage(new Gtk::CheckButton("Handle display brightness keys"));
  auto* exponential = Gtk::manage(new Gtk::CheckButton("Exponential brightness steps"));
  auto* steps = Gtk::manage(new Gtk::SpinButton());
  steps->set_range(2, 100);
  steps->set_increments(1, 5);
  steps->set_digits(0);
  button_form.add_control(*handle_keys);
  button_form.add("Brightness step count", *steps);
  button_form.add_control(*exponential);
  general->pack_start(*make_frame("Buttons", *buttons), Gtk::PACK_SHRINK);

  auto* appearance = make_vbox(8);
  FormGrid look_form(220);
  auto* notifications = Gtk::manage(new Gtk::CheckButton("Status notifications"));
  auto* tray = Gtk::manage(new Gtk::CheckButton("System tray icon"));
  look_form.add_control(*notifications);
  look_form.add_control(*tray);
  appearance->pack_start(look_form.grid(), Gtk::PACK_SHRINK);
  general->pack_start(*make_frame("Appearance", *appearance), Gtk::PACK_SHRINK);
  auto* general_note = make_label(
      "Battery controls stay hidden on a machine with no battery. Existing defaults are left alone until you edit a control.");
  general_note->set_line_wrap(true);
  general_note->set_max_width_chars(70);
  general->pack_start(*general_note, Gtk::PACK_SHRINK);
  notebook_.append_page(*scrolled(*general), "General");

  auto make_supply = [&](bool on_battery) {
    auto* page = make_vbox(8);
    FormGrid form(220);
    auto* inactive = minutes_spin();
    auto* mode = Gtk::manage(new Gtk::ComboBoxText());
    fill_button_actions(*mode);
    auto* profile = Gtk::manage(new Gtk::ComboBoxText());
    fill_profiles(*profile);
    form.add("When inactive for (minutes, 0 = never)", *inactive);
    form.add("System sleep mode", *mode);
    Gtk::ComboBoxText* lid_action = nullptr;
    if (lid) {
      lid_action = Gtk::manage(new Gtk::ComboBoxText());
      fill_lid_actions(*lid_action);
      form.add("When laptop lid is closed", *lid_action);
    }
    form.add("Power profile", *profile);
    page->pack_start(form.grid(), Gtk::PACK_SHRINK);
    const char* inactive_key = on_battery ? "inactivity-on-battery" : "inactivity-on-ac";
    const char* mode_key =
        on_battery ? "inactivity-sleep-mode-on-battery" : "inactivity-sleep-mode-on-ac";
    const char* lid_key = on_battery ? "lid-action-on-battery" : "lid-action-on-ac";
    const char* profile_key = on_battery ? "profile-on-battery" : "profile-on-ac";
    bind_minutes(*inactive, inactive_key, 0);
    bind_uint(*mode, mode_key, 1);
    if (lid_action != nullptr) {
      bind_uint(*lid_action, lid_key, 3);
    }
    bind_string(*profile, profile_key, "balanced");
    if (on_battery) {
      auto* critical = make_vbox(8);
      FormGrid critical_form(220);
      auto* level = Gtk::manage(new Gtk::SpinButton());
      level->set_range(1, 70);
      level->set_increments(1, 5);
      level->set_digits(0);
      auto* action = Gtk::manage(new Gtk::ComboBoxText());
      fill_button_actions(*action);
      critical_form.add("Critical battery level (%)", *level);
      critical_form.add("On critical battery", *action);
      critical->pack_start(critical_form.grid(), Gtk::PACK_SHRINK);
      page->pack_start(*make_frame("Critical power", *critical), Gtk::PACK_SHRINK);
      bind_minutes(*level, "critical-power-level", 10);
      bind_uint(*action, "critical-power-action", 0);
    }
    return page;
  };

  auto* system = make_vbox(8);
  system->set_border_width(12);
  auto* system_stack = Gtk::manage(new Gtk::Stack());
  auto* system_switcher = Gtk::manage(new Gtk::StackSwitcher());
  system_switcher->set_stack(*system_stack);
  system_switcher->set_halign(Gtk::ALIGN_CENTER);
  system->pack_start(*system_switcher, Gtk::PACK_SHRINK);
  system_stack->add(*make_supply(false), "ac", "On AC");
  auto* battery_system = make_supply(true);
  system_stack->add(*battery_system, "battery", "On battery");
  system_stack->set_visible_child("ac");
  system->pack_start(*system_stack, Gtk::PACK_SHRINK);
  if (!battery) {
    hide_without_show_all(*system_switcher);
    hide_without_show_all(*battery_system);
  }
  notebook_.append_page(*scrolled(*system), "System");

  auto make_display = [&](bool on_battery, Gtk::SpinButton*& blank_out, Gtk::SpinButton*& off_out,
                          Gtk::SpinButton*& dim_out) {
    auto* page = make_vbox(8);
    auto* dpms = make_vbox(8);
    FormGrid form(220);
    auto* blank = minutes_spin();
    auto* off = minutes_spin();
    form.add("Put to sleep after (minutes, 0 = never)", *blank);
    form.add("Switch off after (minutes, 0 = never)", *off);
    dpms->pack_start(form.grid(), Gtk::PACK_SHRINK);
    page->pack_start(*make_frame("Display power management", *dpms), Gtk::PACK_SHRINK);
    auto* bright = make_vbox(8);
    FormGrid bright_form(220);
    auto* level = Gtk::manage(new Gtk::SpinButton());
    level->set_range(1, 100);
    level->set_increments(1, 10);
    level->set_digits(0);
    auto* delay = Gtk::manage(new Gtk::SpinButton());
    delay->set_range(0, 60);
    delay->set_increments(1, 6);
    delay->set_digits(0);
    bright_form.add("On inactivity reduce to (%)", *level);
    bright_form.add("Reduce after (0 = never, steps of 10 seconds)", *delay);
    bright->pack_start(bright_form.grid(), Gtk::PACK_SHRINK);
    page->pack_start(*make_frame("Brightness reduction", *bright), Gtk::PACK_SHRINK);
    const char* blank_key = on_battery ? "dpms-on-battery-sleep" : "dpms-on-ac-sleep";
    const char* off_key = on_battery ? "dpms-on-battery-off" : "dpms-on-ac-off";
    const char* level_key = on_battery ? "brightness-level-on-battery" : "brightness-level-on-ac";
    const char* delay_key = on_battery ? "brightness-on-battery" : "brightness-on-ac";
    bind_minutes(*blank, blank_key, on_battery ? 5 : 10);
    bind_minutes(*off, off_key, on_battery ? 10 : 15);
    bind_minutes(*level, level_key, on_battery ? 20 : 80);
    bind_brightness_delay(*delay, delay_key, on_battery ? 300 : 9);
    blank_out = blank;
    off_out = off;
    dim_out = delay;
    return page;
  };

  auto* display = make_vbox(8);
  display->set_border_width(12);
  auto* dpms_enabled = Gtk::manage(new Gtk::CheckButton("Enable display power management"));
  display->pack_start(*dpms_enabled, Gtk::PACK_SHRINK);
  auto* mode_row = make_vbox(8);
  FormGrid mode_form(220);
  auto* sleep_mode = Gtk::manage(new Gtk::ComboBoxText());
  sleep_mode->append("Standby", "Standby");
  sleep_mode->append("Suspend", "Suspend");
  sleep_mode->append("Off", "Off");
  mode_form.add("Blanking mode", *sleep_mode);
  mode_row->pack_start(mode_form.grid(), Gtk::PACK_SHRINK);
  display->pack_start(*mode_row, Gtk::PACK_SHRINK);
  auto* display_stack = Gtk::manage(new Gtk::Stack());
  auto* display_switcher = Gtk::manage(new Gtk::StackSwitcher());
  display_switcher->set_stack(*display_stack);
  display_switcher->set_halign(Gtk::ALIGN_CENTER);
  display->pack_start(*display_switcher, Gtk::PACK_SHRINK);
  Gtk::SpinButton *ac_blank = nullptr, *ac_off = nullptr, *ac_dim = nullptr;
  Gtk::SpinButton *bat_blank = nullptr, *bat_off = nullptr, *bat_dim = nullptr;
  display_stack->add(*make_display(false, ac_blank, ac_off, ac_dim), "ac", "On AC");
  auto* battery_display = make_display(true, bat_blank, bat_off, bat_dim);
  display_stack->add(*battery_display, "battery", "On battery");
  display_stack->set_visible_child("ac");
  display->pack_start(*display_stack, Gtk::PACK_SHRINK);
  if (!battery) {
    hide_without_show_all(*display_switcher);
    hide_without_show_all(*battery_display);
  }
  notebook_.append_page(*scrolled(*display), "Display");

  auto* security = make_vbox(8);
  security->set_border_width(12);
  auto* lock_box = make_vbox(8);
  auto* lock = Gtk::manage(
      new Gtk::CheckButton("Lock screen when the system is going to sleep"));
  auto* note = make_label(
      "xfce4-power-manager asks light-locker, or whichever locker is installed, to lock on suspend and hibernate.");
  note->set_line_wrap(true);
  note->set_max_width_chars(70);
  lock_box->pack_start(*lock, Gtk::PACK_SHRINK);
  lock_box->pack_start(*note, Gtk::PACK_SHRINK);
  security->pack_start(*make_frame("Security", *lock_box), Gtk::PACK_SHRINK);
  notebook_.append_page(*scrolled(*security), "Security");

  bind_uint(*power, "power-button-action", 0);
  bind_uint(*sleep, "sleep-button-action", 0);
  bind_uint(*hibernate, "hibernate-button-action", 0);
  if (battery_button != nullptr) {
    bind_uint(*battery_button, "battery-button-action", 0);
  }
  bind_bool(*handle_keys, "handle-brightness-keys", true);
  bind_minutes(*steps, "brightness-step-count", 10);
  bind_bool(*exponential, "brightness-exponential", false);
  bind_bool(*notifications, "general-notification", true);
  bind_bool(*tray, "show-tray-icon", false);
  bind_bool(*dpms_enabled, "dpms-enabled", true);
  bind_string(*sleep_mode, "dpms-sleep-mode", "Standby");
  bind_bool(*lock, "lock-screen-suspend-hibernate", true);

  const bool dpms_on = dpms_enabled->get_active();
  ac_blank->set_sensitive(dpms_on);
  ac_off->set_sensitive(dpms_on);
  if (battery && bat_blank != nullptr && bat_off != nullptr) {
    bat_blank->set_sensitive(dpms_on);
    bat_off->set_sensitive(dpms_on);
  }
  dpms_enabled->signal_toggled().connect(
      [dpms_enabled, ac_blank, ac_off, bat_blank, bat_off, battery] {
        const bool on = dpms_enabled->get_active();
        ac_blank->set_sensitive(on);
        ac_off->set_sensitive(on);
        if (battery && bat_blank != nullptr && bat_off != nullptr) {
          bat_blank->set_sensitive(on);
          bat_off->set_sensitive(on);
        }
      });
  (void)ac_dim;
  (void)bat_dim;
}

Gtk::Widget* PowerPage::scrolled(Gtk::Widget& child) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  scroll->set_shadow_type(Gtk::SHADOW_NONE);
  scroll->add(child);
  return scroll;
}

std::string PowerPage::prop(const char* leaf) const {
  return std::string("/xfce4-power-manager/") + leaf;
}

void PowerPage::bind_uint(Gtk::ComboBoxText& combo, const char* leaf, unsigned fallback) {
  {
    Freeze freeze(freeze_);
    const unsigned value = store_.get_uint(kChannel, prop(leaf), fallback);
    combo.set_active_id(std::to_string(value));
    if (combo.get_active_id().empty()) {
      combo.set_active(0);
    }
  }
  combo.signal_changed().connect([this, &combo, leaf] {
    if (freeze_ > 0 || combo.get_active_id().empty()) {
      return;
    }
    try {
      store_.set_uint(kChannel, prop(leaf),
                      static_cast<unsigned>(std::stoul(std::string(combo.get_active_id()))));
    } catch (...) {
    }
  });
}

void PowerPage::bind_minutes(Gtk::SpinButton& spin, const char* leaf, unsigned fallback) {
  {
    Freeze freeze(freeze_);
    spin.set_value(store_.get_uint(kChannel, prop(leaf), fallback));
  }
  spin.signal_value_changed().connect([this, &spin, leaf] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_uint(kChannel, prop(leaf), static_cast<unsigned>(std::max(0, spin.get_value_as_int())));
  });
}

void PowerPage::bind_bool(Gtk::CheckButton& check, const char* leaf, bool fallback) {
  {
    Freeze freeze(freeze_);
    check.set_active(store_.get_bool(kChannel, prop(leaf), fallback));
  }
  check.signal_toggled().connect([this, &check, leaf] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, prop(leaf), check.get_active());
  });
}

void PowerPage::bind_string(Gtk::ComboBoxText& combo, const char* leaf,
                            const std::string& fallback) {
  {
    Freeze freeze(freeze_);
    std::string value = store_.get_string(kChannel, prop(leaf), fallback);
    if (value.empty()) {
      value = fallback;
    }
    combo.set_active_id(value);
    if (combo.get_active_row_number() < 0) {
      combo.append(value, value);
      combo.set_active_id(value);
    }
  }
  combo.signal_changed().connect([this, &combo, leaf] {
    if (freeze_ > 0 || combo.get_active_id().empty()) {
      return;
    }
    store_.set_string(kChannel, prop(leaf), combo.get_active_id());
  });
}

void PowerPage::bind_brightness_delay(Gtk::SpinButton& spin, const char* leaf,
                                      unsigned fallback) {
  {
    Freeze freeze(freeze_);
    const unsigned stored = store_.get_uint(kChannel, prop(leaf), fallback);
    spin.set_value(brightness_slider_from_delay(stored));
  }
  spin.signal_value_changed().connect([this, &spin, leaf] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_uint(kChannel, prop(leaf), brightness_delay_from_slider(spin.get_value_as_int()));
  });
}

void PowerPage::prepare_screenshot(int index) {
  if (index >= 0 && index < notebook_.get_n_pages()) {
    notebook_.set_current_page(index);
  }
}

std::string PowerPage::screenshot_suffix(int index) const {
  switch (index) {
    case 0:
      return "general";
    case 1:
      return "system";
    case 2:
      return "display";
    case 3:
      return "security";
    default:
      return {};
  }
}

}  // namespace lcos
