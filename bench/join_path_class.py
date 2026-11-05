#!/usr/bin/env python3
"""Join path_class_asm.txt + path_class_c_o2.txt into markdown tables.

Note: harness prints sum as uint32 low word; for large n use contrib=(asm_avg-c_avg)*n.
"""
from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path

LINE_RE = re.compile(
    r"^(?P<op>\w+)\.(?P<kind>cls|path)=(?P<name>[\w]+)"
    r"(?: n_hi=(?P<n_hi>[0-9a-fA-F]+))?"
    r" n=(?P<n>[0-9a-fA-F]+)"
    r" avg=(?P<avg>[0-9a-fA-F]+)"
    r" max=(?P<max>[0-9a-fA-F]+)"
    r" wa=(?P<wa>[0-9a-fA-F]+)"
    r" wb=(?P<wb>[0-9a-fA-F]+)"
    r"(?: sum_hi=(?P<sum_hi>[0-9a-fA-F]+))?"
    r"(?: sum=(?P<sum>[0-9a-fA-F]+))?$"
)

BINARY = {"add", "sub", "mul", "div"}


def parse(path: Path) -> dict[tuple[str, str, str], dict]:
    out: dict[tuple[str, str, str], dict] = {}
    for line in path.read_text().splitlines():
        m = LINE_RE.match(line.strip())
        if not m:
            continue
        key = (m.group("op"), m.group("kind"), m.group("name"))
        n = int(m.group("n"), 16)
        if m.group("n_hi"):
            n |= int(m.group("n_hi"), 16) << 32
        # legacy: binary all n printed 0 when 2^32
        if key[0] in BINARY and key[2] == "all" and n == 0:
            n = 1 << 32
        d = {
            "n": n,
            "avg": int(m.group("avg"), 16),
            "max": int(m.group("max"), 16),
            "wa": int(m.group("wa"), 16),
            "wb": int(m.group("wb"), 16),
        }
        out[key] = d
    return out


def main() -> int:
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} asm.txt c.txt out.md", file=sys.stderr)
        return 2
    asm = parse(Path(sys.argv[1]))
    cref = parse(Path(sys.argv[2]))
    out = Path(sys.argv[3])

    keys = sorted(set(asm) | set(cref))
    by_op: dict[str, list] = defaultdict(list)
    for op, kind, name in keys:
        by_op[op].append((kind, name))

    lines: list[str] = []
    lines.append("# path_class join (ASM vs C GCC -O2)\n")
    lines.append(
        "Measured on rv32emu (`cycle≡instret`), wrap included. "
        "`contrib ≈ (ASM_avg − C_avg) × n` (log `sum=` is low32 only on huge buckets).\n"
    )

    for op in sorted(by_op):
        lines.append(f"\n## `{op}`\n")
        for kind in ("cls", "path"):
            rows = [(k, n) for k, n in by_op[op] if k == kind]
            if not rows:
                continue
            lines.append(f"\n### {kind}\n")
            lines.append(
                "| name | n | ASM avg | C avg | gap | contrib | ASM max | C max |"
            )
            lines.append(
                "| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
            )
            scored = []
            for _, name in rows:
                a = asm.get((op, kind, name))
                c = cref.get((op, kind, name))
                if not a and not c:
                    continue
                an = a["n"] if a else 0
                cn = c["n"] if c else 0
                n = an or cn
                nnote = f"{an}/{cn}" if a and c and an != cn else str(n)
                aa = a["avg"] if a else 0
                ca = c["avg"] if c else 0
                gap = aa - ca
                contrib = gap * n
                amx = a["max"] if a else 0
                cmx = c["max"] if c else 0
                scored.append((abs(contrib), name, nnote, aa, ca, gap, contrib, amx, cmx))
            scored.sort(reverse=True)
            for _, name, nnote, aa, ca, gap, contrib, amx, cmx in scored:
                lines.append(
                    f"| `{name}` | {nnote} | {aa} | {ca} | {gap:+d} | {contrib:+d} | {amx} | {cmx} |"
                )

    out.write_text("\n".join(lines) + "\n")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
