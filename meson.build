project(
  'lcos-settings',
  'cpp',
  version: '1.1~dev',
  license: 'GPL-3.0-or-later',
  meson_version: '>=0.56.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
    'werror=false',
  ],
)

gtkmm_dep = dependency('gtkmm-3.0', version: '>=3.24')
xfconf_dep = dependency('libxfconf-0')
pulse_dep = dependency('libpulse')
pulse_glib_dep = dependency('libpulse-mainloop-glib')
x11_dep = dependency('x11')
xrandr_dep = dependency('xrandr')
xi_dep = dependency('xi')
glib_dep = dependency('glib-2.0')
sigc_dep = dependency('sigc++-2.0')

lcos_inc = include_directories('src')

logic_lib = static_library(
  'lcos_logic',
  files(
    'src/logic/names.cpp',
    'src/logic/display_layout.cpp',
    'src/logic/shortcuts.cpp',
    'src/logic/themes.cpp',
    'src/logic/xkb_rules.cpp',
  ),
  include_directories: lcos_inc,
  dependencies: glib_dep,
)

backend_sources = files(
  'src/backends/xfconf_store.cpp',
  'src/backends/xrandr_backend.cpp',
  'src/backends/pointer_backend.cpp',
  'src/backends/pulse_client.cpp',
)

page_sources = files(
  'src/pages/display_page.cpp',
  'src/pages/appearance_page.cpp',
  'src/pages/keyboard_page.cpp',
  'src/pages/mouse_page.cpp',
  'src/pages/sound_page.cpp',
  'src/main_window.cpp',
  'src/application.cpp',
)

app_deps = [gtkmm_dep, xfconf_dep, pulse_dep, pulse_glib_dep, x11_dep, xrandr_dep, xi_dep, glib_dep]

lcos_settings = executable(
  'lcos-settings',
  files('src/main.cpp'),
  backend_sources,
  page_sources,
  link_with: logic_lib,
  include_directories: lcos_inc,
  dependencies: app_deps,
  install: true,
)

logic_tests = executable(
  'lcos-settings-logic-tests',
  files('tests/logic_tests.cpp'),
  link_with: logic_lib,
  include_directories: lcos_inc,
  dependencies: glib_dep,
)

session_tests = executable(
  'lcos-settings-session-tests',
  files('tests/session_tests.cpp'),
  backend_sources,
  link_with: logic_lib,
  include_directories: lcos_inc,
  dependencies: [xfconf_dep, pulse_dep, pulse_glib_dep, x11_dep, xrandr_dep, xi_dep, glib_dep, sigc_dep],
)

test('logic', logic_tests, args: [], env: ['LCOS_SOURCE_ROOT=' + meson.current_source_dir()])

run_session = find_program('tests/run-session.sh')
test(
  'session',
  run_session,
  args: [session_tests],
  timeout: 180,
)

install_data(
  'data/org.lunduke.LcosSettings.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

install_data(
  'data/org.lunduke.LcosSettings.metainfo.xml',
  install_dir: join_paths(get_option('datadir'), 'metainfo'),
)

fs = import('fs')
app_icon_sizes = ['16', '22', '24', '32', '48', '64', '96', '128', '256', '512']
foreach size : app_icon_sizes
  icon = 'data/icons/hicolor/' + size + 'x' + size + '/apps/org.lunduke.LcosSettings.png'
  if fs.exists(icon)
    install_data(
      icon,
      install_dir: join_paths(get_option('datadir'), 'icons/hicolor/' + size + 'x' + size + '/apps'),
    )
  endif
endforeach
