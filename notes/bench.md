# BF16 truncate — classed instret log

Same idea as `03-gcd/notes/bench.md`: one row per commit that changes guest hot paths,
measured the same way every time.

## Two measurement modes

| Mode | Target | Command | When |
| :--- | :--- | :--- | :--- |
| **samp** | ~65k **hot** pairs / op (LCG, fixed seed) | `make stats-samp-asm` | **every** opt commit |
| **full** | FULL 65536² classed buckets | `make stats-class-asm` (~26 min) | **milestones only** |

Tool: `rv32emu` + `instret` CSR. **`cycle` ≡ `instret` (CPI=1).**  
KPI: **hot** avg (both finite normal). Hex in raw logs; decimal in tables.  
`get_instret` wrap included. Samp also prints `sum` (low 32) so sub-integer moves show up before avg ticks.

```bash
cd 02-bf16/bench && source env.sh
make stats-samp-asm          # default per-opt (~seconds)
# N_HOT=32768 make stats-samp-asm
make stats-class-asm         # milestone only
```

C GCC `-O2` hot reference (**samp**, same seed): **add 45 / sub 48 / mul 102 / div 96**.

## Log — samp (ASM hot avg)

| Commit | Change | add | sub | mul | div | add−C | sum Δ add |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| `95974ab` | drop add door `mv` a6/a7 | 54 | 56 | 118 | 104 | +9 | (full also 54/56) |
| `24755e0` | same-sign skip left renorm (GCC-O2) | 52 | 54 | 118 | 104 | +7 | ≈ −2.0 /call |

Tried align common-path fall-through (uncommon tails after `ret`): samp **regressed** ~+0.44 insn/call on add/sub — **not kept**.

Append a samp row for every later hot-path commit. Tooling-only commits stay out.

## Log — full (milestones)

| Commit | Milestone | add | sub | mul | div |
| :--- | :--- | ---: | ---: | ---: | ---: |
| *(pre-git)* | door mv present | 56 | 58 | 118 | 104 |
| `95974ab` | door mv dropped | 54 | 56 | 118 | 104 |

Full dumps: [`PERF_TRUNCATE.md`](../PERF_TRUNCATE.md), `bench/results/stats_class_*.txt`.  
Samp dumps: `bench/results/stats_samp_*.txt`.
