#!/usr/bin/env python3
"""Generate the synthetic baseline test images (stdlib only, deterministic).

photo.png is not generated here; it is a public-domain NPS photograph
(see tests/images/README.md).
"""
import math
import struct
import sys
import zlib
from pathlib import Path


def write_png(path, w, h, pixel):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw.extend(pixel(x, y))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def flat(x, y):
    # Flat-colour graphic: background, circle, rectangle, triangle. 400x300.
    if (x - 130) ** 2 + (y - 140) ** 2 < 80 ** 2:
        return (220, 60, 50)
    if 220 <= x < 370 and 40 <= y < 150:
        return (40, 90, 200)
    if 200 <= x < 380 and 180 <= y < 280 and (x - 200) > (280 - y) * 0.9:
        return (250, 200, 40)
    return (235, 235, 225)


def gradient(x, y):
    # Smooth two-axis gradient with a hue sweep. 512x256.
    t = x / 511.0
    s = y / 255.0
    r = int(255 * t)
    g = int(255 * s)
    b = int(127.5 + 127.5 * math.sin(2 * math.pi * (t + s) / 2))
    return (r, g, b)


def contrast(x, y):
    # High contrast: checkerboard, hard stripes, thin lines. 300x300.
    if y < 100:
        return (255, 255, 255) if ((x // 10) + (y // 10)) % 2 else (0, 0, 0)
    if y < 200:
        return (255, 255, 255) if (x // 3) % 2 else (0, 0, 0)
    return (255, 0, 0) if x % 17 == 0 or y % 13 == 0 else (0, 0, 0)


def small(x, y):
    # 64x64 mixed content.
    if x < 32 and y < 32:
        return (x * 8, y * 8, 128)
    if x >= 32 and y < 32:
        return (255, 255, 255) if (x + y) % 4 < 2 else (20, 20, 20)
    if x < 32:
        return (60, 160, 90)
    return ((x * 37) % 256, (y * 53) % 256, ((x ^ y) * 11) % 256)


def tiny(x, y):
    # 37x23: odd, non-power-of-two, smaller than GLIC's 64px decode minimum.
    return ((x * 7) % 256, (y * 11) % 256, 255 if (x + y) % 5 == 0 else 40)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "tests/images")
    out.mkdir(parents=True, exist_ok=True)
    write_png(out / "flat.png", 400, 300, flat)
    write_png(out / "gradient.png", 512, 256, gradient)
    write_png(out / "contrast.png", 300, 300, contrast)
    write_png(out / "small64.png", 64, 64, small)
    write_png(out / "tiny.png", 37, 23, tiny)


if __name__ == "__main__":
    main()
