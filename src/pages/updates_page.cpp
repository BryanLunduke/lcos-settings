// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/updates_page.hpp"

#include "logic/sample.hpp"
#include "logic/updates_status.hpp"
#include "ui_util.hpp"

#include <glibmm/datetime.h>
#include <glibmm/fileutils.h>

#include <fstream>

namespace lcos {
namespace {

bool program_installed(const char* name) {
  gchar* path = g_find_program_in_path(name);
  const bool found = path != nullptr;
  g_free(path);
  return found;
}

UpdateSnapshot read_status() {
  if (settings_sample()) {
    UpdateSnapshot snapshot;
    snapshot.present = true;
    snapshot.last_check = 1732665600;
    snapshot.available = 3;
    return snapshot;
  }
  const char* override_path = g_getenv("LCOS_UPDATES_STATUS");
  std::string path = override_path != nullptr ? override_path : std::string();
  if (path.empty()) {
    const char* cache = g_get_user_cache_dir();
    path = update_status_path(cache != nullptr ? cache : "");
  }
  if (path.empty()) {
    return {};
  }
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  return parse_update_status(text);
}

}  // namespace

UpdatesPage::UpdatesPage() : check_("_Check for Updates", true) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  auto* page = make_vbox(8);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  auto* body = make_vbox(8);
  page->set_margin_bottom(16);
  auto* banner = make_empty_state("software-update-available",
                                  "Check for Updates opens lcos-updates.", &status_);
  banner->set_no_show_all(true);
  body->pack_start(*banner, Gtk::PACK_SHRINK);
  auto* facts = make_vbox(4);
  checked_ = make_label("Last check: —");
  available_ = make_label("Available updates: —");
  available_->get_style_context()->add_class("lcos-strong");
  checked_->set_line_wrap(true);
  available_->set_line_wrap(true);
  checked_->get_style_context()->add_class("lcos-note");
  facts->pack_start(*available_, Gtk::PACK_SHRINK);
  facts->pack_start(*checked_, Gtk::PACK_SHRINK);
  body->pack_start(*facts, Gtk::PACK_SHRINK);
  auto* actions = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  actions->get_style_context()->add_class("lcos-actions");
  check_.set_halign(Gtk::ALIGN_START);
  actions->pack_start(check_, Gtk::PACK_SHRINK);
  body->pack_start(*actions, Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Software updates", *body), Gtk::PACK_SHRINK);
  check_.signal_clicked().connect([this] { launch_program(*this, "lcos-updates"); });
  show_all();
  refresh();
}

void UpdatesPage::refresh() {
  const bool installed = program_installed("lcos-updates") || settings_sample();
  check_.set_sensitive(installed);
  if (auto* banner = status_->get_parent()) {
    banner->set_visible(!installed);
  }
  if (!installed) {
    status_->set_text("lcos-updates is not installed.");
    available_->set_text("");
    checked_->set_text("");
    return;
  }
  const UpdateSnapshot snapshot = read_status();
  if (!snapshot.present || snapshot.last_check < 0) {
    checked_->set_text("Last check: lcos-updates has not published one.");
  } else {
    Glib::DateTime when = Glib::DateTime::create_now_local(snapshot.last_check);
    checked_->set_text("Last check: " + when.format("%e %b %Y, %H:%M"));
  }
  if (snapshot.available < 0) {
    available_->set_text("Available updates: lcos-updates has not published a count.");
  } else if (snapshot.available == 1) {
    available_->set_text("Available updates: 1");
  } else {
    available_->set_text("Available updates: " + std::to_string(snapshot.available));
  }
}

}  // namespace lcos
