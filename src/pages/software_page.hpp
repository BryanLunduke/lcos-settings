// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_SOFTWARE_PAGE_HPP
#define LCOS_SOFTWARE_PAGE_HPP

#include "logic/mime_files.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/comboboxtext.h>
#include <gtkmm/entry.h>
#include <gtkmm/liststore.h>
#include <gtkmm/notebook.h>
#include <gtkmm/treeview.h>

#include <string>
#include <vector>

namespace lcos {

class SoftwarePage : public SettingsPage {
 public:
  SoftwarePage();

  Glib::ustring title() const override { return "Default Software"; }
  Glib::ustring summary() const override {
    return "Preferred helpers, and the default application for each file type.";
  }
  void revert_page() override;
  int screenshot_count() const override { return 2; }
  void prepare_screenshot(int index) override;
  std::string screenshot_suffix(int index) const override;

 private:
  struct MimeRow {
    std::string type;
    std::string desktop_id;
    std::string app_name;
  };

  void load_helpers();
  void load_applications();
  void rebuild_mime_rows();
  void refill_mime_view();
  void save_files();
  void choose_mime_default();
  std::string app_name(const std::string& desktop_id) const;

  Gtk::Notebook notebook_;
  int freeze_ = 0;
  std::string helpers_path_;
  std::string mime_path_;
  std::string helpers_original_;
  std::string mime_original_;
  std::string helpers_text_;
  std::string mime_text_;
  bool helpers_existed_ = false;
  bool mime_existed_ = false;

  Gtk::ComboBoxText browser_;
  Gtk::ComboBoxText mail_;
  Gtk::ComboBoxText files_;
  Gtk::ComboBoxText terminal_;
  Gtk::Entry filter_;
  Glib::RefPtr<Gtk::ListStore> mime_store_;
  Gtk::TreeView mime_view_;
  std::vector<DesktopFile> helpers_;
  std::vector<DesktopFile> applications_;
  std::vector<std::string> web_mimes_;
  std::vector<std::string> mail_mimes_;
  std::vector<std::string> file_mimes_;
  std::vector<MimeRow> mime_rows_;
};

}  // namespace lcos

#endif
