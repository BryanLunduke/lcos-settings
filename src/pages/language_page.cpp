// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/language_page.hpp"

#include "logic/system_model.hpp"
#include "ui_util.hpp"

#include <glib.h>

#include <gdkmm/cursor.h>
#include <gdkmm/window.h>
#include <gtkmm/box.h>
#include <gtkmm/messagedialog.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

namespace lcos {
namespace {

std::string read_text(const std::string& path) {
  std::ifstream in(path);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::map<std::string, std::string> locale_file(const std::string& text) {
  std::map<std::string, std::string> values;
  std::string line;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i != text.size() && text[i] != '\n') {
      line.push_back(text[i]);
      continue;
    }
    const auto eq = line.find('=');
    if (!line.empty() && line[0] != '#' && eq != std::string::npos) {
      std::string value = line.substr(eq + 1);
      if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
      }
      values[line.substr(0, eq)] = value;
    }
    line.clear();
  }
  return values;
}

std::string command_output(const char* program) {
  std::string output;
  gchar* text = nullptr;
  gint status = 0;
  if (g_spawn_command_line_sync(program, &text, nullptr, &status, nullptr) && text != nullptr) {
    output = text;
  }
  g_free(text);
  return output;
}

}  // namespace

LanguagePage::LanguagePage(PrivSession& session, Gtk::Window& parent)
    : session_(session), parent_(parent) {
  auto* page = make_vbox(8);
  page->set_border_width(12);
  FormGrid form(160);
  form.add("System language", language_);
  form.add("Dates and times", dates_);
  form.add("Numbers", numbers_);
  form.add("Currency", currency_);
  page->pack_start(*make_frame("Locale", form.grid()), Gtk::PACK_SHRINK);
  auto* busy_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  spinner_ = Gtk::manage(new Gtk::Spinner());
  spinner_->set_size_request(16, 16);
  status_ = make_label("");
  status_->set_line_wrap(true);
  cancel_locale_ = Gtk::manage(new Gtk::Button("Stop"));
  busy_row->pack_start(*spinner_, Gtk::PACK_SHRINK);
  busy_row->pack_start(*status_, Gtk::PACK_EXPAND_WIDGET);
  busy_row->pack_start(*cancel_locale_, Gtk::PACK_SHRINK);
  busy_row->set_no_show_all(true);
  busy_row->hide();
  page->pack_start(*busy_row, Gtk::PACK_SHRINK);
  cancel_locale_->signal_clicked().connect([this] {
    if (busy_) {
      session_.cancel();
    }
  });
  auto* apply_button = Gtk::manage(new Gtk::Button("_Apply language", true));
  apply_button->set_halign(Gtk::ALIGN_START);
  page->pack_start(*apply_button, Gtk::PACK_SHRINK);
  auto* note = make_label(
      "Every locale in the supported list is offered. Apply enables a locale that is not generated "
      "yet in /etc/locale.gen and compiles it with localedef. Stop, or Cancel, restores locale.gen. "
      "Keyboard layouts are set on the Keyboard page.");
  note->set_line_wrap(true);
  page->pack_start(*note, Gtk::PACK_SHRINK);
  pack_start(*make_page_scroll(*page), Gtk::PACK_EXPAND_WIDGET);

  apply_button->signal_clicked().connect([this] { apply(); });
  load();
}

void LanguagePage::fill(Gtk::ComboBoxText& combo, bool same_as_language) {
  const std::string selected = combo.get_active_id();
  combo.remove_all();
  if (same_as_language) {
    combo.append("", "Same as the system language");
  }
  for (const auto& locale : locales_) {
    const std::string label = locale.generated ? locale.name : locale.name + " (not generated)";
    combo.append(locale.name, label);
  }
  if (!selected.empty()) {
    combo.set_active_id(selected);
  }
}

bool LanguagePage::needs_generation(const std::string& name) const {
  if (name.empty() || name == "C" || name == "C.UTF-8" || name == "POSIX") {
    return false;
  }
  for (const auto& locale : locales_) {
    if (locale.name == name) {
      return !locale.generated;
    }
  }
  return true;
}

void LanguagePage::set_busy(bool busy, const std::string& message) {
  busy_ = busy;
  language_.set_sensitive(!busy);
  dates_.set_sensitive(!busy);
  numbers_.set_sensitive(!busy);
  currency_.set_sensitive(!busy);
  if (spinner_ != nullptr) {
    if (busy) {
      spinner_->start();
      spinner_->get_parent()->set_no_show_all(false);
      spinner_->get_parent()->show_all();
    } else {
      spinner_->stop();
      spinner_->get_parent()->hide();
    }
  }
  if (status_ != nullptr) {
    status_->set_text(message);
  }
  if (auto window = parent_.get_window()) {
    if (busy) {
      window->set_cursor(Gdk::Cursor::create(window->get_display(), "wait"));
    } else {
      window->set_cursor();
    }
    parent_.queue_draw();
    window->process_updates(true);
  }
  // Paint the status row before pkexec's dialog takes the screen.
  if (busy) {
    while (g_main_context_pending(nullptr)) {
      g_main_context_iteration(nullptr, FALSE);
    }
  }
}

void LanguagePage::load() {
  Freeze freeze(freeze_);
  const auto generated = generated_locale_names(command_output("locale -a"));
  std::set<std::string> ready(generated.begin(), generated.end());
  const auto supported = parse_supported_locales(read_text("/usr/share/i18n/SUPPORTED"));
  locales_.clear();
  auto add_choice = [&](const std::string& name) {
    for (const auto& locale : locales_) {
      if (locale.name == name) {
        return;
      }
    }
    LocaleChoice choice;
    choice.name = name;
    choice.generated = name == "C" || name == "C.UTF-8" || name == "POSIX" || ready.count(name) != 0;
    locales_.push_back(std::move(choice));
  };
  add_choice("C");
  add_choice("C.UTF-8");
  for (const auto& locale : supported) {
    add_choice(locale.name);
  }
  if (supported.empty()) {
    for (const auto& name : generated) {
      add_choice(name);
    }
  }
  std::sort(locales_.begin(), locales_.end(),
            [](const LocaleChoice& a, const LocaleChoice& b) { return a.name < b.name; });
  fill(language_, false);
  fill(dates_, true);
  fill(numbers_, true);
  fill(currency_, true);
  const auto values = locale_file(read_text("/etc/default/locale"));
  const auto pick = [](const std::map<std::string, std::string>& map, const char* key,
                       const std::string& fallback) {
    const auto found = map.find(key);
    return found == map.end() || found->second.empty() ? fallback : found->second;
  };
  const std::string lang = pick(values, "LANG", "C.UTF-8");
  language_.set_active_id(normalize_locale_name(lang));
  if (language_.get_active_row_number() < 0) {
    language_.set_active(0);
  }
  auto regional = [&](Gtk::ComboBoxText& combo, const char* key) {
    const auto found = values.find(key);
    if (found == values.end() || found->second.empty() || found->second == lang) {
      combo.set_active_id("");
    } else {
      combo.set_active_id(normalize_locale_name(found->second));
      if (combo.get_active_row_number() < 0) {
        combo.set_active_id("");
      }
    }
  };
  regional(dates_, "LC_TIME");
  regional(numbers_, "LC_NUMERIC");
  regional(currency_, "LC_MONETARY");
}

void LanguagePage::apply() {
  if (busy_ || language_.get_active_id().empty()) {
    return;
  }
  const std::string lang = language_.get_active_id();
  auto regional = [&](Gtk::ComboBoxText& combo) {
    const std::string id = combo.get_active_id();
    return id.empty() ? lang : std::string(id);
  };
  const std::string dates = regional(dates_);
  const std::string numbers = regional(numbers_);
  const std::string currency = regional(currency_);
  std::string pending;
  for (const std::string& name : {lang, dates, numbers, currency}) {
    if (needs_generation(name) && pending.find(name) == std::string::npos) {
      if (!pending.empty()) {
        pending += ", ";
      }
      pending += name;
    }
  }
  const std::vector<std::string> args = {"set-locale", lang, dates, numbers, currency};
  if (!pending.empty()) {
    set_busy(true, "Generating " + pending + "… This can take a minute. Stop cancels it.");
    session_.run_async(args, "", [this](PrivResult result) {
      set_busy(false, "");
      if (!result.ok) {
        if (result.error == "Cancelled." || result.error.find("cancelled") != std::string::npos) {
          Gtk::MessageDialog dialog(parent_, "Locale generation was cancelled.", false,
                                    Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK, true);
          dialog.set_secondary_text("The locale list was restored.");
          dialog.run();
        } else {
          tell(result.error);
        }
      }
      load();
    });
    return;
  }
  const PrivResult result = session_.run(args);
  if (!result.ok) {
    tell(result.error);
    load();
  }
}

void LanguagePage::tell(const std::string& message) {
  Gtk::MessageDialog dialog(parent_, message, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
  dialog.run();
}

void LanguagePage::on_page_shown() { load(); }

}  // namespace lcos
