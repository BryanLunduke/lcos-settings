# LCOS Command Center

Unified settings application for the Lunduke Computer Operating System (LCOS).
This is the first development snapshot (`1.1~dev`). It is not yet a replacement
for the individual XFCE settings dialogs.

- **Version:** 1.1~dev
- **License:** GPL-3.0-or-later
- **Stack:** C++17, Meson, gtkmm-3.0

Categories: Display, Desktop (background, icons, and menus), Appearance,
Window Manager, Window Manager Tweaks, Keyboard, Mouse (including touchpad),
Accessibility, Power, Notifications, Removable Drives and Media, Sound, and
Default Software, plus date and time, language, users, and startup. The window writes the same xfconf channels the XFCE 4.20
tools already watch (`displays`, `xfce4-desktop`, `xsettings`, `xfwm4`,
`keyboards`, `keyboard-layout`, `xfce4-keyboard-shortcuts`, `pointers`,
`accessibility`, `xfce4-session`, `xfce4-power-manager`, `xfce4-notifyd`,
`thunar-volman`), so a change made here shows up in those tools and is still
there after logout. Sound levels go through PulseAudio, which keeps them in
its own database, the same way `pavucontrol` does. There is no Workspaces
page and no workspace-count control.

## Build

```
meson setup build
meson compile -C build
meson test -C build
./build/lcos-settings
```

A Debian package, on trixie or another debhelper 13 system:

```
dpkg-buildpackage -us -uc -b
```

`packaging/build-deb.sh` runs that and copies the `.deb` into `packaging/debs/`.

Runtime pieces LCOS already ships: an X11 session (XLibre or Xorg), `xfconfd`
on the session bus, `xfsettingsd` (so the values take effect), and PulseAudio.
The program forces the X11 GDK backend.

## How a change is saved

Appearance, keyboard, mouse, and sound write as soon as you move a control.
Display keeps the layout in the window until **Apply**. Apply pushes it through
RandR, asks `xfsettingsd` to apply the same layout, and gives you 10 seconds to
keep it. **OK** does that and closes. **Cancel**, and the window Close button,
put back every value written during the visit.

Screenshots of each category can be written with:

```
LCOS_SETTINGS_DUMP_PAGES=/tmp/lcos-pages ./build/lcos-settings
```
