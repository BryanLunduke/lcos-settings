// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/notifications_page.hpp"

#include "ui_util.hpp"

#include <gio/gio.h>
#include <glibmm/miscutils.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/cellrenderertoggle.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/scrolledwindow.h>

#include <algorithm>
#include <set>

namespace lcos {
namespace {

constexpr const char* kChannel = "xfce4-notifyd";

bool theme_dir_has_css(const std::string& directory) {
  return g_file_test((directory + "/xfce-notify-4.0/gtk.css").c_str(), G_FILE_TEST_IS_REGULAR) != 0;
}

void collect_themes(const std::string& root, std::set<std::string>& names) {
  if (!g_file_test(root.c_str(), G_FILE_TEST_IS_DIR)) {
    return;
  }
  GDir* dir = g_dir_open(root.c_str(), 0, nullptr);
  if (dir == nullptr) {
    return;
  }
  while (const char* name = g_dir_read_name(dir)) {
    if (theme_dir_has_css(root + "/" + name)) {
      names.insert(name);
    }
  }
  g_dir_close(dir);
}

}  // namespace

NotificationsPage::NotificationsPage(XfconfStore& store, Gtk::Window& parent)
    : store_(store), parent_(parent), opacity_(Gtk::ORIENTATION_HORIZONTAL) {
  display_fields_.append("full", "Icon, summary, and body");
  display_fields_.append("icon-summary", "Icon and summary");
  display_fields_.append("icon-appname", "Icon and application name");
  show_on_.append("active-monitor", "Monitor with the mouse pointer");
  show_on_.append("primary-monitor", "Primary monitor");
  show_on_.append("all-monitors", "All monitors");
  location_.append("top-left", "Top left");
  location_.append("bottom-left", "Bottom left");
  location_.append("top-right", "Top right");
  location_.append("bottom-right", "Bottom right");
  location_.append("top-center", "Top center");
  location_.append("bottom-center", "Bottom center");
  datetime_.append("locale-default", "Locale default");
  datetime_.append("relative-times", "Relative times");
  datetime_.append("iso8601", "ISO8601");
  datetime_.append("custom", "Custom");
  log_level_.append("not-fully-shown", "Only when not fully shown");
  log_level_.append("always", "Always");
  log_apps_.append("all", "All");
  log_apps_.append("except-blocked", "All except blocked");
  log_apps_.append("only-blocked", "Only blocked");

  do_not_disturb_.set_label("Do not disturb");
  gauge_ignores_.set_label("Always show percent-value notifications");
  mute_sounds_.set_label("Mute sounds");
  min_width_enabled_.set_label("Minimum notification width");
  expire_enabled_.set_label("Disappear after a timeout");
  expire_override_.set_label("Honor the time requested by the application");
  fadeout_.set_label("Fade out when a notification closes");
  slideout_.set_label("Slide out when a notification closes");
  text_with_gauge_.set_label("Show text with percent-value notifications");
  log_enabled_.set_label("Keep a notification log");
  log_limit_.set_label("Limit the log size");

  opacity_.set_range(0.1, 1.0);
  opacity_.set_increments(0.05, 0.1);
  opacity_.set_digits(2);
  opacity_.set_value_pos(Gtk::POS_RIGHT);
  min_width_.set_range(1, 1000);
  min_width_.set_increments(1, 10);
  min_width_.set_digits(0);
  expire_timeout_.set_range(1, 480);
  expire_timeout_.set_increments(1, 5);
  expire_timeout_.set_digits(0);
  log_size_.set_range(1, 100000);
  log_size_.set_increments(10, 100);
  log_size_.set_digits(0);

  pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);

  auto* general = make_vbox(8);
  general->set_border_width(12);
  FormGrid general_form(240);
  general_form.add("When displaying notifications, show", display_fields_);
  general_form.add_control(do_not_disturb_);
  general_form.add_control(gauge_ignores_);
  general_form.add("Show notifications on", show_on_);
  general_form.add_control(mute_sounds_);
  general->pack_start(general_form.grid(), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*general), "General");

  auto* appearance = make_vbox(8);
  appearance->set_border_width(12);
  FormGrid look(220);
  look.add("Theme", theme_);
  look.add("Position", location_);
  look.add("Opacity", opacity_);
  look.add_control(min_width_enabled_);
  look.add("Width", min_width_);
  look.add_control(expire_enabled_);
  look.add("Seconds", expire_timeout_);
  look.add_control(expire_override_);
  look.add_control(fadeout_);
  look.add_control(slideout_);
  look.add_control(text_with_gauge_);
  look.add("Date format", datetime_);
  look.add("Custom format", datetime_custom_);
  appearance->pack_start(look.grid(), Gtk::PACK_SHRINK);
  auto* preview = Gtk::manage(new Gtk::Button("Show preview"));
  preview->set_halign(Gtk::ALIGN_START);
  appearance->pack_start(*preview, Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*appearance), "Appearance");

  auto* applications = make_vbox(8);
  applications->set_border_width(12);
  auto* app_caption = make_label(
      "Applications appear here after they send a notification. Muted applications stay silent. "
      "Critical allows urgent notifications. Log keeps them in the notification log.");
  app_caption->set_line_wrap(true);
  applications->pack_start(*app_caption, Gtk::PACK_SHRINK);
  apps_ = Gtk::ListStore::create(app_columns_);
  apps_view_.set_model(apps_);
  apps_view_.append_column("Application", app_columns_.name);
  auto add_toggle = [this](const char* title, Gtk::TreeModelColumn<bool>& column, int kind) {
    auto* renderer = Gtk::manage(new Gtk::CellRendererToggle());
    renderer->set_activatable(true);
    const int index = apps_view_.append_column(title, *renderer);
    auto* view_column = apps_view_.get_column(index - 1);
    view_column->add_attribute(renderer->property_active(), column);
    renderer->signal_toggled().connect([this, kind](const Glib::ustring& path) {
      auto iter = apps_->get_iter(path);
      if (!iter) {
        return;
      }
      const std::string name = static_cast<Glib::ustring>((*iter)[app_columns_.name]);
      if (kind == 0) {
        const bool next = !(*iter)[app_columns_.muted];
        set_membership("/applications/muted_applications", name, next);
      } else if (kind == 1) {
        const bool allow = !(*iter)[app_columns_.critical];
        set_membership("/applications/denied-critical-notifications", name, !allow);
      } else {
        const bool keep = !(*iter)[app_columns_.logged];
        set_membership("/applications/excluded-from-log", name, !keep);
      }
      load_apps();
    });
  };
  add_toggle("Muted", app_columns_.muted, 0);
  add_toggle("Critical", app_columns_.critical, 1);
  add_toggle("Log", app_columns_.logged, 2);
  apps_view_.get_column(0)->set_expand(true);
  auto* app_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*app_scroll);
  app_scroll->set_shadow_type(Gtk::SHADOW_IN);
  app_scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  app_scroll->set_size_request(-1, 220);
  app_scroll->add(apps_view_);
  applications->pack_start(*app_scroll, Gtk::PACK_SHRINK);
  auto* remove = Gtk::manage(new Gtk::Button("Remove application"));
  remove->set_halign(Gtk::ALIGN_START);
  applications->pack_start(*remove, Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*applications), "Applications");

  auto* log = make_vbox(8);
  log->set_border_width(12);
  FormGrid log_form(180);
  log_form.add_control(log_enabled_);
  log_form.add("Log notifications", log_level_);
  log_form.add("Log applications", log_apps_);
  log_form.add_control(log_limit_);
  log_form.add("Maximum entries", log_size_);
  log->pack_start(log_form.grid(), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*log), "Log");

  auto bind_string = [this](Gtk::ComboBoxText& combo, const char* prop) {
    combo.signal_changed().connect([this, &combo, prop] {
      if (freeze_ > 0 || combo.get_active_id().empty()) {
        return;
      }
      store_.set_string(kChannel, prop, combo.get_active_id());
    });
  };
  auto bind_bool = [this](Gtk::CheckButton& check, const char* prop) {
    check.signal_toggled().connect([this, &check, prop] {
      if (freeze_ > 0) {
        return;
      }
      store_.set_bool(kChannel, prop, check.get_active());
    });
  };
  bind_string(display_fields_, "/notification-display-fields");
  bind_bool(do_not_disturb_, "/do-not-disturb");
  bind_bool(gauge_ignores_, "/gauge-ignores-dnd");
  bind_string(show_on_, "/show-notifications-on");
  bind_bool(mute_sounds_, "/mute-sounds");
  theme_.signal_changed().connect([this] {
    if (freeze_ > 0 || theme_.get_active_id().empty()) {
      return;
    }
    store_.set_string(kChannel, "/theme", theme_.get_active_id());
  });
  bind_string(location_, "/notify-location");
  opacity_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_double(kChannel, "/initial-opacity", opacity_.get_value());
  });
  min_width_enabled_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/min-width-enabled", min_width_enabled_.get_active());
    min_width_.set_sensitive(min_width_enabled_.get_active());
  });
  min_width_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_uint(kChannel, "/min-width", static_cast<unsigned>(min_width_.get_value_as_int()));
  });
  expire_enabled_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/expire-timeout-enabled", expire_enabled_.get_active());
    expire_timeout_.set_sensitive(expire_enabled_.get_active());
  });
  expire_timeout_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_int(kChannel, "/expire-timeout", expire_timeout_.get_value_as_int());
  });
  bind_bool(expire_override_, "/expire-timeout-allow-override");
  bind_bool(fadeout_, "/do-fadeout");
  bind_bool(slideout_, "/do-slideout");
  bind_bool(text_with_gauge_, "/show-text-with-gauge");
  datetime_.signal_changed().connect([this] {
    if (freeze_ > 0 || datetime_.get_active_id().empty()) {
      return;
    }
    store_.set_string(kChannel, "/date-time-format", datetime_.get_active_id());
    datetime_custom_.set_sensitive(datetime_.get_active_id() == "custom");
  });
  datetime_custom_.signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_string(kChannel, "/date-time-custom-format", datetime_custom_.get_text());
  });
  bind_bool(log_enabled_, "/notification-log");
  bind_string(log_level_, "/log-level");
  bind_string(log_apps_, "/log-level-apps");
  log_limit_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/log-max-size-enabled", log_limit_.get_active());
    log_size_.set_sensitive(log_limit_.get_active());
  });
  log_size_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_uint(kChannel, "/log-max-size", static_cast<unsigned>(log_size_.get_value_as_int()));
  });
  preview->signal_clicked().connect(sigc::mem_fun(*this, &NotificationsPage::preview));
  remove->signal_clicked().connect(sigc::mem_fun(*this, &NotificationsPage::remove_selected));

  load_themes();
  load();
  load_apps();
}

void NotificationsPage::load_themes() {
  Freeze freeze(freeze_);
  const std::string current = store_.get_string(kChannel, "/theme", "Default");
  std::set<std::string> names;
  names.insert("Default");
  if (!current.empty()) {
    names.insert(current);
  }
  collect_themes(Glib::get_home_dir() + "/.themes", names);
  const char* data_home = g_get_user_data_dir();
  if (data_home != nullptr) {
    collect_themes(std::string(data_home) + "/xfce4/themes", names);
  }
  const char* data_dirs = g_getenv("XDG_DATA_DIRS");
  const std::string dirs = data_dirs != nullptr ? data_dirs : "/usr/local/share:/usr/share";
  std::string::size_type start = 0;
  while (start <= dirs.size()) {
    const auto end = dirs.find(':', start);
    const std::string piece = dirs.substr(start, end == std::string::npos ? end : end - start);
    if (!piece.empty()) {
      collect_themes(piece + "/xfce4/themes", names);
      collect_themes(piece + "/themes", names);
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  const std::string selected = theme_.get_active_id();
  theme_.remove_all();
  for (const auto& name : names) {
    theme_.append(name, name);
  }
  theme_.set_active_id(selected.empty() ? current : selected);
}

void NotificationsPage::load() {
  Freeze freeze(freeze_);
  const auto choose = [](Gtk::ComboBoxText& combo, const std::string& id, const char* fallback) {
    combo.set_active_id(id);
    if (combo.get_active_row_number() < 0) {
      combo.set_active_id(fallback);
    }
  };
  choose(display_fields_, store_.get_string(kChannel, "/notification-display-fields", "full"),
         "full");
  do_not_disturb_.set_active(store_.get_bool(kChannel, "/do-not-disturb", false));
  gauge_ignores_.set_active(store_.get_bool(kChannel, "/gauge-ignores-dnd", true));
  choose(show_on_, store_.get_string(kChannel, "/show-notifications-on", "active-monitor"),
         "active-monitor");
  mute_sounds_.set_active(store_.get_bool(kChannel, "/mute-sounds", false));
  choose(theme_, store_.get_string(kChannel, "/theme", "Default"), "Default");
  choose(location_, store_.get_string(kChannel, "/notify-location", "top-left"), "top-left");
  double opacity = store_.get_double(kChannel, "/initial-opacity", 0.9);
  if (opacity < 0.1) {
    opacity = 0.1;
  }
  if (opacity > 1.0) {
    opacity = 1.0;
  }
  opacity_.set_value(opacity);
  min_width_enabled_.set_active(store_.get_bool(kChannel, "/min-width-enabled", false));
  min_width_.set_value(store_.get_uint(kChannel, "/min-width", 150));
  min_width_.set_sensitive(min_width_enabled_.get_active());
  expire_enabled_.set_active(store_.get_bool(kChannel, "/expire-timeout-enabled", true));
  expire_timeout_.set_value(store_.get_int(kChannel, "/expire-timeout", 10));
  expire_timeout_.set_sensitive(expire_enabled_.get_active());
  expire_override_.set_active(store_.get_bool(kChannel, "/expire-timeout-allow-override", true));
  fadeout_.set_active(store_.get_bool(kChannel, "/do-fadeout", true));
  slideout_.set_active(store_.get_bool(kChannel, "/do-slideout", false));
  text_with_gauge_.set_active(store_.get_bool(kChannel, "/show-text-with-gauge", false));
  choose(datetime_, store_.get_string(kChannel, "/date-time-format", "locale-default"),
         "locale-default");
  datetime_custom_.set_text(store_.get_string(kChannel, "/date-time-custom-format", "%a %H:%M:%S"));
  datetime_custom_.set_sensitive(datetime_.get_active_id() == "custom");
  log_enabled_.set_active(store_.get_bool(kChannel, "/notification-log", false));
  choose(log_level_, store_.get_string(kChannel, "/log-level", "not-fully-shown"),
         "not-fully-shown");
  choose(log_apps_, store_.get_string(kChannel, "/log-level-apps", "all"), "all");
  log_limit_.set_active(store_.get_bool(kChannel, "/log-max-size-enabled", false));
  log_size_.set_value(store_.get_uint(kChannel, "/log-max-size", 1000));
  log_size_.set_sensitive(log_limit_.get_active());
}

void NotificationsPage::load_apps() {
  const auto known = store_.get_strings(kChannel, "/applications/known_applications");
  const auto muted = store_.get_strings(kChannel, "/applications/muted_applications");
  const auto denied = store_.get_strings(kChannel, "/applications/denied-critical-notifications");
  const auto excluded = store_.get_strings(kChannel, "/applications/excluded-from-log");
  std::set<std::string> names(known.begin(), known.end());
  names.insert(muted.begin(), muted.end());
  names.insert(denied.begin(), denied.end());
  names.insert(excluded.begin(), excluded.end());
  apps_->clear();
  for (const auto& name : names) {
    if (name.empty()) {
      continue;
    }
    auto row = apps_->append();
    (*row)[app_columns_.name] = name;
    (*row)[app_columns_.muted] = std::find(muted.begin(), muted.end(), name) != muted.end();
    (*row)[app_columns_.critical] = std::find(denied.begin(), denied.end(), name) == denied.end();
    (*row)[app_columns_.logged] =
        std::find(excluded.begin(), excluded.end(), name) == excluded.end();
  }
}

void NotificationsPage::set_membership(const char* prop, const std::string& app, bool member) {
  auto list = store_.get_strings(kChannel, prop);
  const auto found = std::find(list.begin(), list.end(), app);
  if (member && found == list.end()) {
    list.push_back(app);
  } else if (!member && found != list.end()) {
    list.erase(found);
  } else {
    return;
  }
  store_.set_strings(kChannel, prop, list);
}

void NotificationsPage::remove_selected() {
  auto iter = apps_view_.get_selection()->get_selected();
  if (!iter) {
    return;
  }
  const std::string name = static_cast<Glib::ustring>((*iter)[app_columns_.name]);
  for (const char* prop : {"/applications/known_applications", "/applications/muted_applications",
                           "/applications/denied-critical-notifications",
                           "/applications/excluded-from-log"}) {
    set_membership(prop, name, false);
  }
  load_apps();
}

void NotificationsPage::preview() {
  GError* error = nullptr;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (bus == nullptr) {
    Gtk::MessageDialog dialog(parent_, "Notification preview failed", false, Gtk::MESSAGE_ERROR,
                              Gtk::BUTTONS_OK, true);
    dialog.set_secondary_text(error != nullptr ? error->message : "No session bus.");
    if (error != nullptr) {
      g_error_free(error);
    }
    dialog.run();
    return;
  }
  GVariantBuilder actions;
  g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
  GVariantBuilder hints;
  g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&hints, "{sv}", "urgency", g_variant_new_byte(1));
  GVariant* params = g_variant_new("(susssasa{sv}i)", "LCOS Command Center", 0u,
                                   "preferences-system-notifications", "Notification preview",
                                   "This is a preview from LCOS Command Center.", &actions, &hints,
                                   -1);
  GError* call_error = nullptr;
  GVariant* result = g_dbus_connection_call_sync(
      bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
      "org.freedesktop.Notifications", "Notify", params, G_VARIANT_TYPE("(u)"),
      G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &call_error);
  if (result != nullptr) {
    g_variant_unref(result);
  }
  g_object_unref(bus);
  if (call_error != nullptr) {
    Gtk::MessageDialog dialog(parent_, "Notification preview failed", false, Gtk::MESSAGE_ERROR,
                              Gtk::BUTTONS_OK, true);
    dialog.set_secondary_text(call_error->message);
    g_error_free(call_error);
    dialog.run();
    return;
  }
  load_apps();
}

void NotificationsPage::on_page_shown() {
  load_themes();
  load();
  load_apps();
}

void NotificationsPage::prepare_screenshot(int index) {
  if (index >= 0 && index < notebook_.get_n_pages()) {
    notebook_.set_current_page(index);
  }
}

std::string NotificationsPage::screenshot_suffix(int index) const {
  switch (index) {
    case 0:
      return "general";
    case 1:
      return "appearance";
    case 2:
      return "applications";
    case 3:
      return "log";
    default:
      return {};
  }
}

}  // namespace lcos
