#!/usr/bin/env python3
"""Print the per-opcode profile (PROFOP lines) of a PROFOPS.EXE serial log.

    profops.py SERIAL_LOG

Cost = guest instructions from one dispatch to the next, so it includes the
C helpers the opcode called (builtins, tables). Each dispatch also has a
fixed profiling overhead (OVERHEAD), which the per-op column removes.
"""
import os
import re
import sys

OVERHEAD = 18   # prof_op call + body; see prof_op in src/p386_dispatch.asm


def opcode_names():
    here = os.path.dirname(os.path.abspath(__file__))
    names = {}
    with open(os.path.join(here, "..", "include", "p386_bytecode.h")) as f:
        for line in f:
            m = re.match(r"#define P386_OP_(\w+)\s+(0x[0-9a-fA-F]+|\d+)", line)
            if m:
                names[int(m.group(2), 0)] = m.group(1)
    return names


def main(path):
    names = opcode_names()
    rows = []
    for line in open(path, errors="replace"):
        m = re.match(r"PROFOP ([0-9a-fA-F]+) count (\d+) cost (\d+)", line)
        if m:
            op, n, cost = int(m.group(1), 16), int(m.group(2)), int(m.group(3))
            rows.append((cost - n * OVERHEAD, n, names.get(op, hex(op))))
    total = sum(r[0] for r in rows) or 1
    print(f"  {'opcode':<10} {'count':>9} {'instr/op':>9} {'share':>6}")
    for cost, n, name in sorted(rows, reverse=True)[:16]:
        print(f"  {name:<10} {n:>9} {cost / n:>9.1f} {cost * 100 / total:>5.1f}%")


if __name__ == "__main__":
    main(sys.argv[1])
