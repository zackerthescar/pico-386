#!/usr/bin/env python3
"""Convert a PICO-8 .p8 text cart to a .p8.png cart (and inspect .p8.png).

    mkcart.py in.p8 out.p8.png      write a cart
    mkcart.py --dump in.p8.png      print the Lua code of a cart (not PXA)

Supported .p8 sections: __lua__, __gfx__, __gff__, __map__. Sound sections
(__sfx__, __music__) and __label__ are ignored with a warning. The Lua code
is stored uncompressed when it fits in 0x4300-0x7FFF (15616 bytes). If it is
larger, mkcart uses the old ":c:" compression (same format as
src/p8_compress.c). It fails if the result is still too large.

PNG layout (PICO-8 format): 160x205 RGBA, one cart byte per pixel, stored in
the low 2 bits of each channel: A = bits 7-6, R = 5-4, G = 3-2, B = 1-0.
Only stdlib is used.
"""
import struct
import sys
import zlib

W, H = 160, 205
CART_SIZE = W * H                   # 0x8020 bytes
CODE_START, CODE_END = 0x4300, 0x8000
VERSION_ADDR = 0x8000
CART_VERSION = 41

GFX, MAP, GFF = 0x0000, 0x2000, 0x3000


# ":c:" compression: 60 common characters have a one-byte code. A block copy
# (3 to 17 bytes, up to 3135 bytes back) takes two bytes.
LITERALS = "^\n 0123456789abcdefghijklmnopqrstuvwxyz!#%(){}[]<>+=/*:;.,~_"
MAX_BLOCK, MAX_BACK = 17, (255 - 60) * 16 + 15


def compress_old(code):
    """Return code (bytes) in the ":c:" format, as src/p8_compress.c writes it."""
    lit = {ch: i for i, ch in enumerate(LITERALS.encode()) if i > 0}
    out = bytearray(b":c:\0")
    out += struct.pack(">HH", len(code), 0)
    heads = {}                                  # 3-byte prefix -> positions
    pos = 0
    while pos < len(code):
        best_len = best_off = 0
        for i in reversed(heads.get(code[pos:pos + 3], ())):
            if pos - i > MAX_BACK:
                break
            n = 0
            while n < MAX_BLOCK and pos + n < len(code) and i + n < pos \
                    and code[i + n] == code[pos + n]:
                n += 1
            if n > best_len:
                best_len, best_off = n, pos - i
        step = best_len if best_len >= 3 else 1
        if best_len >= 3:
            out += bytes((best_off // 16 + 60, best_off % 16 + (best_len - 2) * 16))
        elif code[pos] in lit:
            out.append(lit[code[pos]])
        else:
            out += bytes((0, code[pos]))
        for k in range(pos, pos + step):
            heads.setdefault(code[k:k + 3], []).append(k)
        pos += step
    return bytes(out)


def decompress_old(blob):
    """Return the code (bytes) of a ":c:" blob (the inverse of compress_old)."""
    n = struct.unpack(">H", blob[4:6])[0]
    out, i = bytearray(), 8
    while len(out) < n:
        b = blob[i]
        i += 1
        if b == 0:
            out.append(blob[i])
            i += 1
        elif b < 60:
            out.append(ord(LITERALS[b]))
        else:
            off = (b - 60) * 16 + (blob[i] & 15)
            for _ in range((blob[i] >> 4) + 2):
                out.append(out[-off])
            i += 1
    return bytes(out)


# PICO-8 character set above 0x7F, plus 0x01-0x0F and 0x10-0x1F. A .p8 text
# file holds these as Unicode. A cart file holds one byte per character.
CHARSET_LOW = ("\0¹²³⁴⁵⁶⁷⁸\t\nᵇᶜ\rᵉᶠ" "▮■□⁙⁘‖◀▶「」¥•、。゛゜")
CHARSET_HIGH = (
    "○█▒🐱⬇░✽●♥☉웃⌂⬅😐♪🅾◆…➡★⧗⬆ˇ∧❎▤▥"
    "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめも"
    "やゆよらりるれろわをんっゃゅょアイウエオカキクケコサシスセソタチツテト"
    "ナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲンッャュョ◜◝")
UNI_TO_P8 = {c: i for i, c in enumerate(CHARSET_LOW) if c not in "\0\t\n\r"}
UNI_TO_P8.update({c: 0x7F + i for i, c in enumerate(CHARSET_HIGH)})


def p8_bytes(text):
    """Encode Unicode Lua code as PICO-8 bytes (glyph characters become 0x80+)."""
    text = text.replace("\ufe0f", "")           # variation selector after glyphs
    out = bytearray()
    for ch in text:
        if ord(ch) < 0x80:
            out.append(ord(ch))
        elif ch in UNI_TO_P8:
            out.append(UNI_TO_P8[ch])
        else:
            print(f"mkcart: warning: no PICO-8 character for U+{ord(ch):04X}", file=sys.stderr)
            out.append(ord("?"))
    return bytes(out)


def parse_p8(text):
    sections = {}
    current = None
    for line in text.splitlines():
        if line.startswith("__") and line.rstrip().endswith("__") and len(line.strip()) > 4:
            current = line.strip()[2:-2]
            sections[current] = []
        elif current is not None:
            sections[current].append(line)
    return sections


def hex_rows(lines, width, max_rows, name):
    rows = [l.strip() for l in lines if l.strip()]
    if len(rows) > max_rows:
        sys.exit(f"__{name}__: {len(rows)} rows, max {max_rows}")
    for i, r in enumerate(rows):
        if len(r) != width or any(c not in "0123456789abcdefABCDEF" for c in r):
            sys.exit(f"__{name}__ row {i}: expected {width} hex digits")
    return rows


def build_cart(sections):
    data = bytearray(CART_SIZE)

    # __gfx__: one hex digit per pixel; low nibble = left pixel.
    for y, row in enumerate(hex_rows(sections.get("gfx", []), 128, 128, "gfx")):
        for i in range(64):
            lo, hi = int(row[2 * i], 16), int(row[2 * i + 1], 16)
            data[GFX + y * 64 + i] = lo | (hi << 4)

    # __gff__ and __map__: plain bytes, two hex digits each.
    for y, row in enumerate(hex_rows(sections.get("gff", []), 256, 2, "gff")):
        data[GFF + y * 128:GFF + (y + 1) * 128] = bytes.fromhex(row)
    for y, row in enumerate(hex_rows(sections.get("map", []), 256, 32, "map")):
        data[MAP + y * 128:MAP + (y + 1) * 128] = bytes.fromhex(row)

    code = p8_bytes("\n".join(sections.get("lua", [])))
    if len(code) > CODE_END - CODE_START:
        packed = compress_old(code)
        if len(packed) > CODE_END - CODE_START:
            sys.exit(f"__lua__: {len(code)} bytes, {len(packed)} compressed, "
                     f"max {CODE_END - CODE_START}")
        code = packed
    data[CODE_START:CODE_START + len(code)] = code
    data[VERSION_ADDR] = CART_VERSION

    for name in sections:
        if name not in ("lua", "gfx", "gff", "map"):
            print(f"mkcart: warning: section __{name}__ ignored", file=sys.stderr)
    return data


def png_chunk(kind, payload):
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def encode_png(data):
    raw = bytearray()
    for y in range(H):
        raw.append(0)                               # filter: none
        for x in range(W):
            b = data[y * W + x]
            # Visible image: a flat grey; cart bits in the low 2 bits.
            raw += bytes((0x80 | (b >> 4) & 3, 0x80 | (b >> 2) & 3,
                          0x80 | b & 3, 0xFC | (b >> 6) & 3))
    ihdr = struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)
    # pico-386's loader needs IHDR then a single IDAT.
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr)
            + png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + png_chunk(b"IEND", b""))


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def decode_png(blob):
    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("not a PNG")
    pos, idat = 8, b""
    while pos < len(blob):
        n, kind = struct.unpack(">I4s", blob[pos:pos + 8])
        if kind == b"IDAT":
            idat += blob[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = W * 4
    prev = bytearray(stride)
    data = bytearray(CART_SIZE)
    for y in range(H):
        f = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = row[x - 4] if x >= 4 else 0
            c = prev[x - 4] if x >= 4 else 0
            row[x] = (row[x] + (0, a, prev[x], (a + prev[x]) // 2, paeth(a, prev[x], c))[f]) & 0xFF
        for x in range(W):
            r, g, b, al = row[x * 4:x * 4 + 4]
            data[y * W + x] = (al & 3) << 6 | (r & 3) << 4 | (g & 3) << 2 | (b & 3)
        prev = row
    return data


def main(argv):
    if len(argv) == 3 and argv[1] == "--dump":
        data = decode_png(open(argv[2], "rb").read())
        code = bytes(data[CODE_START:CODE_END])
        if code[:4] == b":c:\0":
            print(decompress_old(code).decode("latin-1"))
        elif code[:4] == b"\0pxa":
            print(f"(compressed code: {code[:4]!r}, version {data[VERSION_ADDR]})")
        else:
            print(code.split(b"\0")[0].decode("latin-1"))
        return
    if len(argv) != 3:
        sys.exit(__doc__)
    data = build_cart(parse_p8(open(argv[1], encoding="utf-8", errors="replace").read()))
    with open(argv[2], "wb") as f:
        f.write(encode_png(data))


if __name__ == "__main__":
    main(sys.argv)
