#!/usr/bin/env python3
"""Draw the EBOOT's XMB icon (ICON0.PNG, 144x80) with the standard library.

    python3 tools/make_icon.py ports/psp/assets/ICON0.PNG

A plain design: "MicroPython" over "PSP" in a small pixel font, on a dark
panel with rounded corners. The output is committed; rerun this after
changing the design.
"""
import struct
import sys
import zlib

WIDTH, HEIGHT = 144, 80
RADIUS = 8

BACKGROUND_TOP = (0x23, 0x2a, 0x3a)
BACKGROUND_BOTTOM = (0x12, 0x16, 0x20)
TITLE = (0xff, 0xff, 0xff)
ACCENT = (0x4f, 0xc3, 0xa1)

# 5-wide pixel glyphs, 8 rows (the last row is for descenders).
GLYPHS = {
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#", "....."],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#....", "....."],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####.", "....."],
    "c": [".....", ".....", ".###.", "#....", "#....", "#....", ".###.", "....."],
    "h": ["#....", "#....", "#.##.", "##..#", "#...#", "#...#", "#...#", "....."],
    "i": ["..#..", ".....", ".##..", "..#..", "..#..", "..#..", ".###.", "....."],
    "n": [".....", ".....", "#.##.", "##..#", "#...#", "#...#", "#...#", "....."],
    "o": [".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###.", "....."],
    "r": [".....", ".....", "#.##.", "##..#", "#....", "#....", "#....", "....."],
    "t": [".#...", ".#...", "####.", ".#...", ".#...", ".#..#", "..##.", "....."],
    "y": [".....", ".....", "#...#", "#...#", "#...#", ".####", "....#", ".###."],
}


def text_width(text, scale):
    return (len(text) * 6 - 1) * scale


def draw_text(pixels, text, x, y, scale, colour):
    for ch in text:
        for row, bits in enumerate(GLYPHS[ch]):
            for col, bit in enumerate(bits):
                if bit == "#":
                    for dy in range(scale):
                        for dx in range(scale):
                            pixels[y + row * scale + dy][x + col * scale + dx] = colour + (255,)
        x += 6 * scale


def inside_rounded(x, y):
    # Distance test against the corner circles; everything else is inside.
    cx = min(max(x, RADIUS), WIDTH - 1 - RADIUS)
    cy = min(max(y, RADIUS), HEIGHT - 1 - RADIUS)
    return (x - cx) ** 2 + (y - cy) ** 2 <= RADIUS**2


def render():
    pixels = []
    for y in range(HEIGHT):
        t = y / (HEIGHT - 1)
        shade = tuple(round(a + (b - a) * t) for a, b in zip(BACKGROUND_TOP, BACKGROUND_BOTTOM))
        pixels.append([shade + (255,) if inside_rounded(x, y) else (0, 0, 0, 0) for x in range(WIDTH)])

    title, sub = "MicroPython", "PSP"
    draw_text(pixels, title, (WIDTH - text_width(title, 2)) // 2, 14, 2, TITLE)
    # A thin accent rule between the two lines.
    for x in range(22, WIDTH - 22):
        pixels[36][x] = ACCENT + (255,)
    draw_text(pixels, sub, (WIDTH - text_width(sub, 3)) // 2, 44, 3, ACCENT)
    return pixels


def write_png(path, pixels):
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in pixels)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    write_png(sys.argv[1], render())
