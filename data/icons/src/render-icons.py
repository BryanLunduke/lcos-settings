#!/usr/bin/env python3
"""Regenerate the LCOS Command Center hicolor icons.

Sources (self-contained SVGs, no external images or fonts):

  1-console-S16.svg   16 px, hand-hinted. Do not scale another master.
  1-console-S24.svg   24 px, hand-hinted. Do not scale another master.
  1-console-M.svg     22, 32, 48, and 64. Medium drawing for small sizes.
  1-console-L.svg     96, 128, 256, 512, and the scalable icon.

16, 24, 32, 48, 128, and 256 match the approved rsvg-convert renders.
64 uses the medium SVG (the large drawing's tick marks turn to noise).
22 uses the medium SVG (there is no hinted master between 16 and 24).
96 and 512 use the large SVG; the detail still reads at 96.

Requires rsvg-convert (librsvg2-bin).
"""

import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
ICON = "org.lunduke.LcosSettings"

# Pixel size -> source SVG. 16 and 24 are exact hinted masters.
SIZES = {
    16: "1-console-S16.svg",
    22: "1-console-M.svg",
    24: "1-console-S24.svg",
    32: "1-console-M.svg",
    48: "1-console-M.svg",
    64: "1-console-M.svg",
    96: "1-console-L.svg",
    128: "1-console-L.svg",
    256: "1-console-L.svg",
    512: "1-console-L.svg",
}

# url(#id) and href="#id" are internal. Anything else is an external reference.
EXTERNAL = re.compile(
    r"""(?:href|xlink:href)\s*=\s*["'](?!#)"""
    r"""|url\(\s*["']?(?!#)"""
    r"""|@import|@font-face""",
    re.IGNORECASE,
)


def assert_closed(path: str) -> None:
    text = open(path, encoding="utf-8").read()
    if EXTERNAL.search(text):
        raise SystemExit(f"{path} has an external reference")
    if "xmlns=\"http://www.w3.org/2000/svg\"" not in text:
        raise SystemExit(f"{path} is not an SVG")


def render(svg: str, size: int, dest: str) -> None:
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    subprocess.run(
        ["rsvg-convert", "-w", str(size), "-h", str(size), svg, "-o", dest],
        check=True,
    )


def main() -> None:
    if shutil.which("rsvg-convert") is None:
        raise SystemExit("rsvg-convert is required (package librsvg2-bin)")
    for name in set(SIZES.values()):
        assert_closed(os.path.join(HERE, name))
    for size, name in sorted(SIZES.items()):
        dest = os.path.join(
            ROOT,
            "data",
            "icons",
            "hicolor",
            f"{size}x{size}",
            "apps",
            f"{ICON}.png",
        )
        render(os.path.join(HERE, name), size, dest)
        print(dest)
    scalable = os.path.join(
        ROOT, "data", "icons", "hicolor", "scalable", "apps", f"{ICON}.svg"
    )
    os.makedirs(os.path.dirname(scalable), exist_ok=True)
    shutil.copyfile(os.path.join(HERE, "1-console-L.svg"), scalable)
    assert_closed(scalable)
    # Prove the installed SVG renders. A failed rsvg-convert stops the script.
    preview = scalable + ".preview.png"
    render(scalable, 256, preview)
    os.remove(preview)
    print(scalable)


if __name__ == "__main__":
    sys.exit(main())
