#!/usr/bin/env python3
"""Resolve the minimal set of object files from the extracted Rust staticlib.

The Rust staticlib bundles all of core/alloc/compiler_builtins/peg as separate
object files. wlink (OMF) has no archive-style on-demand pulling for these ELF
objects, so naively linking every object bloats the EXE by ~2 MB. This script
performs a reachability walk: starting from the crate's own objects, it keeps
only archive members that are needed to satisfy undefined symbols (transitively).

Usage:
  resolve_rust_objs.py [--prune] OBJDIR

--prune  delete the unreachable .o files in place (default: just print the set)
"""
import subprocess, sys, os, glob

# Symbols satisfied by crt_shim.c / the Watcom C runtime at link time.
SHIM_PROVIDED = {
    "memcpy", "memset", "memcmp", "memmove", "strlen",
    "wc_malloc", "wc_free", "wc_realloc",
}

def syms(o):
    defined, undef = set(), set()
    out = subprocess.run(["nm", o], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == 'U':
            undef.add(parts[1])
        elif len(parts) >= 3 and parts[1] in "TtWwRrDdBbVvGgSs":
            defined.add(parts[2])
    return defined, undef

def main():
    prune = "--prune" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    objdir = args[0]
    objs = glob.glob(os.path.join(objdir, "*.o"))
    info = {o: syms(o) for o in objs}
    # Seed with the crate's own translation units.
    selected = set(o for o in objs if "pico386_rs" in os.path.basename(o))
    if not selected:
        selected = set(objs)
    changed = True
    while changed:
        changed = False
        have = set()
        for o in selected:
            have |= info[o][0]
        need = set()
        for o in selected:
            need |= info[o][1]
        for m in need - have - SHIM_PROVIDED:
            for o in objs:
                if o in selected:
                    continue
                if m in info[o][0]:
                    selected.add(o)
                    changed = True
                    break
    if prune:
        for o in objs:
            if o not in selected:
                os.remove(o)
        print("  kept %d of %d Rust objects" % (len(selected), len(objs)))
    else:
        print(" ".join(sorted(selected)))

if __name__ == "__main__":
    main()
