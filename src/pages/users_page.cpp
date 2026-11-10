// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/users_page.hpp"

#include "ui_util.hpp"

#include <gdkmm/pixbuf.h>
#include <gtkmm/icontheme.h>

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/entry.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/label.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/scrolledwindow.h>

#include <dirent.h>
#include <fstream>
#include <pwd.h>
#include <unistd.h>

#include <algorithm>

namespace lcos {
namespace {

std::string read_text(const std::string& path) {
  std::ifstream in(path);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string sudo_members() {
  const std::string text = read_text("/etc/group");
  std::string line;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i != text.size() && text[i] != '\n') {
      line.push_back(text[i]);
      continue;
    }
    if (line.compare(0, 5, "sudo:") == 0) {
      const auto last = line.rfind(':');
      return last == std::string::npos ? std::string() : line.substr(last + 1);
    }
    line.clear();
  }
  return {};
}

void append_conf_dir(const std::string& directory, std::vector<std::string>& texts) {
  DIR* dir = opendir(directory.c_str());
  if (dir == nullptr) {
    return;
  }
  std::vector<std::string> names;
  while (const dirent* item = readdir(dir)) {
    const std::string name = item->d_name;
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".conf") == 0) {
      names.push_back(name);
    }
  }
  closedir(dir);
  std::sort(names.begin(), names.end());
  for (const auto& name : names) {
    texts.push_back(read_text(directory + "/" + name));
  }
}

std::string autologin_now() {
  // LightDM applies files in this order. 60-lcos-settings.conf sorts after
  // the installer drop-in, so it wins without editing 50-lcos.conf.
  std::vector<std::string> texts;
  texts.push_back(read_text("/usr/share/lightdm/lightdm.conf"));
  append_conf_dir("/usr/share/lightdm/lightdm.conf.d", texts);
  append_conf_dir("/etc/xdg/lightdm/lightdm.conf.d", texts);
  texts.push_back(read_text("/etc/lightdm/lightdm.conf"));
  append_conf_dir("/etc/lightdm/lightdm.conf.d", texts);
  return effective_autologin(texts);
}

bool ask_text(Gtk::Window& parent, const Glib::ustring& title, const Glib::ustring& caption,
              std::string& value) {
  Gtk::Dialog dialog(title, parent, true);
  auto* entry = Gtk::manage(new Gtk::Entry());
  entry->set_text(value);
  entry->set_hexpand(true);
  entry->set_margin_top(12);
  entry->set_margin_bottom(12);
  entry->set_margin_start(12);
  entry->set_margin_end(12);
  dialog.get_content_area()->pack_start(*make_label(caption), Gtk::PACK_SHRINK);
  dialog.get_content_area()->pack_start(*entry, Gtk::PACK_SHRINK);
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("OK", Gtk::RESPONSE_OK);
  dialog.show_all();
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return false;
  }
  value = entry->get_text();
  return true;
}

}  // namespace

UsersPage::UsersPage(PrivSession& session, Gtk::Window& parent)
    : session_(session), parent_(parent) {
  if (const passwd* entry = getpwuid(getuid())) {
    current_user_ = entry->pw_name != nullptr ? entry->pw_name : "";
  }
  store_ = Gtk::ListStore::create(columns_);
  view_.set_model(store_);
  view_.append_column("Username", columns_.name);
  view_.append_column("Name", columns_.real);
  view_.append_column("Account", columns_.kind);
  view_.append_column("Automatic login", columns_.automatic);
  view_.get_column(1)->set_expand(true);
  picture_.set_size_request(64, 64);
  type_.append("standard", "Standard");
  type_.append("administrator", "Administrator");

  auto* page = make_vbox(8);
  page->set_border_width(12);
  auto* list_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*list_scroll);
  list_scroll->set_shadow_type(Gtk::SHADOW_IN);
  list_scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  list_scroll->set_size_request(-1, 220);
  list_scroll->add(view_);
  page->pack_start(*list_scroll, Gtk::PACK_SHRINK);

  auto* buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  auto* add = Gtk::manage(new Gtk::Button("Add user"));
  auto* remove = Gtk::manage(new Gtk::Button("Remove user"));
  auto* password = Gtk::manage(new Gtk::Button("Change password"));
  auto* name = Gtk::manage(new Gtk::Button("Real name"));
  auto* photo = Gtk::manage(new Gtk::Button("Picture"));
  buttons->pack_start(*add, Gtk::PACK_SHRINK);
  buttons->pack_start(*remove, Gtk::PACK_SHRINK);
  buttons->pack_start(*password, Gtk::PACK_SHRINK);
  buttons->pack_start(*name, Gtk::PACK_SHRINK);
  buttons->pack_start(*photo, Gtk::PACK_SHRINK);
  page->pack_start(*buttons, Gtk::PACK_SHRINK);

  auto* detail = make_vbox(6);
  detail->pack_start(picture_, Gtk::PACK_SHRINK);
  FormGrid form(160);
  form.add("Account type", type_);
  form.add("Log in automatically", autologin_combo_);
  detail->pack_start(form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Selected account", *detail), Gtk::PACK_SHRINK);
  auto* note = make_label(
      "Pictures are stored as ~/.face because LCOS does not ship AccountsService. "
      "Automatic login stays as the installer left it until you change it here. "
      "The current user and the last administrator cannot be removed.");
  note->set_line_wrap(true);
  page->pack_start(*note, Gtk::PACK_SHRINK);
  pack_start(*make_page_scroll(*page), Gtk::PACK_EXPAND_WIDGET);

  add->signal_clicked().connect(sigc::mem_fun(*this, &UsersPage::add_user));
  remove->signal_clicked().connect(sigc::mem_fun(*this, &UsersPage::remove_user));
  password->signal_clicked().connect(sigc::mem_fun(*this, &UsersPage::change_password));
  name->signal_clicked().connect(sigc::mem_fun(*this, &UsersPage::change_name));
  photo->signal_clicked().connect(sigc::mem_fun(*this, &UsersPage::change_picture));
  view_.get_selection()->signal_changed().connect([this] { show_detail(); });
  type_.signal_changed().connect(sigc::mem_fun(*this, &UsersPage::change_type));
  autologin_combo_.signal_changed().connect(sigc::mem_fun(*this, &UsersPage::change_autologin));
  reload();
}

void UsersPage::tell(const std::string& message) {
  Gtk::MessageDialog dialog(parent_, message, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
  dialog.run();
}

std::string UsersPage::selected_name() {
  auto iter = view_.get_selection()->get_selected();
  if (!iter) {
    return {};
  }
  return static_cast<Glib::ustring>((*iter)[columns_.name]);
}

const Account* UsersPage::selected() {
  const std::string name = selected_name();
  for (const auto& account : accounts_) {
    if (account.name == name) {
      return &account;
    }
  }
  return nullptr;
}

void UsersPage::reload() {
  const std::string keep = selected_name();
  accounts_ = human_accounts(read_text("/etc/passwd"), sudo_members(), 1000);
  autologin_ = autologin_now();
  Freeze freeze(freeze_);
  store_->clear();
  Gtk::TreeModel::iterator selected_row;
  for (const auto& account : accounts_) {
    auto row = store_->append();
    (*row)[columns_.name] = account.name;
    (*row)[columns_.real] = account.real_name;
    (*row)[columns_.kind] = account.administrator ? "Administrator" : "Standard";
    (*row)[columns_.automatic] = account.name == autologin_ ? "Yes" : "";
    if (account.name == keep || (keep.empty() && account.name == current_user_)) {
      selected_row = row;
    }
  }
  if (!selected_row && !store_->children().empty()) {
    selected_row = store_->children().begin();
  }
  if (selected_row) {
    view_.get_selection()->select(selected_row);
  }
  show_detail();
}

void UsersPage::show_detail() {
  Freeze freeze(freeze_);
  autologin_combo_.remove_all();
  autologin_combo_.append("none", "Ask for a password");
  for (const auto& account : accounts_) {
    autologin_combo_.append(account.name, account.name);
  }
  autologin_combo_.set_active_id(autologin_.empty() ? "none" : autologin_);
  if (autologin_combo_.get_active_row_number() < 0) {
    autologin_combo_.set_active_id("none");
  }
  if (const Account* account = selected()) {
    type_.set_active_id(account->administrator ? "administrator" : "standard");
    const std::string face = account->home + "/.face";
    try {
      picture_.set(Gdk::Pixbuf::create_from_file(face, 64, 64, true));
      return;
    } catch (const Glib::Error&) {
    }
  }
  auto theme = Gtk::IconTheme::create();
  theme->set_custom_theme("AdwaitaLegacy");
  try {
    picture_.set(theme->load_icon("avatar-default", 64, 1, Gtk::ICON_LOOKUP_FORCE_SIZE));
  } catch (const Glib::Error&) {
    picture_.clear();
  }
}

void UsersPage::add_user() {
  Gtk::Dialog dialog("Add user", parent_, true);
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  auto* username = Gtk::manage(new Gtk::Entry());
  auto* real = Gtk::manage(new Gtk::Entry());
  auto* password = Gtk::manage(new Gtk::Entry());
  auto* confirm = Gtk::manage(new Gtk::Entry());
  password->set_visibility(false);
  confirm->set_visibility(false);
  auto* kind = Gtk::manage(new Gtk::ComboBoxText());
  kind->append("standard", "Standard");
  kind->append("administrator", "Administrator");
  kind->set_active_id("standard");
  grid->attach(*make_label("Username"), 0, 0, 1, 1);
  grid->attach(*username, 1, 0, 1, 1);
  grid->attach(*make_label("Real name"), 0, 1, 1, 1);
  grid->attach(*real, 1, 1, 1, 1);
  grid->attach(*make_label("Password"), 0, 2, 1, 1);
  grid->attach(*password, 1, 2, 1, 1);
  grid->attach(*make_label("Confirm"), 0, 3, 1, 1);
  grid->attach(*confirm, 1, 3, 1, 1);
  grid->attach(*make_label("Account type"), 0, 4, 1, 1);
  grid->attach(*kind, 1, 4, 1, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("Add", Gtk::RESPONSE_OK);
  dialog.show_all();
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  const std::string name = username->get_text();
  const std::string full = real->get_text();
  const std::string secret = password->get_text();
  if (!valid_username(name) || !valid_real_name(full) || !valid_password(secret)) {
    tell("Use a lowercase username and a password without a line break.");
    return;
  }
  if (secret != confirm->get_text()) {
    tell("The passwords do not match.");
    return;
  }
  const PrivResult result = session_.run({"add-user", name, full, kind->get_active_id()}, secret + "\n");
  if (!result.ok) {
    tell(result.error);
  }
  reload();
}

void UsersPage::remove_user() {
  const Account* account = selected();
  if (account == nullptr) {
    return;
  }
  if (account->name == current_user_) {
    tell("The current user cannot be deleted.");
    return;
  }
  if (is_last_administrator(account->name, accounts_)) {
    tell("The last administrator cannot be removed.");
    return;
  }
  Gtk::MessageDialog confirm(parent_, "Remove " + account->name + "?", false, Gtk::MESSAGE_QUESTION,
                             Gtk::BUTTONS_YES_NO, true);
  confirm.set_secondary_text("Cancel puts the account back. Files from the home directory are restored too.");
  if (confirm.run() != Gtk::RESPONSE_YES) {
    return;
  }
  const PrivResult result = session_.run({"remove-user", account->name});
  if (!result.ok) {
    tell(result.error);
  }
  reload();
}

void UsersPage::change_password() {
  const Account* account = selected();
  if (account == nullptr) {
    return;
  }
  const bool self = account->name == current_user_;
  Gtk::Dialog dialog("Change password", parent_, true);
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  auto* current = Gtk::manage(new Gtk::Entry());
  auto* next = Gtk::manage(new Gtk::Entry());
  auto* confirm = Gtk::manage(new Gtk::Entry());
  current->set_visibility(false);
  next->set_visibility(false);
  confirm->set_visibility(false);
  int row = 0;
  if (self) {
    grid->attach(*make_label("Current password"), 0, row, 1, 1);
    grid->attach(*current, 1, row, 1, 1);
    ++row;
  }
  grid->attach(*make_label("New password"), 0, row, 1, 1);
  grid->attach(*next, 1, row, 1, 1);
  ++row;
  grid->attach(*make_label("Confirm"), 0, row, 1, 1);
  grid->attach(*confirm, 1, row, 1, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("Change", Gtk::RESPONSE_OK);
  dialog.show_all();
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  const std::string secret = next->get_text();
  if (!valid_password(secret) || secret != confirm->get_text()) {
    tell("Enter the new password twice, without a line break.");
    return;
  }
  std::string input = secret + "\n";
  std::string mode = "other";
  if (self) {
    mode = "self";
    input = std::string(current->get_text()) + "\n" + secret + "\n";
  }
  const PrivResult result = session_.run({"set-password", account->name, mode}, input);
  if (!result.ok) {
    tell(result.error);
  }
}

void UsersPage::change_name() {
  const Account* account = selected();
  if (account == nullptr) {
    return;
  }
  std::string value = account->real_name;
  if (!ask_text(parent_, "Real name", "Name", value) || !valid_real_name(value)) {
    if (!valid_real_name(value)) {
      tell("That name cannot be stored.");
    }
    return;
  }
  const PrivResult result = session_.run({"set-real-name", account->name, value});
  if (!result.ok) {
    tell(result.error);
  }
  reload();
}

void UsersPage::change_picture() {
  const Account* account = selected();
  if (account == nullptr) {
    return;
  }
  Gtk::FileChooserDialog dialog(parent_, "Account picture", Gtk::FILE_CHOOSER_ACTION_OPEN);
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("Open", Gtk::RESPONSE_OK);
  auto filter = Gtk::FileFilter::create();
  filter->set_name("PNG and JPEG");
  filter->add_mime_type("image/png");
  filter->add_mime_type("image/jpeg");
  dialog.add_filter(filter);
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  const PrivResult result = session_.run({"set-avatar", account->name, dialog.get_filename()});
  if (!result.ok) {
    tell(result.error);
  }
  reload();
}

void UsersPage::change_type() {
  if (freeze_ > 0 || type_.get_active_id().empty()) {
    return;
  }
  const Account* account = selected();
  if (account == nullptr) {
    return;
  }
  const bool admin = type_.get_active_id() == "administrator";
  if (account->administrator == admin) {
    return;
  }
  if (!admin && is_last_administrator(account->name, accounts_)) {
    tell("The last administrator cannot be demoted.");
    reload();
    return;
  }
  const PrivResult result =
      session_.run({"set-type", account->name, admin ? "administrator" : "standard"});
  if (!result.ok) {
    tell(result.error);
  }
  reload();
}

void UsersPage::change_autologin() {
  if (freeze_ > 0 || autologin_combo_.get_active_id().empty()) {
    return;
  }
  const std::string chosen = autologin_combo_.get_active_id();
  const std::string current = autologin_.empty() ? "none" : autologin_;
  if (chosen == current) {
    return;
  }
  const PrivResult result = session_.run({"set-autologin", chosen});
  if (!result.ok) {
    tell(result.error);
  }
  reload();
}

bool UsersPage::commit_pending() { return session_.commit().ok; }

void UsersPage::revert_page() { session_.revert(); }

void UsersPage::on_page_shown() { reload(); }

}  // namespace lcos
