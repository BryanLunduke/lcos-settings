// SPDX-License-Identifier: GPL-3.0-or-later

#include "backends/pointer_backend.hpp"
#include "backends/pulse_client.hpp"
#include "backends/xfconf_store.hpp"
#include "backends/xrandr_backend.hpp"
#include "logic/display_layout.hpp"
#include "logic/names.hpp"
#include "logic/shortcuts.hpp"

#include <X11/Xlib.h>
#include <glib.h>
#include <sigc++/connection.h>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* file, int line, const char* text) {
  if (!condition) {
    std::cerr << file << ":" << line << "  " << text << "\n";
    ++failures;
  }
}

#define EXPECT(cond) expect((cond), __FILE__, __LINE__, #cond)

pid_t pulse_pid = -1;

bool start_pulse() {
  const char* runtime = g_getenv("XDG_RUNTIME_DIR");
  if (runtime == nullptr) {
    std::cerr << "XDG_RUNTIME_DIR is not set\n";
    return false;
  }
  const std::string dir = std::string(runtime) + "/lcos-pulse";
  g_mkdir_with_parents(dir.c_str(), 0700);
  g_setenv("PULSE_RUNTIME_PATH", dir.c_str(), TRUE);
  g_unsetenv("PULSE_SERVER");
  pulse_pid = fork();
  if (pulse_pid < 0) {
    return false;
  }
  if (pulse_pid == 0) {
    execlp("pulseaudio", "pulseaudio", "--daemonize=no", "--exit-idle-time=-1", "-n",
           "--load=module-native-protocol-unix",
           "--load=module-null-sink sink_name=lcos_test sink_properties=device.description=LCOS_Test",
           "--load=module-null-source source_name=lcos_src", nullptr);
    _exit(127);
  }
  const std::string socket = dir + "/native";
  for (int i = 0; i < 50; ++i) {
    if (g_file_test(socket.c_str(), G_FILE_TEST_EXISTS)) {
      return true;
    }
    g_usleep(100 * 1000);
  }
  std::cerr << "PulseAudio socket did not appear\n";
  return false;
}

void stop_pulse() {
  if (pulse_pid > 0) {
    kill(pulse_pid, SIGTERM);
    waitpid(pulse_pid, nullptr, 0);
    pulse_pid = -1;
  }
}

void test_xfconf(lcos::XfconfStore& store) {
  EXPECT(store.set_string("xsettings", "/Net/ThemeName", "LcosTestTheme"));
  EXPECT(store.get_string("xsettings", "/Net/ThemeName", "") == "LcosTestTheme");
  EXPECT(store.set_int("xsettings", "/Xft/DPI", 120));
  EXPECT(store.get_int("xsettings", "/Xft/DPI", -1) == 120);
  EXPECT(store.set_bool("keyboards", "/Default/KeyRepeat", false));
  EXPECT(store.get_bool("keyboards", "/Default/KeyRepeat", true) == false);
  EXPECT(store.set_double("displays", "/Default/HDMI-1/RefreshRate", 59.94));
  EXPECT(lcos::refresh_matches(store.get_double("displays", "/Default/HDMI-1/RefreshRate", 0),
                               59.94));
  EXPECT(store.set_ints("pointers", "/TestPad/Properties/libinput_Scroll_Method_Enabled",
                        {1, 0, 0}));
  const auto ints =
      store.get_ints("pointers", "/TestPad/Properties/libinput_Scroll_Method_Enabled");
  EXPECT(ints.size() == 3 && ints[0] == 1 && ints[1] == 0 && ints[2] == 0);

  EXPECT(store.set_string(lcos::kShortcutsChannel,
                          lcos::shortcut_command_path(true, "<Primary><Alt>t"),
                          "lcos-settings"));
  EXPECT(store.set_bool(lcos::kShortcutsChannel,
                        lcos::shortcut_notify_path(true, "<Primary><Alt>t"), true));
  bool saw = false;
  for (const auto& entry : store.list(lcos::kShortcutsChannel, "/commands/custom")) {
    if (entry.first.find("<Primary><Alt>t") != std::string::npos &&
        entry.second.type == lcos::PropType::String &&
        entry.second.s == "lcos-settings") {
      saw = true;
    }
  }
  EXPECT(saw);
  store.reset(lcos::kShortcutsChannel, lcos::shortcut_command_path(true, "<Primary><Alt>t"),
              true);

  const int touched = store.session_dirty() ? 1 : 0;
  EXPECT(touched == 1);
  store.revert_session();
  EXPECT(!store.session_dirty());
  EXPECT(store.get_string("xsettings", "/Net/ThemeName", "missing") != "LcosTestTheme");
}

void test_persistence(lcos::XfconfStore& store) {
  const char* config = g_getenv("XDG_CONFIG_HOME");
  EXPECT(config != nullptr);
  if (config == nullptr) {
    return;
  }
  EXPECT(store.set_string("xsettings", "/Net/ThemeName", "LcosPersistTheme"));
  const std::string path =
      std::string(config) + "/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml";
  std::string xml;
  // xfconfd flushes the channel file a few seconds after the write.
  for (int i = 0; i < 80; ++i) {
    std::ifstream in(path);
    xml.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (xml.find("LcosPersistTheme") != std::string::npos) {
      break;
    }
    g_usleep(100 * 1000);
  }
  EXPECT(xml.find("LcosPersistTheme") != std::string::npos);
}

void test_xrandr() {
  Display* display = XOpenDisplay(nullptr);
  EXPECT(display != nullptr);
  if (display == nullptr) {
    return;
  }
  lcos::XrandrBackend backend(display);
  EXPECT(backend.available());
  auto outputs = backend.read();
  EXPECT(!outputs.empty());
  if (!outputs.empty()) {
    std::cerr << "output " << outputs.front().name << " "
              << outputs.front().width << "x" << outputs.front().height << " @"
              << outputs.front().refresh << "\n";
    const std::string same = backend.apply(outputs);
    if (!same.empty()) {
      std::cerr << "apply current: " << same << "\n";
    }
    EXPECT(same.empty());
    if (outputs.front().modes.size() >= 2) {
      lcos::OutputConfig changed = outputs.front();
      const lcos::ModeInfo& other = outputs.front().modes[1];
      if (other.width != changed.width || other.height != changed.height ||
          !lcos::refresh_matches(other.refresh, changed.refresh)) {
        changed.width = other.width;
        changed.height = other.height;
        changed.refresh = other.refresh;
        std::vector<lcos::OutputConfig> layout{changed};
        const std::string switched = backend.apply(layout);
        if (!switched.empty()) {
          std::cerr << "apply switched: " << switched << "\n";
        }
        EXPECT(switched.empty());
        auto after = backend.read();
        EXPECT(!after.empty());
        if (!after.empty() && switched.empty()) {
          EXPECT(after.front().width == other.width);
          EXPECT(after.front().height == other.height);
        }
        EXPECT(backend.apply(outputs).empty());
      }
    }
  }
  lcos::PointerBackend pointers(display);
  const auto devices = pointers.list();
  EXPECT(!devices.empty());
  for (const auto& device : devices) {
    EXPECT(!device.xfconf_name.empty());
    EXPECT(device.xfconf_name.find(' ') == std::string::npos);
    EXPECT(device.xfconf_name.find('/') == std::string::npos);
  }
  XCloseDisplay(display);
}

void test_pulse() {
  EXPECT(start_pulse());
  if (pulse_pid <= 0) {
    return;
  }
  lcos::PulseClient client;
  GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
  bool ready = false;
  client.changed.connect([&] {
    if (client.state().ready && !client.state().sinks.empty()) {
      ready = true;
      g_main_loop_quit(loop);
    }
  });
  client.connect();
  g_timeout_add(8000, [](gpointer data) {
    g_main_loop_quit(static_cast<GMainLoop*>(data));
    return G_SOURCE_REMOVE;
  }, loop);
  g_main_loop_run(loop);
  EXPECT(ready);
  if (ready) {
    const auto& sink = client.state().sinks.front();
    std::cerr << "sink " << sink.name << " " << sink.description << "\n";
    client.set_sink_volume(sink.index, 42);
    client.set_sink_mute(sink.index, true);
    bool seen = false;
    sigc::connection connection = client.changed.connect([&] {
      for (const auto& item : client.state().sinks) {
        if (item.index == sink.index && item.mute && item.volume_percent == 42) {
          seen = true;
          g_main_loop_quit(loop);
        }
      }
    });
    g_timeout_add(4000, [](gpointer data) {
      g_main_loop_quit(static_cast<GMainLoop*>(data));
      return G_SOURCE_REMOVE;
    }, loop);
    g_main_loop_run(loop);
    connection.disconnect();
    EXPECT(seen);
    if (!client.state().cards.empty() && !client.state().cards.front().profiles.empty()) {
      std::cerr << "card profile " << client.state().cards.front().active_profile << "\n";
    }
    EXPECT(!client.state().sources.empty());
  }
  g_main_loop_unref(loop);
  stop_pulse();
}

}  // namespace

int main() {
  if (g_getenv("DISPLAY") == nullptr) {
    std::cout << "session tests skipped (no DISPLAY)\n";
    return 77;
  }
  lcos::XfconfStore store;
  std::string error;
  if (!store.init(&error)) {
    std::cerr << "xfconf: " << error << "\n";
    return 1;
  }
  test_persistence(store);
  test_xfconf(store);
  test_xrandr();
  if (g_getenv("LCOS_SKIP_PULSE") == nullptr) {
    test_pulse();
  }
  if (failures != 0) {
    std::cerr << failures << " session check(s) failed\n";
    stop_pulse();
    return 1;
  }
  std::cout << "session tests passed\n";
  return 0;
}
