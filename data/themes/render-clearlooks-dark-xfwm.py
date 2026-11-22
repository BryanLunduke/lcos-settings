#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Draws the LCOS Clearlooks Dark xfwm4 border theme next to this script.
# Original artwork. Re-run after a palette change; the XPM files are what
# the package installs.

import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "LCOS Clearlooks Dark", "xfwm4")

# Palette taken from the GTK 3 stylesheet (header bar and buttons).
ACTIVE_TOP = "#4a6280"
ACTIVE_MID = "#2c3a4d"
ACTIVE_BOT = "#1a2430"
INACTIVE_TOP = "#45484d"
INACTIVE_MID = "#2e3033"
INACTIVE_BOT = "#151618"
FACE = "#3c4450"
FACE_HOVER = "#4d5c70"
FACE_PRESS = "#243040"
HI = "#8eacd0"
SHADOW = "#12161c"
GLYPH = "#f4f7fb"
GLYPH_DIM = "#9a968e"
CLOSE_HOVER = "#8d3d3d"
NONE = "None"

TITLE_H = 22
BUTTON_W = 18
CORNER_W = 6
SIDE_W = 3
BOTTOM_H = 4


def xpm(name, width, height, rows, colors):
    if len(rows) != height:
        raise SystemExit(f"{name}: {len(rows)} rows, expected {height}")
    lines = [
        "/* XPM */",
        f"static char * {name}[] = {{",
        f'"{width} {height} {len(colors)} 1",',
    ]
    for char, color in colors:
        if len(char) != 1:
            raise SystemExit(f"{name}: color key {char!r}")
        lines.append(f'"{char} c {color}",')
    for index, row in enumerate(rows):
        if len(row) != width:
            raise SystemExit(f"{name}: row {index} is {len(row)} px, expected {width}")
        comma = "," if index + 1 < height else ""
        lines.append(f'"{row}"{comma}')
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def title_rows(width, top, mid, bot):
    rows = []
    for y in range(TITLE_H):
        color = "t" if y == 0 else "b" if y == TITLE_H - 1 else "m"
        rows.append(color * width)
    return rows, [("t", top), ("m", mid), ("b", bot), (" ", NONE)]


def rounded_top(width, top, mid, bot, left):
    rows = []
    for y in range(TITLE_H):
        color = "t" if y == 0 else "b" if y == TITLE_H - 1 else "m"
        chars = []
        for x in range(width):
            # Outer two pixels of the top two rows stay clear, so the
            # corner reads as rounded against the desktop.
            outer = x if left else width - 1 - x
            if y < 2 and outer < 2 - y:
                chars.append(" ")
            else:
                chars.append(color)
        rows.append("".join(chars))
    return rows, [("t", top), ("m", mid), ("b", bot), (" ", NONE)]


def side_rows(width, color):
    return ["." * width] * 8, [(".", color), (" ", NONE)]


def bottom_rows(width, fill, edge):
    rows = []
    for y in range(BOTTOM_H):
        rows.append(("e" if y == BOTTOM_H - 1 else "f") * width)
    return rows, [("f", fill), ("e", edge), (" ", NONE)]


def bottom_corner(left, fill, edge):
    size = CORNER_W
    rows = []
    for y in range(size):
        chars = []
        for x in range(size):
            outer = x if left else size - 1 - x
            if y >= size - 2 and outer < (size - y):
                chars.append(" ")
            elif y == size - 1 or outer == 0:
                chars.append("e")
            else:
                chars.append("f")
        rows.append("".join(chars))
    return rows, [("f", fill), ("e", edge), (" ", NONE)]


GLYPHS = {
    "close": [
        "#.....#",
        ".#...#.",
        "..#.#..",
        "...#...",
        "..#.#..",
        ".#...#.",
        "#.....#",
    ],
    "hide": [
        ".......",
        ".......",
        ".......",
        ".......",
        ".......",
        "#######",
        ".......",
    ],
    "maximize": [
        "#######",
        "#.....#",
        "#.....#",
        "#.....#",
        "#.....#",
        "#.....#",
        "#######",
    ],
    "maximize-toggled": [
        ".####..",
        ".#..#..",
        ".####..",
        "####...",
        "#..#...",
        "#..#...",
        "####...",
    ],
    "menu": [
        "...#...",
        "...#...",
        "..###..",
        ".#####.",
        "..###..",
        "...#...",
        ".......",
    ],
    "shade": [
        "#######",
        ".......",
        ".......",
        ".......",
        ".......",
        ".......",
        ".......",
    ],
    "shade-toggled": [
        ".......",
        ".......",
        ".......",
        ".......",
        ".......",
        ".......",
        "#######",
    ],
    "stick": [
        "...#...",
        "..###..",
        "...#...",
        "...#...",
        "..###..",
        ".#####.",
        "...#...",
    ],
    "stick-toggled": [
        "...#...",
        "..###..",
        ".#####.",
        "#######",
        ".#####.",
        "..###..",
        "...#...",
    ],
}


def button_rows(kind, state, active):
    top = ACTIVE_TOP if active else INACTIVE_TOP
    mid = ACTIVE_MID if active else INACTIVE_MID
    bot = ACTIVE_BOT if active else INACTIVE_BOT
    if not active:
        face, hi, shadow, glyph = FACE, INACTIVE_TOP, INACTIVE_BOT, GLYPH_DIM
    elif state == "prelight":
        face = CLOSE_HOVER if kind == "close" else FACE_HOVER
        hi, shadow, glyph = HI, SHADOW, GLYPH
    elif state == "pressed":
        face, hi, shadow, glyph = FACE_PRESS, SHADOW, HI, GLYPH
    else:
        face, hi, shadow, glyph = FACE, HI, SHADOW, GLYPH
    glyph_art = GLYPHS[kind]
    face_size = 14
    y0 = (TITLE_H - face_size) // 2
    x0 = (BUTTON_W - face_size) // 2
    pressed = state == "pressed"
    rows = []
    for y in range(TITLE_H):
        chars = []
        bar = "t" if y == 0 else "b" if y == TITLE_H - 1 else "m"
        for x in range(BUTTON_W):
            fy = y - y0
            fx = x - x0
            if 0 <= fy < face_size and 0 <= fx < face_size:
                if fy == 0 or fx == 0:
                    chars.append("s" if pressed else "h")
                elif fy == face_size - 1 or fx == face_size - 1:
                    chars.append("h" if pressed else "s")
                else:
                    gy = fy - 3 - (1 if pressed else 0)
                    gx = fx - 3 - (1 if pressed else 0)
                    # Glyphs are 7x7. A shift on press stays inside the face.
                    if 0 <= gy < 7 and 0 <= gx < 7 and glyph_art[gy][gx] == "#":
                        chars.append("g")
                    else:
                        chars.append("f")
            else:
                chars.append(bar)
        rows.append("".join(chars))
    colors = [
        ("t", top),
        ("m", mid),
        ("b", bot),
        ("f", face),
        ("h", hi),
        ("s", shadow),
        ("g", glyph),
        (" ", NONE),
    ]
    return rows, colors


def write(filename, payload):
    path = os.path.join(ROOT, filename)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(payload)


def main():
    os.makedirs(ROOT, exist_ok=True)
    parts = []

    def add(filename, width, height, rows, colors):
        write(filename, xpm(filename.replace(".", "_").replace("-", "_"), width, height, rows, colors))
        parts.append(filename)

    for active, tag, top, mid, bot in (
        (True, "active", ACTIVE_TOP, ACTIVE_MID, ACTIVE_BOT),
        (False, "inactive", INACTIVE_TOP, INACTIVE_MID, INACTIVE_BOT),
    ):
        for name, width in (("title-1", 4), ("title-2", 2), ("title-3", 1), ("title-4", 2), ("title-5", 4)):
            rows, colors = title_rows(width, top, mid, bot)
            add(f"{name}-{tag}.xpm", width, TITLE_H, rows, colors)
        for left in (True, False):
            rows, colors = rounded_top(CORNER_W, top, mid, bot, left)
            side = "left" if left else "right"
            add(f"top-{side}-{tag}.xpm", CORNER_W, TITLE_H, rows, colors)
        for name in ("left", "right"):
            rows, colors = side_rows(SIDE_W, mid)
            add(f"{name}-{tag}.xpm", SIDE_W, 8, rows, colors)
        rows, colors = bottom_rows(8, mid, bot)
        add(f"bottom-{tag}.xpm", 8, BOTTOM_H, rows, colors)
        for left in (True, False):
            rows, colors = bottom_corner(left, mid, bot)
            side = "left" if left else "right"
            add(f"bottom-{side}-{tag}.xpm", CORNER_W, CORNER_W, rows, colors)

    buttons = ["close", "hide", "maximize", "menu", "shade", "stick"]
    toggled = {"maximize", "shade", "stick"}
    for kind in buttons:
        for state in ("active", "inactive", "prelight", "pressed"):
            rows, colors = button_rows(kind, state, state != "inactive")
            add(f"{kind}-{state}.xpm", BUTTON_W, TITLE_H, rows, colors)
        if kind in toggled:
            for state in ("active", "inactive", "prelight", "pressed"):
                rows, colors = button_rows(kind + "-toggled", state, state != "inactive")
                add(f"{kind}-toggled-{state}.xpm", BUTTON_W, TITLE_H, rows, colors)

    themerc = """button_offset=2
button_spacing=2
full_width_title=true
title_shadow_active=false
title_shadow_inactive=false
title_horizontal_offset=3
title_vertical_offset_active=1
title_vertical_offset_inactive=1
show_app_icon=false
frame_border_top=0
maximized_offset=0
shadow_delta_height=4
shadow_delta_width=4
shadow_delta_x=-2
shadow_delta_y=-2
shadow_opacity=40
active_text_color=#f4f7fb
inactive_text_color=#b7b3ac
active_text_shadow_color=#1a2430
inactive_text_shadow_color=#151618
active_border_color=#1a2430
inactive_border_color=#151618
active_color_1=#4a6280
active_color_2=#2c3a4d
active_hilight_1=#8eacd0
active_hilight_2=#4d6f99
active_mid_1=#354760
active_mid_2=#2c3a4d
active_shadow_1=#1a2430
active_shadow_2=#12161c
inactive_color_1=#45484d
inactive_color_2=#2e3033
inactive_hilight_1=#5a5e64
inactive_hilight_2=#3c3f44
inactive_mid_1=#34373b
inactive_mid_2=#2e3033
inactive_shadow_1=#151618
inactive_shadow_2=#101114
"""
    write("themerc", themerc)
    print(f"wrote {len(parts)} pixmaps and themerc in {ROOT}")


if __name__ == "__main__":
    main()
