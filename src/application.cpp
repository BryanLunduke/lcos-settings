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
      ".lcos-sidebar { background-color: #e4e2de; "
      "background-image: linear-gradient(to right, #e6e4e1, #e3e1dd); "
      "color: #24221f; border-right: 1px solid #c4bfb9; "
      "box-shadow: inset -1px 0 0 alpha(#fff, 0.55); padding: 10px 6px 2px 12px; }"
      ".lcos-sidebar scrolledwindow, .lcos-sidebar viewport, .lcos-sidebar list { "
      "background-image: none; background-color: transparent; border-style: none; color: #24221f; }"
      ".lcos-sidebar row { background-color: transparent; background-image: none; border-image: none; "
      "border-style: none; border-width: 0; border-radius: 7px; min-height: 28px; "
      "margin: 0 6px 0 0; padding: 0; }"
      "entry.lcos-search { min-height: 28px; margin: 0 6px 6px 0; color: #1a1a1a; "
      "background-color: #ffffff; background-image: none; border-image: none; "
      "border: 1px solid #a29d97; border-top-color: #8f8a84; border-radius: 4px; "
      "box-shadow: inset 0 1px 2px alpha(#000, 0.07); }"
      "entry.lcos-search placeholder { color: #9a958e; }"
      ".lcos-sidebar row label { color: #24221f; }"
      ".lcos-sidebar row.lcos-nav:hover:not(:selected) { background-color: alpha(#86abd9, 0.22); }"
      ".lcos-sidebar row.lcos-nav:selected { border-image: none; border-width: 0; "
      "background-image: linear-gradient(to bottom, #b0cbec, #93b5e0 55%, #86abd9); "
      "border-radius: 7px; color: #ffffff; "
      "box-shadow: inset 0 0 0 1px #6f97c9, inset 0 1px 0 alpha(#fff, 0.45); }"
      ".lcos-sidebar row.lcos-nav:selected label { color: #ffffff; }"
      ".lcos-sidebar row.lcos-section { min-height: 22px; background-image: none; "
      "background-color: transparent; border-style: none; "
      "margin: 0; padding: 0; border-radius: 0; box-shadow: none; }"
      ".lcos-sidebar row.lcos-section label { color: #7c776f; font-weight: 700; font-size: 12.5px; }"
      ".lcos-title { font-size: 19px; font-weight: 700; color: #1c1b19; }"
      ".lcos-subtitle { color: #67635d; font-size: 12.5px; }"
      ".lcos-group-title { font-weight: 700; font-size: 12.5px; color: #26241f; }"
      "separator.lcos-rule { background-color: #d2cec8; background-image: none; border: none; "
      "color: #d2cec8; min-height: 1px; }"
      ".lcos-kicker { font-weight: 700; color: #8d8982; font-size: 12.5px; }"
      ".lcos-version { font-size: 26px; font-weight: 700; color: #1d2b40; }"
      ".lcos-host { font-size: 13px; color: #2c2a26; }"
      ".lcos-fact { font-size: 11.5px; color: #3b3934; }"
      ".lcos-faint { color: #8d8982; }"
      ".lcos-dim { color: #67635d; }"
      ".lcos-strong { font-weight: 700; }"
      ".lcos-user-name { font-size: 16px; font-weight: 700; }"
      ".lcos-dialog-title { font-size: 15px; font-weight: 700; }"
      ".lcos-dot { color: #e08a2c; font-size: 12px; }"
      ".lcos-footer { background-image: linear-gradient(to bottom, #efeeed, #e9e8e6); "
      "border-top: 1px solid #cbc7c1; box-shadow: inset 0 1px 0 alpha(#fff, 0.75); "
      "min-height: 52px; padding: 0 24px; }"
      "button.lcos-action { min-width: 86px; min-height: 28px; padding: 0 14px; border-radius: 4px; "
      "border-image: none; border-style: solid; border-width: 1px; border-color: #9b968f; "
      "background-image: linear-gradient(to bottom, #fefefd, #f4f3f2 45%, #ebe9e7 55%, #e4e2df); "
      "box-shadow: inset 0 1px 0 #fff; color: #1a1a1a; }"
      "button.lcos-action.lcos-primary:not(:disabled) { color: #ffffff; font-weight: 700; "
      "border-color: #5279ad; "
      "background-image: linear-gradient(to bottom, #b6cfee, #98b9e3 45%, #85a9d9 55%, #7ea3d4); "
      "box-shadow: inset 0 1px 0 alpha(#fff, 0.55); }"
      "button.lcos-action:disabled { color: #a7a29b; "
      "background-image: linear-gradient(to bottom, #f6f5f4, #eeedeb); "
      "border-color: #bdb8b2; box-shadow: none; }"
      "box.lcos-card { background-color: transparent; background-image: none; border: none; "
      "border-image: none; padding: 18px 24px 18px 18px; }"
      ".lcos-quick { border-left: 1px solid #d2cec8; padding-left: 18px; "
      "box-shadow: -1px 0 0 alpha(#fff, 0.8); min-width: 194px; }"
      ".lcos-infobar { background-image: linear-gradient(to bottom, #fdf5d8, #f8e9b5); "
      "border: 1px solid #d6b55e; border-radius: 6px; padding: 9px 12px; }"
      ".lcos-infobar-icon { background-color: #e08a2c; color: #ffffff; font-weight: 700; "
      "border-radius: 11px; min-width: 22px; min-height: 22px; padding: 2px; }"
      ".lcos-infobar-detail { color: #6b5a2e; font-size: 11.5px; }"
      "button.lcos-primary:not(:disabled) { color: #ffffff; font-weight: 700; "
      "background-image: linear-gradient(to bottom, #b6cfee, #98b9e3 45%, #85a9d9 55%, #7ea3d4); "
      "border-color: #5279ad; }"
      "button.lcos-danger:not(:disabled) { color: #ffffff; font-weight: 700; "
      "background-image: linear-gradient(to bottom, #ee8f84, #e06a5d 45%, #d3584a 55%, #cf5547); "
      "border-color: #a8382d; }"
      ".lcos-linked button { border-radius: 0; margin-left: -1px; }"
      ".lcos-linked button:first-child { border-radius: 4px 0 0 4px; margin-left: 0; }"
      ".lcos-linked button:last-child { border-radius: 0 4px 4px 0; }"
      "button.lcos-on:not(:disabled) { color: #ffffff; font-weight: 700; "
      "background-image: linear-gradient(to bottom, #7ea3d4, #93b5e0); border-color: #5279ad; }"
      "button.lcos-tcard { background-color: #ffffff; border-image: none; border-style: solid; "
      "border-width: 1px; border-color: #cdc9c3; border-radius: 8px; padding: 6px; "
      "box-shadow: 0 1px 2px alpha(#000, 0.06); }"
      "button.lcos-tcard-selected { border-color: #6f97c9; "
      "box-shadow: 0 0 0 2px alpha(#86abd9, 0.75), 0 2px 6px alpha(#325078, 0.18); }"
      ".lcos-check { background-color: #6f97cb; border-radius: 11px; min-width: 18px; "
      "min-height: 18px; }"
      "button.lcos-tile { border-radius: 7px; min-height: 80px; padding: 12px 4px 6px; "
      "border: 1px solid transparent; border-image: none; background-image: none; box-shadow: none; }"
      "button.lcos-tile:hover { background-image: linear-gradient(to bottom, #fbfbfa, #f1f0ee); "
      "border-color: #cdc9c3; box-shadow: 0 1px 2px alpha(#000, 0.07), inset 0 1px 0 #fff; }"
      ".lcos-tile-label { font-size: 11.5px; }"
      ".lcos-volume scale { min-width: 148px; min-height: 24px; padding: 2px 0; }"
      ".lcos-volume scale trough { min-height: 8px; min-width: 120px; border-radius: 4px; "
      "background-color: #d8d5d1; background-image: none; border: 1px solid #9b968f; margin: 6px 0; }"
      ".lcos-volume scale highlight { background-color: #7ea3d4; background-image: none; "
      "border-color: #5279ad; min-height: 8px; border-radius: 4px; }"
      ".lcos-volume scale slider { min-width: 16px; min-height: 16px; border-radius: 8px; "
      "background-color: white; background-image: none; border: 1px solid #8c8780; margin: -4px 0; }"
      ".lcos-link { color: #3f6597; }"
      ".lcos-row-sub { color: #8d8982; font-size: 11px; }"
      "scrolledwindow.lcos-users { border-radius: 6px; background-color: #ffffff; }"
      "list.lcos-users { background-color: #ffffff; color: #1a1a1a; }"
      "list.lcos-users row { border-radius: 5px; margin: 1px 2px; padding: 0; }"
      "list.lcos-users row:selected { border-image: none; "
      "background-image: linear-gradient(to bottom, #b0cbec, #8fb2df); color: #ffffff; "
      "box-shadow: inset 0 1px 0 alpha(#fff, 0.4); }"
      "list.lcos-users row:selected label { color: #ffffff; }"
      "list.lcos-users row:selected .lcos-row-sub { color: #eef4fb; }"
      "switch { background-image: none; background-color: #d9d6d2; border: 1px solid #8f8a84; "
      "border-radius: 4px; min-width: 48px; min-height: 22px; padding: 1px; }"
      "switch:checked { background-color: #7aa1d3; border-color: #5279ad; }"
      "switch slider { background-image: none; background-color: #f7f6f4; border: 1px solid #9b968f; "
      "border-radius: 3px; min-width: 22px; min-height: 18px; margin: 0; }"
      "button.lcos-infobar { min-height: 28px; padding: 0 14px; border-radius: 4px; border-image: none; }"
      "dialog.lcos-keep { background-color: #edeceb; border: 1px solid #35475f; }"
      "notebook header tabs tab { padding: 6px 14px; }");
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
