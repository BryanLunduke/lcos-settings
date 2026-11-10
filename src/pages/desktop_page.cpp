// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/desktop_page.hpp"

#include "logic/backdrop.hpp"
#include "ui_util.hpp"

#include <gdkmm/pixbuf.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm/box.h>
#include <gtkmm/image.h>
#include <gtkmm/scrolledwindow.h>

#include <algorithm>
#include <filesystem>

namespace lcos {
namespace {

constexpr const char* kChannel = "xfce4-desktop";

std::string first_wallpaper_dir() {
  const char* candidates[] = {"/usr/share/backgrounds", "/usr/share/xfce4/backdrops",
                              "/usr/share/pixmaps"};
  for (const char* candidate : candidates) {
    if (!Glib::file_test(candidate, Glib::FILE_TEST_IS_DIR)) {
      continue;
    }
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(candidate, error)) {
      error.clear();
      if (entry.is_regular_file(error) && is_backdrop_image(entry.path().filename().string())) {
        return candidate;
      }
    }
  }
  return Glib::get_home_dir();
}

Gdk::RGBA color_from(const std::vector<double>& values, double red, double green, double blue) {
  Gdk::RGBA color;
  if (values.size() >= 3) {
    const double alpha = values.size() >= 4 ? values[3] : 1.0;
    color.set_rgba(values[0], values[1], values[2], alpha);
  } else {
    color.set_rgba(red, green, blue, 1.0);
  }
  return color;
}

}  // namespace

DesktopPage::DesktopPage(XfconfStore& store, XrandrBackend& randr)
    : store_(store),
      randr_(randr),
      folder_("Background folder", Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  auto* page = make_vbox(8);
  page->set_border_width(12);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  workspace_.set_range(1, 1);
  workspace_.set_increments(1, 1);
  workspace_.set_digits(0);
  workspace_.set_numeric(true);
  all_workspaces_.set_label("Apply to all workspaces");

  FormGrid where(180);
  where.add("Monitor", monitor_);
  where.add("Workspace", workspace_);
  where.add_control(all_workspaces_);
  page->pack_start(*make_frame("Where", where.grid()), Gtk::PACK_SHRINK);

  auto* pictures = make_vbox(8);
  FormGrid folder_form(180);
  folder_form.add("Folder", folder_);
  pictures->pack_start(folder_form.grid(), Gtk::PACK_SHRINK);
  images_.set_selection_mode(Gtk::SELECTION_SINGLE);
  images_.set_homogeneous(true);
  images_.set_min_children_per_line(4);
  images_.set_max_children_per_line(8);
  auto* image_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*image_scroll);
  image_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  image_scroll->set_shadow_type(Gtk::SHADOW_IN);
  image_scroll->add(images_);
  pictures->pack_start(*image_scroll, Gtk::PACK_SHRINK);
  image_caption_ = make_label("No image selected.");
  image_caption_->set_line_wrap(true);
  image_caption_->set_ellipsize(Pango::ELLIPSIZE_END);
  image_caption_->set_halign(Gtk::ALIGN_START);
  pictures->pack_start(*image_caption_, Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Image", *pictures), Gtk::PACK_SHRINK);

  style_.append("0", "None");
  style_.append("1", "Centered");
  style_.append("2", "Tiled");
  style_.append("3", "Stretched");
  style_.append("4", "Scaled");
  style_.append("5", "Zoomed");
  style_.append("6", "Spanning screens");
  color_style_.append("0", "Solid color");
  color_style_.append("1", "Horizontal gradient");
  color_style_.append("2", "Vertical gradient");
  color_style_.append("3", "Transparent");
  color1_.set_use_alpha(true);
  color2_.set_use_alpha(true);
  FormGrid look(180);
  look.add("Style", style_);
  look.add("Color", color_style_);
  look.add("First color", color1_);
  look.add("Second color", color2_);
  page->pack_start(*make_frame("Style", look.grid()), Gtk::PACK_SHRINK);

  cycle_.set_label("Change the background on a timer");
  cycle_random_.set_label("Random order");
  cycle_period_.append("0", "Seconds");
  cycle_period_.append("1", "Minutes");
  cycle_period_.append("2", "Hours");
  cycle_period_.append("3", "When the desktop starts");
  cycle_period_.append("4", "Hourly");
  cycle_period_.append("5", "Daily");
  cycle_period_.append("6", "Chronological");
  cycle_timer_.set_range(1, 999);
  cycle_timer_.set_increments(1, 10);
  cycle_timer_.set_digits(0);
  FormGrid cycle_form(180);
  cycle_form.add_control(cycle_);
  cycle_form.add("Every", cycle_period_);
  cycle_form.add("Amount", cycle_timer_);
  cycle_form.add_control(cycle_random_);
  page->pack_start(*make_frame("Change background", cycle_form.grid()), Gtk::PACK_SHRINK);

  monitor_.signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    reload_fields();
  });
  workspace_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (all_workspaces_.get_active()) {
      store_.set_int(kChannel, "/backdrop/single-workspace-number", workspace_index());
    }
    reload_fields();
  });
  all_workspaces_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/backdrop/single-workspace-mode", all_workspaces_.get_active());
    if (all_workspaces_.get_active()) {
      store_.set_int(kChannel, "/backdrop/single-workspace-number", workspace_index());
    }
  });
  folder_.signal_file_set().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    scan_folder();
  });
  images_.signal_selected_children_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    const auto selected = images_.get_selected_children();
    if (selected.empty() || selected.front() == nullptr) {
      return;
    }
    const int index = selected.front()->get_index();
    if (index >= 0 && index < static_cast<int>(image_paths_.size())) {
      choose_image(image_paths_[static_cast<std::size_t>(index)]);
    }
  });
  style_.signal_changed().connect([this] {
    if (freeze_ > 0 || style_.get_active_id().empty()) {
      return;
    }
    write_uint("image-style", static_cast<unsigned>(std::stoul(std::string(style_.get_active_id()))));
  });
  color_style_.signal_changed().connect([this] {
    if (freeze_ > 0 || color_style_.get_active_id().empty()) {
      return;
    }
    write_uint("color-style",
               static_cast<unsigned>(std::stoul(std::string(color_style_.get_active_id()))));
    const int style = color_style_.get_active_row_number();
    color1_.set_sensitive(style != 3);
    color2_.set_sensitive(style == 1 || style == 2);
  });
  color1_.signal_color_set().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    const auto color = color1_.get_rgba();
    store_.set_doubles(kChannel, property("rgba1"),
                       {color.get_red(), color.get_green(), color.get_blue(), color.get_alpha()});
  });
  color2_.signal_color_set().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    const auto color = color2_.get_rgba();
    store_.set_doubles(kChannel, property("rgba2"),
                       {color.get_red(), color.get_green(), color.get_blue(), color.get_alpha()});
  });
  cycle_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    write_bool("backdrop-cycle-enable", cycle_.get_active());
  });
  cycle_period_.signal_changed().connect([this] {
    if (freeze_ > 0 || cycle_period_.get_active_id().empty()) {
      return;
    }
    write_uint("backdrop-cycle-period",
               static_cast<unsigned>(std::stoul(std::string(cycle_period_.get_active_id()))));
    const int period = cycle_period_.get_active_row_number();
    cycle_timer_.set_sensitive(period >= 0 && period <= 2);
  });
  cycle_timer_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    write_uint("backdrop-cycle-timer", static_cast<unsigned>(std::max(1, cycle_timer_.get_value_as_int())));
  });
  cycle_random_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    write_bool("backdrop-cycle-random-order", cycle_random_.get_active());
  });

  reload_monitors();
  {
    Freeze freeze(freeze_);
    const std::string saved = store_.get_string(kChannel, property("last-image"), "");
    std::string start = first_wallpaper_dir();
    if (!saved.empty()) {
      const auto slash = saved.find_last_of('/');
      if (slash != std::string::npos &&
          Glib::file_test(saved.substr(0, slash), Glib::FILE_TEST_IS_DIR)) {
        start = saved.substr(0, slash);
      }
    }
    folder_.set_filename(start);
  }
  scan_folder();
  reload_fields();
}

void DesktopPage::on_page_shown() {
  Freeze freeze(freeze_);
  const int count = std::max(1, store_.get_int("xfwm4", "/general/workspace_count", 1));
  const double current = workspace_.get_value();
  workspace_.set_range(1, count);
  if (current >= 1 && current <= count) {
    workspace_.set_value(current);
  }
}

void DesktopPage::reload_monitors() {
  Freeze freeze(freeze_);
  const std::string previous = monitor_.get_active_id();
  monitor_.remove_all();
  for (const auto& output : randr_.read()) {
    const std::string label = output.friendly.empty() ? output.name : output.friendly;
    monitor_.append(output.name, label);
  }
  if (!previous.empty()) {
    monitor_.set_active_id(previous);
  }
  if (monitor_.get_active_row_number() < 0) {
    monitor_.set_active(0);
  }
  if (monitor_.get_active_row_number() < 0) {
    monitor_.append("default", "Default");
    monitor_.set_active(0);
  }
}

int DesktopPage::workspace_index() const {
  return std::max(0, workspace_.get_value_as_int() - 1);
}

std::string DesktopPage::property(const std::string& leaf) const {
  const std::string monitor = monitor_.get_active_id().empty()
                                  ? std::string("default")
                                  : std::string(monitor_.get_active_id());
  return backdrop_property(0, monitor, workspace_index(), leaf);
}

void DesktopPage::write_uint(const char* leaf, unsigned value) {
  store_.set_uint(kChannel, property(leaf), value);
}

void DesktopPage::write_bool(const char* leaf, bool value) {
  store_.set_bool(kChannel, property(leaf), value);
}

void DesktopPage::reload_fields() {
  Freeze freeze(freeze_);
  const unsigned image_style = store_.get_uint(kChannel, property("image-style"), 5);
  style_.set_active_id(std::to_string(image_style));
  if (style_.get_active_row_number() < 0) {
    style_.set_active_id("5");
  }
  const unsigned colors = store_.get_uint(kChannel, property("color-style"), 0);
  color_style_.set_active_id(std::to_string(colors));
  if (color_style_.get_active_row_number() < 0) {
    color_style_.set_active(0);
  }
  color1_.set_rgba(color_from(store_.get_doubles(kChannel, property("rgba1")), 0.18, 0.27, 0.38));
  color2_.set_rgba(color_from(store_.get_doubles(kChannel, property("rgba2")), 0.05, 0.08, 0.12));
  const int color_index = color_style_.get_active_row_number();
  color1_.set_sensitive(color_index != 3);
  color2_.set_sensitive(color_index == 1 || color_index == 2);

  all_workspaces_.set_active(store_.get_bool(kChannel, "/backdrop/single-workspace-mode", false));
  cycle_.set_active(store_.get_bool(kChannel, property("backdrop-cycle-enable"), false));
  const unsigned period = store_.get_uint(kChannel, property("backdrop-cycle-period"), 1);
  cycle_period_.set_active_id(std::to_string(period));
  if (cycle_period_.get_active_row_number() < 0) {
    cycle_period_.set_active(1);
  }
  const unsigned timer = store_.get_uint(kChannel, property("backdrop-cycle-timer"), 10);
  cycle_timer_.set_value(timer == 0 ? 10 : timer);
  cycle_timer_.set_sensitive(cycle_period_.get_active_row_number() <= 2);
  cycle_random_.set_active(store_.get_bool(kChannel, property("backdrop-cycle-random-order"), false));

  const std::string image = store_.get_string(kChannel, property("last-image"), "");
  image_caption_->set_text(image.empty() ? "No image selected." : image);
  images_.unselect_all();
  for (int i = 0; i < static_cast<int>(image_paths_.size()); ++i) {
    if (image_paths_[static_cast<std::size_t>(i)] == image) {
      if (auto* child = images_.get_child_at_index(i)) {
        images_.select_child(*child);
      }
      break;
    }
  }
}

void DesktopPage::choose_image(const std::string& path) {
  store_.set_string(kChannel, property("last-image"), path);
  image_caption_->set_text(path);
}

void DesktopPage::scan_folder() {
  Freeze freeze(freeze_);
  const std::string folder = folder_.get_filename();
  std::vector<std::string> paths;
  if (!folder.empty() && Glib::file_test(folder, Glib::FILE_TEST_IS_DIR)) {
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
      error.clear();
      if (!entry.is_regular_file(error)) {
        continue;
      }
      if (!is_backdrop_image(entry.path().filename().string())) {
        continue;
      }
      paths.push_back(entry.path().string());
    }
  }
  std::sort(paths.begin(), paths.end());
  constexpr std::size_t kLimit = 80;
  const bool truncated = paths.size() > kLimit;
  if (truncated) {
    paths.resize(kLimit);
  }
  image_paths_ = std::move(paths);

  std::vector<Gtk::Widget*> children = images_.get_children();
  for (auto* child : children) {
    images_.remove(*child);
  }
  for (const auto& path : image_paths_) {
    auto* tile = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
    tile->set_size_request(108, 96);
    Glib::RefPtr<Gdk::Pixbuf> pixbuf;
    try {
      pixbuf = Gdk::Pixbuf::create_from_file(path, 96, 72, true);
    } catch (const Glib::Error&) {
    }
    if (pixbuf) {
      tile->pack_start(*Gtk::manage(new Gtk::Image(pixbuf)), Gtk::PACK_SHRINK);
    } else {
      auto* missing = make_label("No preview");
      missing->set_size_request(96, 72);
      tile->pack_start(*missing, Gtk::PACK_SHRINK);
    }
    const auto slash = path.find_last_of('/');
    auto* name = make_label(slash == std::string::npos ? path : path.substr(slash + 1));
    name->set_ellipsize(Pango::ELLIPSIZE_END);
    name->set_max_width_chars(14);
    name->set_halign(Gtk::ALIGN_CENTER);
    tile->pack_start(*name, Gtk::PACK_SHRINK);
    images_.add(*tile);
  }
  images_.show_all();
  if (truncated && image_caption_ != nullptr) {
    image_caption_->set_text("Showing the first 80 images in this folder.");
  }
  reload_fields();
}

}  // namespace lcos
