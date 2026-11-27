// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.hpp"

#include "display_minimal.hpp"
#include "main_window.hpp"
#include "widgets/drawn_controls.hpp"

#include <gdk/gdkx.h>
#include <glib-unix.h>

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
  css->load_from_data(".lcos-sidebar {   background-color: mix(@theme_bg_color, @theme_fg_color, 0.06);   color: @theme_fg_color;   border-right: 1px solid alpha(@theme_fg_color, 0.18);   padding: 10px 4px 2px 8px; } .lcos-sidebar scrolledwindow, .lcos-sidebar viewport, .lcos-sidebar list {   background-image: none;   background-color: transparent;   border-style: none;   color: @theme_fg_color; } .lcos-sidebar row {   background-color: transparent;   background-image: none;   border-image: none;   border-style: none;   border-width: 0;   border-radius: 7px;   min-height: 28px;   margin: 0 6px 0 0;   padding: 0; } entry.lcos-search {   min-height: 28px;   margin: 0 6px 6px 0;   color: @theme_text_color;   background-color: @theme_base_color;   background-image: none;   border-image: none;   border: 1px solid alpha(@theme_fg_color, 0.28);   border-radius: 4px; } entry.lcos-search placeholder { color: alpha(@theme_fg_color, 0.45); } .lcos-sidebar row label { color: @theme_fg_color; } .lcos-sidebar row.lcos-nav:hover:not(:selected) {   background-color: alpha(@theme_selected_bg_color, 0.22); } .lcos-sidebar row.lcos-nav:selected {   border-image: none;   border-width: 0;   background-color: @theme_selected_bg_color;   background-image: none;   border-radius: 7px;   color: @theme_selected_fg_color; } .lcos-sidebar row.lcos-nav:selected label { color: @theme_selected_fg_color; } .lcos-sidebar row.lcos-section {   min-height: 22px;   background-image: none;   background-color: transparent;   border-style: none;   margin: 0;   padding: 0;   border-radius: 0;   box-shadow: none; } .lcos-sidebar row.lcos-section label {   color: alpha(@theme_fg_color, 0.62);   font-weight: 700;   font-size: 12.5px; } .lcos-title { font-size: 19px; font-weight: 700; color: @theme_fg_color; } .lcos-subtitle { color: alpha(@theme_fg_color, 0.72); font-size: 12.5px; } .lcos-group-title { font-weight: 700; font-size: 12.5px; color: @theme_fg_color; } separator.lcos-rule {   background-color: alpha(@theme_fg_color, 0.18);   background-image: none;   border: none;   min-height: 1px; } .lcos-kicker { font-weight: 700; color: alpha(@theme_fg_color, 0.62); font-size: 12.5px; } .lcos-version { font-size: 26px; font-weight: 700; color: @theme_fg_color; } .lcos-host { font-size: 13px; color: @theme_fg_color; } .lcos-fact { font-size: 11.5px; color: @theme_fg_color; } .lcos-faint { color: alpha(@theme_fg_color, 0.62); } .lcos-dim { color: alpha(@theme_fg_color, 0.72); } .lcos-strong { font-weight: 700; color: @theme_fg_color; } .lcos-user-name { font-size: 16px; font-weight: 700; color: @theme_fg_color; } .lcos-dialog-title { font-size: 15px; font-weight: 700; color: @theme_fg_color; } .lcos-dot { color: #e08a2c; font-size: 12px; } .lcos-footer {   background-color: mix(@theme_bg_color, @theme_fg_color, 0.05);   color: @theme_fg_color;   border-top: 1px solid alpha(@theme_fg_color, 0.16);   min-height: 52px;   padding: 0 24px; } .lcos-footer label { color: @theme_fg_color; } button.lcos-action {   min-width: 86px;   min-height: 28px;   padding: 0 14px;   border-radius: 4px;   border-image: none;   border-style: solid;   border-width: 1px;   border-color: alpha(@theme_fg_color, 0.35); } button.lcos-action label { color: @theme_fg_color; } button.lcos-action.lcos-primary:not(:disabled) {   font-weight: 700;   border-color: shade(@theme_selected_bg_color, 0.75);   background-color: @theme_selected_bg_color;   background-image: none; } button.lcos-action.lcos-primary:not(:disabled) label { color: @theme_selected_fg_color; } button.lcos-action:disabled label { color: alpha(@theme_fg_color, 0.4); } box.lcos-card {   background-color: transparent;   background-image: none;   border: none;   border-image: none;   padding: 18px 24px 18px 18px;   color: @theme_fg_color; } .lcos-quick {   border-left: 1px solid alpha(@theme_fg_color, 0.18);   padding-left: 18px;   min-width: 194px;   color: @theme_fg_color; } .lcos-quick label { color: @theme_fg_color; } .lcos-infobar {   background-color: alpha(#e0b84a, 0.28);   border: 1px solid alpha(#b8860b, 0.85);   border-radius: 6px;   padding: 9px 12px;   color: @theme_fg_color; } .lcos-infobar label { color: @theme_fg_color; } .lcos-infobar-icon {   background-color: #e08a2c;   color: #ffffff;   font-weight: 700;   border-radius: 11px;   min-width: 22px;   min-height: 22px;   padding: 2px; } .lcos-infobar-detail { color: @theme_fg_color; font-size: 11.5px; } button.lcos-primary:not(:disabled) {   font-weight: 700;   background-color: @theme_selected_bg_color;   background-image: none;   border-color: shade(@theme_selected_bg_color, 0.75); } button.lcos-primary:not(:disabled) label { color: @theme_selected_fg_color; } button.lcos-danger:not(:disabled) {   font-weight: 700;   background-color: #a8382d;   background-image: none;   border-color: #7a241c; } button.lcos-danger:not(:disabled) label { color: #ffffff; } .lcos-linked button { border-radius: 0; margin-left: -1px; } .lcos-linked button:first-child { border-radius: 4px 0 0 4px; margin-left: 0; } .lcos-linked button:last-child { border-radius: 0 4px 4px 0; } button.lcos-on:not(:disabled) {   font-weight: 700;   background-color: @theme_selected_bg_color;   background-image: none;   border-color: shade(@theme_selected_bg_color, 0.75); } button.lcos-on:not(:disabled) label { color: @theme_selected_fg_color; } button.lcos-tcard {   background-color: @theme_base_color;   color: @theme_text_color;   border-image: none;   border-style: solid;   border-width: 1px;   border-color: alpha(@theme_fg_color, 0.22);   border-radius: 8px;   padding: 6px; } button.lcos-tcard label { color: @theme_text_color; } button.lcos-tcard-selected {   border-color: @theme_selected_bg_color;   box-shadow: 0 0 0 2px alpha(@theme_selected_bg_color, 0.75); } .lcos-check {   background-color: @theme_selected_bg_color;   color: @theme_selected_fg_color;   border-radius: 11px;   min-width: 18px;   min-height: 18px; } button.lcos-tile {   border-radius: 7px;   min-height: 80px;   padding: 12px 4px 6px;   border: 1px solid transparent;   border-image: none;   background-image: none;   box-shadow: none;   color: @theme_fg_color; } button.lcos-tile label { color: @theme_fg_color; } button.lcos-tile:hover {   background-color: alpha(@theme_selected_bg_color, 0.12);   border-color: alpha(@theme_fg_color, 0.2); } .lcos-tile-label { font-size: 11.5px; color: @theme_fg_color; } .lcos-volume scale { min-width: 148px; min-height: 24px; padding: 2px 0; } .lcos-link { color: @theme_selected_bg_color; } .lcos-row-sub { color: alpha(@theme_fg_color, 0.62); font-size: 11px; } scrolledwindow.lcos-users, list.lcos-users, scrolledwindow.lcos-list, scrolledwindow.lcos-list treeview.view {   background-color: @theme_base_color;   color: @theme_text_color; } list.lcos-users row { border-radius: 5px; margin: 1px 2px; padding: 0; } list.lcos-users row:selected, scrolledwindow.lcos-list treeview.view:selected, scrolledwindow.lcos-list treeview.view:selected:focus {   background-color: @theme_selected_bg_color;   color: @theme_selected_fg_color; } list.lcos-users row:selected label, scrolledwindow.lcos-list treeview.view:selected {   color: @theme_selected_fg_color; } list.lcos-users row:selected .lcos-row-sub { color: @theme_selected_fg_color; } button.lcos-infobar { min-height: 28px; padding: 0 14px; border-radius: 4px; border-image: none; } button.lcos-infobar label { color: @theme_fg_color; } notebook header tabs tab { padding: 6px 14px; } button.lcos-link-button {   background-image: none;   background-color: transparent;   border: none;   border-image: none;   box-shadow: none;   color: @theme_selected_bg_color;   padding: 0 2px;   min-width: 0;   min-height: 0;   font-weight: 400; } button.lcos-link-button label { color: @theme_selected_bg_color; } box.lcos-sections button, stackswitcher.lcos-sections button {   min-width: 0;   min-height: 26px;   padding: 2px 12px;   border-image: none;   border-style: solid;   border-width: 1px;   border-color: alpha(@theme_fg_color, 0.35);   margin-left: -1px;   border-radius: 0;   font-weight: 400;   color: @theme_fg_color; } box.lcos-sections button label, stackswitcher.lcos-sections button label { color: @theme_fg_color; } box.lcos-sections button:first-child, stackswitcher.lcos-sections button:first-child {   border-radius: 4px 0 0 4px;   margin-left: 0; } box.lcos-sections button:last-child, stackswitcher.lcos-sections button:last-child {   border-radius: 0 4px 4px 0; } box.lcos-sections button.lcos-on, stackswitcher.lcos-sections button:checked {   font-weight: 700;   border-color: shade(@theme_selected_bg_color, 0.75);   background-color: @theme_selected_bg_color;   background-image: none; } box.lcos-sections button.lcos-on label, stackswitcher.lcos-sections button:checked label {   color: @theme_selected_fg_color; } scrolledwindow.lcos-list { border: 1px solid alpha(@theme_fg_color, 0.2); border-radius: 6px; }  switch:focus, combobox:focus { outline: 2px solid @theme_selected_bg_color; outline-offset: 2px; } switch:checked:focus { outline-color: @theme_fg_color; } check, radio { margin-right: 6px; } calendar:disabled { opacity: 0.48; } calendar:disabled label, spinbutton:disabled, spinbutton:disabled entry, entry:disabled { color: alpha(@theme_text_color, 0.5); } entry:disabled, spinbutton:disabled { background-color: mix(@theme_base_color, @theme_bg_color, 0.45); background-image: none; } entry.lcos-search placeholder { color: alpha(@theme_fg_color, 0.72); }");
  Gtk::StyleContext::add_provider_for_screen(screen, css,
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  auto extra = Gtk::CssProvider::create();
  extra->load_from_data(
      ".lcos-sidebar row.lcos-nav:focus { box-shadow: inset 0 0 0 2px alpha(@theme_selected_bg_color, 0.9); }"
      "flowboxchild.lcos-wallpaper, flowboxchild.lcos-wallpaper:selected, flowboxchild.lcos-wallpaper:selected:focus {"
      "  background-color: transparent; background-image: none; box-shadow: none; outline: none; border-radius: 5px; }"
      "flowboxchild.lcos-wallpaper.lcos-picked {"
      "  background-color: alpha(@theme_selected_bg_color, 0.22); background-image: none;"
      "  box-shadow: inset 0 0 0 2px @theme_selected_bg_color; }"
      "flowboxchild.lcos-wallpaper label.lcos-thumb-name,"
      "flowboxchild.lcos-wallpaper:selected label.lcos-thumb-name,"
      "flowboxchild.lcos-wallpaper.lcos-picked label.lcos-thumb-name {"
      "  color: @theme_text_color; }"
      "scale.lcos-scale trough { margin-top: 4px; margin-bottom: 4px; margin-right: 12px; }"
      "scale.lcos-scale label { min-width: 2.6em; }"
      "label.lcos-note { font-size: 11.5px; }"
      ".lcos-empty { margin-top: 8px; margin-bottom: 8px; }"
      "box.lcos-group {"
      "  background-color: alpha(@theme_base_color, 0.55);"
      "  border: 1px solid alpha(@theme_fg_color, 0.14);"
      "  border-radius: 6px;"
      "  padding: 2px 12px 8px 4px;"
      "  margin-top: 8px;"
      "}"
      "label.lcos-badge {"
      "  font-size: 11px;"
      "  font-weight: 700;"
      "  padding: 1px 8px;"
      "  border-radius: 9px;"
      "  background-color: alpha(@theme_fg_color, 0.08);"
      "  border: 1px solid alpha(@theme_fg_color, 0.18);"
      "}"
      "label.lcos-badge-busy { color: #1d4f86; }"
      "label.lcos-badge-paused { color: #8a5a12; }"
      "box.lcos-actions { margin-top: 8px; }"
      "levelbar.lcos-meter trough {"
      "  min-height: 16px;"
      "  background-color: mix(@theme_bg_color, @theme_fg_color, 0.16);"
      "  background-image: none;"
      "  border: 1px solid alpha(@theme_fg_color, 0.28);"
      "  border-radius: 3px;"
      "}"
      "levelbar.lcos-meter block.empty {"
      "  background-color: transparent;"
      "  background-image: none;"
      "  border: none;"
      "}"
      "levelbar.lcos-meter block.filled,"
      "levelbar.lcos-meter block.filled.low,"
      "levelbar.lcos-meter block.filled.high,"
      "levelbar.lcos-meter block.filled.full {"
      "  background-color: @theme_selected_bg_color;"
      "  background-image: linear-gradient(to bottom,"
      "      shade(@theme_selected_bg_color, 1.18),"
      "      shade(@theme_selected_bg_color, 0.86));"
      "  border: 1px solid shade(@theme_selected_bg_color, 0.72);"
      "  border-radius: 2px;"
      "  min-height: 12px;"
      "}"
      "button.lcos-tile { min-height: 64px; padding: 4px 4px 2px; }"
      "box.lcos-card { padding: 10px 16px 8px 12px; }");
  Gtk::StyleContext::add_provider_for_screen(screen, extra,
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
  install_drawn_controls();
  std::string error;
  if (!store_.init(&error)) {
    g_warning("xfconf: %s", error.c_str());
  }
  g_unix_signal_add(SIGTERM, &Application::on_unix_signal, this);
  g_unix_signal_add(SIGHUP, &Application::on_unix_signal, this);
  signal_shutdown().connect([this] { rollback_live(); });
}

gboolean Application::on_unix_signal(void* data) {
  auto* app = static_cast<Application*>(data);
  if (app != nullptr) {
    app->rollback_and_quit();
  }
  return G_SOURCE_REMOVE;
}

void Application::rollback_live() {
  if (rolling_back_) {
    return;
  }
  rolling_back_ = true;
  if (window_ != nullptr) {
    window_->rollback_live_changes();
  } else {
    store_.revert_session();
  }
}

void Application::rollback_and_quit() {
  rollback_live();
  quit();
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
          "PAGE/SECTION opens that section, for example power/system or window-manager/advanced.\n"
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
