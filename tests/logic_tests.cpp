// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/atomic_file.hpp"
#include "logic/backdrop.hpp"
#include "logic/confirm.hpp"
#include "logic/display_layout.hpp"
#include "logic/helper_status.hpp"
#include "logic/mime_files.hpp"
#include "logic/names.hpp"
#include "logic/power.hpp"
#include "logic/shortcuts.hpp"
#include "logic/sound_edit.hpp"
#include "logic/system_model.hpp"
#include "logic/text.hpp"
#include "logic/themes.hpp"
#include "logic/volume.hpp"
#include "logic/wm_shortcuts.hpp"
#include "logic/xkb_rules.hpp"

#include <glib.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, const char* file, int line, const char* text) {
  if (!condition) {
    std::cerr << file << ":" << line << "  " << text << "\n";
    ++failures;
  }
}

#define EXPECT(cond) expect((cond), __FILE__, __LINE__, #cond)

std::string fixture(const char* name) {
  const char* root = g_getenv("LCOS_SOURCE_ROOT");
  fs::path path = root != nullptr ? fs::path(root) / "tests" / "fixtures" / name
                                  : fs::path("tests") / "fixtures" / name;
  std::ifstream in(path);
  if (!in) {
    path = fs::path("..") / "tests" / "fixtures" / name;
    in.open(path);
  }
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return data;
}

void test_backdrop_power_mime() {
  EXPECT(lcos::backdrop_property(0, "HDMI 1", 2, "image-style") ==
         "/backdrop/screen0/monitorHDMI1/workspace2/image-style");
  EXPECT(lcos::is_backdrop_image("Beach.JPG"));
  EXPECT(!lcos::is_backdrop_image("notes.txt"));
  EXPECT(lcos::power_supply_type_is_battery("Battery\n"));
  EXPECT(!lcos::power_supply_type_is_battery("Mains\n"));
  EXPECT(lcos::brightness_delay_from_slider(0) == 9u);
  EXPECT(lcos::brightness_delay_from_slider(6) == 60u);
  EXPECT(lcos::brightness_slider_from_delay(9) == 0);
  EXPECT(lcos::brightness_slider_from_delay(60) == 6);
  EXPECT(!lcos::backlight_directory_has_device("/no/such/backlight"));
  EXPECT(lcos::upower_kind_name(2) == "Battery");
  EXPECT(lcos::upower_state_name(1) == "Charging");
  EXPECT(lcos::upower_technology_name(1) == "Lithium ion");
  EXPECT(lcos::format_upower_duration(0).empty());
  EXPECT(lcos::format_upower_duration(90) == "1 min");
  EXPECT(lcos::format_upower_duration(3660) == "1 h 1 min");
  EXPECT(lcos::format_upower_measure(0, "Wh").empty());
  EXPECT(lcos::format_upower_measure(12.5, "Wh") == "12.50 Wh");

  const std::string rc = "# comment\nWebBrowser=firefox\n\n[Group]\nWebBrowser=hidden\n";
  EXPECT(lcos::rc_lookup(rc, "WebBrowser") == "firefox");
  const std::string assigned = lcos::rc_assign(rc, "MailReader", "thunderbird");
  EXPECT(lcos::rc_lookup(assigned, "MailReader") == "thunderbird");
  EXPECT(lcos::rc_lookup(assigned, "WebBrowser") == "firefox");
  EXPECT(lcos::rc_lookup(lcos::rc_assign(assigned, "WebBrowser", ""), "WebBrowser").empty());

  const std::string ini = "[Default Applications]\ntext/html=firefox.desktop\n";
  EXPECT(lcos::ini_lookup(ini, "Default Applications", "text/html") == "firefox.desktop");
  const std::string next =
      lcos::ini_assign(ini, "Added Associations", "text/html", "firefox.desktop;");
  EXPECT(lcos::ini_lookup(next, "Added Associations", "text/html") == "firefox.desktop;");
  EXPECT(lcos::ini_entries(next, "Default Applications").size() == 1);

  const lcos::DesktopFile helper = lcos::parse_desktop_file(
      "[Desktop Entry]\nType=X-XFCE-Helper\nName=Mozilla Firefox\n"
      "Name[fr]=Autre\nX-XFCE-Category=WebBrowser\n"
      "X-XFCE-MimeType=text/html;text/xml;\n",
      "firefox");
  EXPECT(helper.helper);
  EXPECT(helper.name == "Mozilla Firefox");
  EXPECT(helper.category == "WebBrowser");
  EXPECT(helper.mime_types.size() == 2 && helper.mime_types[0] == "text/html");
}

void test_names() {
  EXPECT(lcos::device_xfconf_name("SynPS/2 Synaptics TouchPad") ==
         "SynPS2_Synaptics_TouchPad");
  EXPECT(lcos::device_xfconf_name("Virtual core pointer") == "Virtual_core_pointer");
  EXPECT(lcos::property_xfconf_token("libinput Tapping Enabled") ==
         "libinput_Tapping_Enabled");
  EXPECT(lcos::pointer_prop("Virtual_core_pointer", "RightHanded") ==
         "/Virtual_core_pointer/RightHanded");
  EXPECT(lcos::pointer_xinput_prop("Touchpad", "libinput Tapping Enabled") ==
         "/Touchpad/Properties/libinput_Tapping_Enabled");
  EXPECT(lcos::display_prop("Default", "HDMI-1", "Resolution") ==
         "/Default/HDMI-1/Resolution");
  EXPECT(lcos::display_prop("Temp", "eDP-1", "Scale/X") == "/Temp/eDP-1/Scale/X");
  EXPECT(lcos::display_prop("Default", "HDMI-1", "") == "/Default/HDMI-1");
  EXPECT(lcos::shortcut_prop("/commands/custom", "<Primary><Alt>t") ==
         "/commands/custom/<Primary><Alt>t");
  EXPECT(lcos::profile_id_from_label("Desk left") == "P_Desk_left");
  EXPECT(lcos::acceleration_to_libinput(5.0) == 0.0);
  EXPECT(lcos::acceleration_to_libinput(0.0) == -1.0);
  EXPECT(lcos::acceleration_to_libinput(10.0) == 1.0);
  EXPECT(lcos::libinput_to_acceleration(0.0) == 5.0);
}

void test_layout() {
  int w = 0;
  int h = 0;
  EXPECT(lcos::parse_resolution("1920x1080", w, h));
  EXPECT(w == 1920 && h == 1080);
  EXPECT(!lcos::parse_resolution("1920", w, h));
  EXPECT(!lcos::parse_resolution("x1080", w, h));
  EXPECT(!lcos::refresh_matches(59.94, 60.00));
  EXPECT(lcos::refresh_matches(59.994, 59.991));
  EXPECT(lcos::mode_refresh_hz(138700000, 2080, 1111) > 59.0);

  lcos::OutputConfig left;
  left.name = "HDMI-1";
  left.width = 1920;
  left.height = 1080;
  left.refresh = 60;
  left.scale = 1;
  left.modes.push_back({1920, 1080, 60, 1});
  left.modes.push_back({1280, 720, 60, 2});
  lcos::OutputConfig right = left;
  right.name = "DP-1";
  right.x = 50;
  right.y = -20;
  right.rotation = 90;
  right.scale = 2;
  EXPECT(lcos::rotated_width(right) == 1080);
  // UI scale 2 keeps a framebuffer of mode/2, which is what XFCE's RandR
  // transform of 1/scale describes.
  EXPECT(lcos::footprint_width(right) == 540);
  EXPECT(lcos::footprint_height(right) == 960);

  std::vector<lcos::OutputConfig> outputs{left, right};
  lcos::arrange_extend(outputs);
  EXPECT(outputs[0].x == 0 && outputs[0].y == 0);
  EXPECT(outputs[1].x == 1920 && outputs[1].y == 0);
  lcos::normalize_positions(outputs);
  EXPECT(outputs[0].x == 0);

  outputs[0].x = 100;
  outputs[0].y = 15;
  outputs[1].x = 40;
  outputs[1].y = 30;
  lcos::normalize_positions(outputs);
  EXPECT(outputs[1].x == 0 && outputs[1].y == 15);
  EXPECT(outputs[0].x == 60 && outputs[0].y == 0);

  lcos::arrange_mirror(outputs);
  EXPECT(outputs[0].x == 0 && outputs[1].x == 0);
  EXPECT(outputs[0].width == 1920 && outputs[1].width == 1920);

  lcos::OutputConfig only_small = left;
  only_small.modes = {{1280, 720, 60, 2}};
  std::vector<lcos::OutputConfig> pair{left, only_small};
  lcos::arrange_mirror(pair);
  EXPECT(pair[0].width == 1280 && pair[1].width == 1280);

  const lcos::ModeInfo* mode = lcos::find_mode(left.modes, 1920, 1080, 60);
  EXPECT(mode != nullptr && mode->id == 1);
  EXPECT(lcos::find_mode(left.modes, 800, 600, 60) == nullptr);
  EXPECT(lcos::friendly_output_name("eDP-1", "") == "Laptop");
  EXPECT(lcos::friendly_output_name("HDMI-1", "Dell") == "Dell");
  EXPECT(lcos::millimeters_for_pixels(1920) == 508);
  const auto bounds = lcos::screen_bounds(outputs);
  EXPECT(bounds.width > 0 && bounds.height > 0);
}

void test_confirm_and_volume() {
  lcos::ConfirmCountdown countdown;
  EXPECT(countdown.seconds == 10);
  for (int i = 0; i < 9; ++i) {
    countdown.tick();
  }
  EXPECT(!countdown.decided && countdown.seconds == 1);
  countdown.tick();
  EXPECT(countdown.decided && !countdown.keep);

  lcos::ConfirmCountdown kept;
  kept.accept();
  kept.tick();
  EXPECT(kept.keep);

  EXPECT(lcos::volume_to_percent(lcos::kPaVolumeNorm) == 100);
  EXPECT(lcos::volume_to_percent(0) == 0);
  EXPECT(lcos::percent_to_volume(100) == lcos::kPaVolumeNorm);
  EXPECT(lcos::percent_to_volume(150) == lcos::kPaVolumeNorm / 2 * 3);
  EXPECT(lcos::volume_to_percent(lcos::percent_to_volume(73)) == 73);
  EXPECT(lcos::percent_to_volume(400) == lcos::percent_to_volume(150));
  const auto balanced =
      lcos::scale_channel_volumes({lcos::kPaVolumeNorm, lcos::kPaVolumeNorm / 2}, 50);
  EXPECT(balanced.size() == 2);
  EXPECT(balanced[0] == lcos::percent_to_volume(50));
  EXPECT(balanced[1] == lcos::percent_to_volume(25));
}

void test_shortcuts() {
  std::map<std::string, std::string> values{
      {"/commands/default/<Alt>F2", "xfce4-appfinder"},
      {"/commands/default/<Alt>F2/startup-notify", "true"},
      {"/commands/default/<Primary><Alt>t", "exo-open --launch TerminalEmulator"},
      {"/commands/custom/<Alt>F2", "my-finder"},
      {"/commands/custom/<Alt>F2/startup-notify", "false"},
      {"/commands/custom/<Super>e", "thunar"},
      {"/xfwm4/default/<Alt>F4", "close_window_key"},
  };
  const auto rows = lcos::merge_shortcuts(values);
  EXPECT(rows.size() == 3);
  const auto wm = lcos::merge_wm_shortcuts(values);
  const lcos::WmBinding* close = nullptr;
  for (const auto& row : wm) {
    if (row.feature == "close_window_key") {
      close = &row;
    }
  }
  EXPECT(close != nullptr && close->accel == "<Alt>F4" && !close->custom);
  EXPECT(lcos::wm_feature_label("tile_left_key") != nullptr);
  const auto moved = lcos::plan_wm_shortcut(values, "close_window_key", "<Primary>q");
  EXPECT(moved.size() == 2);
  EXPECT(moved[0].action == lcos::WmShortcutAction::Set && moved[0].value.empty());
  EXPECT(moved[0].property == "/xfwm4/custom/<Alt>F4");
  EXPECT(moved[1].action == lcos::WmShortcutAction::Set && moved[1].value == "close_window_key");
  EXPECT(moved[1].property == "/xfwm4/custom/<Primary>q");
  EXPECT(lcos::plan_wm_shortcut(values, "close_window_key", "<Alt>F4").empty());
  std::map<std::string, std::string> custom_only{
      {"/xfwm4/custom/<Primary>w", "close_window_key"},
  };
  const auto cleared = lcos::plan_wm_shortcut(custom_only, "close_window_key", "");
  EXPECT(cleared.size() == 1 && cleared[0].action == lcos::WmShortcutAction::Reset);
  const lcos::Shortcut* f2 = nullptr;
  const lcos::Shortcut* term = nullptr;
  const lcos::Shortcut* files = nullptr;
  for (const auto& row : rows) {
    if (row.accel == "<Alt>F2") {
      f2 = &row;
    } else if (row.accel == "<Primary><Alt>t") {
      term = &row;
    } else if (row.accel == "<Super>e") {
      files = &row;
    }
  }
  EXPECT(f2 != nullptr && f2->custom && f2->command == "my-finder" && !f2->startup_notify);
  EXPECT(term != nullptr && !term->custom &&
         term->command == "exo-open --launch TerminalEmulator");
  EXPECT(files != nullptr && files->custom && files->command == "thunar");
  EXPECT(lcos::shortcut_notify_path(true, "<Super>e") ==
         "/commands/custom/<Super>e/startup-notify");
}

void test_system_model() {
  EXPECT(lcos::valid_username("lcos"));
  EXPECT(!lcos::valid_username("Root"));
  EXPECT(!lcos::valid_username("root"));
  EXPECT(!lcos::valid_username("has space"));
  EXPECT(lcos::valid_password("correct horse"));
  EXPECT(!lcos::valid_password(""));
  EXPECT(!lcos::valid_password("line\nbreak"));
  EXPECT(lcos::valid_timezone_name("Europe/Oslo"));
  EXPECT(!lcos::valid_timezone_name("../etc/passwd"));
  EXPECT(!lcos::valid_timezone_name("/Europe/Oslo"));
  EXPECT(lcos::valid_locale_name("en_US.UTF-8"));
  EXPECT(lcos::normalize_locale_name("en_US.utf8") == "en_US.UTF-8");
  EXPECT(lcos::valid_undo_token("0123456789abcdef0123456789abcdef"));
  EXPECT(!lcos::valid_undo_token("../etc/shadow"));

  const std::string passwd =
      "root:x:0:0:root:/root:/bin/bash\n"
      "nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin\n"
      "lcos:x:1000:1000:Lunduke:/home/lcos:/bin/bash\n"
      "guest:x:1001:1001:Guest:/home/guest:/bin/bash\n"
      "svc:x:1002:1002:Service:/var/lib/svc:/usr/sbin/nologin\n";
  const auto accounts = lcos::human_accounts(passwd, "lcos", 1000);
  EXPECT(accounts.size() == 2);
  EXPECT(accounts[0].name == "guest" && !accounts[0].administrator);
  EXPECT(accounts[1].name == "lcos" && accounts[1].administrator && accounts[1].real_name == "Lunduke");
  EXPECT(lcos::is_last_administrator("lcos", accounts));
  EXPECT(!lcos::is_last_administrator("guest", accounts));

  EXPECT(lcos::classify_clock_format("%R") == lcos::HourClock::Hour24);
  EXPECT(lcos::classify_clock_format("%I:%M %p") == lcos::HourClock::Hour12);
  EXPECT(lcos::classify_clock_format("%A") == lcos::HourClock::Unknown);
  const std::string conf = "[Seat:*]\n#autologin-user=old\nautologin-user=lcos\nuser-session=xfce\n";
  EXPECT(lcos::autologin_user_in_conf(conf) == "lcos");
  const std::string updated = lcos::conf_with_autologin(conf, "guest");
  EXPECT(lcos::autologin_user_in_conf(updated) == "guest");
  EXPECT(updated.find("user-session=xfce") != std::string::npos);
  EXPECT(updated.find("[Seat:*]\nautologin-user=guest\n") != std::string::npos);
  std::string present;
  EXPECT(lcos::autologin_key_present("autologin-user=\n", present));
  EXPECT(present.empty());
  EXPECT(!lcos::autologin_key_present("#autologin-user=lcos\n", present));
  const auto roots = lcos::lightdm_config_roots();
  EXPECT(!roots.empty());
  EXPECT(roots.back().path == "/etc/lightdm/lightdm.conf");
  EXPECT(!roots.back().directory);
  bool saw_conf_d = false;
  for (std::size_t i = 0; i + 1 < roots.size(); ++i) {
    if (roots[i].path == "/etc/lightdm/lightdm.conf.d") {
      saw_conf_d = true;
    }
    EXPECT(roots[i].path != "/etc/lightdm/lightdm.conf");
  }
  EXPECT(saw_conf_d);
  EXPECT(lcos::effective_autologin({"autologin-user=\n", "autologin-user=tester\n"}) == "tester");
  EXPECT(updated.find("user-session=xfce") != std::string::npos);
  EXPECT(lcos::autologin_user_in_conf(lcos::conf_with_autologin(conf, "")).empty());

  const std::string desktop =
      "[Desktop Entry]\nType=Application\nName=Example\nExec=/bin/true\nHidden=false\n";
  const auto rows = lcos::merge_autostart({{"example.desktop", desktop}},
                                          {{"example.desktop", desktop + "Hidden=true\n"}});
  EXPECT(rows.size() == 1 && !rows[0].enabled && !rows[0].removable);
  const std::string sparse = "[Desktop Entry]\nHidden=true\n";
  const auto overlaid = lcos::merge_autostart({{"example.desktop", desktop}},
                                              {{"example.desktop", sparse}});
  EXPECT(overlaid.size() == 1);
  EXPECT(!overlaid[0].enabled);
  EXPECT(overlaid[0].command == "/bin/true");
  EXPECT(overlaid[0].name == "Example");
  const std::string missing_exec = desktop + "TryExec=/no/such/lcos-tryexec\n";
  EXPECT(lcos::merge_autostart({{"example.desktop", missing_exec}}, {}).empty());
  const auto xfce_off = lcos::autostart_toggle_keys(true, true);
  EXPECT(xfce_off.set_hidden && xfce_off.hidden && !xfce_off.set_override);
  const auto xfce_on = lcos::autostart_toggle_keys(true, false);
  EXPECT(xfce_on.set_hidden && !xfce_on.hidden);
  const auto other_off = lcos::autostart_toggle_keys(false, true);
  EXPECT(!other_off.set_hidden && other_off.set_override && !other_off.override_on);
  const auto other_on = lcos::autostart_toggle_keys(false, false);
  EXPECT(other_on.set_hidden && !other_on.hidden && other_on.set_override && other_on.override_on);
  const auto added = lcos::generated_locale_names("C\nen_US.utf8\n");
  EXPECT(added.size() == 2);

  const auto supported = lcos::parse_supported_locales(
      "# comment\nen_GB.UTF-8 UTF-8\nen_US ISO-8859-1\n");
  EXPECT(supported.size() == 2);
  EXPECT(lcos::find_supported_locale(supported, "en_GB.UTF-8") != nullptr);
  EXPECT(lcos::find_supported_locale(supported, "en_GB") == nullptr);
  std::string enabled;
  EXPECT(lcos::enable_locale_in_gen("# en_GB.UTF-8 UTF-8\nen_US.UTF-8 UTF-8\n", supported[0],
                                   supported, enabled));
  EXPECT(enabled.find("# en_GB.UTF-8 UTF-8") == std::string::npos);
  EXPECT(enabled.find("en_GB.UTF-8 UTF-8\n") != std::string::npos);
  EXPECT(enabled.find("en_US.UTF-8 UTF-8\n") != std::string::npos);
  std::string twice;
  EXPECT(lcos::enable_locale_in_gen(enabled, supported[0], supported, twice));
  EXPECT(twice.find("en_GB.UTF-8 UTF-8\n") != std::string::npos);
  EXPECT(twice.find("en_GB.UTF-8 UTF-8\nen_GB.UTF-8 UTF-8") == std::string::npos);
  EXPECT(enabled.size() < 2 || enabled.substr(enabled.size() - 2) != "\n\n");
  EXPECT(twice == enabled);
  lcos::SupportedLocale unknown{"zz_ZZ.UTF-8", "UTF-8"};
  EXPECT(!lcos::enable_locale_in_gen(enabled, unknown, supported, twice));

  EXPECT(lcos::effective_autologin({"autologin-user=lcos\n", "autologin-user=guest\n"}) == "guest");
  EXPECT(lcos::effective_autologin({"autologin-user=lcos\nuser-session=xfce\n", "autologin-user=\n"})
             .empty());
  EXPECT(lcos::effective_autologin({"#autologin-user=lcos\n"}).empty());
}

void test_helper_files_and_status() {
  EXPECT(lcos::xft_dpi_to_spin(-1) == -1);
  EXPECT(lcos::xft_dpi_to_spin(0) == -1);
  EXPECT(lcos::xft_dpi_to_spin(96) == 96);
  EXPECT(lcos::xft_dpi_to_spin(120) == 120);
  // An earlier build stored DPI * 1024. Reading that still shows plain DPI.
  EXPECT(lcos::xft_dpi_to_spin(96 * 1024) == 96);
  EXPECT(lcos::xft_dpi_to_spin(120 * 1024) == 120);
  EXPECT(lcos::spin_to_xft_dpi(96) == 96);
  EXPECT(lcos::spin_to_xft_dpi(120) == 120);
  EXPECT(lcos::spin_to_xft_dpi(-1) == -1);
  EXPECT(std::abs(lcos::xfconf_scale_from_ui(2.0) - 0.5) < 0.001);
  EXPECT(std::abs(lcos::ui_scale_from_xfconf(0.5) - 2.0) < 0.001);
  EXPECT(std::abs(lcos::xfconf_scale_from_ui(1.0) - 1.0) < 0.001);

  const auto dismissed = lcos::interpret_helper_exit(1, 126 << 8, "");
  EXPECT(!dismissed.ok);
  EXPECT(dismissed.error.find("dismissed") != std::string::npos);
  const auto denied = lcos::interpret_helper_exit(1, 127 << 8, "");
  EXPECT(!denied.ok);
  EXPECT(denied.error.find("denied") != std::string::npos);
  const auto helper_failed = lcos::interpret_helper_exit(1, 1 << 8, "could not set the password\n");
  EXPECT(!helper_failed.ok);
  EXPECT(helper_failed.error == "could not set the password");
  const auto lost = lcos::interpret_helper_exit(-1, 0, "");
  EXPECT(!lost.ok);
  const auto kept = lcos::interpret_helper_exit(1, 0, "ignored");
  EXPECT(kept.ok);

  const fs::path root = fs::temp_directory_path() / "lcos-atomic-file";
  fs::remove_all(root);
  fs::create_directories(root);
  const fs::path regular = root / "shadow-like";
  {
    std::ofstream out(regular);
    out << "before\n";
  }
  EXPECT(chmod(regular.c_str(), 0640) == 0);
  struct stat before;
  EXPECT(stat(regular.c_str(), &before) == 0);
  lcos::ReplaceRequest request;
  request.mode_if_new = 0644;
  std::string error;
  EXPECT(lcos::replace_regular_file(regular.string(), "after\n", request, &error));
  struct stat after;
  EXPECT(stat(regular.c_str(), &after) == 0);
  EXPECT((after.st_mode & 0777) == 0640);
  EXPECT(after.st_uid == before.st_uid);
  EXPECT(after.st_gid == before.st_gid);
  {
    std::ifstream in(regular);
    std::string text;
    std::getline(in, text);
    EXPECT(text == "after");
  }

  const fs::path secret = root / "secret";
  const fs::path link = root / "face";
  {
    std::ofstream out(secret);
    out << "SECRET";
  }
  EXPECT(symlink(secret.c_str(), link.c_str()) == 0);
  EXPECT(!lcos::replace_regular_file(link.string(), "PWNED", request, &error));
  {
    std::ifstream in(secret);
    std::string text;
    std::getline(in, text);
    EXPECT(text == "SECRET");
  }
  struct stat link_st;
  EXPECT(lstat(link.c_str(), &link_st) == 0);
  EXPECT(S_ISLNK(link_st.st_mode));

  // A locale symlink is followed only when the real path is allowlisted.
  const fs::path locale_real = root / "locale.conf";
  const fs::path locale_link = root / "default-locale";
  {
    std::ofstream out(locale_real);
    out << "LANG=C\n";
  }
  EXPECT(symlink(locale_real.c_str(), locale_link.c_str()) == 0);
  lcos::ReplaceRequest follow;
  follow.follow_symlink = true;
  follow.symlink_targets = {fs::canonical(locale_real).string()};
  EXPECT(lcos::replace_regular_file(locale_link.string(), "LANG=en_US.UTF-8\n", follow, &error));
  {
    std::ifstream in(locale_real);
    std::string text;
    std::getline(in, text);
    EXPECT(text == "LANG=en_US.UTF-8");
  }
  EXPECT(lstat(locale_link.c_str(), &link_st) == 0);
  EXPECT(S_ISLNK(link_st.st_mode));
  lcos::ReplaceRequest wrong = follow;
  wrong.symlink_targets = {"/etc/passwd"};
  EXPECT(!lcos::replace_regular_file(locale_link.string(), "LANG=bad\n", wrong, &error));
  {
    std::ifstream in(locale_real);
    std::string text;
    std::getline(in, text);
    EXPECT(text == "LANG=en_US.UTF-8");
  }
  fs::remove_all(root);
}

void test_themes_and_xkb() {
  const std::string index = fixture("index.theme");
  EXPECT(!index.empty());
  const lcos::IndexTheme parsed = lcos::parse_index_theme(index);
  EXPECT(parsed.name == "Sample Theme");
  EXPECT(!parsed.hidden);
  EXPECT(parsed.icon_theme);

  const fs::path root = fs::temp_directory_path() / "lcos-theme-scan";
  fs::remove_all(root);
  fs::create_directories(root / "themes" / "Sample" / "gtk-3.0");
  fs::create_directories(root / "themes" / "Sample" / "xfwm4");
  fs::create_directories(root / "themes" / "Hidden" / "gtk-3.0");
  fs::create_directories(root / "icons" / "Cursors" / "cursors");
  {
    std::ofstream((root / "themes" / "Sample" / "index.theme").string())
        << "[X-GNOME-Metatheme]\nName=Sample Theme\n";
    std::ofstream((root / "themes" / "Sample" / "xfwm4" / "themerc").string()) << "button_offset=0\n";
    std::ofstream((root / "themes" / "Hidden" / "index.theme").string())
        << "[Desktop Entry]\nName=Hidden\nHidden=true\n";
    std::ofstream((root / "icons" / "Cursors" / "index.theme").string())
        << "[Icon Theme]\nName=Cursors\n";
    std::ofstream((root / "icons" / "Cursors" / "cursors" / "left_ptr").string()) << "x";
  }
  const auto gtk = lcos::scan_gtk_themes({(root / "themes").string()});
  EXPECT(gtk.size() == 1 && gtk[0].id == "Sample");
  const auto xfwm = lcos::scan_xfwm_themes({(root / "themes").string()});
  EXPECT(xfwm.size() == 1 && xfwm[0].id == "Sample");
  const auto cursors = lcos::scan_cursor_themes({(root / "icons").string()});
  EXPECT(cursors.size() == 1 && cursors[0].id == "Cursors");
  fs::remove_all(root);

  std::string error;
  lcos::XkbRules rules;
  EXPECT(lcos::parse_xkb_rules(fixture("evdev-sample.xml"), rules, error));
  EXPECT(error.empty());
  EXPECT(rules.models.size() == 1 && rules.models[0].name == "pc105");
  EXPECT(rules.layouts.size() == 2);
  EXPECT(rules.layouts[0].name == "us");
  EXPECT(rules.layouts[0].variants.size() == 1 &&
         rules.layouts[0].variants[0].name == "dvorak");
  const lcos::XkbOptionGroup* group = lcos::find_option_group(rules, "grp");
  EXPECT(group != nullptr && group->options.size() == 1 &&
         group->options[0].name == "grp:alt_shift_toggle");

  const auto encoded = lcos::encode_layouts({{"us", ""}, {"de", "nodeadkeys"}});
  EXPECT(encoded.first == "us,de");
  EXPECT(encoded.second == ",nodeadkeys");
  const auto decoded = lcos::decode_layouts(encoded.first, encoded.second);
  EXPECT(decoded.size() == 2);
  EXPECT(decoded[0].layout == "us" && decoded[0].variant.empty());
  EXPECT(decoded[1].layout == "de" && decoded[1].variant == "nodeadkeys");

  const std::string evdev = lcos::read_text_file("/usr/share/X11/xkb/rules/evdev.xml");
  if (!evdev.empty()) {
    lcos::XkbRules live;
    EXPECT(lcos::parse_xkb_rules(evdev, live, error));
    EXPECT(lcos::find_layout(live, "us") != nullptr);
    EXPECT(lcos::find_option_group(live, "grp") != nullptr);
  }
}

}  // namespace

void test_review_fixes() {
  lcos::SoundEdit clean;
  EXPECT(!clean.any());
  EXPECT(!lcos::sound_revert_touches(clean, "output-volume"));
  lcos::SoundEdit volume;
  volume.output_volume = true;
  EXPECT(volume.any());
  EXPECT(lcos::sound_revert_touches(volume, "output-volume"));
  EXPECT(!lcos::sound_revert_touches(volume, "output-port"));
  EXPECT(!lcos::sound_revert_touches(volume, "input-device"));

  const std::time_t restored = lcos::restored_wall_clock(1'700'000'000, 1000, 1300);
  EXPECT(restored == 1'700'000'300);

  const std::string locale =
      "# kept\nLANGUAGE=en_GB:en\nLANG=en_US.UTF-8\nLC_TIME=en_GB.UTF-8\nLC_PAPER=en_GB.UTF-8\n";
  const std::string rewritten =
      lcos::locale_file_with_values(locale, "de_DE.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8", "");
  EXPECT(rewritten.find("LANGUAGE=en_GB:en") != std::string::npos);
  EXPECT(rewritten.find("LC_PAPER=en_GB.UTF-8") != std::string::npos);
  EXPECT(rewritten.find("# kept") != std::string::npos);
  EXPECT(rewritten.find("LANG=de_DE.UTF-8") != std::string::npos);
  EXPECT(rewritten.find("LC_NUMERIC=fr_FR.UTF-8") != std::string::npos);
  EXPECT(rewritten.find("LC_TIME=") == std::string::npos);
  EXPECT(rewritten.find("LC_MONETARY=") == std::string::npos);

  const std::string full = "alice:$y$abc:19000:0:99999:7:::";
  const auto missing = lcos::restore_shadow_line("bob:x:::::::\n", full, false);
  EXPECT(!missing.ok);
  EXPECT(missing.text.find("alice:") == std::string::npos);
  const auto appended = lcos::restore_shadow_line("bob:x:::::::\n", "alice:$y$abc", true);
  EXPECT(!appended.ok);
  EXPECT(appended.text.find("alice:") == std::string::npos);
  const auto replaced =
      lcos::restore_shadow_line("alice:locked:1:0:99999:7:::\nother:x:::::::\n", full, true);
  EXPECT(replaced.ok);
  EXPECT(replaced.text.find(full) != std::string::npos);
  EXPECT(replaced.text.find("alice:locked") == std::string::npos);
  EXPECT(replaced.text.find("other:x:") != std::string::npos);
  const auto added = lcos::restore_shadow_line("other:x:::::::\n", full, true);
  EXPECT(added.ok);
  EXPECT(added.text.find(full) != std::string::npos);

  EXPECT(lcos::valid_timezone_name("UTC"));
  EXPECT(lcos::timezone_from_localtime("/usr/share/zoneinfo/Europe/Oslo") == "Europe/Oslo");
  EXPECT(lcos::timezone_from_localtime("../usr/share/zoneinfo/UTC") == "UTC");
  EXPECT(lcos::timezone_from_localtime("/etc/localtime").empty());

  const std::string spaced = "[Seat:*]\nautologin-user = x\nuser-session=xfce\n";
  EXPECT(lcos::autologin_user_in_conf(spaced) == "x");
  const std::string seat0 = "[Seat:seat0]\nautologin-user = x\n";
  const std::string edited = lcos::conf_with_autologin(seat0, "guest");
  EXPECT(edited.find("[Seat:*]") == std::string::npos);
  EXPECT(edited.find("[Seat:seat0]\n") != std::string::npos);
  EXPECT(lcos::autologin_user_in_conf(edited) == "guest");
  EXPECT(lcos::autologin_user_in_conf(lcos::conf_with_autologin(spaced, "")).empty());
  EXPECT(lcos::conf_with_autologin(spaced, "").find("user-session=xfce") != std::string::npos);
  EXPECT(lcos::effective_autologin({"autologin-user=dropin\n", "[Seat:*]\nautologin-user = x\n"}) ==
         "x");
  const std::string both = "[Seat:*]\nautologin-user=star\n[Seat:seat0]\nautologin-user=seat\n";
  EXPECT(lcos::autologin_user_in_conf(both) == "seat");

  EXPECT(lcos::is_login_shell("/bin/bash"));
  EXPECT(!lcos::is_login_shell("/usr/sbin/nologin"));
  EXPECT(!lcos::is_local_human_account(0, "/bin/bash", 1000));
  EXPECT(lcos::is_local_human_account(1000, "/bin/bash", 1000));
  const std::string primary =
      "ada:x:1000:27:Ada:/home/ada:/bin/bash\n"
      "svc:x:1001:27:Service:/var/lib/svc:/usr/sbin/nologin\n";
  const auto admins = lcos::human_accounts(primary, "", 1000, 27);
  EXPECT(admins.size() == 1);
  EXPECT(admins[0].name == "ada" && admins[0].administrator);

  EXPECT(lcos::clock_format_has_seconds("%T"));
  EXPECT(!lcos::clock_format_has_seconds("%R"));
  EXPECT(lcos::clock_format_for(lcos::HourClock::Hour24, true) == "%T");
  EXPECT(lcos::clock_format_for(lcos::HourClock::Hour12, true) == "%I:%M:%S %p");
  EXPECT(lcos::classify_clock_format("%T") == lcos::HourClock::Hour24);

  EXPECT(lcos::sysv_start_link("S20chrony", "chrony"));
  EXPECT(!lcos::sysv_start_link("K01chrony", "chrony"));
  EXPECT(!lcos::sysv_start_link("S20chronyd", "chrony"));

  const std::string sessions =
      "[General]\nSessionName=Default\n\n[Session: Default]\nCount=1\n\n"
      "[Session: Project]\nCount=2\n\n[Failsafe]\nCount=0\n";
  const auto names = lcos::saved_session_names(sessions);
  EXPECT(names.size() == 2);
  EXPECT(names[0] == "Default" && names[1] == "Project");
  const std::string kept = lcos::keyfile_without_session(sessions, "Project");
  EXPECT(lcos::saved_session_names(kept).size() == 1);
  EXPECT(kept.find("[General]") != std::string::npos);
  EXPECT(kept.find("[Session: Default]") != std::string::npos);
  EXPECT(kept.find("Project") == std::string::npos);
  EXPECT(kept.find("[Failsafe]") != std::string::npos);

  EXPECT(lcos::prepend_desktop_id("firefox.desktop;writer.desktop;", "browser.desktop") ==
         "browser.desktop;firefox.desktop;writer.desktop;");
  EXPECT(lcos::prepend_desktop_id("firefox.desktop;browser.desktop;", "browser.desktop") ==
         "browser.desktop;firefox.desktop;");
  EXPECT(lcos::prepend_desktop_id("firefox.desktop;", "") == "firefox.desktop;");

  const std::vector<std::pair<std::string, std::string>> props = {
      {"/IdentityPopups", "true"},
      {"/Notify", "1"},
      {"/Desk", "Office"},
      {"/Desk/HDMI-1", "HDMI"},
      {"/Desk/HDMI-1/EDID", "edid-a"},
      {"/Desk/HDMI-1/Active", "true"},
      {"/Other", "Old"},
      {"/Other/DP-1", "DP"},
      {"/Other/DP-1/EDID", "edid-b"},
  };
  EXPECT(lcos::is_user_display_profile("Desk", props));
  EXPECT(!lcos::is_user_display_profile("IdentityPopups", props));
  EXPECT(!lcos::is_user_display_profile("Notify", props));
  EXPECT(lcos::display_profile_matches("Desk", {"edid-a"}, props));
  EXPECT(!lcos::display_profile_matches("Desk", {"edid-a", "edid-b"}, props));
  const auto matched = lcos::matching_display_profiles({"edid-a"}, props);
  EXPECT(matched.size() == 1 && matched[0] == "Desk");

  const auto inactivity = lcos::power_choices(lcos::PowerChoiceKind::Inactivity, true, false, false, true);
  EXPECT(inactivity.size() == 1 && inactivity[0].id == "1");
  const auto unknown = lcos::power_choices(lcos::PowerChoiceKind::Inactivity, false, false, false, false);
  EXPECT(unknown.size() == 2);
  for (const auto& choice : unknown) {
    EXPECT(choice.id != "0" && choice.id != "3" && choice.id != "4");
  }
  const auto dpms = lcos::power_choices(lcos::PowerChoiceKind::Dpms, true, true, true, true);
  EXPECT(dpms.size() == 2);
  EXPECT(dpms[0].id == "Standby" && dpms[1].id == "Suspend");

  EXPECT(lcos::priv_domain_for_action("remove-user") == lcos::PrivDomain::Accounts);
  EXPECT(lcos::priv_domain_for_action("set-locale") == lcos::PrivDomain::Locale);
  EXPECT(lcos::priv_domain_for_action("set-timezone") == lcos::PrivDomain::System);
  EXPECT(!lcos::priv_action_allowed(lcos::PrivDomain::System, "remove-user"));
  EXPECT(lcos::priv_action_allowed(lcos::PrivDomain::Accounts, "set-autologin"));
  EXPECT(lcos::priv_action_allowed(lcos::PrivDomain::System, "revert"));
  EXPECT(lcos::undo_file_belongs_to(lcos::PrivDomain::Accounts, "passwd.ada"));
  EXPECT(lcos::undo_file_belongs_to(lcos::PrivDomain::Accounts, "shadow.ada"));
  EXPECT(lcos::undo_file_belongs_to(lcos::PrivDomain::Accounts, "group.ada"));
  EXPECT(lcos::undo_file_belongs_to(lcos::PrivDomain::Locale, "locale.gen"));
  EXPECT(!lcos::undo_file_belongs_to(lcos::PrivDomain::Locale, "localtime"));
  EXPECT(lcos::undo_file_belongs_to(lcos::PrivDomain::System, "clock-wall"));
  EXPECT(lcos::revert_keeps_snapshot(true));
  EXPECT(!lcos::revert_keeps_snapshot(false));
}

int main() {
  test_backdrop_power_mime();
  test_names();
  test_layout();
  test_confirm_and_volume();
  test_shortcuts();
  test_system_model();
  test_review_fixes();
  test_helper_files_and_status();
  test_themes_and_xkb();
  if (failures != 0) {
    std::cerr << failures << " logic check(s) failed\n";
    return 1;
  }
  std::cout << "logic tests passed\n";
  return 0;
}
