// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.hpp"

#include "display_minimal.hpp"
#include "main_window.hpp"

#include <gdk/gdkx.h>

#include <vector>

#include <gdkmm/screen.h>
#include <glibmm/main.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/stylecontext.h>

namespace lcos {
namespace {

constexpr const char* kAppId = "org.lunduke.LcosSettings";

void install_css() {
  auto screen = Gdk::Screen::get_default();
  if (!screen) {
    return;
  }
  auto css = Gtk::CssProvider::create();
  css->load_from_data(
      ".lcos-sidebar { background-color: @theme_bg_color; color: @theme_fg_color; padding-top: 6px; }"
      ".lcos-sidebar row { border-bottom: 1px solid alpha(@theme_fg_color, 0.08); }"
      ".lcos-sidebar row label { color: @theme_fg_color; }"
      ".lcos-sidebar row:selected { background-color: @theme_selected_bg_color; }"
      ".lcos-sidebar row:selected label { color: @theme_selected_fg_color; }"
      ".lcos-title { font-size: 18px; font-weight: 700; }"
      ".lcos-subtitle { color: alpha(@theme_fg_color, 0.75); margin-bottom: 4px; }"
      "notebook header tabs tab { padding: 6px 14px; }"
      "frame > label { font-weight: 600; }");
  Gtk::StyleContext::add_provider_for_screen(screen, css,
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

}  // namespace

Glib::RefPtr<Application> Application::create() {
  return Glib::RefPtr<Application>(new Application());
}

Application::Application()
    : Gtk::Application(kAppId, Gio::APPLICATION_HANDLES_COMMAND_LINE) {}

void Application::on_startup() {
  Gtk::Application::on_startup();
  Gtk::Window::set_default_icon_name(kAppId);
  install_css();
  std::string error;
  if (!store_.init(&error)) {
    g_warning("xfconf: %s", error.c_str());
  }
}

int Application::on_command_line(const Glib::RefPtr<Gio::ApplicationCommandLine>& command_line) {
  display_minimal_ = false;
  page_id_.clear();
  int argc = 0;
  char** argv = command_line->get_arguments(argc);
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) {
    args.emplace_back(argv[i] != nullptr ? argv[i] : "");
  }
  g_strfreev(argv);
  for (std::size_t i = 1; i < args.size(); ++i) {
    const auto& arg = args[i];
    if (arg == "--display-minimal" || arg == "--minimal") {
      display_minimal_ = true;
    } else if (arg == "--page" && i + 1 < args.size()) {
      page_id_ = args[++i];
    } else if (arg.compare(0, 7, "--page=") == 0) {
      page_id_ = arg.substr(7);
    } else if (arg == "--help" || arg == "-h") {
      command_line->print(
          "Usage: lcos-settings [--page PAGE] [--display-minimal]\n"
          "PAGE is a sidebar id such as display, sound, or power.\n"
          "--display-minimal opens the hot-plug layout chooser.\n");
      return 0;
    } else if (!arg.empty() && arg[0] != '-') {
      page_id_ = arg;
    }
  }
  activate();
  return 0;
}

void Application::open_full() {
  display_minimal_ = false;
  if (page_id_.empty()) {
    page_id_ = "display";
  }
  present_requested();
}

void Application::present_requested() {
  if (window_ != nullptr) {
    if (display_minimal_ && page_id_.empty()) {
      window_->show_page("display");
    } else if (!page_id_.empty()) {
      window_->show_page(page_id_);
    }
    window_->present();
    return;
  }
  if (display_minimal_) {
    if (minimal_ != nullptr) {
      minimal_->present();
      return;
    }
    if (randr_ == nullptr) {
      randr_ = new XrandrBackend(gdk_x11_display_get_xdisplay(gdk_display_get_default()));
    }
    minimal_ = new DisplayMinimal(store_, *randr_, [this] { open_full(); });
    add_window(*minimal_);
    minimal_->signal_hide().connect([this] {
      DisplayMinimal* closing = minimal_;
      minimal_ = nullptr;
      Glib::signal_idle().connect([closing]() {
        delete closing;
        return false;
      });
    });
    minimal_->present();
    return;
  }
  if (minimal_ != nullptr) {
    minimal_->hide();
  }
  auto* window = new MainWindow(store_);
  window_ = window;
  add_window(*window);
  window->signal_hide().connect([this, window]() {
    if (window_ == window) {
      window_ = nullptr;
    }
    Glib::signal_idle().connect([window]() {
      delete window;
      return false;
    });
  });
  if (!page_id_.empty()) {
    window->show_page(page_id_);
  }
  window->present();
  if (const char* dir = g_getenv("LCOS_SETTINGS_DUMP_PAGES")) {
    window->dump_pages(dir);
  }
}

void Application::on_activate() { present_requested(); }

}  // namespace lcos
