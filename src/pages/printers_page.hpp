// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_PRINTERS_PAGE_HPP
#define LCOS_PRINTERS_PAGE_HPP

#include "backends/cups_client.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>

#include <map>
#include <string>

namespace lcos {

class PrintersPage : public SettingsPage {
 public:
  PrintersPage();
  Glib::ustring title() const override { return "Printers"; }
  Glib::ustring summary() const override { return "Queues, jobs, and a test page."; }
  bool has_pending() const override { return dirty_; }
  bool commit_pending() override;
  void revert_page() override;
  bool discard_pending() override;
  std::string pending_detail() const override;
  void on_page_shown() override;

 private:
  void refresh();
  void rebuild();
  void tell(const std::string& message);
  const PrinterRecord* selected() const;
  const PrintJob* selected_job() const;

  CupsClient cups_;
  PrinterSnapshot snapshot_;
  std::string selected_name_;
  int selected_job_ = -1;
  bool dirty_ = false;
  bool have_default_ = false;
  std::string original_default_;
  std::map<std::string, bool> original_paused_;
  int freeze_ = 0;

  Gtk::Label* status_ = nullptr;
  Gtk::ListBox printers_;
  Gtk::ListBox jobs_;
  Gtk::Button pause_;
  Gtk::Button remove_;
  Gtk::Button cancel_job_;
  Gtk::Box* body_ = nullptr;
};

}  // namespace lcos

#endif
