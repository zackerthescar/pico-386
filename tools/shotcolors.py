#!/usr/bin/env python3
"""Count the colours on the PICO-8 screen in a QEMU screendump (PPM).

    shotcolors.py shot.ppm

Prints the number of different colours in the 512x384 game area. Exits with
status 1 if the screen has only one colour (a blank screen).
"""
import sys

ORIGIN_X, ORIGIN_Y, WIDTH, HEIGHT = 64, 9, 512, 384


def main(path):
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
    w = int(fields[1])
    px = data[pos + 1:]
    colors = set()
    for y in range(ORIGIN_Y, ORIGIN_Y + HEIGHT):
        row = (y * w + ORIGIN_X) * 3
        for x in range(WIDTH):
            colors.add(px[row + 3 * x:row + 3 * x + 3])
    print(len(colors))
    sys.exit(0 if len(colors) > 1 else 1)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
