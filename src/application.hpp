// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_APPLICATION_HPP
#define LCOS_APPLICATION_HPP

#include "backends/xfconf_store.hpp"

#include <gtkmm/application.h>

namespace lcos {

class Application : public Gtk::Application {
 public:
  static Glib::RefPtr<Application> create();

 protected:
  Application();
  void on_startup() override;
  void on_activate() override;

 private:
  XfconfStore store_;
};

}  // namespace lcos

#endif
