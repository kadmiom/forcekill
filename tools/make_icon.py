#!/usr/bin/env python3
"""Generate src/forcekill.ico: rounded red tile with a white X.

Pure standard library so the icon stays reproducible without Pillow.
"""

import math
import struct
from pathlib import Path

SIZES = (16, 24, 32, 48, 64, 128, 256)
SUPERSAMPLE = 4
BACKGROUND = (0xE5, 0x48, 0x4D)
FOREGROUND = (0xFF, 0xFF, 0xFF)


def inside_rounded_rect(x, y, width, height, radius):
    if x < 0 or y < 0 or x > width or y > height:
        return False
    cx = min(max(x, radius), width - radius)
    cy = min(max(y, radius), height - radius)
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius * radius


def distance_to_segment(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    length_squared = dx * dx + dy * dy
    if length_squared == 0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length_squared))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def render_pixels(size):
    radius = size * 0.22
    stroke = max(1.0, size * 0.085)
    pad = size * 0.29
    ax, ay = pad, pad
    bx, by = size - pad, size - pad
    samples = SUPERSAMPLE * SUPERSAMPLE

    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            inside = 0
            stroke_hits = 0
            for sy in range(SUPERSAMPLE):
                for sx in range(SUPERSAMPLE):
                    px = x + (sx + 0.5) / SUPERSAMPLE
                    py = y + (sy + 0.5) / SUPERSAMPLE
                    if not inside_rounded_rect(px, py, size, size, radius):
                        continue
                    inside += 1
                    d1 = distance_to_segment(px, py, ax, ay, bx, by)
                    d2 = distance_to_segment(px, py, bx, ay, ax, by)
                    if min(d1, d2) <= stroke:
                        stroke_hits += 1
            if inside == 0:
                row += bytes((0, 0, 0, 0))
                continue
            blend = stroke_hits / inside
            red = int(round(BACKGROUND[0] * (1 - blend) + FOREGROUND[0] * blend))
            green = int(round(BACKGROUND[1] * (1 - blend) + FOREGROUND[1] * blend))
            blue = int(round(BACKGROUND[2] * (1 - blend) + FOREGROUND[2] * blend))
            alpha = int(round((inside / samples) * 255))
            row += bytes((blue, green, red, alpha))
        rows.append(row)

    # 32bpp DIBs are stored bottom-up.
    return b"".join(reversed(rows))


def dib_image(size):
    pixels = render_pixels(size)
    header = struct.pack(
        "<IiiHHIIiiII",
        40,        # biSize
        size,      # biWidth
        size * 2,  # biHeight (colour + mask)
        1,         # biPlanes
        32,        # biBitCount
        0,         # biCompression
        len(pixels),
        0, 0, 0, 0,
    )
    mask_row_bytes = ((size + 31) // 32) * 4
    mask = bytes(mask_row_bytes * size)
    return header + pixels + mask


def main():
    images = [(size, dib_image(size)) for size in SIZES]

    header = struct.pack("<HHH", 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    entries = bytearray()
    payload = bytearray()
    for size, data in images:
        dimension = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(data), offset)
        payload += data
        offset += len(data)

    target = Path(__file__).resolve().parent.parent / "src" / "forcekill.ico"
    target.write_bytes(header + entries + payload)
    print(f"wrote {target} ({target.stat().st_size} bytes, sizes {', '.join(str(s) for s in SIZES)})")


if __name__ == "__main__":
    main()
