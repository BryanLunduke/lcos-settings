// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/startup_page.hpp"

#include "logic/system_model.hpp"
#include "ui_util.hpp"

#include <glibmm/miscutils.h>

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/cellrenderertoggle.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/scrolledwindow.h>

#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

#include <gdk/gdk.h>

namespace lcos {
namespace {

std::string read_text(const std::string& path) {
  std::ifstream in(path);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_text(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

std::string xfce_session_file() {
  const char* display = gdk_display_get_name(gdk_display_get_default());
  std::string name;
  if (display != nullptr && display[0] == ':') {
    char host[256] = {};
    if (gethostname(host, sizeof(host) - 1) != 0) {
      host[0] = '\0';
    }
    name = host;
    for (const char* cursor = display; *cursor != '\0' && *cursor != '.'; ++cursor) {
      name.push_back(*cursor);
    }
  } else if (display != nullptr) {
    name = display;
    const auto colon = name.rfind(':');
    const auto dot = name.rfind('.');
    if (colon != std::string::npos && dot != std::string::npos && dot > colon) {
      name.resize(dot);
    }
  }
  return Glib::get_user_cache_dir() + "/sessions/xfce4-session-" + name;
}

std::map<std::string, std::string> desktop_dir(const std::string& directory) {
  std::map<std::string, std::string> files;
  DIR* dir = opendir(directory.c_str());
  if (dir == nullptr) {
    return files;
  }
  while (const dirent* item = readdir(dir)) {
    const std::string name = item->d_name;
    if (name.size() > 8 && name.compare(name.size() - 8, 8, ".desktop") == 0) {
      files[name] = read_text(directory + "/" + name);
    }
  }
  closedir(dir);
  return files;
}

std::string set_desktop_key(std::string text, const std::string& key, const std::string& value) {
  if (text.find("[Desktop Entry]") == std::string::npos) {
    text = "[Desktop Entry]\nType=Application\n" + text;
  }
  std::string out;
  bool in_group = false;
  bool wrote = false;
  std::string line;
  auto flush = [&](const std::string& current) {
    if (current == "[Desktop Entry]") {
      in_group = true;
      out += current + "\n";
      return;
    }
    if (!current.empty() && current.front() == '[') {
      if (in_group && !wrote) {
        out += key + "=" + value + "\n";
        wrote = true;
      }
      in_group = false;
    }
    if (in_group && current.compare(0, key.size() + 1, key + "=") == 0) {
      if (!wrote) {
        out += key + "=" + value + "\n";
        wrote = true;
      }
      return;
    }
    out += current + "\n";
  };
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      if (!(i == text.size() && line.empty())) {
        flush(line);
      }
      line.clear();
    } else {
      line.push_back(text[i]);
    }
  }
  if (!wrote) {
    out += key + "=" + value + "\n";
  }
  return out;
}

bool command_exists(const char* path) { return g_file_test(path, G_FILE_TEST_EXISTS) != 0; }

}  // namespace

StartupPage::StartupPage(XfconfStore& store, PrivSession& session, Gtk::Window& parent)
    : store_(store), session_(session), parent_(parent) {
  prompt_.set_label("Prompt on logout");
  save_.set_label("Automatically save session on logout");
  chooser_.set_label("Display the session chooser on login");
  gnome_.set_label("Launch GNOME services on startup");
  kde_.set_label("Launch KDE services on startup");
  tcp_.set_label("Manage remote applications");
  apps_ = Gtk::ListStore::create(columns_);
  apps_view_.set_model(apps_);
  auto* toggle = Gtk::manage(new Gtk::CellRendererToggle());
  toggle->set_activatable(true);
  apps_view_.append_column("On", *toggle);
  apps_view_.get_column(0)->add_attribute(toggle->property_active(), columns_.enabled);
  apps_view_.append_column("Application", columns_.name);
  apps_view_.append_column("Command", columns_.command);
  apps_view_.get_column(1)->set_expand(true);
  toggle->signal_toggled().connect(sigc::mem_fun(*this, &StartupPage::toggle_selected));

  auto* autostart = make_vbox(8);
  autostart->set_border_width(12);
  auto* app_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*app_scroll);
  app_scroll->set_shadow_type(Gtk::SHADOW_IN);
  app_scroll->set_size_request(-1, 240);
  app_scroll->add(apps_view_);
  autostart->pack_start(*app_scroll, Gtk::PACK_SHRINK);
  auto* buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  auto* add = Gtk::manage(new Gtk::Button("Add"));
  auto* edit = Gtk::manage(new Gtk::Button("Edit"));
  auto* remove = Gtk::manage(new Gtk::Button("Remove"));
  buttons->pack_start(*add, Gtk::PACK_SHRINK);
  buttons->pack_start(*edit, Gtk::PACK_SHRINK);
  buttons->pack_start(*remove, Gtk::PACK_SHRINK);
  autostart->pack_start(*buttons, Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*autostart), "Autostart");

  auto* session_page = make_vbox(8);
  session_page->set_border_width(12);
  auto* general = make_vbox(6);
  general->pack_start(prompt_, Gtk::PACK_SHRINK);
  general->pack_start(save_, Gtk::PACK_SHRINK);
  general->pack_start(chooser_, Gtk::PACK_SHRINK);
  session_page->pack_start(*make_frame("Logout", *general), Gtk::PACK_SHRINK);

  class NameColumns : public Gtk::TreeModel::ColumnRecord {
   public:
    NameColumns() { add(name); }
    Gtk::TreeModelColumn<Glib::ustring> name;
  };
  static NameColumns name_columns;
  sessions_ = Gtk::ListStore::create(name_columns);
  sessions_view_.set_model(sessions_);
  sessions_view_.append_column("Saved session", name_columns.name);
  auto* saved = make_vbox(6);
  auto* saved_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*saved_scroll);
  saved_scroll->set_shadow_type(Gtk::SHADOW_IN);
  saved_scroll->set_size_request(-1, 120);
  saved_scroll->add(sessions_view_);
  saved->pack_start(*saved_scroll, Gtk::PACK_SHRINK);
  auto* clear = Gtk::manage(new Gtk::Button("Delete session"));
  clear->set_halign(Gtk::ALIGN_START);
  saved->pack_start(*clear, Gtk::PACK_SHRINK);
  session_page->pack_start(*make_frame("Saved sessions", *saved), Gtk::PACK_SHRINK);

  auto* advanced = make_vbox(6);
  // GNOME and KDE toggles stay hidden unless those desktops are installed.
  // LCOS does not ship them, so an installed system does not show the toggles.
  // gnome-keyring-daemon ships without GNOME. The toggle is for a GNOME session.
  const bool gnome = command_exists("/usr/bin/gnome-session");
  const bool kde = command_exists("/usr/bin/startplasma-x11") || command_exists("/usr/bin/startkde");
  if (gnome) {
    advanced->pack_start(gnome_, Gtk::PACK_SHRINK);
  }
  if (kde) {
    advanced->pack_start(kde_, Gtk::PACK_SHRINK);
  }
  advanced->pack_start(tcp_, Gtk::PACK_SHRINK);
  if (!gnome && !kde) {
    auto* note = make_label(
        "GNOME and KDE startup services are not installed, so those options are left out.");
    note->set_line_wrap(true);
    advanced->pack_start(*note, Gtk::PACK_SHRINK);
  }
  session_page->pack_start(*make_frame("Advanced", *advanced), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*session_page), "Session");
  pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);

  auto bind = [this](Gtk::CheckButton& check, const char* prop, bool fallback) {
    {
      Freeze freeze(freeze_);
      check.set_active(store_.get_bool("xfce4-session", prop, fallback));
    }
    check.signal_toggled().connect([this, &check, prop] {
      if (freeze_ > 0) {
        return;
      }
      store_.set_bool("xfce4-session", prop, check.get_active());
    });
  };
  bind(prompt_, "/general/PromptOnLogout", true);
  bind(save_, "/general/SaveOnExit", false);
  bind(chooser_, "/chooser/AlwaysDisplay", false);
  bind(gnome_, "/compat/LaunchGNOME", false);
  bind(kde_, "/compat/LaunchKDE", false);
  bind(tcp_, "/security/EnableTcp", false);
  add->signal_clicked().connect(sigc::mem_fun(*this, &StartupPage::add_app));
  edit->signal_clicked().connect(sigc::mem_fun(*this, &StartupPage::edit_app));
  remove->signal_clicked().connect(sigc::mem_fun(*this, &StartupPage::remove_app));
  clear->signal_clicked().connect(sigc::mem_fun(*this, &StartupPage::clear_sessions));
  reload_apps();
  reload_sessions();
}

std::string StartupPage::user_autostart_dir() const {
  return Glib::get_home_dir() + "/.config/autostart";
}

void StartupPage::remember_autostart() {
  if (autostart_snapshot_.count(".captured") != 0) {
    return;
  }
  autostart_snapshot_ = desktop_dir(user_autostart_dir());
  autostart_snapshot_[".captured"] = "1";
  files_dirty_ = true;
}

void StartupPage::remember_sessions() {
  if (session_snapshot_.count(".captured") != 0) {
    return;
  }
  if (session_file_.empty()) {
    session_file_ = xfce_session_file();
  }
  const bool exists = g_file_test(session_file_.c_str(), G_FILE_TEST_EXISTS) != 0;
  session_snapshot_[".path"] = session_file_;
  session_snapshot_[".existed"] = exists ? "1" : "0";
  session_snapshot_[".body"] = exists ? read_text(session_file_) : std::string();
  session_snapshot_[".captured"] = "1";
  files_dirty_ = true;
}

void StartupPage::reload_apps() {
  const auto system_files = desktop_dir("/etc/xdg/autostart");
  const auto user_files = desktop_dir(user_autostart_dir());
  const auto rows = merge_autostart(system_files, user_files);
  apps_->clear();
  for (const auto& row : rows) {
    auto iter = apps_->append();
    (*iter)[columns_.enabled] = row.enabled;
    Glib::ustring name = row.name;
    if (row.run_hook != 0) {
      name += " (not at login)";
    }
    (*iter)[columns_.name] = name;
    (*iter)[columns_.command] = row.command;
    (*iter)[columns_.id] = row.id;
    (*iter)[columns_.removable] = row.removable;
    (*iter)[columns_.show_in_xfce] = row.show_in_xfce;
  }
}

void StartupPage::reload_sessions() {
  sessions_->clear();
  session_file_ = xfce_session_file();
  for (const auto& name : saved_session_names(read_text(session_file_))) {
    auto row = sessions_->append();
    row->set_value(0, Glib::ustring(name));
  }
}

std::string StartupPage::selected_id() {
  auto iter = apps_view_.get_selection()->get_selected();
  if (!iter) {
    return {};
  }
  return static_cast<Glib::ustring>((*iter)[columns_.id]);
}

void StartupPage::toggle_selected(const std::string& path) {
  auto iter = apps_->get_iter(path);
  if (!iter) {
    return;
  }
  const std::string id = static_cast<Glib::ustring>((*iter)[columns_.id]);
  const bool enabled = (*iter)[columns_.enabled];
  const bool show_in_xfce = (*iter)[columns_.show_in_xfce];
  remember_autostart();
  const std::string dir = user_autostart_dir();
  g_mkdir_with_parents(dir.c_str(), 0700);
  const std::string user_path = dir + "/" + id;
  std::string text = read_text(user_path);
  if (text.empty()) {
    text = read_text("/etc/xdg/autostart/" + id);
  }
  const AutostartToggle toggle = autostart_toggle_keys(show_in_xfce, enabled);
  if (toggle.set_hidden) {
    text = set_desktop_key(text, "Hidden", toggle.hidden ? "true" : "false");
  }
  if (toggle.set_override) {
    text = set_desktop_key(text, "X-XFCE-Autostart-Override", toggle.override_on ? "true" : "false");
  }
  write_text(user_path, text);
  reload_apps();
}

void StartupPage::add_app() {
  Gtk::Dialog dialog("Add application", parent_, true);
  auto* name = Gtk::manage(new Gtk::Entry());
  auto* command = Gtk::manage(new Gtk::Entry());
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  grid->attach(*make_label("Name"), 0, 0, 1, 1);
  grid->attach(*name, 1, 0, 1, 1);
  grid->attach(*make_label("Command"), 0, 1, 1, 1);
  grid->attach(*command, 1, 1, 1, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("Add", Gtk::RESPONSE_OK);
  dialog.show_all();
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  const std::string title = name->get_text();
  const std::string exec = command->get_text();
  if (title.empty() || exec.empty() || title.find('\n') != std::string::npos ||
      exec.find('\n') != std::string::npos) {
    return;
  }
  remember_autostart();
  const std::string dir = user_autostart_dir();
  g_mkdir_with_parents(dir.c_str(), 0700);
  for (int i = 1; i < 1000; ++i) {
    const std::string id = "lcos-settings-" + std::to_string(i) + ".desktop";
    const std::string path = dir + "/" + id;
    if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
      continue;
    }
    write_text(path, autostart_desktop_text(title, "", exec, false, 0));
    break;
  }
  reload_apps();
}

void StartupPage::edit_app() {
  const std::string id = selected_id();
  if (id.empty()) {
    return;
  }
  auto iter = apps_view_.get_selection()->get_selected();
  Gtk::Dialog dialog("Edit application", parent_, true);
  auto* name = Gtk::manage(new Gtk::Entry());
  auto* command = Gtk::manage(new Gtk::Entry());
  name->set_text(static_cast<Glib::ustring>((*iter)[columns_.name]));
  command->set_text(static_cast<Glib::ustring>((*iter)[columns_.command]));
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  grid->attach(*make_label("Name"), 0, 0, 1, 1);
  grid->attach(*name, 1, 0, 1, 1);
  grid->attach(*make_label("Command"), 0, 1, 1, 1);
  grid->attach(*command, 1, 1, 1, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("Save", Gtk::RESPONSE_OK);
  dialog.show_all();
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  remember_autostart();
  const std::string dir = user_autostart_dir();
  g_mkdir_with_parents(dir.c_str(), 0700);
  const std::string path = dir + "/" + id;
  std::string text = read_text(path);
  if (text.empty()) {
    text = read_text("/etc/xdg/autostart/" + id);
  }
  text = set_desktop_key(text, "Name", name->get_text());
  text = set_desktop_key(text, "Exec", command->get_text());
  write_text(path, text);
  reload_apps();
}

void StartupPage::remove_app() {
  auto iter = apps_view_.get_selection()->get_selected();
  if (!iter) {
    return;
  }
  const std::string id = static_cast<Glib::ustring>((*iter)[columns_.id]);
  const bool removable = (*iter)[columns_.removable];
  if (!removable) {
    Gtk::MessageDialog dialog(parent_, "System applications stay in the list.", false, Gtk::MESSAGE_INFO,
                              Gtk::BUTTONS_OK, true);
    dialog.set_secondary_text("Turn the application off to stop it from starting. Its desktop file stays installed.");
    dialog.run();
    return;
  }
  remember_autostart();
  unlink((user_autostart_dir() + "/" + id).c_str());
  reload_apps();
}

void StartupPage::clear_sessions() {
  auto iter = sessions_view_.get_selection()->get_selected();
  if (!iter) {
    Gtk::MessageDialog dialog(parent_, "Choose a saved session to delete.", false, Gtk::MESSAGE_INFO,
                              Gtk::BUTTONS_OK, true);
    dialog.run();
    return;
  }
  Glib::ustring name;
  iter->get_value(0, name);
  Gtk::MessageDialog dialog(parent_, "Delete saved session \"" + name + "\"?", false,
                            Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);
  dialog.set_secondary_text("Other saved sessions stay. Cancel puts this one back.");
  if (dialog.run() != Gtk::RESPONSE_YES) {
    return;
  }
  remember_sessions();
  const std::string updated = keyfile_without_session(read_text(session_file_), name);
  g_mkdir_with_parents(Glib::path_get_dirname(session_file_).c_str(), 0700);
  write_text(session_file_, updated);
  reload_sessions();
}

void StartupPage::restore_files() {
  if (autostart_snapshot_.count(".captured") != 0) {
    const std::string dir = user_autostart_dir();
    const auto current = desktop_dir(dir);
    for (const auto& entry : current) {
      if (autostart_snapshot_.count(entry.first) == 0) {
        unlink((dir + "/" + entry.first).c_str());
      }
    }
    for (const auto& entry : autostart_snapshot_) {
      if (entry.first == ".captured") {
        continue;
      }
      write_text(dir + "/" + entry.first, entry.second);
    }
    autostart_snapshot_.clear();
  }
  if (session_snapshot_.count(".captured") != 0) {
    const std::string path = session_snapshot_.count(".path") ? session_snapshot_.at(".path") : session_file_;
    if (!path.empty()) {
      if (session_snapshot_[".existed"] == "1") {
        g_mkdir_with_parents(Glib::path_get_dirname(path).c_str(), 0700);
        write_text(path, session_snapshot_[".body"]);
      } else {
        unlink(path.c_str());
      }
    }
    session_snapshot_.clear();
  }
  files_dirty_ = false;
}

bool StartupPage::has_pending() const { return files_dirty_; }

bool StartupPage::commit_pending() {
  autostart_snapshot_.clear();
  session_snapshot_.clear();
  files_dirty_ = false;
  return true;
}

void StartupPage::revert_page() {
  restore_files();
  reload_apps();
  reload_sessions();
}

void StartupPage::prepare_screenshot(int index) {
  if (index >= 0 && index < notebook_.get_n_pages()) {
    notebook_.set_current_page(index);
  }
}

std::string StartupPage::screenshot_suffix(int index) const {
  return index == 1 ? "session" : "autostart";
}

}  // namespace lcos
