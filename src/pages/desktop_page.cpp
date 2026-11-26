// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/desktop_page.hpp"

#include "logic/backdrop.hpp"
#include "logic/display_layout.hpp"
#include "ui_util.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdkmm/display.h>
#include <gdkmm/general.h>
#include <gdkmm/pixbuf.h>
#include <gdkmm/screen.h>
#include <glibmm/fileutils.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <gtkmm/box.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/image.h>
#include <gtkmm/scrolledwindow.h>

#include <cmath>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

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

std::string human_bytes(std::uintmax_t bytes) {
  if (bytes < 1024) {
    return std::to_string(bytes) + " B";
  }
  const double kilo = static_cast<double>(bytes) / 1024.0;
  if (kilo < 1024.0) {
    return std::to_string(static_cast<int>(std::lround(kilo))) + " KB";
  }
  const double mega = kilo / 1024.0;
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(mega >= 10 ? 0 : 1);
  out << mega << " MB";
  return out.str();
}

void rounded_rect(const Cairo::RefPtr<Cairo::Context>& cr, double x, double y, double width,
                  double height, double radius) {
  const double r = std::min(radius, std::min(width, height) / 2.0);
  cr->begin_new_sub_path();
  cr->arc(x + width - r, y + r, r, -M_PI / 2.0, 0);
  cr->arc(x + width - r, y + height - r, r, 0, M_PI / 2.0);
  cr->arc(x + r, y + height - r, r, M_PI / 2.0, M_PI);
  cr->arc(x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
  cr->close_path();
}

void paint_pixbuf_rect(const Cairo::RefPtr<Cairo::Context>& cr, const Glib::RefPtr<Gdk::Pixbuf>& pix,
                       double x, double y, double width, double height) {
  if (!pix || pix->get_width() < 1 || pix->get_height() < 1 || width < 1 || height < 1) {
    return;
  }
  cr->save();
  cr->translate(x, y);
  cr->scale(width / pix->get_width(), height / pix->get_height());
  Gdk::Cairo::set_source_pixbuf(cr, pix, 0, 0);
  cr->paint();
  cr->restore();
}

}  // namespace

struct DesktopPage::WallpaperThumbs {
  struct Job {
    std::string path;
    int width = 0;
    int height = 0;
    bool preview = false;
    std::uint64_t generation = 0;
  };
  struct Result {
    std::string path;
    int width = 0;
    int height = 0;
    bool preview = false;
    std::uint64_t generation = 0;
    Glib::RefPtr<Gdk::Pixbuf> pix;
  };

  std::mutex mu;
  std::condition_variable cv;
  std::deque<Job> jobs;
  std::vector<Result> ready;
  std::map<std::string, Glib::RefPtr<Gdk::Pixbuf>> cache;
  bool stop = false;
  DesktopPage* page = nullptr;
  std::thread thread;

  static std::string key_for(const std::string& path, int width, int height) {
    return path + "\n" + std::to_string(width) + "x" + std::to_string(height);
  }
};

void DesktopPage::thumb_loop(const std::shared_ptr<WallpaperThumbs>& box) {
  for (;;) {
    WallpaperThumbs::Job job;
    Glib::RefPtr<Gdk::Pixbuf> cached;
    {
      std::unique_lock<std::mutex> lock(box->mu);
      box->cv.wait(lock, [&] { return box->stop || !box->jobs.empty(); });
      if (box->stop && box->jobs.empty()) {
        return;
      }
      job = std::move(box->jobs.front());
      box->jobs.pop_front();
      const auto found = box->cache.find(WallpaperThumbs::key_for(job.path, job.width, job.height));
      if (found != box->cache.end()) {
        cached = found->second;
      }
    }
    Glib::RefPtr<Gdk::Pixbuf> pix = cached;
    if (!pix) {
      GError* error = nullptr;
      GdkPixbuf* raw = gdk_pixbuf_new_from_file_at_scale(job.path.c_str(), job.width, job.height, TRUE, &error);
      if (error != nullptr) {
        g_error_free(error);
      }
      if (raw != nullptr) {
        pix = Glib::wrap(raw);
      }
    }
    bool schedule = false;
    {
      std::lock_guard<std::mutex> lock(box->mu);
      if (pix) {
        box->cache[WallpaperThumbs::key_for(job.path, job.width, job.height)] = pix;
      }
      if (box->page != nullptr) {
        schedule = true;
        box->ready.push_back(
            {job.path, job.width, job.height, job.preview, job.generation, pix});
      }
    }
    if (schedule) {
      auto* holder = new std::shared_ptr<WallpaperThumbs>(box);
      g_idle_add(
          [](gpointer data) -> gboolean {
            std::unique_ptr<std::shared_ptr<WallpaperThumbs>> holder(
                static_cast<std::shared_ptr<WallpaperThumbs>*>(data));
            if ((*holder)->page != nullptr) {
              (*holder)->page->apply_decoded();
            }
            return G_SOURCE_REMOVE;
          },
          holder);
    }
  }
}

DesktopPage::DesktopPage(XfconfStore& store, XrandrBackend& randr)
    : store_(store), randr_(randr) {
  pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);
  thumbs_ = std::make_shared<WallpaperThumbs>();
  thumbs_->page = this;
  thumbs_->thread = std::thread(&DesktopPage::thumb_loop, thumbs_);

  // The background page does not scroll as a whole. The left column scrolls
  // only when the window is short. The gallery scrolls on its own.
  auto* split = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  split->set_vexpand(true);
  split->set_hexpand(true);
  auto* left_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*left_scroll);
  left_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  left_scroll->set_size_request(300, -1);
  left_scroll->set_hexpand(false);
  left_scroll->set_vexpand(true);
  left_scroll->set_propagate_natural_height(false);
  auto* left = make_vbox(8);
  left->set_margin_end(2);
  left_scroll->add(*left);

  // The drawing area is the column. The monitor itself is centered in paint.
  preview_.set_hexpand(true);
  preview_.set_halign(Gtk::ALIGN_FILL);
  preview_.set_size_request(276, 186);
  preview_.signal_draw().connect([this](const Cairo::RefPtr<Cairo::Context>& context) {
    paint_preview(context);
    return true;
  });
  left->pack_start(preview_, Gtk::PACK_SHRINK);
  image_caption_ = make_label("No image selected.");
  image_caption_->set_line_wrap(false);
  image_caption_->set_ellipsize(Pango::ELLIPSIZE_END);
  image_caption_->set_max_width_chars(28);
  image_caption_->set_hexpand(true);
  image_caption_->set_halign(Gtk::ALIGN_CENTER);
  image_caption_->set_xalign(0.5);
  image_caption_->get_style_context()->add_class("lcos-dim");
  left->pack_start(*image_caption_, Gtk::PACK_SHRINK);

  workspace_.set_range(1, 1);
  workspace_.set_increments(1, 1);
  workspace_.set_digits(0);
  workspace_.set_numeric(true);
  all_workspaces_.set_no_show_all(true);
  all_workspaces_.hide();
  workspace_.set_no_show_all(true);
  workspace_.hide();

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
  auto* swatches = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  swatches->pack_start(color1_, Gtk::PACK_SHRINK);
  swatches->pack_start(color2_, Gtk::PACK_SHRINK);
  FormGrid look(108);
  auto narrow = [](Gtk::Label* label) {
    if (label == nullptr) {
      return;
    }
    label->set_size_request(84, -1);
    label->set_width_chars(11);
    label->set_max_width_chars(12);
    label->set_line_wrap(false);
  };
  narrow(look.add("Fit", style_));
  narrow(look.add("Colors", color_style_));
  narrow(look.add("Background", *swatches));
  monitor_caption_ = look.add("Monitor", monitor_);
  narrow(monitor_caption_);
  left->pack_start(look.grid(), Gtk::PACK_SHRINK);

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
  FormGrid cycle_form(108);
  cycle_form.add_control(cycle_);
  narrow(cycle_form.add("Every", cycle_period_));
  narrow(cycle_form.add("Amount", cycle_timer_));
  cycle_form.add_control(cycle_random_);
  left->pack_start(*make_frame("Slideshow", cycle_form.grid()), Gtk::PACK_SHRINK);
  split->pack_start(*left_scroll, Gtk::PACK_SHRINK);

  auto* right = make_vbox(6);
  right->set_vexpand(true);
  right->set_hexpand(true);
  folder_combo_.set_hexpand(true);
  add_picture_.set_label("Add Picture…");
  add_picture_.set_halign(Gtk::ALIGN_START);
  right->pack_start(folder_combo_, Gtk::PACK_SHRINK);
  right->pack_start(add_picture_, Gtk::PACK_SHRINK);
  images_.set_selection_mode(Gtk::SELECTION_SINGLE);
  images_.set_homogeneous(true);
  images_.set_row_spacing(8);
  images_.set_column_spacing(8);
  images_.set_min_children_per_line(3);
  images_.set_max_children_per_line(3);
  images_.set_vexpand(true);
  images_.get_style_context()->add_class("lcos-gallery");
  auto* image_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*image_scroll);
  image_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  image_scroll->set_shadow_type(Gtk::SHADOW_IN);
  image_scroll->set_vexpand(true);
  image_scroll->set_propagate_natural_height(false);
  image_scroll->set_min_content_height(0);
  image_scroll->add(images_);
  right->pack_start(*image_scroll, Gtk::PACK_EXPAND_WIDGET);
  split->pack_start(*right, Gtk::PACK_EXPAND_WIDGET);
  images_.signal_size_allocate().connect([this](Gtk::Allocation& allocation) {
    if (allocation.get_width() < 80) {
      return;
    }
    const int cell = std::max(96, (allocation.get_width() - 28) / 3);
    const int height = std::max(54, cell * 9 / 16);
    if (std::abs(cell - thumb_w_) < 16 && std::abs(height - thumb_h_) < 12) {
      return;
    }
    thumb_w_ = cell;
    thumb_h_ = height;
    for (auto* image : thumb_images_) {
      if (image != nullptr) {
        image->set_size_request(thumb_w_, thumb_h_);
      }
    }
  });
  image_scroll->get_vadjustment()->signal_value_changed().connect([this] {
    if (thumbs_ == nullptr) {
      return;
    }
    for (const auto& path : image_paths_) {
      queue_image(path, thumb_w_, thumb_h_, false);
    }
  });

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
  folder_combo_.signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    const std::string id = folder_combo_.get_active_id();
    if (id.empty()) {
      return;
    }
    if (id == "__lcos_other__") {
      choose_other_folder();
      return;
    }
    folder_path_ = id;
    folder_combo_.set_tooltip_text(folder_path_);
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
    preview_.queue_draw();
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
    preview_.queue_draw();
  });
  color1_.signal_color_set().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    const auto color = color1_.get_rgba();
    store_.set_doubles(kChannel, property("rgba1"),
                       {color.get_red(), color.get_green(), color.get_blue(), color.get_alpha()});
    preview_.queue_draw();
  });
  color2_.signal_color_set().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    const auto color = color2_.get_rgba();
    store_.set_doubles(kChannel, property("rgba2"),
                       {color.get_red(), color.get_green(), color.get_blue(), color.get_alpha()});
    preview_.queue_draw();
  });
  cycle_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    write_bool("backdrop-cycle-enable", cycle_.get_active());
    sync_cycle();
  });
  cycle_period_.signal_changed().connect([this] {
    if (freeze_ > 0 || cycle_period_.get_active_id().empty()) {
      return;
    }
    write_uint("backdrop-cycle-period",
               static_cast<unsigned>(std::stoul(std::string(cycle_period_.get_active_id()))));
    sync_cycle();
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

  add_picture_.signal_clicked().connect(sigc::mem_fun(*this, &DesktopPage::add_picture));
  images_.signal_selected_children_changed().connect([this] { mark_selection(); });
  notebook_.append_page(*split, "Background");
  build_icons();
  build_menus();

  reload_monitors();
  signal_realize().connect(sigc::mem_fun(*this, &DesktopPage::watch_monitors));
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
    folder_path_ = start;
  }
  rebuild_folders();
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
  watch_monitors();
  reload_monitors();
}

void DesktopPage::reload_monitors() {
  Freeze freeze(freeze_);
  const std::string previous = monitor_.get_active_id();
  monitor_.remove_all();
  int count = 0;
  for (const auto& output : randr_.read()) {
    ++count;
    std::string label = output.friendly.empty() ? output.name : output.friendly;
    if (label == "screen" || label == "default") {
      label = "Screen";
    }
    monitor_.append(output.name, label);
  }
  if (!previous.empty()) {
    monitor_.set_active_id(previous);
  }
  if (monitor_.get_active_row_number() < 0) {
    monitor_.set_active(0);
  }
  if (monitor_.get_active_row_number() < 0) {
    monitor_.append("default", "Screen");
    monitor_.set_active(0);
    count = 0;
  }
  sync_monitor_row(count);
  sync_preview_size();
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
  cycle_random_.set_active(store_.get_bool(kChannel, property("backdrop-cycle-random-order"), false));
  sync_cycle();

  const std::string image = store_.get_string(kChannel, property("last-image"), "");
  if (image_caption_ != nullptr) {
    image_caption_->set_text(picture_caption(image));
    image_caption_->set_tooltip_text(image);
  }
  if (image != preview_path_) {
    preview_path_ = image;
    preview_pixbuf_.reset();
    preview_orig_w_ = 0;
    preview_orig_h_ = 0;
    if (!image.empty()) {
      gdk_pixbuf_get_file_info(image.c_str(), &preview_orig_w_, &preview_orig_h_);
      queue_image(image, 480, 270, true);
    }
  }
  preview_.queue_draw();
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
  if (image_caption_ != nullptr) {
    image_caption_->set_text(picture_caption(path));
    image_caption_->set_tooltip_text(path);
  }
  if (path != preview_path_) {
    preview_path_ = path;
    preview_pixbuf_.reset();
    preview_orig_w_ = 0;
    preview_orig_h_ = 0;
    if (!path.empty()) {
      gdk_pixbuf_get_file_info(path.c_str(), &preview_orig_w_, &preview_orig_h_);
      queue_image(path, 480, 270, true);
    }
  }
  preview_.queue_draw();
}

void DesktopPage::rebuild_folders() {
  std::vector<std::string> folders;
  auto add_dir = [&](const std::string& path, bool require_image) {
    if (path.empty() || !Glib::file_test(path, Glib::FILE_TEST_IS_DIR)) {
      return;
    }
    if (std::find(folders.begin(), folders.end(), path) != folders.end()) {
      return;
    }
    if (require_image) {
      bool found = false;
      std::error_code error;
      for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
        error.clear();
        if (entry.is_regular_file(error) && is_backdrop_image(entry.path().filename().string())) {
          found = true;
          break;
        }
      }
      if (!found) {
        return;
      }
    }
    folders.push_back(path);
  };
  add_dir(folder_path_, false);
  add_dir("/usr/share/backgrounds", true);
  add_dir("/usr/share/xfce4/backdrops", true);
  add_dir("/usr/share/pixmaps", true);
  const std::string pictures = Glib::build_filename(Glib::get_home_dir(), "Pictures");
  add_dir(pictures, true);
  add_dir(Glib::build_filename(pictures, "Wallpapers"), true);
  add_dir(Glib::build_filename(pictures, "Backgrounds"), true);
  std::error_code error;
  if (Glib::file_test("/usr/share/backgrounds", Glib::FILE_TEST_IS_DIR)) {
    for (const auto& entry : std::filesystem::directory_iterator("/usr/share/backgrounds", error)) {
      error.clear();
      if (entry.is_directory(error)) {
        add_dir(entry.path().string(), true);
      }
    }
  }
  if (folders.empty()) {
    add_dir(Glib::get_home_dir(), false);
  }
  Freeze freeze(freeze_);
  folder_combo_.remove_all();
  for (const auto& path : folders) {
    const auto slash = path.find_last_of('/');
    std::string label = slash == std::string::npos ? path : path.substr(slash + 1);
    if (label.empty()) {
      label = path;
    }
    int same = 0;
    for (const auto& other : folders) {
      if (other != path && other.size() > label.size() &&
          other.compare(other.size() - label.size(), label.size(), label) == 0) {
        ++same;
      }
    }
    if (same > 0) {
      label = path;
    }
    folder_combo_.append(path, label);
  }
  folder_combo_.append("__lcos_other__", "Other folder…");
  if (!folder_path_.empty()) {
    folder_combo_.set_active_id(folder_path_);
  }
  if (folder_combo_.get_active_row_number() < 0 && !folders.empty()) {
    folder_path_ = folders.front();
    folder_combo_.set_active_id(folder_path_);
  }
  folder_combo_.set_tooltip_text(folder_path_);
}

void DesktopPage::choose_other_folder() {
  auto* parent = dynamic_cast<Gtk::Window*>(get_toplevel());
  Gtk::FileChooserDialog dialog("Background folder", Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);
  if (parent != nullptr) {
    dialog.set_transient_for(*parent);
  }
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("Select", Gtk::RESPONSE_OK);
  if (!folder_path_.empty()) {
    dialog.set_current_folder(folder_path_);
  }
  const int response = dialog.run();
  dialog.hide();
  if (response == Gtk::RESPONSE_OK) {
    const std::string chosen = dialog.get_filename();
    if (!chosen.empty()) {
      folder_path_ = chosen;
    }
  }
  rebuild_folders();
  scan_folder();
}

void DesktopPage::scan_folder() {
  Freeze freeze(freeze_);
  const std::string folder = folder_path_;
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
  image_paths_ = std::move(paths);
  ++thumb_generation_;
  thumb_images_.clear();

  std::vector<Gtk::Widget*> children = images_.get_children();
  for (auto* child : children) {
    images_.remove(*child);
  }
  auto placeholder = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, std::max(1, thumb_w_),
                                         std::max(1, thumb_h_));
  placeholder->fill(0xD5D8DCFF);
  int queued = 0;
  for (const auto& path : image_paths_) {
    auto* tile = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
    tile->set_margin_top(4);
    tile->set_margin_bottom(4);
    tile->set_margin_start(4);
    tile->set_margin_end(4);
    auto* image = Gtk::manage(new Gtk::Image(placeholder));
    image->set_size_request(thumb_w_, thumb_h_);
    image->set_halign(Gtk::ALIGN_CENTER);
    tile->pack_start(*image, Gtk::PACK_SHRINK);
    thumb_images_.push_back(image);
    const auto slash = path.find_last_of('/');
    const std::string filename = slash == std::string::npos ? path : path.substr(slash + 1);
    auto* name = make_label(filename);
    name->set_ellipsize(Pango::ELLIPSIZE_END);
    name->set_justify(Gtk::JUSTIFY_CENTER);
    name->set_hexpand(true);
    name->set_halign(Gtk::ALIGN_FILL);
    name->set_max_width_chars(14);
    name->set_tooltip_text(filename);
    name->get_style_context()->add_class("lcos-thumb-name");
    tile->pack_start(*name, Gtk::PACK_SHRINK);
    images_.add(*tile);
    if (auto* parent = tile->get_parent()) {
      parent->get_style_context()->add_class("lcos-wallpaper");
    }
    if (queued < 18) {
      queue_image(path, thumb_w_, thumb_h_, false);
      ++queued;
    }
  }
  images_.show_all();
  reload_fields();
}

DesktopPage::~DesktopPage() {
  alive_ = false;
  monitor_added_.disconnect();
  monitor_removed_.disconnect();
  monitors_changed_.disconnect();
  if (!thumbs_) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(thumbs_->mu);
    thumbs_->page = nullptr;
    thumbs_->stop = true;
  }
  thumbs_->cv.notify_all();
  if (thumbs_->thread.joinable()) {
    thumbs_->thread.join();
  }
}

std::string DesktopPage::picture_caption(const std::string& path) {
  if (path.empty()) {
    return "No image selected.";
  }
  const auto slash = path.find_last_of('/');
  const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  std::error_code error;
  const auto bytes = std::filesystem::file_size(path, error);
  if (error) {
    return name;
  }
  return name + "  ·  " + human_bytes(bytes);
}

void DesktopPage::sync_cycle() {
  const bool on = cycle_.get_active();
  cycle_period_.set_sensitive(on);
  const int period = cycle_period_.get_active_row_number();
  cycle_timer_.set_sensitive(on && period >= 0 && period <= 2);
  cycle_random_.set_sensitive(on);
}

void DesktopPage::sync_monitor_row(int count) {
  const bool show = count > 1;
  monitor_.set_visible(show);
  if (monitor_caption_ != nullptr) {
    monitor_caption_->set_visible(show);
  }
}

void DesktopPage::watch_monitors() {
  if (monitors_watched_) {
    return;
  }
  auto display = get_display();
  if (!display) {
    return;
  }
  monitors_watched_ = true;
  auto refresh = [this] {
    if (!alive_) {
      return;
    }
    reload_monitors();
    reload_fields();
  };
  monitor_added_ = display->signal_monitor_added().connect(
      [refresh](const Glib::RefPtr<Gdk::Monitor>&) { refresh(); });
  monitor_removed_ = display->signal_monitor_removed().connect(
      [refresh](const Glib::RefPtr<Gdk::Monitor>&) { refresh(); });
  if (auto screen = display->get_default_screen()) {
    monitors_changed_ = screen->signal_monitors_changed().connect(refresh);
  }
}

void DesktopPage::queue_image(const std::string& path, int width, int height, bool preview) {
  if (!thumbs_ || path.empty() || width < 1 || height < 1) {
    return;
  }
  WallpaperThumbs::Job job;
  job.path = path;
  job.width = width;
  job.height = height;
  job.preview = preview;
  job.generation = thumb_generation_;
  bool cached = false;
  {
    std::lock_guard<std::mutex> lock(thumbs_->mu);
    const std::string key = WallpaperThumbs::key_for(path, width, height);
    const auto found = thumbs_->cache.find(key);
    if (!preview && found != thumbs_->cache.end()) {
      WallpaperThumbs::Result ready;
      ready.path = path;
      ready.width = width;
      ready.height = height;
      ready.preview = false;
      ready.generation = thumb_generation_;
      ready.pix = found->second;
      thumbs_->ready.push_back(std::move(ready));
      cached = true;
    } else {
      thumbs_->jobs.push_back(std::move(job));
    }
  }
  thumbs_->cv.notify_one();
  if (cached) {
    Glib::signal_idle().connect([this] {
      if (alive_) {
        apply_decoded();
      }
      return false;
    });
  }
}

void DesktopPage::apply_decoded() {
  if (!alive_ || !thumbs_) {
    return;
  }
  std::vector<WallpaperThumbs::Result> ready;
  {
    std::lock_guard<std::mutex> lock(thumbs_->mu);
    ready.swap(thumbs_->ready);
  }
  for (const auto& item : ready) {
    if (!item.pix) {
      continue;
    }
    if (item.preview) {
      if (item.path == preview_path_) {
        preview_pixbuf_ = item.pix;
        preview_.queue_draw();
      }
      continue;
    }
    if (item.generation != thumb_generation_) {
      continue;
    }
    for (std::size_t i = 0; i < image_paths_.size() && i < thumb_images_.size(); ++i) {
      if (image_paths_[i] == item.path && thumb_images_[i] != nullptr) {
        thumb_images_[i]->set(item.pix);
      }
    }
  }
}

namespace {

void keep_thumb_text_readable(Gtk::Widget& widget) {
  if (auto* label = dynamic_cast<Gtk::Label*>(&widget)) {
    if (label->get_style_context()->has_class("lcos-thumb-name")) {
      Gdk::RGBA ink;
      ink.set_rgba(0.12, 0.12, 0.13, 1);
      const Gtk::StateFlags states[] = {Gtk::STATE_FLAG_NORMAL, Gtk::STATE_FLAG_SELECTED,
                                        Gtk::STATE_FLAG_PRELIGHT, Gtk::STATE_FLAG_ACTIVE,
                                        Gtk::STATE_FLAG_FOCUSED};
      for (const auto state : states) {
        label->override_color(ink, state);
      }
    }
  }
  if (auto* container = dynamic_cast<Gtk::Container*>(&widget)) {
    container->foreach ([&](Gtk::Widget& child) { keep_thumb_text_readable(child); });
  }
}

}  // namespace

void DesktopPage::mark_selection() {
  const auto selected = images_.get_selected_children();
  for (auto* child : images_.get_children()) {
    if (child == nullptr) {
      continue;
    }
    const bool picked = std::find(selected.begin(), selected.end(), child) != selected.end();
    if (picked) {
      child->get_style_context()->add_class("lcos-picked");
    } else {
      child->get_style_context()->remove_class("lcos-picked");
    }
    keep_thumb_text_readable(*child);
  }
}

void DesktopPage::add_picture() {
  auto* parent = dynamic_cast<Gtk::Window*>(get_toplevel());
  Gtk::FileChooserDialog dialog("Add picture", Gtk::FILE_CHOOSER_ACTION_OPEN);
  if (parent != nullptr) {
    dialog.set_transient_for(*parent);
  }
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("Add", Gtk::RESPONSE_OK);
  auto filter = Gtk::FileFilter::create();
  filter->set_name("Pictures");
  filter->add_pixbuf_formats();
  dialog.add_filter(filter);
  if (!folder_path_.empty()) {
    dialog.set_current_folder(folder_path_);
  }
  if (dialog.run() != Gtk::RESPONSE_OK) {
    dialog.hide();
    return;
  }
  const std::string chosen = dialog.get_filename();
  dialog.hide();
  if (chosen.empty()) {
    return;
  }
  const auto slash = chosen.find_last_of('/');
  if (slash != std::string::npos) {
    folder_path_ = chosen.substr(0, slash);
  }
  rebuild_folders();
  choose_image(chosen);
  scan_folder();
}

namespace {

struct PreviewBox {
  double aspect = 16.0 / 10.0;
  int widget_w = 276;
  int widget_h = 186;
  int layout_w = 1600;
  int layout_h = 1000;
};

PreviewBox preview_box_for(int width, int height) {
  PreviewBox box;
  box.aspect = preview_monitor_aspect(width, height);
  constexpr double kMaxScreenW = 248;
  constexpr double kMaxScreenH = 148;
  double screen_w = kMaxScreenW;
  double screen_h = screen_w / box.aspect;
  if (screen_h > kMaxScreenH) {
    screen_h = kMaxScreenH;
    screen_w = screen_h * box.aspect;
  }
  box.widget_w = static_cast<int>(std::ceil(screen_w + 24));
  box.widget_h = static_cast<int>(std::ceil(screen_h + 36));
  if (width > 0 && height > 0 &&
      std::fabs(static_cast<double>(width) / static_cast<double>(height) - box.aspect) < 0.02) {
    box.layout_w = width;
    box.layout_h = height;
  } else {
    box.layout_w = 1600;
    box.layout_h = std::max(1, static_cast<int>(std::lround(1600.0 / box.aspect)));
  }
  return box;
}

}  // namespace

void DesktopPage::sync_preview_size() {
  int mon_w = 0;
  int mon_h = 0;
  const std::string id = monitor_.get_active_id();
  for (const auto& output : randr_.read()) {
    if (!id.empty() && output.name != std::string(id)) {
      continue;
    }
    if (output.width > 0 && output.height > 0) {
      mon_w = output.width;
      mon_h = output.height;
    }
    if (!id.empty()) {
      break;
    }
  }
  const PreviewBox box = preview_box_for(mon_w, mon_h);
  preview_.set_size_request(box.widget_w, box.widget_h);
}

void DesktopPage::paint_preview(const Cairo::RefPtr<Cairo::Context>& cr) {
  int real_w = 0;
  int real_h = 0;
  int mon_x = 0;
  int mon_y = 0;
  const auto outputs = randr_.read();
  const std::string id = monitor_.get_active_id();
  for (const auto& output : outputs) {
    if (!id.empty() && output.name != std::string(id)) {
      continue;
    }
    if (output.width > 0 && output.height > 0) {
      real_w = output.width;
      real_h = output.height;
      mon_x = output.x;
      mon_y = output.y;
    }
    if (!id.empty()) {
      break;
    }
  }
  const PreviewBox box = preview_box_for(real_w, real_h);
  const double width = std::max(1, preview_.get_allocated_width());
  const double height = std::max(1, preview_.get_allocated_height());
  const double avail_w = std::max(40.0, width - 24);
  const double avail_h = std::max(30.0, height - 36);
  double screen_w = avail_w;
  double screen_h = screen_w / box.aspect;
  if (screen_h > avail_h) {
    screen_h = avail_h;
    screen_w = screen_h * box.aspect;
  }
  const double screen_x = std::max(10.0, (width - screen_w) / 2.0);
  const double screen_y = 12;
  cr->set_source_rgb(0.20, 0.22, 0.25);
  rounded_rect(cr, screen_x - 10, screen_y - 10, screen_w + 20, screen_h + 20, 8);
  cr->fill();
  const int colors = std::max(0, color_style_.get_active_row_number());
  const auto first = color1_.get_rgba();
  const auto second = color2_.get_rgba();
  cr->save();
  cr->rectangle(screen_x, screen_y, screen_w, screen_h);
  cr->clip();
  if (colors == 3) {
    const double cell = 8;
    for (double y = screen_y; y < screen_y + screen_h; y += cell) {
      for (double x = screen_x; x < screen_x + screen_w; x += cell) {
        const bool light = (static_cast<int>(x / cell) + static_cast<int>(y / cell)) % 2 == 0;
        cr->set_source_rgb(light ? 0.82 : 0.62, light ? 0.84 : 0.64, light ? 0.86 : 0.66);
        cr->rectangle(x, y, cell, cell);
        cr->fill();
      }
    }
  } else if (colors == 1 || colors == 2) {
    auto gradient = colors == 1 ? Cairo::LinearGradient::create(screen_x, screen_y, screen_x + screen_w, screen_y)
                                : Cairo::LinearGradient::create(screen_x, screen_y, screen_x, screen_y + screen_h);
    gradient->add_color_stop_rgba(0, first.get_red(), first.get_green(), first.get_blue(), first.get_alpha());
    gradient->add_color_stop_rgba(1, second.get_red(), second.get_green(), second.get_blue(),
                                  second.get_alpha());
    cr->set_source(gradient);
    cr->rectangle(screen_x, screen_y, screen_w, screen_h);
    cr->fill();
  } else {
    cr->set_source_rgba(first.get_red(), first.get_green(), first.get_blue(), first.get_alpha());
    cr->rectangle(screen_x, screen_y, screen_w, screen_h);
    cr->fill();
  }
  const int fit = std::max(0, style_.get_active_row_number());
  if (fit != 0 && preview_pixbuf_) {
    const int mon_w = box.layout_w;
    const int mon_h = box.layout_h;
    const int image_w = preview_orig_w_ > 0 ? preview_orig_w_ : preview_pixbuf_->get_width();
    const int image_h = preview_orig_h_ > 0 ? preview_orig_h_ : preview_pixbuf_->get_height();
    cr->save();
    cr->rectangle(screen_x, screen_y, screen_w, screen_h);
    cr->clip();
    cr->translate(screen_x, screen_y);
    cr->scale(screen_w / mon_w, screen_h / mon_h);
    if (fit == 1) {
      paint_pixbuf_rect(cr, preview_pixbuf_, (mon_w - image_w) / 2.0, (mon_h - image_h) / 2.0, image_w,
                        image_h);
    } else if (fit == 2 && image_w > 0 && image_h > 0) {
      for (double y = 0; y < mon_h; y += image_h) {
        for (double x = 0; x < mon_w; x += image_w) {
          paint_pixbuf_rect(cr, preview_pixbuf_, x, y, image_w, image_h);
        }
      }
    } else if (fit == 4 || fit == 5) {
      const double scale = fit == 4 ? std::min(mon_w / static_cast<double>(image_w),
                                               mon_h / static_cast<double>(image_h))
                                    : std::max(mon_w / static_cast<double>(image_w),
                                               mon_h / static_cast<double>(image_h));
      const double drawn_w = image_w * scale;
      const double drawn_h = image_h * scale;
      paint_pixbuf_rect(cr, preview_pixbuf_, (mon_w - drawn_w) / 2.0, (mon_h - drawn_h) / 2.0, drawn_w,
                        drawn_h);
    } else if (fit == 6) {
      const auto bounds = screen_bounds(outputs);
      const double span_w = std::max(1, bounds.width);
      const double span_h = std::max(1, bounds.height);
      paint_pixbuf_rect(cr, preview_pixbuf_, -mon_x, -mon_y, span_w, span_h);
    } else {
      paint_pixbuf_rect(cr, preview_pixbuf_, 0, 0, mon_w, mon_h);
    }
    cr->restore();
  }
  cr->restore();
  cr->set_source_rgb(0.75, 0.78, 0.82);
  cr->set_line_width(1);
  cr->rectangle(screen_x + 0.5, screen_y + 0.5, screen_w - 1, screen_h - 1);
  cr->stroke();
  cr->set_source_rgb(0.28, 0.30, 0.33);
  const double stand_w = std::min(screen_w * 0.28, 72.0);
  rounded_rect(cr, screen_x + (screen_w - stand_w) / 2.0, screen_y + screen_h + 8, stand_w, 7, 2);
  cr->fill();
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
  // LCOS stays on one workspace, so add and remove are not offered.
  winlist_add_.set_no_show_all(true);
  winlist_add_.hide();
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
