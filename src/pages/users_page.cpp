// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/users_page.hpp"

#include "ui_util.hpp"

#include <gdkmm/general.h>
#include <gdkmm/pixbuf.h>
#include <gtkmm/box.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/scrolledwindow.h>

#include <dirent.h>
#include <pwd.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>

namespace lcos {
namespace {

std::string read_text(const std::string& path) {
  std::ifstream in(path);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct SudoGroup {
  std::string members;
  unsigned gid = 0;
};

SudoGroup sudo_group() {
  SudoGroup group;
  const std::string text = read_text("/etc/group");
  std::string line;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i != text.size() && text[i] != '\n') {
      line.push_back(text[i]);
      continue;
    }
    if (line.compare(0, 5, "sudo:") == 0) {
      std::vector<std::string> fields;
      std::string field;
      for (char c : line) {
        if (c == ':') {
          fields.push_back(field);
          field.clear();
        } else {
          field.push_back(c);
        }
      }
      fields.push_back(field);
      if (fields.size() >= 4) {
        try {
          group.gid = static_cast<unsigned>(std::stoul(fields[2]));
        } catch (const std::exception&) {
          group.gid = 0;
        }
        group.members = fields[3];
      }
      return group;
    }
    line.clear();
  }
  return group;
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
  // LightDM applies /etc/lightdm/lightdm.conf after every drop-in. 50-lcos.conf
  // is only read here; opening this page does not write it.
  std::vector<std::string> texts;
  for (const auto& root : lightdm_config_roots()) {
    if (root.directory) {
      append_conf_dir(root.path, texts);
    } else {
      texts.push_back(read_text(root.path));
    }
  }
  return effective_autologin(texts);
}

std::string initials_for(const std::string& real, const std::string& user) {
  const std::string source = real.empty() ? user : real;
  std::string letters;
  bool take = true;
  for (unsigned char c : source) {
    if (std::isspace(c)) {
      take = true;
      continue;
    }
    if (take && std::isalpha(c)) {
      letters.push_back(static_cast<char>(std::toupper(c)));
      take = false;
      if (letters.size() == 2) {
        break;
      }
    }
  }
  if (letters.empty() && !user.empty()) {
    letters.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(user.front()))));
  }
  return letters;
}

Glib::RefPtr<Gdk::Pixbuf> avatar_pixbuf(const Account& account, int size) {
  auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, size, size);
  auto cr = Cairo::Context::create(surface);
  const std::string face = account.home + "/.face";
  Glib::RefPtr<Gdk::Pixbuf> photo;
  try {
    photo = Gdk::Pixbuf::create_from_file(face, size, size, true);
  } catch (const Glib::Error&) {
  }
  cr->arc(size / 2.0, size / 2.0, size / 2.0 - 0.5, 0, 6.28318530718);
  cr->clip();
  if (photo) {
    Gdk::Cairo::set_source_pixbuf(cr, photo, 0, 0);
    cr->paint();
  } else {
    const unsigned hash = static_cast<unsigned>(std::hash<std::string>{}(account.name));
    const double hues[][6] = {
        {0.66, 0.78, 0.92, 0.36, 0.51, 0.72}, {0.94, 0.73, 0.48, 0.82, 0.51, 0.18},
        {0.66, 0.79, 0.60, 0.37, 0.57, 0.31}, {0.62, 0.55, 0.78, 0.40, 0.34, 0.62},
        {0.86, 0.55, 0.52, 0.70, 0.32, 0.30},
    };
    const double* tone = hues[hash % 5];
    auto pattern = Cairo::LinearGradient::create(0, 0, size, size);
    pattern->add_color_stop_rgb(0, tone[0], tone[1], tone[2]);
    pattern->add_color_stop_rgb(1, tone[3], tone[4], tone[5]);
    cr->set_source(pattern);
    cr->paint();
    cr->set_source_rgb(1, 1, 1);
    cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
    cr->set_font_size(size * (initials_for(account.real_name, account.name).size() > 1 ? 0.34 : 0.42));
    const std::string letters = initials_for(account.real_name, account.name);
    Cairo::TextExtents extents;
    cr->get_text_extents(letters, extents);
    cr->move_to((size - extents.width) / 2.0 - extents.x_bearing,
                (size + extents.height) / 2.0);
    cr->show_text(letters);
  }
  cr->reset_clip();
  return Gdk::Pixbuf::create(surface, 0, 0, size, size);
}

}  // namespace

UsersPage::UsersPage(PrivSession& session, Gtk::Window& parent) : session_(session), parent_(parent) {
  if (const passwd* entry = getpwuid(getuid())) {
    current_user_ = entry->pw_name != nullptr ? entry->pw_name : "";
  }
  type_.append("standard", "Standard");
  type_.append("administrator", "Administrator");

  auto* page = make_vbox(0);
  page->set_border_width(0);
  auto* columns = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 24));
  columns->set_margin_top(18);

  auto* list_col = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  list_col->set_size_request(236, -1);
  auto* list_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*list_scroll);
  list_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  list_scroll->set_shadow_type(Gtk::SHADOW_IN);
  list_scroll->get_style_context()->add_class("lcos-users");
  list_.get_style_context()->add_class("lcos-users");
  list_scroll->set_size_request(236, 262);
  list_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  list_.set_selection_mode(Gtk::SELECTION_BROWSE);
  list_scroll->add(list_);
  list_col->pack_start(*list_scroll, Gtk::PACK_SHRINK);
  auto* tools = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  tools->set_margin_top(12);
  auto* add = Gtk::manage(new Gtk::Button("Add User…"));
  remove_button_ = Gtk::manage(new Gtk::Button("Remove User"));
  remove_button_->get_style_context()->add_class("lcos-danger");
  add->set_hexpand(true);
  remove_button_->set_hexpand(true);
  tools->pack_start(*add, Gtk::PACK_EXPAND_WIDGET);
  tools->pack_start(*remove_button_, Gtk::PACK_EXPAND_WIDGET);
  list_col->pack_start(*tools, Gtk::PACK_SHRINK);
  auto* list_hint = make_label("You can't remove yourself or the last administrator.");
  list_hint->set_line_wrap(true);
  list_hint->set_margin_top(8);
  list_hint->get_style_context()->add_class("lcos-faint");
  list_col->pack_start(*list_hint, Gtk::PACK_SHRINK);
  columns->pack_start(*list_col, Gtk::PACK_SHRINK);

  auto* detail = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  auto* hero = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 18));
  picture_.set_size_request(72, 72);
  picture_.set_halign(Gtk::ALIGN_START);
  hero->pack_start(picture_, Gtk::PACK_SHRINK);
  auto* hero_text = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
  heading_name_ = Gtk::manage(new Gtk::Label());
  heading_name_->set_halign(Gtk::ALIGN_START);
  heading_name_->get_style_context()->add_class("lcos-user-name");
  heading_meta_ = Gtk::manage(new Gtk::Label());
  heading_meta_->set_halign(Gtk::ALIGN_START);
  heading_meta_->get_style_context()->add_class("lcos-dim");
  picture_button_ = Gtk::manage(new Gtk::Button("Change picture…"));
  picture_button_->set_relief(Gtk::RELIEF_NONE);
  picture_button_->set_halign(Gtk::ALIGN_START);
  picture_button_->get_style_context()->add_class("lcos-link");
  hero_text->pack_start(*heading_name_, Gtk::PACK_SHRINK);
  hero_text->pack_start(*heading_meta_, Gtk::PACK_SHRINK);
  hero_text->pack_start(*picture_button_, Gtk::PACK_SHRINK);
  hero->pack_start(*hero_text, Gtk::PACK_EXPAND_WIDGET);
  detail->pack_start(*hero, Gtk::PACK_SHRINK);

  auto* account = make_vbox(0);
  FormGrid form(112);
  name_dot_ = Gtk::manage(new Gtk::Label("●"));
  name_dot_->get_style_context()->add_class("lcos-dot");
  name_dot_->set_no_show_all(true);
  auto* name_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  name_entry_.set_hexpand(true);
  name_row->pack_start(*name_dot_, Gtk::PACK_SHRINK);
  name_row->pack_start(name_entry_, Gtk::PACK_EXPAND_WIDGET);
  form.add("Full name", *name_row);
  username_value_ = Gtk::manage(new Gtk::Label());
  username_value_->set_halign(Gtk::ALIGN_START);
  username_value_->set_hexpand(true);
  form.add("Username", *username_value_);
  form.add("Account type", type_);
  auto* password_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  auto* dots = Gtk::manage(new Gtk::Label("● ● ● ● ● ● ● ●"));
  dots->set_halign(Gtk::ALIGN_START);
  dots->get_style_context()->add_class("lcos-dim");
  auto* change_pw = Gtk::manage(new Gtk::Button("Change…"));
  password_row->pack_start(*dots, Gtk::PACK_SHRINK);
  password_row->pack_end(*change_pw, Gtk::PACK_SHRINK);
  form.add("Password", *password_row);
  account->pack_start(form.grid(), Gtk::PACK_SHRINK);
  detail->pack_start(*make_frame("Account", *account), Gtk::PACK_SHRINK);

  auto* login = make_vbox(0);
  FormGrid login_form(112);
  auto* login_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  autologin_switch_.set_valign(Gtk::ALIGN_CENTER);
  autologin_switch_.set_size_request(48, 22);
  autologin_hint_ = Gtk::manage(new Gtk::Label());
  autologin_hint_->set_halign(Gtk::ALIGN_START);
  autologin_hint_->set_line_wrap(true);
  autologin_hint_->set_hexpand(true);
  autologin_hint_->get_style_context()->add_class("lcos-faint");
  login_row->pack_start(autologin_switch_, Gtk::PACK_SHRINK);
  login_row->pack_start(*autologin_hint_, Gtk::PACK_EXPAND_WIDGET);
  login_form.add("Automatic login", *login_row);
  login->pack_start(login_form.grid(), Gtk::PACK_SHRINK);
  detail->pack_start(*make_frame("Login", *login), Gtk::PACK_SHRINK);

  columns->pack_start(*detail, Gtk::PACK_EXPAND_WIDGET);
  page->pack_start(*columns, Gtk::PACK_SHRINK);
  auto* note = make_label(
      "Pictures are stored as ~/.face because LCOS does not ship AccountsService.");
  note->set_line_wrap(true);
  note->set_margin_top(12);
  note->get_style_context()->add_class("lcos-faint");
  page->pack_start(*note, Gtk::PACK_SHRINK);
  auto* page_scroll = make_page_scroll(*page);
  page_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  pack_start(*page_scroll, Gtk::PACK_EXPAND_WIDGET);

  add->signal_clicked().connect(sigc::mem_fun(*this, &UsersPage::add_user));
  remove_button_->signal_clicked().connect(sigc::mem_fun(*this, &UsersPage::remove_user));
  change_pw->signal_clicked().connect(sigc::mem_fun(*this, &UsersPage::change_password));
  picture_button_->signal_clicked().connect(sigc::mem_fun(*this, &UsersPage::change_picture));
  list_.signal_row_selected().connect([this](Gtk::ListBoxRow*) { show_detail(); });
  type_.signal_changed().connect(sigc::mem_fun(*this, &UsersPage::change_type));
  autologin_switch_.property_active().signal_changed().connect(
      sigc::mem_fun(*this, &UsersPage::change_autologin));
  name_entry_.signal_changed().connect(sigc::mem_fun(*this, &UsersPage::on_name_changed));
  reload();
}

void UsersPage::tell(const std::string& message) {
  std::string primary = message;
  std::string secondary;
  const auto nl = message.find('\n');
  if (nl != std::string::npos) {
    primary = message.substr(0, nl);
    secondary = message.substr(nl + 1);
  }
  Gtk::MessageDialog dialog(parent_, primary, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
  if (!secondary.empty()) {
    dialog.set_secondary_text(secondary);
  }
  dialog.run();
}

std::string UsersPage::selected_name() const {
  auto* row = list_.get_selected_row();
  if (row == nullptr) {
    return {};
  }
  for (const auto& item : rows_) {
    if (item.first == row) {
      return item.second;
    }
  }
  return {};
}

const Account* UsersPage::find_account(const std::string& name) const {
  for (const auto& account : accounts_) {
    if (account.name == name) {
      return &account;
    }
  }
  return nullptr;
}

const Account* UsersPage::selected() const { return find_account(selected_name()); }

std::string UsersPage::pending_detail() const {
  std::string text;
  for (const auto& item : staged_names_) {
    const Account* account = find_account(item.first);
    if (account == nullptr || item.second == account->real_name) {
      continue;
    }
    if (!text.empty()) {
      text.push_back('\n');
    }
    text += "Full name for " + account->name + ": \"" + account->real_name + "\" → \"" +
            item.second + "\"";
  }
  return text;
}

bool UsersPage::commit_pending() {
  if (staged_names_.empty()) {
    return true;
  }
  std::map<std::string, std::string> pending = staged_names_;
  for (const auto& item : pending) {
    if (!valid_real_name(item.second)) {
      tell("That name cannot be stored.");
      return false;
    }
    const PrivResult result = session_.run({"set-real-name", item.first, item.second});
    if (!result.ok) {
      tell(result.error.empty() ? "The name was not changed." : result.error);
      reload();
      return false;
    }
    staged_names_.erase(item.first);
  }
  reload();
  pending_changed.emit();
  return true;
}

bool UsersPage::discard_pending() {
  if (staged_names_.empty()) {
    return false;
  }
  staged_names_.clear();
  show_detail();
  refresh_row_dots();
  pending_changed.emit();
  return true;
}

void UsersPage::revert_page() {
  staged_names_.clear();
  reload();
}

void UsersPage::reload() {
  const std::string keep = selected_name();
  const SudoGroup sudo = sudo_group();
  accounts_ = human_accounts(read_text("/etc/passwd"), sudo.members, 1000, sudo.gid);
  autologin_ = autologin_now();
  for (auto it = staged_names_.begin(); it != staged_names_.end();) {
    const Account* account = find_account(it->first);
    if (account == nullptr || it->second == account->real_name) {
      it = staged_names_.erase(it);
    } else {
      ++it;
    }
  }
  Freeze freeze(freeze_);
  std::vector<Gtk::Widget*> old;
  list_.foreach ([&](Gtk::Widget& child) { old.push_back(&child); });
  for (auto* child : old) {
    list_.remove(*child);
    delete child;
  }
  rows_.clear();
  Gtk::ListBoxRow* select = nullptr;
  for (const auto& account : accounts_) {
    auto* row = Gtk::manage(new Gtk::ListBoxRow());
    auto* line = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
    line->set_margin_top(6);
    line->set_margin_bottom(6);
    line->set_margin_start(8);
    line->set_margin_end(8);
    auto* image = Gtk::manage(new Gtk::Image(avatar_pixbuf(account, 36)));
    auto* text = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 1));
    const std::string shown = staged_names_.count(account.name) ? staged_names_.at(account.name)
                                                                : account.real_name;
    auto* name = Gtk::manage(new Gtk::Label(shown.empty() ? account.name : shown));
    name->set_halign(Gtk::ALIGN_START);
    name->get_style_context()->add_class("lcos-strong");
    std::string kind = account.administrator ? "Administrator" : "Standard";
    if (account.name == current_user_) {
      kind += " · You";
    }
    auto* sub = Gtk::manage(new Gtk::Label(kind));
    sub->set_halign(Gtk::ALIGN_START);
    sub->get_style_context()->add_class("lcos-row-sub");
    text->pack_start(*name, Gtk::PACK_SHRINK);
    text->pack_start(*sub, Gtk::PACK_SHRINK);
    auto* dot = Gtk::manage(new Gtk::Label("●"));
    dot->get_style_context()->add_class("lcos-dot");
    dot->set_no_show_all(true);
    dot->set_visible(staged_names_.count(account.name) != 0);
    line->pack_start(*image, Gtk::PACK_SHRINK);
    line->pack_start(*text, Gtk::PACK_EXPAND_WIDGET);
    line->pack_end(*dot, Gtk::PACK_SHRINK);
    row->add(*line);
    list_.append(*row);
    rows_.emplace_back(row, account.name);
    if (account.name == keep || (select == nullptr && account.name == current_user_)) {
      select = row;
    }
  }
  list_.show_all();
  if (select == nullptr && !rows_.empty()) {
    select = rows_.front().first;
  }
  if (select != nullptr) {
    list_.select_row(*select);
  }
  show_detail();
}

void UsersPage::refresh_row_dots() {
  const std::string current = selected_name();
  for (const auto& item : rows_) {
    const bool staged = staged_names_.count(item.second) != 0;
    std::vector<Gtk::Label*> dots;
    if (auto* box = dynamic_cast<Gtk::Box*>(item.first->get_child())) {
      box->foreach ([&](Gtk::Widget& child) {
        if (auto* label = dynamic_cast<Gtk::Label*>(&child)) {
          if (label->get_style_context()->has_class("lcos-dot")) {
            label->set_visible(staged);
          }
        }
      });
    }
    (void)current;
  }
  if (name_dot_ != nullptr) {
    name_dot_->set_visible(staged_names_.count(current) != 0);
  }
}

void UsersPage::show_detail() {
  Freeze freeze(freeze_);
  const Account* account = selected();
  const bool have = account != nullptr;
  name_entry_.set_sensitive(have);
  type_.set_sensitive(have);
  autologin_switch_.set_sensitive(have);
  if (picture_button_ != nullptr) {
    picture_button_->set_sensitive(have);
  }
  if (remove_button_ != nullptr) {
    const bool removable =
        have && account->name != current_user_ && !is_last_administrator(account->name, accounts_);
    remove_button_->set_sensitive(removable);
  }
  if (!have) {
    if (heading_name_ != nullptr) {
      heading_name_->set_text("");
    }
    return;
  }
  const auto staged = staged_names_.find(account->name);
  const std::string shown =
      staged == staged_names_.end() ? account->real_name : staged->second;
  name_entry_.set_text(shown);
  if (heading_name_ != nullptr) {
    heading_name_->set_text(shown.empty() ? account->name : shown);
  }
  if (heading_meta_ != nullptr) {
    heading_meta_->set_text(account->name + " · " + account->home);
  }
  if (username_value_ != nullptr) {
    username_value_->set_text(account->name);
  }
  type_.set_active_id(account->administrator ? "administrator" : "standard");
  autologin_switch_.set_active(!autologin_.empty() && autologin_ == account->name);
  if (autologin_hint_ != nullptr) {
    if (autologin_.empty()) {
      autologin_hint_->set_text("No account logs in automatically. Only one account can.");
    } else {
      const Account* who = find_account(autologin_);
      const std::string label = who != nullptr && !who->real_name.empty() ? who->real_name : autologin_;
      autologin_hint_->set_text("Currently: " + label + ". Only one account can log in automatically.");
    }
  }
  picture_.set(avatar_pixbuf(*account, 72));
  refresh_row_dots();
}

void UsersPage::on_name_changed() {
  if (freeze_ > 0) {
    return;
  }
  const Account* account = selected();
  if (account == nullptr) {
    return;
  }
  const std::string text = name_entry_.get_text();
  if (text == account->real_name) {
    staged_names_.erase(account->name);
  } else {
    staged_names_[account->name] = text;
  }
  if (heading_name_ != nullptr) {
    heading_name_->set_text(text.empty() ? account->name : text);
  }
  refresh_row_dots();
  pending_changed.emit();
}

void UsersPage::add_user() {
  Gtk::Dialog dialog("Add user", parent_, true);
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(6);
  grid->set_column_spacing(12);
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
  const PrivResult result =
      session_.run({"add-user", name, full, kind->get_active_id()}, secret + "\n");
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
  confirm.set_secondary_text(
      "Cancel puts the account back. The home directory stays in place either way.");
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
  grid->set_row_spacing(6);
  grid->set_column_spacing(12);
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
  if (freeze_ > 0) {
    return;
  }
  const Account* account = selected();
  if (account == nullptr) {
    return;
  }
  const bool on = autologin_switch_.get_active();
  const bool already = !autologin_.empty() && autologin_ == account->name;
  if (on == already) {
    return;
  }
  const std::string chosen = on ? account->name : "none";
  const PrivResult result = session_.run({"set-autologin", chosen});
  if (!result.ok) {
    tell(result.error);
  }
  reload();
}

bool UsersPage::automatic_for_current() const {
  return !current_user_.empty() && autologin_ == current_user_;
}

void UsersPage::set_automatic_for_current(bool on) {
  if (current_user_.empty()) {
    return;
  }
  const std::string chosen = on ? current_user_ : "none";
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

void UsersPage::stage_screenshot_edit() {
  const Account* account = selected();
  if (account == nullptr && !accounts_.empty()) {
    list_.select_row(*rows_.front().first);
  }
  const Account* chosen = selected();
  if (chosen == nullptr) {
    return;
  }
  std::string next = chosen->real_name.empty() ? std::string("Preview") : chosen->real_name + " Q.";
  if (!valid_real_name(next)) {
    next = chosen->real_name + " Q";
  }
  name_entry_.set_text(next);
}

void UsersPage::on_page_shown() { reload(); }

}  // namespace lcos
