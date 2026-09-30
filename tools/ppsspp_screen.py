#!/usr/bin/env python3
"""Screenshot helpers for tools/run-ppsspp.sh.

PPSSPPHeadless captures the PSP framebuffer as a 512x272, 32-bit, bottom-up
BGRA bitmap (the width is the framebuffer stride, not the visible 480).

  ppsspp_screen.py reference OUT.bmp   write a blank reference bitmap
  ppsspp_screen.py png IN.bmp OUT.png  crop a capture to 480x272 and save as PNG

The reference exists because PPSSPPHeadless crashes when asked to compare
against a reference it can't load, and it only saves a capture when a
comparison fails.
"""
import struct
import sys
import zlib

STRIDE, HEIGHT = 512, 272
WIDTH = 480
HEADER_SIZE = 14 + 40


def bmp_header():
    data_size = STRIDE * HEIGHT * 4
    file_header = struct.pack("<2sIHHI", b"BM", HEADER_SIZE + data_size, 0, 0, HEADER_SIZE)
    info_header = struct.pack("<IiiHHIIiiII", 40, STRIDE, HEIGHT, 1, 32, 0, data_size, 2834, 2834, 0, 0)
    return file_header + info_header


def write_reference(path):
    with open(path, "wb") as f:
        f.write(bmp_header())
        f.write(bytes(STRIDE * HEIGHT * 4))


def png_chunk(kind, data):
    chunk = kind + data
    return struct.pack(">I", len(data)) + chunk + struct.pack(">I", zlib.crc32(chunk) & 0xFFFFFFFF)


def bmp_to_png(src, dst):
    with open(src, "rb") as f:
        data = f.read()
    offset = struct.unpack_from("<I", data, 10)[0]
    pixels = data[offset:]

    rows = []
    for y in range(HEIGHT):
        # Bitmap rows run bottom to top.
        start = (HEIGHT - 1 - y) * STRIDE * 4
        row = bytearray(b"\x00")  # PNG filter type: none
        for x in range(WIDTH):
            b, g, r = pixels[start + x * 4 : start + x * 4 + 3]
            row += bytes((r, g, b))
        rows.append(bytes(row))

    ihdr = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)
    with open(dst, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(png_chunk(b"IHDR", ihdr))
        f.write(png_chunk(b"IDAT", zlib.compress(b"".join(rows), 9)))
        f.write(png_chunk(b"IEND", b""))


def main(argv):
    if len(argv) == 3 and argv[1] == "reference":
        write_reference(argv[2])
    elif len(argv) == 4 and argv[1] == "png":
        bmp_to_png(argv[2], argv[3])
    else:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
