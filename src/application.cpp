// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.hpp"

#include "main_window.hpp"

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
      ".lcos-sidebar { background-color: #efeae0; padding-top: 6px; }"
      ".lcos-sidebar row { border-bottom: 1px solid rgba(0,0,0,0.05); }"
      ".lcos-sidebar row:selected { background-color: #1f3a5f; color: #f7f4ea; }"
      ".lcos-sidebar row:selected label { color: #f7f4ea; }"
      ".lcos-title { font-size: 18px; font-weight: 700; }"
      ".lcos-subtitle { color: #5c564c; margin-bottom: 4px; }"
      "notebook header tabs tab { padding: 6px 14px; }"
      "frame > label { font-weight: 600; }"
      "scrollbar { background-color: #e6dfd4; min-width: 12px; min-height: 12px; }"
      "scrollbar slider { background-color: #5c564c; min-width: 8px; min-height: 28px; border-radius: 4px; margin: 2px; }");
  Gtk::StyleContext::add_provider_for_screen(screen, css,
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

}  // namespace

Glib::RefPtr<Application> Application::create() {
  return Glib::RefPtr<Application>(new Application());
}

Application::Application() : Gtk::Application(kAppId) {}

void Application::on_startup() {
  Gtk::Application::on_startup();
  Gtk::Window::set_default_icon_name(kAppId);
  install_css();
  std::string error;
  if (!store_.init(&error)) {
    g_warning("xfconf: %s", error.c_str());
  }
}

void Application::on_activate() {
  auto* window = new MainWindow(store_);
  add_window(*window);
  window->signal_hide().connect([window]() {
    Glib::signal_idle().connect([window]() {
      delete window;
      return false;
    });
  });
  window->present();
  if (const char* dir = g_getenv("LCOS_SETTINGS_DUMP_PAGES")) {
    window->dump_pages(dir);
  }
}

}  // namespace lcos
