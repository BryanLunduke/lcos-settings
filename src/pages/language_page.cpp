// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/language_page.hpp"

#include "logic/system_model.hpp"
#include "ui_util.hpp"

#include <glib.h>

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/messagedialog.h>

#include <fstream>
#include <map>

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
  auto* note = make_label(
      "Only locales that are already generated are listed. Generate another with "
      "\"sudo dpkg-reconfigure locales\" or \"sudo locale-gen en_GB.UTF-8\", then open this page again. "
      "Keyboard layouts are set on the Keyboard page.");
  note->set_line_wrap(true);
  page->pack_start(*note, Gtk::PACK_SHRINK);
  pack_start(*make_page_scroll(*page), Gtk::PACK_EXPAND_WIDGET);

  auto changed = [this] {
    if (freeze_ > 0) {
      return;
    }
    apply();
  };
  language_.signal_changed().connect(changed);
  dates_.signal_changed().connect(changed);
  numbers_.signal_changed().connect(changed);
  currency_.signal_changed().connect(changed);
  load();
}

void LanguagePage::fill(Gtk::ComboBoxText& combo, bool same_as_language) {
  const std::string selected = combo.get_active_id();
  combo.remove_all();
  if (same_as_language) {
    combo.append("", "Same as the system language");
  }
  for (const auto& locale : locales_) {
    combo.append(locale, locale);
  }
  if (!selected.empty()) {
    combo.set_active_id(selected);
  }
}

void LanguagePage::load() {
  Freeze freeze(freeze_);
  locales_ = generated_locale_names(command_output("locale -a"));
  if (locales_.empty()) {
    locales_.push_back("C.UTF-8");
  }
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
  if (language_.get_active_id().empty()) {
    return;
  }
  const std::string lang = language_.get_active_id();
  auto regional = [&](Gtk::ComboBoxText& combo) {
    const std::string id = combo.get_active_id();
    return id.empty() ? lang : std::string(id);
  };
  const PrivResult result =
      session_.run({"set-locale", lang, regional(dates_), regional(numbers_), regional(currency_)});
  if (!result.ok) {
    tell(result.error);
    load();
  }
}

void LanguagePage::tell(const std::string& message) {
  Gtk::MessageDialog dialog(parent_, message, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
  dialog.run();
}

bool LanguagePage::commit_pending() { return session_.commit().ok; }

void LanguagePage::revert_page() { session_.revert(); }

void LanguagePage::on_page_shown() { load(); }

}  // namespace lcos
