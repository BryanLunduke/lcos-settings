# LCOS Command Center

Unified settings application for the Lunduke Computer Operating System (LCOS).
This is the 1.1 release (`1.1.0`, Debian package 1.1.0-1) for LCOS 1.1.
It is the settings window for the categories LCOS opens here instead of in a
separate XFCE dialog: Display, Desktop, Appearance, Window Manager (including
title-bar button layout), Window Manager Tweaks, Keyboard, Mouse, Accessibility,
Notifications, Power (including a screen saver and lock group), Sound,
Removable Drives and Media, Default Software, Date and Time, Language, Users,
and Startup (including the logout dialog).

Network, Bluetooth, printers, Panel Preferences, the advanced sound mixer, and
Settings Editor stay separate programs. Overview shows a tile for each of those
only when that program is installed.

The window width is fixed, so the section list stays put. The height can still
change, down to 480px. On an 800×600 screen the width is the work area (the
same fit as the 760×480 minimum), so the window still fits.

There is no Workspaces page and no workspace-count control. LCOS stays on one
workspace.

- **Version:** 1.1.0
- **License:** GPL-3.0-or-later
- **Stack:** C++17, Meson, gtkmm-3.0

The window writes the same xfconf channels the XFCE 4.20 tools already watch
(`displays`, `xfce4-desktop`, `xsettings`, `xfwm4`, `keyboards`,
`keyboard-layout`, `xfce4-keyboard-shortcuts`, `pointers`, `accessibility`,
`xfce4-session`, `xfce4-power-manager`, `xfce4-notifyd`, `xfce4-screensaver`,
`thunar-volman`), so a change made here shows up in those tools and is still
there after logout. Sound levels go through PulseAudio, which keeps them in
its own database, the same way `pavucontrol` does. The screen saver and the
lock stay off until someone turns them on. Opening Command Center does not
enable them.

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

The dark style **LCOS Clearlooks Dark** is installed by this package to
`/usr/share/themes/LCOS Clearlooks Dark` (GTK 3, GTK 2, and an xfwm4 window
border theme). It is an original theme. Turning Dark style on selects that
GTK style and the matching window borders together. Turning it off puts back
the previous style and borders. The live image can ship the same directory
from lcos-live/branding; Command Center lists it as soon as that directory is
on the theme path. If branding replaces the directory, it needs the `xfwm4`
subdirectory as well, or title bars stay on the previous border theme.

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
(or `--page=display`) switches to that page. `--page=power/system` or
`--page=window-manager/advanced` opens that section.
`lcos-settings --display-minimal` is the small hot-plug chooser.

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
