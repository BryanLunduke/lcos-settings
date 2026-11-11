// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_APPLICATION_HPP
#define LCOS_APPLICATION_HPP

#include "backends/xfconf_store.hpp"
#include "backends/xrandr_backend.hpp"

#include <gtkmm/application.h>

#include <string>

namespace lcos {

class MainWindow;
class DisplayMinimal;

class Application : public Gtk::Application {
 public:
  static Glib::RefPtr<Application> create();

 protected:
  Application();
  void on_startup() override;
  void on_activate() override;
  int on_command_line(const Glib::RefPtr<Gio::ApplicationCommandLine>& command_line) override;

 private:
  void present_requested();
  void open_full();

  XfconfStore store_;
  XrandrBackend* randr_ = nullptr;
  MainWindow* window_ = nullptr;
  DisplayMinimal* minimal_ = nullptr;
  bool display_minimal_ = false;
  std::string page_id_;
};

}  // namespace lcos

#endif
