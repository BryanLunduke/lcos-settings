#!/usr/bin/env python3
"""Placeholder app icon for LCOS Command Center. Not final artwork."""

import os

from PIL import Image, ImageDraw


def draw(size: int) -> Image.Image:
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    margin = max(1, size // 16)
    radius = max(2, size // 6)
    draw.rounded_rectangle(
        [margin, margin, size - margin - 1, size - margin - 1],
        radius=radius,
        fill=(31, 58, 95, 255),
    )
    knob = (196, 123, 43, 255)
    track = (247, 244, 234, 255)
    rows = 3
    top = size * 0.28
    gap = size * 0.18
    track_h = max(2, size // 18)
    knob_r = max(2, size // 11)
    positions = (0.28, 0.62, 0.42)
    for i in range(rows):
        y = int(top + i * gap)
        x0 = int(size * 0.22)
        x1 = int(size * 0.78)
        draw.rounded_rectangle([x0, y, x1, y + track_h], radius=track_h, fill=track)
        cx = int(size * positions[i])
        cy = y + track_h // 2
        draw.ellipse([cx - knob_r, cy - knob_r, cx + knob_r, cy + knob_r], fill=knob)
    return image


def main() -> None:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    sizes = [16, 22, 24, 32, 48, 64, 96, 128, 256, 512, 1024]
    for size in sizes:
        image = draw(size)
        if size == 1024:
            path = os.path.join(root, "data", "icons", "org.lunduke.LcosSettings-master.png")
        else:
            path = os.path.join(
                root,
                "data",
                "icons",
                "hicolor",
                f"{size}x{size}",
                "apps",
                "org.lunduke.LcosSettings.png",
            )
        os.makedirs(os.path.dirname(path), exist_ok=True)
        image.save(path, "PNG")
        print(path)


if __name__ == "__main__":
    main()
