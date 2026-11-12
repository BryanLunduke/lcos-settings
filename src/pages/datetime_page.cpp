// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/datetime_page.hpp"

#include "logic/system_model.hpp"
#include "ui_util.hpp"

#include <glibmm/main.h>

#include <dirent.h>
#include <unistd.h>

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/scrolledwindow.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace lcos {
namespace {

std::string read_text(const std::string& path) {
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  return text;
}

void collect_zones(const std::string& root, const std::string& prefix, std::vector<std::string>& out) {
  DIR* directory = opendir(root.c_str());
  if (directory == nullptr) {
    return;
  }
  while (const dirent* item = readdir(directory)) {
    const std::string name = item->d_name;
    if (name.empty() || name[0] == '.' || name == "posix" || name == "right" || name == "leapseconds" ||
        name == "tzdata.zi" || name == "zone.tab" || name == "zone1970.tab" || name == "zonenow.tab" ||
        name == "iso3166.tab") {
      continue;
    }
    const std::string path = root + "/" + name;
    const std::string zone = prefix.empty() ? name : prefix + "/" + name;
    if (item->d_type == DT_DIR) {
      collect_zones(path, zone, out);
    } else if (valid_timezone_name(zone)) {
      out.push_back(zone);
    }
  }
  closedir(directory);
}

}  // namespace

DateTimePage::DateTimePage(XfconfStore& store, PrivSession& session, Gtk::Window& parent)
    : store_(store),
      session_(session),
      parent_(parent),
      hour24_("24-hour clock"),
      hour12_("12-hour clock") {
  hour12_.join_group(hour24_);
  hour_.set_range(0, 23);
  minute_.set_range(0, 59);
  second_.set_range(0, 59);
  for (Gtk::SpinButton* spin : {&hour_, &minute_, &second_}) {
    spin->set_increments(1, 1);
    spin->set_digits(0);
    spin->set_numeric(true);
    spin->set_wrap(true);
  }
  ntp_.set_label("Set the time automatically");
  zone_store_ = Gtk::ListStore::create(zone_columns_);
  zones_view_.set_model(zone_store_);
  zones_view_.append_column("Time zone", zone_columns_.name);
  zones_view_.set_headers_visible(false);
  search_.set_placeholder_text("Search regions and cities");

  auto* scroll_page = make_vbox(8);
  scroll_page->set_border_width(0);
  now_ = make_label("");
  now_->set_line_wrap(true);
  scroll_page->pack_start(*make_frame("Current time", *now_), Gtk::PACK_SHRINK);

  auto* zone_box = make_vbox(6);
  zone_box->pack_start(search_, Gtk::PACK_SHRINK);
  auto* zone_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*zone_scroll);
  zone_scroll->set_shadow_type(Gtk::SHADOW_IN);
  zone_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  zone_scroll->set_size_request(-1, 180);
  zone_scroll->add(zones_view_);
  zone_box->pack_start(*zone_scroll, Gtk::PACK_SHRINK);
  auto* set_zone = Gtk::manage(new Gtk::Button("_Set time zone", true));
  set_zone->set_halign(Gtk::ALIGN_START);
  zone_box->pack_start(*set_zone, Gtk::PACK_SHRINK);
  scroll_page->pack_start(*make_frame("Time zone", *zone_box), Gtk::PACK_SHRINK);

  auto* set_box = make_vbox(6);
  set_box->pack_start(ntp_, Gtk::PACK_SHRINK);
  set_box->pack_start(calendar_, Gtk::PACK_SHRINK);
  FormGrid clock_form(80);
  clock_form.add("Hour", hour_);
  clock_form.add("Minute", minute_);
  clock_form.add("Second", second_);
  set_box->pack_start(clock_form.grid(), Gtk::PACK_SHRINK);
  auto* set_button = Gtk::manage(new Gtk::Button("Set date and time"));
  set_button->set_halign(Gtk::ALIGN_START);
  set_box->pack_start(*set_button, Gtk::PACK_SHRINK);
  auto* ntp_note = make_label(
      "Automatic time uses chrony, which LCOS starts from sysvinit. The clock can be set by hand only while that is off.");
  ntp_note->set_line_wrap(true);
  set_box->pack_start(*ntp_note, Gtk::PACK_SHRINK);
  scroll_page->pack_start(*make_frame("Date and time", *set_box), Gtk::PACK_SHRINK);

  auto* format_box = make_vbox(6);
  format_box->pack_start(hour24_, Gtk::PACK_SHRINK);
  format_box->pack_start(hour12_, Gtk::PACK_SHRINK);
  clock_note_ = make_label("");
  clock_note_->set_line_wrap(true);
  format_box->pack_start(*clock_note_, Gtk::PACK_SHRINK);
  scroll_page->pack_start(*make_frame("Panel clock", *format_box), Gtk::PACK_SHRINK);
  pack_start(*make_page_scroll(*scroll_page), Gtk::PACK_EXPAND_WIDGET);

  search_.signal_changed().connect([this] { show_zones(); });
  auto apply_selected = [this] {
    auto iter = zones_view_.get_selection()->get_selected();
    if (!iter) {
      return;
    }
    apply_zone(static_cast<Glib::ustring>((*iter)[zone_columns_.name]));
  };
  zones_view_.signal_row_activated().connect(
      [apply_selected](const Gtk::TreeModel::Path&, Gtk::TreeViewColumn*) { apply_selected(); });
  set_zone->signal_clicked().connect(apply_selected);
  ntp_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    const PrivResult result = session_.run({"set-ntp", ntp_.get_active() ? "on" : "off"});
    if (!result.ok) {
      tell(result.error);
      load_clock();
    }
  });
  set_button->signal_clicked().connect([this] {
    if (ntp_.get_active()) {
      tell("Turn off automatic time before setting the clock.");
      return;
    }
    guint year = 0;
    guint month = 0;
    guint day = 0;
    calendar_.get_date(year, month, day);
    char date[16];
    std::snprintf(date, sizeof(date), "%04u-%02u-%02u", year, month + 1, day);
    char clock[16];
    std::snprintf(clock, sizeof(clock), "%02d:%02d:%02d", hour_.get_value_as_int(),
                  minute_.get_value_as_int(), second_.get_value_as_int());
    const PrivResult result = session_.run({"set-time", date, clock});
    if (!result.ok) {
      tell(result.error);
    }
    load_clock();
  });
  auto write_clock = [this](HourClock clock) {
    if (freeze_ > 0 || clock_property_.empty()) {
      return;
    }
    store_.set_string("xfce4-panel", clock_property_, clock_format_for(clock, clock_seconds_));
  };
  hour24_.signal_toggled().connect([this, write_clock] {
    if (hour24_.get_active()) {
      write_clock(HourClock::Hour24);
    }
  });
  hour12_.signal_toggled().connect([this, write_clock] {
    if (hour12_.get_active()) {
      write_clock(HourClock::Hour12);
    }
  });

  load_zones();
  load_clock();
  // Disconnected in the destructor. Leaving it connected updates the clock
  // label after the window has been deleted on OK, Cancel, or close.
  clock_timer_ = Glib::signal_timeout().connect(
      [this]() {
        const std::time_t now = std::time(nullptr);
        const std::tm local = *std::localtime(&now);
        char pretty[128];
        std::strftime(pretty, sizeof(pretty), "%A %e %B %Y, %H:%M:%S", &local);
        if (now_ != nullptr) {
          now_->set_text(pretty);
        }
        return true;
      },
      1000);
}

DateTimePage::~DateTimePage() {
  now_ = nullptr;
  if (clock_timer_.connected()) {
    clock_timer_.disconnect();
  }
}

void DateTimePage::tell(const std::string& message) {
  Gtk::MessageDialog dialog(parent_, message, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
  dialog.run();
}

bool DateTimePage::chrony_enabled() const { return chrony_is_enabled(); }

std::string DateTimePage::current_zone() const {
  char target[4096];
  const ssize_t n = ::readlink("/etc/localtime", target, sizeof(target) - 1);
  if (n > 0) {
    target[n] = '\0';
    const std::string zone = timezone_from_localtime(target);
    if (!zone.empty()) {
      return zone;
    }
  }
  return read_text("/etc/timezone");
}

void DateTimePage::load_zones() {
  zones_.clear();
  collect_zones("/usr/share/zoneinfo", "", zones_);
  std::sort(zones_.begin(), zones_.end());
  show_zones();
}

void DateTimePage::show_zones() {
  Freeze freeze(freeze_);
  const std::string current = current_zone();
  const std::string query = search_.get_text();
  std::string needle = query;
  for (char& c : needle) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  zone_store_->clear();
  Gtk::TreeModel::iterator selected;
  for (const auto& zone : zones_) {
    std::string folded = zone;
    for (char& c : folded) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (!needle.empty() && folded.find(needle) == std::string::npos) {
      continue;
    }
    auto row = zone_store_->append();
    (*row)[zone_columns_.name] = zone;
    if (zone == current) {
      selected = row;
    }
  }
  if (selected) {
    zones_view_.get_selection()->select(selected);
    zones_view_.scroll_to_row(zone_store_->get_path(selected));
  }
}

void DateTimePage::load_clock() {
  Freeze freeze(freeze_);
  const std::time_t now = std::time(nullptr);
  const std::tm local = *std::localtime(&now);
  char pretty[128];
  std::strftime(pretty, sizeof(pretty), "%A %e %B %Y, %H:%M:%S", &local);
  if (now_ != nullptr) {
    now_->set_text(pretty);
  }
  const bool automatic = chrony_is_enabled();
  ntp_.set_active(automatic);
  calendar_.set_sensitive(!automatic);
  hour_.set_sensitive(!automatic);
  minute_.set_sensitive(!automatic);
  second_.set_sensitive(!automatic);
  calendar_.select_month(static_cast<guint>(local.tm_mon), static_cast<guint>(local.tm_year + 1900));
  calendar_.select_day(static_cast<guint>(local.tm_mday));
  hour_.set_value(local.tm_hour);
  minute_.set_value(local.tm_min);
  second_.set_value(local.tm_sec);

  std::vector<std::string> clocks;
  for (const auto& entry : store_.list("xfce4-panel", "/plugins")) {
    if (entry.second.type == PropType::String && entry.second.s == "clock" &&
        entry.first.find('/', std::string("/plugins/plugin-").size()) == std::string::npos &&
        entry.first.compare(0, 16, "/plugins/plugin-") == 0) {
      clocks.push_back(entry.first);
    }
  }
  clock_property_.clear();
  hour24_.set_no_show_all(true);
  hour12_.set_no_show_all(true);
  hour24_.hide();
  hour12_.hide();
  if (clocks.size() == 1) {
    const std::string format =
        store_.get_string("xfce4-panel", clocks[0] + "/digital-time-format", "%R");
    const std::string shown = format.empty() ? "%R" : format;
    clock_seconds_ = clock_format_has_seconds(shown);
    const HourClock kind = classify_clock_format(shown);
    if (kind == HourClock::Unknown) {
      clock_note_->set_text(
          "The panel clock uses a custom format, so the 12/24-hour choice is left out.");
      clock_note_->show();
    } else {
      clock_property_ = clocks[0] + "/digital-time-format";
      if (kind == HourClock::Hour12) {
        hour12_.set_active(true);
      } else {
        hour24_.set_active(true);
      }
      hour24_.set_no_show_all(false);
      hour12_.set_no_show_all(false);
      hour24_.show();
      hour12_.show();
      clock_note_->set_no_show_all(true);
      clock_note_->hide();
    }
  } else if (clocks.empty()) {
    clock_note_->set_text("The panel clock is left out because no XFCE panel clock plugin was found.");
    clock_note_->show();
  } else {
    clock_note_->set_text("More than one panel clock was found, so the 12/24-hour choice is left out.");
    clock_note_->show();
  }
}

void DateTimePage::apply_zone(const std::string& zone) {
  if (zone == current_zone()) {
    return;
  }
  const PrivResult result = session_.run({"set-timezone", zone});
  if (!result.ok) {
    tell(result.error);
    show_zones();
  }
}

void DateTimePage::on_page_shown() {
  show_zones();
  load_clock();
}

}  // namespace lcos
