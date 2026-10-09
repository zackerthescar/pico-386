#!/usr/bin/env python3
"""Change the Lua code of a .p8.png cart with a text patch.

    p8patch.py in.p8.png change.patch out.p8.png

A patch file holds one or more blocks. Each "old" text must occur exactly
once in the code. Lines before the first block are comments.

    <<<
    old text
    ===
    new text
    >>>

Use it to make test variants of carts that are not in this repo (for
example, Celeste that starts at a later room). Only uncompressed and ":c:"
code is supported. Only stdlib is used.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkcart as m  # noqa: E402


def parse_patch(text):
    blocks, cur, part = [], None, None
    for line in text.splitlines(keepends=True):
        tag = line.rstrip("\r\n")
        if tag == "<<<":
            cur, part = ["", ""], 0
        elif tag == "===" and cur is not None:
            part = 1
        elif tag == ">>>" and cur is not None:
            blocks.append((cur[0], cur[1]))
            cur = None
        elif cur is not None:
            cur[part] += line
    if cur is not None or not blocks:
        sys.exit("p8patch: bad patch file")
    return blocks


def main(argv):
    if len(argv) != 4:
        sys.exit(__doc__)
    data = bytearray(m.decode_png(open(argv[1], "rb").read()))
    raw = bytes(data[m.CODE_START:m.CODE_END])
    if raw[:4] == b":c:\0":
        code = m.decompress_old(raw)
    elif raw[:4] == b"\0pxa":
        sys.exit("p8patch: PXA code is not supported")
    else:
        code = raw.split(b"\0")[0]
    for old, new in parse_patch(open(argv[2], encoding="latin-1").read()):
        n = code.count(old.encode("latin-1"))
        if n != 1:
            sys.exit(f"p8patch: old text found {n} times:\n{old}")
        code = code.replace(old.encode("latin-1"), new.encode("latin-1"))
    size = m.CODE_END - m.CODE_START
    packed = code if len(code) < size else m.compress_old(code)
    if len(packed) > size:
        sys.exit("p8patch: code too large")
    data[m.CODE_START:m.CODE_END] = packed + bytes(size - len(packed))
    with open(argv[3], "wb") as f:
        f.write(m.encode_png(bytes(data)))


if __name__ == "__main__":
    main(sys.argv)
