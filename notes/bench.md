# BF16 truncate — classed instret log

Same idea as `03-gcd/notes/bench.md`: one row per commit that changes guest hot paths,
measured the same way every time.

## How to read the numbers

Tool: `rv32emu` + `instret` CSR (`bench/stats_class_main.c`).  
**`cycle` ≡ `instret` (CPI=1).** Use for relative ASM vs GCC `-O2` and for HW2 budgets.

KPI for add/sub/mul/div optimization: **hot** bucket avg (both operands finite normal).  
Cold buckets (nan/inf/zero/sub) are logged but not the primary target.

Reproduce:

```bash
cd 02-bf16/bench && source env.sh
make stats-class-asm    # ~26 min here; C baseline: make stats-class-c
# logs: results/stats_class_asm.txt  results/stats_class_c_o2.txt
```

Hex in the raw logs; decimal in the table below. `get_instret` wrap is included (~few insn).

C GCC `-O2` hot avgs (stable reference): **add 45 / sub 48 / mul 102 / div 96**.

## Log (ASM hot avg)

| Commit | Change | add | sub | mul | div | add−C |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: |
| *(pre-git)* | truncate ASM with door `mv a6,a0` / `mv a7,a1` on add | 56 | 58 | 118 | 104 | +11 |
| `TBD` | drop add door copies; unpack/gate from `a0`/`a1` | 54 | 56 | 118 | 104 | +9 |

Append a row for every later commit that changes `bf16_add` / `bf16_sub` / `bf16_mul` / `bf16_div` hot path. Tooling-only commits stay out.

Full bucket dumps: [`PERF_TRUNCATE.md`](../PERF_TRUNCATE.md) and `bench/results/stats_class_*.txt`.
