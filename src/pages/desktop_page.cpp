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

#include <cmath>

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
  pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  auto* page = make_vbox(8);
  page->set_border_width(0);
  scroll->add(*page);

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

  notebook_.append_page(*scroll, "Background");
  build_icons();
  build_menus();

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

void DesktopPage::prepare_screenshot(int index) {
  if (index >= 0 && index < notebook_.get_n_pages()) {
    notebook_.set_current_page(index);
  }
}

std::string DesktopPage::screenshot_suffix(int index) const {
  switch (index) {
    case 0:
      return "background";
    case 1:
      return "icons";
    case 2:
      return "menus";
    default:
      return {};
  }
}

void DesktopPage::on_page_shown() {
  load_icons();
  load_menus();
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

void DesktopPage::bind_desktop_bool(Gtk::CheckButton& check, const char* prop, bool fallback) {
  {
    Freeze freeze(freeze_);
    check.set_active(store_.get_bool(kChannel, prop, fallback));
  }
  check.signal_toggled().connect([this, &check, prop] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, prop, check.get_active());
  });
}

void DesktopPage::set_icon_sensitivity() {
  const bool icons = icon_style_.get_active_id() != "0";
  const bool files = icon_style_.get_active_id() == "2";
  icon_size_.set_sensitive(icons);
  custom_font_.set_sensitive(icons);
  font_size_.set_sensitive(icons && custom_font_.get_active());
  custom_font_color_.set_sensitive(icons);
  font_color_.set_sensitive(icons && custom_font_color_.get_active());
  custom_label_bg_.set_sensitive(icons);
  label_bg_.set_sensitive(icons && custom_label_bg_.get_active());
  show_tooltips_.set_sensitive(icons);
  tooltip_size_.set_sensitive(icons && show_tooltips_.get_active());
  gravity_.set_sensitive(icons);
  primary_display_.set_sensitive(icons);
  single_click_.set_sensitive(icons);
  single_underline_.set_sensitive(icons && single_click_.get_active());
  confirm_sort_.set_sensitive(files);
  if (file_icons_ != nullptr) {
    file_icons_->set_sensitive(files);
  }
}

void DesktopPage::build_icons() {
  icon_style_.append("0", "None");
  icon_style_.append("1", "Minimized application icons");
  icon_style_.append("2", "File/launcher icons");
  const char* gravities[] = {"Top left vertical",  "Top left horizontal",  "Top right vertical",
                             "Top right horizontal", "Bottom left vertical", "Bottom left horizontal",
                             "Bottom right vertical", "Bottom right horizontal"};
  for (int i = 0; i < 8; ++i) {
    gravity_.append(std::to_string(i), gravities[i]);
  }
  icon_size_.set_range(16, 256);
  icon_size_.set_increments(1, 8);
  icon_size_.set_digits(0);
  font_size_.set_range(4, 96);
  font_size_.set_increments(1, 4);
  font_size_.set_digits(0);
  tooltip_size_.set_range(16, 256);
  tooltip_size_.set_increments(1, 8);
  tooltip_size_.set_digits(0);
  font_color_.set_use_alpha(true);
  label_bg_.set_use_alpha(true);
  custom_font_.set_label("Use a custom font size");
  custom_font_color_.set_label("Use a custom label color");
  custom_label_bg_.set_label("Use a custom label background");
  show_tooltips_.set_label("Show tooltips");
  primary_display_.set_label("Show icons only on the primary display");
  single_click_.set_label("Single click to activate items");
  single_underline_.set_label("Underline text on hover");
  confirm_sort_.set_label("Confirm before arranging icons");
  hidden_files_.set_label("Show hidden files on the desktop");
  thumbnails_.set_label("Show thumbnails");
  delete_option_.set_label("Show the Delete option in the file menu");
  folders_first_.set_label("Arrange folders before files");
  show_home_.set_label("Home");
  show_filesystem_.set_label("Filesystem");
  show_trash_.set_label("Trash");
  show_removable_.set_label("Removable devices");
  show_fixed_.set_label("Fixed disks");
  show_device_removable_.set_label("Removable disks");
  show_network_.set_label("Network volumes");
  show_unknown_.set_label("Unknown removable devices");

  auto* page = make_vbox(8);
  page->set_border_width(0);
  FormGrid style(180);
  style.add("Icons", icon_style_);
  style.add("Icon size", icon_size_);
  style.add_control(custom_font_);
  style.add("Font size", font_size_);
  style.add_control(custom_font_color_);
  style.add("Label color", font_color_);
  style.add_control(custom_label_bg_);
  style.add("Label background", label_bg_);
  style.add_control(show_tooltips_);
  style.add("Tooltip size", tooltip_size_);
  style.add("Arrangement", gravity_);
  style.add_control(primary_display_);
  style.add_control(single_click_);
  style.add_control(single_underline_);
  page->pack_start(*make_frame("Appearance", style.grid()), Gtk::PACK_SHRINK);

  file_icons_ = make_vbox(6);
  file_icons_->pack_start(confirm_sort_, Gtk::PACK_SHRINK);
  file_icons_->pack_start(hidden_files_, Gtk::PACK_SHRINK);
  file_icons_->pack_start(thumbnails_, Gtk::PACK_SHRINK);
  file_icons_->pack_start(delete_option_, Gtk::PACK_SHRINK);
  file_icons_->pack_start(folders_first_, Gtk::PACK_SHRINK);
  file_icons_->pack_start(show_home_, Gtk::PACK_SHRINK);
  file_icons_->pack_start(show_filesystem_, Gtk::PACK_SHRINK);
  file_icons_->pack_start(show_trash_, Gtk::PACK_SHRINK);
  file_icons_->pack_start(show_removable_, Gtk::PACK_SHRINK);
  file_icons_->pack_start(show_fixed_, Gtk::PACK_SHRINK);
  file_icons_->pack_start(show_device_removable_, Gtk::PACK_SHRINK);
  file_icons_->pack_start(show_network_, Gtk::PACK_SHRINK);
  file_icons_->pack_start(show_unknown_, Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("File and launcher icons", *file_icons_), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*page), "Icons");

  icon_style_.signal_changed().connect([this] {
    if (freeze_ > 0 || icon_style_.get_active_id().empty()) {
      return;
    }
    store_.set_int(kChannel, "/desktop-icons/style", std::stoi(std::string(icon_style_.get_active_id())));
    set_icon_sensitivity();
  });
  icon_size_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_uint(kChannel, "/desktop-icons/icon-size",
                    static_cast<unsigned>(std::max(0, icon_size_.get_value_as_int())));
  });
  custom_font_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/desktop-icons/use-custom-font-size", custom_font_.get_active());
    set_icon_sensitivity();
  });
  font_size_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_double(kChannel, "/desktop-icons/font-size", font_size_.get_value());
  });
  custom_font_color_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/desktop-icons/use-custom-label-text-color",
                    custom_font_color_.get_active());
    set_icon_sensitivity();
  });
  font_color_.signal_color_set().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    const auto color = font_color_.get_rgba();
    store_.set_doubles(kChannel, "/desktop-icons/label-text-color",
                       {color.get_red(), color.get_green(), color.get_blue(), color.get_alpha()});
  });
  custom_label_bg_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/desktop-icons/use-custom-label-background-color",
                    custom_label_bg_.get_active());
    set_icon_sensitivity();
  });
  label_bg_.signal_color_set().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    const auto color = label_bg_.get_rgba();
    store_.set_doubles(kChannel, "/desktop-icons/label-background-color",
                       {color.get_red(), color.get_green(), color.get_blue(), color.get_alpha()});
  });
  show_tooltips_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/desktop-icons/show-tooltips", show_tooltips_.get_active());
    set_icon_sensitivity();
  });
  tooltip_size_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_double(kChannel, "/desktop-icons/tooltip-size", tooltip_size_.get_value());
  });
  gravity_.signal_changed().connect([this] {
    if (freeze_ > 0 || gravity_.get_active_id().empty()) {
      return;
    }
    store_.set_int(kChannel, "/desktop-icons/gravity", std::stoi(std::string(gravity_.get_active_id())));
  });
  bind_desktop_bool(primary_display_, "/desktop-icons/primary", false);
  single_click_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/desktop-icons/single-click", single_click_.get_active());
    set_icon_sensitivity();
  });
  bind_desktop_bool(single_underline_, "/desktop-icons/single-click-underline-hover", false);
  bind_desktop_bool(confirm_sort_, "/desktop-icons/confirm-sorting", true);
  bind_desktop_bool(hidden_files_, "/desktop-icons/show-hidden-files", false);
  bind_desktop_bool(thumbnails_, "/desktop-icons/show-thumbnails", true);
  bind_desktop_bool(delete_option_, "/desktop-menu/show-delete", true);
  bind_desktop_bool(folders_first_, "/desktop-icons/sort-folders-before-files", true);
  bind_desktop_bool(show_home_, "/desktop-icons/file-icons/show-home", true);
  bind_desktop_bool(show_filesystem_, "/desktop-icons/file-icons/show-filesystem", true);
  bind_desktop_bool(show_trash_, "/desktop-icons/file-icons/show-trash", true);
  bind_desktop_bool(show_removable_, "/desktop-icons/file-icons/show-removable", true);
  bind_desktop_bool(show_fixed_, "/desktop-icons/file-icons/show-device-fixed", true);
  bind_desktop_bool(show_device_removable_, "/desktop-icons/file-icons/show-device-removable", true);
  bind_desktop_bool(show_network_, "/desktop-icons/file-icons/show-network-removable", true);
  bind_desktop_bool(show_unknown_, "/desktop-icons/file-icons/show-unknown-removable", true);
  load_icons();
}

void DesktopPage::load_icons() {
  Freeze freeze(freeze_);
  const int style = store_.get_int(kChannel, "/desktop-icons/style", 2);
  icon_style_.set_active_id(std::to_string(style));
  if (icon_style_.get_active_row_number() < 0) {
    icon_style_.set_active_id("2");
  }
  icon_size_.set_value(store_.get_uint(kChannel, "/desktop-icons/icon-size", 48));
  custom_font_.set_active(store_.get_bool(kChannel, "/desktop-icons/use-custom-font-size", false));
  font_size_.set_value(store_.get_double(kChannel, "/desktop-icons/font-size", 12));
  custom_font_color_.set_active(
      store_.get_bool(kChannel, "/desktop-icons/use-custom-label-text-color", false));
  font_color_.set_rgba(
      color_from(store_.get_doubles(kChannel, "/desktop-icons/label-text-color"), 1, 1, 1));
  custom_label_bg_.set_active(
      store_.get_bool(kChannel, "/desktop-icons/use-custom-label-background-color", false));
  label_bg_.set_rgba(
      color_from(store_.get_doubles(kChannel, "/desktop-icons/label-background-color"), 0, 0, 0));
  show_tooltips_.set_active(store_.get_bool(kChannel, "/desktop-icons/show-tooltips", true));
  tooltip_size_.set_value(store_.get_double(kChannel, "/desktop-icons/tooltip-size", 64));
  const int gravity = store_.get_int(kChannel, "/desktop-icons/gravity", 0);
  gravity_.set_active_id(std::to_string(std::max(0, std::min(7, gravity))));
  primary_display_.set_active(store_.get_bool(kChannel, "/desktop-icons/primary", false));
  single_click_.set_active(store_.get_bool(kChannel, "/desktop-icons/single-click", false));
  single_underline_.set_active(
      store_.get_bool(kChannel, "/desktop-icons/single-click-underline-hover", false));
  confirm_sort_.set_active(store_.get_bool(kChannel, "/desktop-icons/confirm-sorting", true));
  hidden_files_.set_active(store_.get_bool(kChannel, "/desktop-icons/show-hidden-files", false));
  thumbnails_.set_active(store_.get_bool(kChannel, "/desktop-icons/show-thumbnails", true));
  delete_option_.set_active(store_.get_bool(kChannel, "/desktop-menu/show-delete", true));
  folders_first_.set_active(
      store_.get_bool(kChannel, "/desktop-icons/sort-folders-before-files", true));
  show_home_.set_active(store_.get_bool(kChannel, "/desktop-icons/file-icons/show-home", true));
  show_filesystem_.set_active(
      store_.get_bool(kChannel, "/desktop-icons/file-icons/show-filesystem", true));
  show_trash_.set_active(store_.get_bool(kChannel, "/desktop-icons/file-icons/show-trash", true));
  show_removable_.set_active(
      store_.get_bool(kChannel, "/desktop-icons/file-icons/show-removable", true));
  show_fixed_.set_active(store_.get_bool(kChannel, "/desktop-icons/file-icons/show-device-fixed", true));
  show_device_removable_.set_active(
      store_.get_bool(kChannel, "/desktop-icons/file-icons/show-device-removable", true));
  show_network_.set_active(
      store_.get_bool(kChannel, "/desktop-icons/file-icons/show-network-removable", true));
  show_unknown_.set_active(
      store_.get_bool(kChannel, "/desktop-icons/file-icons/show-unknown-removable", true));
  set_icon_sensitivity();
}

void DesktopPage::build_menus() {
  menu_show_.set_label("Show desktop menu on right click");
  menu_icons_.set_label("Show application icons in the menu");
  winlist_show_.set_label("Show window list menu on middle click");
  winlist_icons_.set_label("Show application icons");
  winlist_all_.set_label("Show windows from all workspaces");
  winlist_sticky_.set_label("Show sticky windows only in the active workspace");
  winlist_urgent_.set_label("Show an urgent windows section");
  winlist_names_.set_label("Show workspace names");
  winlist_submenus_.set_label("Show windows in workspace submenus");
  winlist_add_.set_label("Show add and remove workspace options");

  auto* page = make_vbox(8);
  page->set_border_width(0);
  page->pack_start(menu_show_, Gtk::PACK_SHRINK);
  menu_options_ = make_vbox(6);
  menu_options_->pack_start(menu_icons_, Gtk::PACK_SHRINK);
  menu_options_->set_margin_start(18);
  page->pack_start(*menu_options_, Gtk::PACK_SHRINK);
  page->pack_start(winlist_show_, Gtk::PACK_SHRINK);
  winlist_options_ = make_vbox(6);
  winlist_options_->set_margin_start(18);
  winlist_options_->pack_start(winlist_icons_, Gtk::PACK_SHRINK);
  winlist_options_->pack_start(winlist_all_, Gtk::PACK_SHRINK);
  winlist_options_->pack_start(winlist_sticky_, Gtk::PACK_SHRINK);
  winlist_options_->pack_start(winlist_urgent_, Gtk::PACK_SHRINK);
  winlist_options_->pack_start(winlist_names_, Gtk::PACK_SHRINK);
  winlist_name_options_ = make_vbox(6);
  winlist_name_options_->set_margin_start(18);
  winlist_name_options_->pack_start(winlist_submenus_, Gtk::PACK_SHRINK);
  winlist_options_->pack_start(*winlist_name_options_, Gtk::PACK_SHRINK);
  winlist_options_->pack_start(winlist_add_, Gtk::PACK_SHRINK);
  page->pack_start(*winlist_options_, Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*page), "Menus");

  menu_show_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/desktop-menu/show", menu_show_.get_active());
    if (menu_options_ != nullptr) {
      menu_options_->set_sensitive(menu_show_.get_active());
    }
  });
  bind_desktop_bool(menu_icons_, "/desktop-menu/show-icons", true);
  winlist_show_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/windowlist-menu/show", winlist_show_.get_active());
    if (winlist_options_ != nullptr) {
      winlist_options_->set_sensitive(winlist_show_.get_active());
    }
  });
  bind_desktop_bool(winlist_icons_, "/windowlist-menu/show-icons", true);
  bind_desktop_bool(winlist_all_, "/windowlist-menu/show-all-workspaces", false);
  bind_desktop_bool(winlist_sticky_, "/windowlist-menu/show-sticky-once", false);
  bind_desktop_bool(winlist_urgent_, "/windowlist-menu/show-urgent-windows-section", true);
  winlist_names_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/windowlist-menu/show-workspace-names", winlist_names_.get_active());
    if (winlist_name_options_ != nullptr) {
      winlist_name_options_->set_sensitive(winlist_names_.get_active());
    }
  });
  bind_desktop_bool(winlist_submenus_, "/windowlist-menu/show-submenus", false);
  bind_desktop_bool(winlist_add_, "/windowlist-menu/show-add-remove-workspaces", false);
  load_menus();
}

void DesktopPage::load_menus() {
  Freeze freeze(freeze_);
  menu_show_.set_active(store_.get_bool(kChannel, "/desktop-menu/show", true));
  menu_icons_.set_active(store_.get_bool(kChannel, "/desktop-menu/show-icons", true));
  winlist_show_.set_active(store_.get_bool(kChannel, "/windowlist-menu/show", true));
  winlist_icons_.set_active(store_.get_bool(kChannel, "/windowlist-menu/show-icons", true));
  winlist_all_.set_active(store_.get_bool(kChannel, "/windowlist-menu/show-all-workspaces", false));
  winlist_sticky_.set_active(store_.get_bool(kChannel, "/windowlist-menu/show-sticky-once", false));
  winlist_urgent_.set_active(
      store_.get_bool(kChannel, "/windowlist-menu/show-urgent-windows-section", true));
  winlist_names_.set_active(store_.get_bool(kChannel, "/windowlist-menu/show-workspace-names", true));
  winlist_submenus_.set_active(store_.get_bool(kChannel, "/windowlist-menu/show-submenus", false));
  winlist_add_.set_active(
      store_.get_bool(kChannel, "/windowlist-menu/show-add-remove-workspaces", false));
  if (menu_options_ != nullptr) {
    menu_options_->set_sensitive(menu_show_.get_active());
  }
  if (winlist_options_ != nullptr) {
    winlist_options_->set_sensitive(winlist_show_.get_active());
  }
  if (winlist_name_options_ != nullptr) {
    winlist_name_options_->set_sensitive(winlist_names_.get_active());
  }
}

}  // namespace lcos
