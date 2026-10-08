#!/usr/bin/env python3
"""Check PICO-8 pixels in a QEMU screendump (PPM) against expected colors.

    checkshot.py shot.ppm probes.expect

Each probe line: <x> <y> <color> [# note], with PICO-8 screen coordinates
(0-127) and a color index 0-15. QEMU shows the 128x128 screen as 512x384
at (64,9) in a 640x400 dump (each PICO-8 pixel is 4x3). Each probed pixel
is matched to the nearest PICO-8 palette color, because QEMU's 6-bit to
8-bit DAC conversion is not exact.
"""
import sys

# PICO-8 palette as 6-bit VGA DAC values (src/vga.asm _p8_palette_rgb6).
PAL6 = [(0, 0, 0), (7, 10, 20), (31, 9, 20), (0, 33, 20), (42, 20, 13),
        (23, 21, 19), (48, 48, 49), (63, 60, 58), (63, 0, 19), (63, 40, 0),
        (63, 59, 9), (0, 57, 13), (10, 43, 63), (32, 29, 39), (63, 29, 42),
        (63, 51, 42)]
ORIGIN_X, ORIGIN_Y, SCALE_X, SCALE_Y = 64, 9, 4, 3


def read_ppm(path):
    data = open(path, "rb").read()
    fields, pos = [], 0
    while len(fields) < 4:                       # magic, width, height, max
        while data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    if fields[0] != b"P6":
        sys.exit(f"{path}: not a binary PPM")
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[pos + 1:]


def nearest(rgb):
    def dist(c):
        return sum((a - (b << 2)) ** 2 for a, b in zip(rgb, PAL6[c]))
    return min(range(16), key=dist)


def main(shot, probes):
    w, h, px = read_ppm(shot)
    if (w, h) != (640, 400):
        sys.exit(f"{shot}: expected 640x400, got {w}x{h}")
    failed = 0
    for line in open(probes):
        text = line.split("#", 1)[0].split()
        if not text:
            continue
        x, y, want = map(int, text)
        note = line.split("#", 1)[1].strip() if "#" in line else ""
        sx, sy = ORIGIN_X + x * SCALE_X + 1, ORIGIN_Y + y * SCALE_Y + 1
        i = (sy * w + sx) * 3
        got = nearest(tuple(px[i:i + 3]))
        ok = got == want
        failed += not ok
        print(f"  [{'PASS' if ok else 'FAIL'}] ({x},{y}) = {got}, want {want}  {note}")
    print(f"{failed} probe(s) failed" if failed else "all probes passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
