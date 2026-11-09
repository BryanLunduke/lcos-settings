# LCOS Command Center

Unified settings application for the Lunduke Computer Operating System (LCOS).
This is the first development snapshot (`1.1~dev`). It is not yet a replacement
for the individual XFCE settings dialogs.

- **Version:** 1.1~dev
- **License:** GPL-3.0-or-later
- **Stack:** C++17, Meson, gtkmm-3.0

Categories in this pass: Display, Appearance, Keyboard, Mouse (including
touchpad), and Sound. The window writes the same xfconf channels
`xfsettingsd` and `xfwm4` already watch (`xsettings`, `xfwm4`, `keyboards`,
`keyboard-layout`, `xfce4-keyboard-shortcuts`, `pointers`, `displays`), so a
change made here shows up in the existing XFCE tools and is still there after
logout. Sound levels go through PulseAudio, which keeps them in its own
database, the same way `pavucontrol` does.

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
