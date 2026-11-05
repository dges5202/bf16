# BF16 truncate — classed instret log

Same idea as `03-gcd/notes/bench.md`: one row per commit that changes guest hot paths,
measured the same way every time.

## Hard gate (before every push)

1. Run **`make stats-samp-asm`** (~65k hot pairs / op).  
2. Only then write / update numbers in this file, `PERF_TRUNCATE.md`, or any other MD.  
3. Commit the fresh `bench/results/stats_samp_asm.txt` with the code change.  
4. No guessed avgs, no “expect −N”, no MD row without a samp line behind it.

Tooling-only commits (harness/docs with **no** guest `bf16.S` / `bf16.c` change) still run samp once to confirm the KPI did not drift; say so in the commit body if flat.

## Two measurement modes

| Mode | Target | Command | When |
| :--- | :--- | :--- | :--- |
| **samp** | ~65k **hot** pairs / op (LCG, fixed seed) | `make stats-samp-asm` | **every push** (gate above) |
| **full** | FULL 65536² classed buckets | `make stats-class-asm` (~26 min) | **milestones only** |

Tool: `rv32emu` + `instret` CSR. **`cycle` ≡ `instret` (CPI=1).**  
KPI: **hot** avg (both finite normal). Hex in raw logs; decimal in tables.  
`get_instret` wrap included. Samp also prints `sum` (low 32) so sub-integer moves show up before avg ticks.

```bash
cd 02-bf16/bench && source env.sh
make stats-samp-asm          # required before every push
# N_HOT=32768 make stats-samp-asm
make stats-class-asm         # milestone only
```

C GCC `-O2` hot reference (**samp**, same seed): **add 45 / sub 48 / mul 102 / div 96**.

## Log — samp (ASM hot avg)

| Commit | Change | add | sub | mul | div | add−C | sum Δ add |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| `95974ab` | drop add door `mv` a6/a7 | 54 | 56 | 118 | 104 | +9 | (full also 54/56) |
| `24755e0` | same-sign skip left renorm (GCC-O2) | 52 | 54 | 118 | 104 | +7 | ≈ −2.0 /call |
| `7b74626` | early same/opp split + align srl fall-through | 52 | 53 | 118 | 104 | +7 | add ≈ −0.07 /call |
| `cca98e2` | defer mant `andi` to hot / FF | 52 | 53 | 118 | 104 | +7 | hot flat (cold-only) |
| `b5ae676` | same-sign skip Inf check if no exp++ | 51 | 53 | 118 | 104 | +6 | ≈ −0.98 /call |

Tried align fall-through **with** moving sign tails (earlier): samp **regressed** ~+0.44 — not kept. This row keeps sign specialized per arm.

Append a samp row for every later hot-path commit. Tooling-only commits stay out.

## Log — full (milestones)

| Commit | Milestone | add | sub | mul | div |
| :--- | :--- | ---: | ---: | ---: | ---: |
| *(pre-git)* | door mv present | 56 | 58 | 118 | 104 |
| `95974ab` | door mv dropped | 54 | 56 | 118 | 104 |

Full dumps: [`PERF_TRUNCATE.md`](../PERF_TRUNCATE.md), `bench/results/stats_class_*.txt`.  
Samp dumps: `bench/results/stats_samp_*.txt`.  
Add hot-path vs GCC audit: [`ADD_HOT_PATH_AUDIT.md`](ADD_HOT_PATH_AUDIT.md).  
Mul notes: [`MUL_HOT_PATH_AUDIT.md`](MUL_HOT_PATH_AUDIT.md).
