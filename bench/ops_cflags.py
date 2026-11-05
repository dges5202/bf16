#!/usr/bin/env python3
"""Map OPS=all|add,sub|mul,... to -DOPS_FILTER -DOP_*=1 for harness mains."""
from __future__ import annotations

import sys

KNOWN = ("mul", "add", "sub", "div", "sqrt", "sign", "exp", "mant",
         "isnan", "isinf", "iszero", "pack", "negate", "u32", "helpers")


def main() -> int:
    raw = sys.argv[1] if len(sys.argv) > 1 else "all"
    raw = raw.strip().lower()
    if not raw or raw == "all":
        print("")
        return 0
    flags = ["-DOPS_FILTER"]
    for part in raw.split(","):
        op = part.strip()
        if not op:
            continue
        if op == "helpers":
            for h in ("sign", "exp", "mant", "isnan", "isinf", "iszero", "pack", "negate"):
                flags.append(f"-DOP_{h.upper()}=1")
            continue
        if op not in KNOWN:
            print(f"unknown op in OPS: {op}", file=sys.stderr)
            return 2
        flags.append(f"-DOP_{op.upper()}=1")
    print(" ".join(flags))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
