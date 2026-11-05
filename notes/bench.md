# BF16 truncate — classed instret log

Same idea as `03-gcd/notes/bench.md`: one row per commit that changes guest hot paths,
measured the same way every time.

## Hard gate (before every push)

1. Run a **samp** for the leaves you changed (see `OPS=` below).  
2. Only then write / update numbers in this file, leaf reports, or `PERF_TRUNCATE.md`.  
3. Commit the fresh samp log with the code change.  
4. No guessed avgs, no “expect −N”, no MD row without a samp line behind it.

Tooling-only commits (harness/docs with **no** guest `bf16.S` / `bf16.c` change) still run samp once to confirm the KPI did not drift; say so in the commit body if flat.

### `OPS=` — only sweep what you are editing

Default `OPS=all`. While tuning **add/sub**, do **not** re-run mul/div/helpers:

```bash
cd 02-bf16/bench && source env.sh
make stats-samp-asm OPS=add,sub     # → results/stats_samp_asm_add_sub.txt
make path-class-asm OPS=add,sub     # full path for add+sub only (~minutes)
make path-class-join OPS=add,sub
```

| Focus | Samp | Path full |
| :--- | :--- | :--- |
| add/sub | `OPS=add,sub` | `OPS=add,sub` |
| mul | `OPS=mul` | `OPS=mul` |
| div | `OPS=div` | `OPS=div` |
| everything | `OPS=all` (default) | `OPS=all` |

Filtered logs use a slug suffix (`_add_sub`) so they do not overwrite the all-ops files.  
Samp still **burns** LCG draws for skipped ops (no leaf call), so add/sub pairs match `OPS=all` history.

## Measurement modes

| Mode | Target | Command | When |
| :--- | :--- | :--- | :--- |
| **samp** | ~65k **hot** pairs / selected op | `make stats-samp-asm OPS=…` | **every push** (gate above) |
| **full** | FULL 65536² classed buckets | `make stats-class-asm` (~26 min) | **milestones only** |
| **path** | FULL domain + fine paths | `make path-class OPS=…` | **path reports / milestones** |

Per-leaf path reports: [`notes/leaves/`](leaves/).  
**Add/sub experiment write-up:** [`ADD_SUB_EXPERIMENT.md`](ADD_SUB_EXPERIMENT.md).

Tool: `rv32emu` + `instret` CSR. **`cycle` ≡ `instret` (CPI=1).**  
KPI: **hot** avg (both finite normal). Hex in raw logs; decimal in tables.  
`get_instret` wrap included. Samp also prints `sum` (low 32) so sub-integer moves show up before avg ticks.

```bash
cd 02-bf16/bench && source env.sh
make stats-samp-asm OPS=add,sub   # typical while on add/sub
# N_HOT=32768 make stats-samp-asm OPS=add,sub
make path-class OPS=add,sub       # add/sub path milestone
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
| `8bacc39` | mul: drop door `mv` a6/a7 (add paused) | 51 | 53 | **116** | 104 | +6 | mul −2.0 /call |
| `f80519d` | add zero gate: exp-first (not mag slli×2) | **49** | **51** | 116 | 104 | **+4** | −2.0 /call |
| `31b54da` | opp renorm: drop ≥0x100 / Inf (diff∈[1,FF]) | **46** | **48** | 116 | 104 | **+1** | ≈ −2.5 /call |
| `b168a70` | opp already-norm skip bgez | **46** | **48** | 116 | 104 | **+1** | ≈ −0.49 /call |
| `3d32f24` | opp mant0 fall into renorm | **46** | **48** | 116 | 104 | **+1** | ≈ −0.001 /call |
| `c9df043` | opp `ea<eb`: always `mb>ma`, skip cmp | **44** | **46** | 116 | 104 | **−1** | ≈ −1.45 /call |

Tried align fall-through **with** moving sign tails (earlier): samp **regressed** ~+0.44 — not kept. This row keeps sign specialized per arm.

Tried same-sign `dexp==0` fast path: samp **regressed** ~+0.24/call (extra `beqz` on common arm) — **not kept**.

Tried opposite left renorm **binary 4/2/1**: bit-exact (same sink) but add sum **+0x4cd** (~+0.019/call), sub sum **+0x501**; avg flat 46/48 — **not kept**. Hybrid (force-1 + ladder) modeled worse on `k=2..4` — not kept. Detail: `ADD_HOT_PATH_AUDIT.md`.

Append a samp row for every later hot-path commit. Tooling-only commits stay out.

## Log — full (milestones)

| Commit | Milestone | add | sub | mul | div |
| :--- | :--- | ---: | ---: | ---: | ---: |
| *(pre-git)* | door mv present | 56 | 58 | 118 | 104 |
| `95974ab` | door mv dropped | 54 | 56 | 118 | 104 |

Full dumps: [`PERF_TRUNCATE.md`](../PERF_TRUNCATE.md), `bench/results/stats_class_*.txt`.  
Samp dumps: `bench/results/stats_samp_*.txt` (filtered: `*_add_sub.txt`, …).  
Add/sub experiment report: [`ADD_SUB_EXPERIMENT.md`](ADD_SUB_EXPERIMENT.md).  
Add hot-path vs GCC audit: [`ADD_HOT_PATH_AUDIT.md`](ADD_HOT_PATH_AUDIT.md).  
Mul notes: [`MUL_HOT_PATH_AUDIT.md`](MUL_HOT_PATH_AUDIT.md).
