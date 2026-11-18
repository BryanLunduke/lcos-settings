// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_SETTINGS_PAGE_HPP
#define LCOS_SETTINGS_PAGE_HPP

#include <gtk/gtk.h>

#include <gtkmm/box.h>
#include <gtkmm/container.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/sizegroup.h>
#include <sigc++/signal.h>

namespace lcos {

// One category in the right-hand stack. Most pages write xfconf as the
// controls change. Display is the exception: geometry stays local until Apply.
class SettingsPage : public Gtk::Box {
 public:
  SettingsPage() : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0) {}
  ~SettingsPage() override = default;

  virtual Glib::ustring title() const = 0;
  virtual Glib::ustring summary() const = 0;
  virtual bool has_pending() const { return false; }
  // False means a confirm dialog was reverted and the window should stay open.
  virtual bool commit_pending() { return true; }
  // Files outside xfconf (helpers.rc, mimeapps.list) restore themselves here.
  virtual void revert_page() {}
  virtual void on_page_shown() {}
  // How many edits on this page are still local (not yet written).
  virtual int unsaved_count() const { return has_pending() ? 1 : 0; }
  // One line naming the edit, for the yellow info bar. Empty when there is none.
  virtual std::string pending_detail() const { return {}; }
  // Puts the local edit back and updates the controls. False if this page
  // has nothing local to undo.
  virtual bool discard_pending() { return false; }
  // Re-reads this page after its xfconf originals were restored or kept.
  virtual void reload_from_store() {}
  // One horizontal size group so every form shares a label column.
  void align_labels();
  // Extra frames for sections that contain their own tabs.
  virtual int screenshot_count() const { return 1; }
  virtual void prepare_screenshot(int index) { (void)index; }
  virtual std::string screenshot_suffix(int index) const {
    (void)index;
    return {};
  }

  sigc::signal<void> pending_changed;

 private:
  void align_widget(Gtk::Widget& widget);

  Glib::RefPtr<Gtk::SizeGroup> label_group_;
};

inline Glib::RefPtr<Gtk::SizeGroup> shared_label_group() {
  static Glib::RefPtr<Gtk::SizeGroup> group;
  if (!group) {
    group = Gtk::SizeGroup::create(Gtk::SIZE_GROUP_HORIZONTAL);
  }
  return group;
}

inline void SettingsPage::align_labels() {
  label_group_ = shared_label_group();
  align_widget(*this);
}

inline void SettingsPage::align_widget(Gtk::Widget& widget) {
  if (auto* grid = dynamic_cast<Gtk::Grid*>(&widget)) {
    grid->foreach ([&](Gtk::Widget& child) {
      int column = 0;
      int span = 1;
      gtk_container_child_get(GTK_CONTAINER(grid->gobj()), child.gobj(), "left-attach", &column,
                              "width", &span, nullptr);
      // Notes added across both columns are not captions.
      if (column == 0 && span == 1) {
        if (auto* label = dynamic_cast<Gtk::Label*>(&child)) {
          if (label->get_text().find('\n') == Glib::ustring::npos) {
            label->set_line_wrap(true);
            label->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
            label->set_max_width_chars(24);
          }
          label->set_xalign(0);
          label_group_->add_widget(*label);
        }
      }
      align_widget(child);
    });
    return;
  }
  if (auto* container = dynamic_cast<Gtk::Container*>(&widget)) {
    container->foreach ([&](Gtk::Widget& child) { align_widget(child); });
  }
}

}  // namespace lcos

#endif
