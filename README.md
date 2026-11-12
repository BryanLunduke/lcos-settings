# LCOS Command Center

Unified settings application for the Lunduke Computer Operating System (LCOS).
This is the first release (`1.1.0`, Debian package 1.1.0-1) for LCOS 1.1.0.
It is not yet a replacement for the individual XFCE settings dialogs.

- **Version:** 1.1.0
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
keep it. **Apply** is also a checkpoint: a later **Cancel** does not undo a
layout you kept, a sound level you applied, or any other setting you already
applied. **OK** keeps what is left and closes. **Cancel**, and the window Close
button, put back changes made since the last Apply.

A second launch brings the existing window forward. `lcos-settings display`
(or `--page=display`) switches to that page. `lcos-settings --display-minimal`
is the small hot-plug chooser.

## Hot-plug displays

`xfsettingsd` still runs `xfce4-display-settings --minimal` when Notify is on.
This package installs `/usr/libexec/lcos-xfce4-display-settings`, which runs
`lcos-settings --display-minimal` for that flag and opens the Display page
otherwise. LCOS 1.1 packaging can point the hot-plug command at it with a
divert, without changing `xfsettingsd`:

```
dpkg-divert --local --rename --divert /usr/bin/xfce4-display-settings.xfce \
  --add /usr/bin/xfce4-display-settings
ln -s /usr/libexec/lcos-xfce4-display-settings /usr/bin/xfce4-display-settings
```

Remove the link and `dpkg-divert --rename --remove` to restore the XFCE dialog.

Screenshots of each category can be written with:

```
LCOS_SETTINGS_DUMP_PAGES=/tmp/lcos-pages ./build/lcos-settings
```
