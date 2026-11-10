// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/software_page.hpp"

#include "ui_util.hpp"

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

#include <algorithm>
#include <cstdio>
#include <set>

namespace lcos {
namespace {

constexpr const char* kWebWrapper = "xfce4-web-browser.desktop";
constexpr const char* kMailWrapper = "xfce4-mail-reader.desktop";
constexpr const char* kFileWrapper = "xfce4-file-manager.desktop";

std::string read_optional(const std::string& path, bool* existed) {
  if (!Glib::file_test(path, Glib::FILE_TEST_EXISTS)) {
    if (existed != nullptr) {
      *existed = false;
    }
    return {};
  }
  if (existed != nullptr) {
    *existed = true;
  }
  try {
    return Glib::file_get_contents(path);
  } catch (const Glib::Error&) {
    return {};
  }
}

bool write_text(const std::string& path, const std::string& text) {
  const auto slash = path.find_last_of('/');
  if (slash != std::string::npos) {
    g_mkdir_with_parents(path.substr(0, slash).c_str(), 0700);
  }
  try {
    Glib::file_set_contents(path, text);
    return true;
  } catch (const Glib::Error&) {
    return false;
  }
}

std::vector<std::string> data_dirs() {
  std::vector<std::string> dirs;
  dirs.push_back(Glib::get_user_data_dir());
  if (const char* env = g_getenv("XDG_DATA_DIRS")) {
    std::string current;
    for (const char character : std::string(env)) {
      if (character == ':') {
        if (!current.empty()) {
          dirs.push_back(current);
        }
        current.clear();
      } else {
        current.push_back(character);
      }
    }
    if (!current.empty()) {
      dirs.push_back(current);
    }
  }
  dirs.push_back("/usr/local/share");
  dirs.push_back("/usr/share");
  return dirs;
}

std::string helper_id(const std::string& filename) {
  if (filename.size() > 8 && filename.substr(filename.size() - 8) == ".desktop") {
    return filename.substr(0, filename.size() - 8);
  }
  return filename;
}

bool mime_matches(const std::vector<std::string>& types, const std::string& mime) {
  for (const auto& type : types) {
    if (type == mime) {
      return true;
    }
    const auto star = type.find("/*");
    if (star != std::string::npos && mime.size() >= star &&
        mime.compare(0, star, type, 0, star) == 0 &&
        (mime.size() == star || mime[star] == '/')) {
      return true;
    }
  }
  return false;
}

std::string lower_copy(const std::string& text) {
  std::string out = text;
  for (char& character : out) {
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character - 'A' + 'a');
    }
  }
  return out;
}

}  // namespace

SoftwarePage::SoftwarePage() {
  helpers_path_ = Glib::build_filename(Glib::get_user_config_dir(), "xfce4/helpers.rc");
  mime_path_ = Glib::build_filename(Glib::get_user_config_dir(), "mimeapps.list");
  helpers_text_ = read_optional(helpers_path_, &helpers_existed_);
  mime_text_ = read_optional(mime_path_, &mime_existed_);
  helpers_original_ = helpers_text_;
  mime_original_ = mime_text_;

  pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);

  auto* utilities = make_vbox(8);
  utilities->set_border_width(12);
  FormGrid form(180);
  form.add("Web browser", browser_);
  form.add("Mail reader", mail_);
  form.add("File manager", files_);
  form.add("Terminal emulator", terminal_);
  utilities->pack_start(*make_frame("Utilities", form.grid()), Gtk::PACK_SHRINK);
  auto* note = make_label(
      "These are the exo helpers. Choosing one updates xfce4/helpers.rc and the matching xfce4 helper desktop entry in mimeapps.list.");
  note->set_line_wrap(true);
  note->set_max_width_chars(72);
  utilities->pack_start(*note, Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*utilities), "Utilities");

  auto* others = make_vbox(8);
  others->set_border_width(12);
  filter_.set_placeholder_text("Filter file types or applications");
  others->pack_start(filter_, Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*others), "Others");

  load_helpers();
  load_applications();
  rebuild_mime_rows();

  class Columns : public Gtk::TreeModel::ColumnRecord {
   public:
    Columns() {
      add(type);
      add(application);
    }
    Gtk::TreeModelColumn<Glib::ustring> type;
    Gtk::TreeModelColumn<Glib::ustring> application;
  };
  // The column record must outlive the store. Keep it in a function-local
  // static so the model stays valid for the life of the process.
  static Columns columns;
  mime_store_ = Gtk::ListStore::create(columns);
  mime_view_.set_model(mime_store_);
  mime_view_.append_column("File type", columns.type);
  mime_view_.append_column("Default application", columns.application);
  mime_view_.set_headers_visible(true);
  mime_view_.get_column(0)->set_expand(true);
  mime_view_.get_column(1)->set_expand(true);
  auto* list_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*list_scroll);
  list_scroll->set_shadow_type(Gtk::SHADOW_IN);
  list_scroll->set_vexpand(true);
  list_scroll->add(mime_view_);
  others->pack_start(*list_scroll, Gtk::PACK_EXPAND_WIDGET);
  auto* choose = Gtk::manage(new Gtk::Button("Set default application…"));
  choose->set_halign(Gtk::ALIGN_START);
  others->pack_start(*choose, Gtk::PACK_SHRINK);
  filter_.signal_changed().connect(sigc::mem_fun(*this, &SoftwarePage::refill_mime_view));
  choose->signal_clicked().connect(sigc::mem_fun(*this, &SoftwarePage::choose_mime_default));
  refill_mime_view();

  auto wire = [this](Gtk::ComboBoxText& combo, const char* key, const char* wrapper,
                     const std::vector<std::string>* mimes) {
    combo.signal_changed().connect([this, &combo, key, wrapper, mimes] {
      if (freeze_ > 0) {
        return;
      }
      helpers_text_ = rc_assign(helpers_text_, key, combo.get_active_id());
      if (wrapper != nullptr && mimes != nullptr) {
        const std::string desktop = combo.get_active_id().empty()
                                        ? std::string()
                                        : std::string(wrapper);
        for (const auto& mime : *mimes) {
          mime_text_ = ini_assign(mime_text_, "Default Applications", mime, desktop);
          mime_text_ =
              ini_assign(mime_text_, "Added Associations", mime,
                         desktop.empty() ? std::string() : desktop + ";");
        }
      }
      save_files();
      rebuild_mime_rows();
      refill_mime_view();
    });
  };
  wire(browser_, "WebBrowser", kWebWrapper, &web_mimes_);
  wire(mail_, "MailReader", kMailWrapper, &mail_mimes_);
  wire(files_, "FileManager", kFileWrapper, &file_mimes_);
  wire(terminal_, "TerminalEmulator", nullptr, nullptr);
}

void SoftwarePage::load_helpers() {
  std::set<std::string> seen;
  auto consider = [&](const std::string& path) {
    const auto slash = path.find_last_of('/');
    const std::string filename = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::string id = helper_id(filename);
    if (!seen.insert(id).second) {
      return;
    }
    try {
      const DesktopFile file = parse_desktop_file(Glib::file_get_contents(path), id);
      if (file.hidden || file.category.empty()) {
        return;
      }
      helpers_.push_back(file);
    } catch (const Glib::Error&) {
    }
  };
  for (const auto& root : data_dirs()) {
    const std::string dir = Glib::build_filename(root, "xfce4/helpers");
    try {
      Glib::Dir listing(dir);
      for (const auto& name : listing) {
        if (name.size() > 8 && name.substr(name.size() - 8) == ".desktop") {
          consider(Glib::build_filename(dir, name));
        }
      }
    } catch (const Glib::Error&) {
    }
  }
  std::sort(helpers_.begin(), helpers_.end(), [](const DesktopFile& a, const DesktopFile& b) {
    return a.name < b.name;
  });

  auto fill = [this](Gtk::ComboBoxText& combo, const char* category, const char* key) {
    Freeze freeze(freeze_);
    combo.remove_all();
    combo.append("", "Not set");
    for (const auto& helper : helpers_) {
      if (helper.category == category) {
        combo.append(helper.id, helper.name);
      }
    }
    const std::string current = rc_lookup(helpers_text_, key);
    if (!current.empty()) {
      combo.set_active_id(current);
      if (combo.get_active_row_number() < 0) {
        combo.append(current, current);
        combo.set_active_id(current);
      }
    } else {
      combo.set_active_id("");
    }
  };
  fill(browser_, "WebBrowser", "WebBrowser");
  fill(mail_, "MailReader", "MailReader");
  fill(files_, "FileManager", "FileManager");
  fill(terminal_, "TerminalEmulator", "TerminalEmulator");

  auto wrapper_mimes = [](const std::string& filename, std::vector<std::string>& out) {
    for (const auto& root : data_dirs()) {
      const std::string path = Glib::build_filename(root, "applications", filename);
      if (!Glib::file_test(path, Glib::FILE_TEST_EXISTS)) {
        continue;
      }
      try {
        const DesktopFile file = parse_desktop_file(Glib::file_get_contents(path), filename);
        out = file.mime_types;
        return;
      } catch (const Glib::Error&) {
      }
    }
  };
  wrapper_mimes(kWebWrapper, web_mimes_);
  wrapper_mimes(kMailWrapper, mail_mimes_);
  wrapper_mimes(kFileWrapper, file_mimes_);
  if (web_mimes_.empty()) {
    web_mimes_ = {"x-scheme-handler/http", "x-scheme-handler/https", "text/html"};
  }
  if (mail_mimes_.empty()) {
    mail_mimes_ = {"x-scheme-handler/mailto"};
  }
  if (file_mimes_.empty()) {
    file_mimes_ = {"inode/directory"};
  }
}

void SoftwarePage::load_applications() {
  std::set<std::string> seen;
  for (const auto& root : data_dirs()) {
    const std::string dir = Glib::build_filename(root, "applications");
    std::vector<std::string> names;
    try {
      Glib::Dir listing(dir);
      for (const auto& name : listing) {
        names.push_back(name);
      }
    } catch (const Glib::Error&) {
      continue;
    }
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
      if (name.size() <= 8 || name.substr(name.size() - 8) != ".desktop") {
        continue;
      }
      if (!seen.insert(name).second) {
        continue;
      }
      try {
        DesktopFile file =
            parse_desktop_file(Glib::file_get_contents(Glib::build_filename(dir, name)), name);
        if (file.hidden || !file.application) {
          continue;
        }
        applications_.push_back(std::move(file));
      } catch (const Glib::Error&) {
      }
    }
  }
  std::sort(applications_.begin(), applications_.end(),
            [](const DesktopFile& a, const DesktopFile& b) { return a.name < b.name; });
}

std::string SoftwarePage::app_name(const std::string& desktop_id) const {
  if (desktop_id.empty()) {
    return {};
  }
  for (const auto& app : applications_) {
    if (app.id == desktop_id) {
      return app.name;
    }
  }
  return desktop_id;
}

void SoftwarePage::rebuild_mime_rows() {
  mime_rows_.clear();
  std::set<std::string> types;
  for (const char* group : {"Default Applications", "Added Associations"}) {
    for (const auto& entry : ini_entries(mime_text_, group)) {
      types.insert(entry.first);
    }
  }
  for (const auto& app : applications_) {
    for (const auto& type : app.mime_types) {
      types.insert(type);
    }
  }
  mime_rows_.reserve(types.size());
  for (const auto& type : types) {
    MimeRow row;
    row.type = type;
    row.desktop_id = ini_lookup(mime_text_, "Default Applications", type);
    const auto semi = row.desktop_id.find(';');
    if (semi != std::string::npos) {
      row.desktop_id.resize(semi);
    }
    row.app_name = app_name(row.desktop_id);
    mime_rows_.push_back(std::move(row));
  }
}

void SoftwarePage::refill_mime_view() {
  if (!mime_store_) {
    return;
  }
  mime_store_->clear();
  const std::string needle = lower_copy(filter_.get_text());
  for (const auto& row : mime_rows_) {
    if (!needle.empty() && lower_copy(row.type).find(needle) == std::string::npos &&
        lower_copy(row.app_name).find(needle) == std::string::npos) {
      continue;
    }
    auto iter = mime_store_->append();
    iter->set_value(0, Glib::ustring(row.type));
    iter->set_value(1, Glib::ustring(row.app_name.empty() ? "—" : row.app_name));
  }
}

void SoftwarePage::save_files() {
  if (helpers_text_ != helpers_original_ || helpers_existed_) {
    if (helpers_text_ != read_optional(helpers_path_, nullptr) || !helpers_existed_) {
      write_text(helpers_path_, helpers_text_);
    }
  } else if (!helpers_text_.empty()) {
    write_text(helpers_path_, helpers_text_);
  }
  if (!mime_text_.empty() || mime_existed_) {
    write_text(mime_path_, mime_text_);
  }
}

void SoftwarePage::choose_mime_default() {
  auto iter = mime_view_.get_selection()->get_selected();
  if (!iter) {
    return;
  }
  Glib::ustring type;
  iter->get_value(0, type);
  std::string current;
  for (const auto& row : mime_rows_) {
    if (row.type == std::string(type)) {
      current = row.desktop_id;
      break;
    }
  }
  Gtk::Dialog dialog("Default application", true);
  if (auto* top = dynamic_cast<Gtk::Window*>(get_toplevel())) {
    dialog.set_transient_for(*top);
  }
  dialog.set_default_size(420, 140);
  auto* combo = Gtk::manage(new Gtk::ComboBoxText());
  combo->append("", "None");
  for (const auto& app : applications_) {
    if (app.no_display && app.id != current) {
      continue;
    }
    if (mime_matches(app.mime_types, type) || app.id == current) {
      combo->append(app.id, app.name);
    }
  }
  if (!current.empty() && combo->get_model()) {
    combo->set_active_id(current);
  }
  if (combo->get_active_row_number() < 0) {
    if (!current.empty()) {
      combo->append(current, current);
      combo->set_active_id(current);
    } else {
      combo->set_active(0);
    }
  }
  auto* row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 8));
  row->set_border_width(12);
  auto* label = make_label(std::string(type));
  label->set_line_wrap(true);
  row->pack_start(*label, Gtk::PACK_SHRINK);
  row->pack_start(*combo, Gtk::PACK_SHRINK);
  dialog.get_content_area()->pack_start(*row, Gtk::PACK_EXPAND_WIDGET);
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("Set", Gtk::RESPONSE_OK);
  dialog.show_all();
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  const std::string chosen = combo->get_active_id();
  mime_text_ = ini_assign(mime_text_, "Default Applications", type, chosen);
  mime_text_ = ini_assign(mime_text_, "Added Associations", type,
                          chosen.empty() ? std::string() : chosen + ";");
  save_files();
  rebuild_mime_rows();
  refill_mime_view();
}

void SoftwarePage::revert_page() {
  if (helpers_text_ == helpers_original_ && mime_text_ == mime_original_) {
    return;
  }
  helpers_text_ = helpers_original_;
  mime_text_ = mime_original_;
  if (helpers_existed_) {
    write_text(helpers_path_, helpers_original_);
  } else if (Glib::file_test(helpers_path_, Glib::FILE_TEST_EXISTS)) {
    std::remove(helpers_path_.c_str());
  }
  if (mime_existed_) {
    write_text(mime_path_, mime_original_);
  } else if (Glib::file_test(mime_path_, Glib::FILE_TEST_EXISTS)) {
    std::remove(mime_path_.c_str());
  }
}

void SoftwarePage::prepare_screenshot(int index) {
  if (index >= 0 && index < notebook_.get_n_pages()) {
    notebook_.set_current_page(index);
  }
}

std::string SoftwarePage::screenshot_suffix(int index) const {
  return index == 1 ? "others" : "utilities";
}

}  // namespace lcos
