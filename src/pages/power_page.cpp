// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/power_page.hpp"

#include "logic/power.hpp"
#include "ui_util.hpp"

#include <gio/gio.h>

#include <gtkmm/box.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stackswitcher.h>

#include <glib.h>

#include <cstring>
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

struct SleepCaps {
  bool suspend = false;
  bool hibernate = false;
  bool hybrid = false;
  bool known = false;
};

std::string login1_can(const char* method) {
  GError* error = nullptr;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
  if (bus == nullptr) {
    if (error != nullptr) {
      g_error_free(error);
    }
    return {};
  }
  GVariant* result = g_dbus_connection_call_sync(
      bus, "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
      method, nullptr, G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 1500, nullptr, &error);
  g_object_unref(bus);
  if (result == nullptr) {
    if (error != nullptr) {
      g_error_free(error);
    }
    return {};
  }
  const char* answer = nullptr;
  g_variant_get(result, "(&s)", &answer);
  const std::string text = answer != nullptr ? answer : "";
  g_variant_unref(result);
  return text;
}

bool login1_yes(const std::string& answer) { return answer == "yes" || answer == "challenge"; }

SleepCaps query_sleep_caps() {
  SleepCaps caps;
  const std::string suspend = login1_can("CanSuspend");
  const std::string hibernate = login1_can("CanHibernate");
  const std::string hybrid = login1_can("CanHybridSleep");
  caps.known = !suspend.empty() || !hibernate.empty() || !hybrid.empty();
  caps.suspend = login1_yes(suspend);
  caps.hibernate = login1_yes(hibernate);
  caps.hybrid = login1_yes(hybrid);
  return caps;
}

void fill_power_choices(Gtk::ComboBoxText& combo, PowerChoiceKind kind, const SleepCaps& caps) {
  combo.remove_all();
  for (const auto& choice :
       power_choices(kind, caps.suspend, caps.hibernate, caps.hybrid, caps.known)) {
    combo.append(choice.id, choice.label);
  }
  if (kind == PowerChoiceKind::Inactivity && combo.get_model()->children().empty()) {
    combo.set_sensitive(false);
    combo.set_tooltip_text("Suspend and hibernate are not available on this machine.");
  }
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

PowerPage::PowerPage(XfconfStore& store, XrandrBackend& randr) : store_(store) {
  const SleepCaps caps = query_sleep_caps();
  const bool battery = machine_has_battery();
  const bool lid = machine_has_lid();
  const bool backlight =
      backlight_directory_has_device("/sys/class/backlight") || randr.outputs_have_backlight();
  pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);

  auto* general = make_vbox(8);
  general->set_border_width(12);
  auto* buttons = make_vbox(8);
  FormGrid button_form(220);
  auto* power = Gtk::manage(new Gtk::ComboBoxText());
  auto* sleep = Gtk::manage(new Gtk::ComboBoxText());
  auto* hibernate = Gtk::manage(new Gtk::ComboBoxText());
  Gtk::ComboBoxText* battery_button = nullptr;
  fill_power_choices(*power, PowerChoiceKind::Button, caps);
  fill_power_choices(*sleep, PowerChoiceKind::Button, caps);
  fill_power_choices(*hibernate, PowerChoiceKind::Button, caps);
  button_form.add("When power button is pressed", *power);
  button_form.add("When sleep button is pressed", *sleep);
  button_form.add("When hibernate button is pressed", *hibernate);
  if (battery) {
    battery_button = Gtk::manage(new Gtk::ComboBoxText());
    fill_power_choices(*battery_button, PowerChoiceKind::Button, caps);
    button_form.add("When battery button is pressed", *battery_button);
  }
  buttons->pack_start(button_form.grid(), Gtk::PACK_SHRINK);
  general->pack_start(*make_frame("Buttons", *buttons), Gtk::PACK_SHRINK);
  Gtk::CheckButton* handle_keys = nullptr;
  Gtk::CheckButton* exponential = nullptr;
  Gtk::SpinButton* steps = nullptr;
  if (backlight) {
    auto* bright = make_vbox(8);
    FormGrid bright_keys(220);
    handle_keys = Gtk::manage(new Gtk::CheckButton("Handle display brightness keys"));
    exponential = Gtk::manage(new Gtk::CheckButton("Exponential brightness steps"));
    steps = Gtk::manage(new Gtk::SpinButton());
    steps->set_range(2, 100);
    steps->set_increments(1, 5);
    steps->set_digits(0);
    bright_keys.add_control(*handle_keys);
    bright_keys.add("Brightness step count", *steps);
    bright_keys.add_control(*exponential);
    bright->pack_start(bright_keys.grid(), Gtk::PACK_SHRINK);
    general->pack_start(*make_frame("Brightness", *bright), Gtk::PACK_SHRINK);
  }

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
    fill_power_choices(*mode, PowerChoiceKind::Inactivity, caps);
    auto* profile = Gtk::manage(new Gtk::ComboBoxText());
    fill_profiles(*profile);
    form.add("_When inactive for\n(minutes, 0 = never)", *inactive);
    form.add("System sleep mode", *mode);
    Gtk::ComboBoxText* lid_action = nullptr;
    if (lid) {
      lid_action = Gtk::manage(new Gtk::ComboBoxText());
      fill_power_choices(*lid_action, PowerChoiceKind::Lid, caps);
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
      fill_power_choices(*action, PowerChoiceKind::Button, caps);
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
  auto* lock_box = make_vbox(8);
  auto* lock = Gtk::manage(
      new Gtk::CheckButton("Lock screen when the system is going to sleep"));
  auto* lock_note = make_label(
      "xfce4-power-manager asks light-locker, or whichever locker is installed, to lock on suspend and hibernate.");
  lock_note->set_line_wrap(true);
  lock_note->set_max_width_chars(70);
  lock_box->pack_start(*lock, Gtk::PACK_SHRINK);
  lock_box->pack_start(*lock_note, Gtk::PACK_SHRINK);
  system->pack_start(*make_frame("Lock", *lock_box), Gtk::PACK_SHRINK);
  auto* logind = make_vbox(6);
  auto* handle_power = Gtk::manage(new Gtk::CheckButton("Let elogind handle the _power key", true));
  auto* handle_suspend = Gtk::manage(new Gtk::CheckButton("Let elogind handle the _suspend key", true));
  auto* handle_hibernate =
      Gtk::manage(new Gtk::CheckButton("Let elogind handle the _hibernate key", true));
  auto* handle_lid = Gtk::manage(new Gtk::CheckButton("Let elogind handle the _lid switch", true));
  logind->pack_start(*handle_power, Gtk::PACK_SHRINK);
  logind->pack_start(*handle_suspend, Gtk::PACK_SHRINK);
  logind->pack_start(*handle_hibernate, Gtk::PACK_SHRINK);
  logind->pack_start(*handle_lid, Gtk::PACK_SHRINK);
  auto* logind_note = make_label(
      "Leave these off for xfce4-power-manager to handle the keys. Turn one on when elogind should inhibit that key instead.");
  logind_note->set_line_wrap(true);
  logind_note->set_max_width_chars(70);
  logind->pack_start(*logind_note, Gtk::PACK_SHRINK);
  system->pack_start(*make_frame("Login manager", *logind), Gtk::PACK_SHRINK);
  notebook_.append_page(*scrolled(*system), "System");

  auto make_display = [&](bool on_battery, Gtk::SpinButton*& blank_out, Gtk::SpinButton*& off_out) {
    auto* page = make_vbox(8);
    auto* dpms = make_vbox(8);
    FormGrid form(220);
    auto* blank = minutes_spin();
    auto* off = minutes_spin();
    form.add("_Put the display to sleep after\n(minutes, 0 = never)", *blank);
    form.add("_Switch off after\n(minutes, 0 = never)", *off);
    dpms->pack_start(form.grid(), Gtk::PACK_SHRINK);
    page->pack_start(*make_frame("Display power management", *dpms), Gtk::PACK_SHRINK);
    if (backlight) {
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
      bright_form.add("On _inactivity reduce to (%)", *level);
      bright_form.add("_Reduce after\n(0 = never, steps of 10 seconds)", *delay);
      bright->pack_start(bright_form.grid(), Gtk::PACK_SHRINK);
      page->pack_start(*make_frame("Brightness reduction", *bright), Gtk::PACK_SHRINK);
      const char* level_key = on_battery ? "brightness-level-on-battery" : "brightness-level-on-ac";
      const char* delay_key = on_battery ? "brightness-on-battery" : "brightness-on-ac";
      bind_minutes(*level, level_key, on_battery ? 20 : 80);
      bind_brightness_delay(*delay, delay_key, on_battery ? 300 : 9);
    }
    const char* blank_key = on_battery ? "dpms-on-battery-sleep" : "dpms-on-ac-sleep";
    const char* off_key = on_battery ? "dpms-on-battery-off" : "dpms-on-ac-off";
    bind_minutes(*blank, blank_key, on_battery ? 5 : 10);
    bind_minutes(*off, off_key, on_battery ? 10 : 15);
    blank_out = blank;
    off_out = off;
    return page;
  };

  auto* display = make_vbox(8);
  display->set_border_width(12);
  auto* presentation = Gtk::manage(new Gtk::CheckButton("_Presentation mode", true));
  presentation->set_tooltip_text("Keeps the display awake and stops inactivity actions while you present.");
  display->pack_start(*presentation, Gtk::PACK_SHRINK);
  auto* dpms_enabled = Gtk::manage(new Gtk::CheckButton("Enable display _power management", true));
  display->pack_start(*dpms_enabled, Gtk::PACK_SHRINK);
  auto* mode_row = make_vbox(8);
  FormGrid mode_form(220);
  auto* sleep_mode = Gtk::manage(new Gtk::ComboBoxText());
  fill_power_choices(*sleep_mode, PowerChoiceKind::Dpms, caps);
  mode_form.add("_Blanking mode", *sleep_mode);
  mode_row->pack_start(mode_form.grid(), Gtk::PACK_SHRINK);
  display->pack_start(*mode_row, Gtk::PACK_SHRINK);
  auto* display_stack = Gtk::manage(new Gtk::Stack());
  auto* display_switcher = Gtk::manage(new Gtk::StackSwitcher());
  display_switcher->set_stack(*display_stack);
  display_switcher->set_halign(Gtk::ALIGN_CENTER);
  display->pack_start(*display_switcher, Gtk::PACK_SHRINK);
  Gtk::SpinButton *ac_blank = nullptr, *ac_off = nullptr;
  Gtk::SpinButton *bat_blank = nullptr, *bat_off = nullptr;
  display_stack->add(*make_display(false, ac_blank, ac_off), "ac", "On AC");
  auto* battery_display = make_display(true, bat_blank, bat_off);
  display_stack->add(*battery_display, "battery", "On battery");
  display_stack->set_visible_child("ac");
  display->pack_start(*display_stack, Gtk::PACK_SHRINK);
  if (!battery) {
    hide_without_show_all(*display_switcher);
    hide_without_show_all(*battery_display);
  }
  notebook_.append_page(*scrolled(*display), "Display");

  auto* devices = make_vbox(8);
  devices->set_border_width(12);
  device_empty_.set_line_wrap(true);
  device_empty_.set_justify(Gtk::JUSTIFY_CENTER);
  device_empty_.set_halign(Gtk::ALIGN_CENTER);
  device_empty_.set_valign(Gtk::ALIGN_CENTER);
  device_empty_.set_margin_top(28);
  device_empty_.set_margin_bottom(28);
  device_empty_.set_margin_start(16);
  device_empty_.set_margin_end(16);
  device_empty_.set_max_width_chars(48);
  auto* empty_wrap = make_vbox(8);
  empty_wrap->set_valign(Gtk::ALIGN_CENTER);
  empty_wrap->pack_start(device_empty_, Gtk::PACK_SHRINK);
  device_face_.add(*empty_wrap, "empty");
  auto* device_body = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  device_list_.set_selection_mode(Gtk::SELECTION_BROWSE);
  device_list_.set_size_request(220, -1);
  auto* list_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*list_scroll);
  list_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  list_scroll->add(device_list_);
  device_body->pack_start(*list_scroll, Gtk::PACK_SHRINK);
  detail_grid_.set_row_spacing(6);
  detail_grid_.set_column_spacing(12);
  detail_grid_.set_margin_top(4);
  auto* detail_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*detail_scroll);
  detail_scroll->add(detail_grid_);
  device_body->pack_start(*detail_scroll, Gtk::PACK_EXPAND_WIDGET);
  device_face_.add(*device_body, "list");
  devices->pack_start(device_face_, Gtk::PACK_EXPAND_WIDGET);
  device_list_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
    if (row == nullptr) {
      return;
    }
    show_device(row->get_name());
  });
  upower_.changed.connect(sigc::mem_fun(*this, &PowerPage::refresh_devices));
  upower_.start();
  refresh_devices();
  notebook_.append_page(*scrolled(*devices), "Devices");

  bind_uint(*power, "power-button-action", 0);
  bind_uint(*sleep, "sleep-button-action", 0);
  bind_uint(*hibernate, "hibernate-button-action", 0);
  if (battery_button != nullptr) {
    bind_uint(*battery_button, "battery-button-action", 0);
  }
  if (handle_keys != nullptr && steps != nullptr && exponential != nullptr) {
    bind_bool(*handle_keys, "handle-brightness-keys", true);
    bind_minutes(*steps, "brightness-step-count", 10);
    bind_bool(*exponential, "brightness-exponential", false);
  }
  bind_bool(*notifications, "general-notification", true);
  bind_bool(*tray, "show-tray-icon", false);
  bind_bool(*presentation, "presentation-mode", false);
  bind_bool(*dpms_enabled, "dpms-enabled", true);
  {
    Freeze freeze(freeze_);
    std::string value = store_.get_string(kChannel, prop("dpms-sleep-mode"), "Standby");
    // xfpm treats "Off" as Suspend. Do not offer it.
    if (value == "Off") {
      value = "Suspend";
    }
    sleep_mode->set_active_id(value);
    if (sleep_mode->get_active_row_number() < 0) {
      sleep_mode->set_active_id("Standby");
    }
  }
  sleep_mode->signal_changed().connect([this, sleep_mode] {
    if (freeze_ > 0 || sleep_mode->get_active_id().empty()) {
      return;
    }
    const std::string mode = sleep_mode->get_active_id();
    if (mode == "Standby" || mode == "Suspend") {
      store_.set_string(kChannel, prop("dpms-sleep-mode"), mode);
    }
  });
  bind_bool(*lock, "lock-screen-suspend-hibernate", true);
  bind_bool(*handle_power, "logind-handle-power-key", false);
  bind_bool(*handle_suspend, "logind-handle-suspend-key", false);
  bind_bool(*handle_hibernate, "logind-handle-hibernate-key", false);
  bind_bool(*handle_lid, "logind-handle-lid-switch", false);

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
}

Gtk::Widget* PowerPage::scrolled(Gtk::Widget& child) { return make_page_scroll(child); }

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
      return "devices";
    default:
      return {};
  }
}

void PowerPage::refresh_devices() {
  const auto rows = device_list_.get_children();
  for (auto* child : rows) {
    device_list_.remove(*child);
  }
  if (!upower_.available()) {
    const std::string detail = upower_.message().empty()
                                   ? "UPower is not available."
                                   : upower_.message();
    device_empty_.set_text("No power devices to show.\n\n" + detail +
                           "\n\nBatteries and power supplies appear here, with their charge and capacity, while UPower is running.");
    device_face_.set_visible_child("empty");
    return;
  }
  if (upower_.devices().empty()) {
    device_empty_.set_text(
        "No power devices are connected.\n\nWhen UPower reports a battery or a power supply, it is listed here with its charge, state, and capacity.");
    device_face_.set_visible_child("empty");
    return;
  }
  device_face_.set_visible_child("list");
  bool kept = false;
  for (const auto& device : upower_.devices()) {
    auto* row = Gtk::manage(new Gtk::ListBoxRow());
    row->set_name(device.path);
    auto* line = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
    line->set_margin_top(6);
    line->set_margin_bottom(6);
    line->set_margin_start(6);
    line->set_margin_end(6);
    auto* icon = Gtk::manage(new Gtk::Image());
    const std::string icon_name = device.icon.empty() ? "battery-missing-symbolic" : device.icon;
    icon->set_from_icon_name(icon_name, Gtk::ICON_SIZE_DND);
    icon->set_pixel_size(36);
    auto* text = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
    const std::string title = !device.model.empty() ? device.model : upower_kind_name(device.kind);
    auto* name = make_label(title);
    name->set_ellipsize(Pango::ELLIPSIZE_END);
    std::string subtitle = upower_state_name(device.state);
    if (device.kind == 1 && device.online) {
      subtitle = "Plugged in";
    }
    if (device.percentage >= 0 && device.kind != 1) {
      subtitle = std::to_string(static_cast<int>(device.percentage + 0.5)) + "% · " + subtitle;
    }
    auto* sub = make_label(subtitle);
    sub->get_style_context()->add_class("lcos-subtitle");
    sub->set_ellipsize(Pango::ELLIPSIZE_END);
    text->pack_start(*name, Gtk::PACK_SHRINK);
    text->pack_start(*sub, Gtk::PACK_SHRINK);
    line->pack_start(*icon, Gtk::PACK_SHRINK);
    line->pack_start(*text, Gtk::PACK_EXPAND_WIDGET);
    row->add(*line);
    device_list_.append(*row);
    if (device.path == selected_device_) {
      device_list_.select_row(*row);
      kept = true;
    }
  }
  device_list_.show_all();
  if (!kept) {
    if (auto* first = device_list_.get_row_at_index(0)) {
      device_list_.select_row(*first);
    }
  }
}

void PowerPage::show_device(const std::string& path) {
  selected_device_ = path;
  const UpDeviceInfo* device = nullptr;
  for (const auto& item : upower_.devices()) {
    if (item.path == path) {
      device = &item;
      break;
    }
  }
  const auto children = detail_grid_.get_children();
  for (auto* child : children) {
    detail_grid_.remove(*child);
  }
  if (device == nullptr) {
    return;
  }
  int row = 0;
  auto add = [&](const Glib::ustring& caption, const std::string& value) {
    if (value.empty()) {
      return;
    }
    auto* label = make_label(caption);
    label->set_size_request(168, -1);
    auto* text = make_label(value);
    text->set_line_wrap(true);
    text->set_hexpand(true);
    detail_grid_.attach(*label, 0, row, 1, 1);
    detail_grid_.attach(*text, 1, row, 1, 1);
    ++row;
  };
  const std::string short_path =
      path.rfind("/org/freedesktop/UPower/devices/", 0) == 0
          ? path.substr(std::strlen("/org/freedesktop/UPower/devices/"))
          : path;
  add("Device", short_path);
  add("Type", upower_kind_name(device->kind));
  add("Model", device->model);
  add("Vendor", device->vendor);
  add("Serial", device->serial);
  add("Technology", device->kind == 1 ? std::string() : upower_technology_name(device->technology));
  if (device->kind == 1) {
    add("State", device->online ? "Plugged in" : "Unplugged");
  } else {
    add("State", upower_state_name(device->state));
    if (device->percentage >= 0) {
      add("Current charge", std::to_string(static_cast<int>(device->percentage + 0.5)) + "%");
    }
  }
  add("Energy", format_upower_measure(device->energy, "Wh"));
  if (device->energy_full > 0 && device->energy_full_design > 0) {
    const int ratio =
        static_cast<int>(device->energy_full / device->energy_full_design * 100.0 + 0.5);
    add("Fully charged", format_upower_measure(device->energy_full, "Wh") + " (" +
                             std::to_string(ratio) + "%)");
  } else {
    add("Fully charged", format_upower_measure(device->energy_full, "Wh"));
  }
  add("Fully charged (design)", format_upower_measure(device->energy_full_design, "Wh"));
  add("Energy empty", format_upower_measure(device->energy_empty, "Wh"));
  add("Energy rate", format_upower_measure(device->energy_rate, "W"));
  add("Voltage", format_upower_measure(device->voltage, "V"));
  if (device->capacity > 0) {
    add("Capacity", std::to_string(static_cast<int>(device->capacity + 0.5)) + "%");
  }
  add("Time to empty", format_upower_duration(device->time_to_empty));
  add("Time to full", format_upower_duration(device->time_to_full));
  detail_grid_.show_all();
}

}  // namespace lcos
