# Truncate BF16 — instruction counts (rv32emu)

Measured: 2026-11-05 (post add door-mv drop)  
Tool: `rv32emu` + `instret` CSR (`bench/stats_class_main.c`)  
ISA: `rv32i_zicsr` / `ilp32`  
Commit log (hot KPI): [`notes/bench.md`](notes/bench.md)

## Builds

| Build | Source |
|-------|--------|
| **C GCC -O2** | [`bench/bf16_c_o2.c`](bench/bf16_c_o2.c) (= truncate `cross/bf16_ref.c`) |
| **Hand ASM** | [`bf16.S`](bf16.S) |

## How to re-run

```bash
cd bench && source env.sh
make stats-samp-asm       # per-opt KPI: ~65k hot pairs / op (seconds)
make stats-class          # milestone only: FULL domain; ~27 min each
# logs:
#   results/stats_samp_*.txt
#   results/stats_class_c_o2.txt
#   results/stats_class_asm.txt
#   results/STATS_CLASS_SUMMARY.md
# commit log: notes/bench.md
```

## rv32emu limit

**`cycle` ≡ `instret` (CPI=1).** No LU stall / branch flush / cache.  
Each sample includes `get_instret` wrap (~few insn). Use for **relative** ASM vs C and HW2 budgets.

## Classification (exclusive ladder)

| Bucket | Meaning |
|--------|---------|
| **hot** | finite normal (`exp∈[1,254]`); binary = **both** hot |
| **nan** | either operand NaN |
| **inf** | either Inf, neither NaN |
| **zero** | either ±0 (`0x0000`/`0x8000`), neither nan/inf (log also has `n_p0` / `n_m0`) |
| **sub** | either subnormal (`exp==0 && mant≠0`) |
| **all** | full domain |

Domains: helpers/sqrt/negate **65536**; pack **2×256×128**; binary **65536²**; `u32_to_bf16` **2³²**.

---

## Hot-path focus (optimize these)

Full-domain path-class (post `c9df043` / this scan): see [`notes/leaves/`](notes/leaves/).

| Op | C hot avg | ASM hot avg | gap | Source |
|----|----------:|------------:|----:|:---|
| `mul` | 102 | **116** | **+14** | path_class full hot |
| `add` | 45 | **44** | **−1** | path_class full hot |
| `sub` | 48 | **46** | **−2** | path_class full hot |
| `div` | 96 | **104** | **+8** | path_class full hot |
| `sqrt` | 527/534† | 523/531† | −4/−3 | path even/odd; overall still ~tied |
| `u32_to_bf16` | 23 | 22 | −1 | prior stats_class |

† sqrt hot split by even/odd unbiased exp (bisect dominates).

**Priority:** **mul** (+14) then **div** (+8). Add/sub already below C on full hot.

Per-path tables: [`notes/leaves/`](notes/leaves/).  
Canonical experiment reports: [`notes/experiments/`](notes/experiments/) (add/sub · mul · div · sqrt).

---

## Full bucket tables (decimal avg / max)

### Binary — FULL 65536²

| Op | Bucket | C avg | ASM avg | ASM−C | C max | ASM max |
|----|--------|------:|--------:|------:|------:|--------:|
| `mul` | all | 101 | 117 | +16 | 108 | 124 |
| `mul` | **hot** | **102** | **118** | **+16** | 108 | 124 |
| `mul` | nan | 24 | 34 | +10 | 29 | 36 |
| `mul` | inf | 32 | 41 | +9 | 33 | 43 |
| `mul` | zero | 27 | 41 | +14 | 31 | 43 |
| `mul` | sub | 91 | 115 | +24 | 100 | 118 |
| `add` | all | 45 | 53 | +8 | 76 | 77 |
| `add` | **hot** | **45** | **54** | **+9** | 76 | 77 |
| `add` | nan | 26 | 29 | +3 | 30 | 31 |
| `add` | inf | 28 | 30 | +2 | 33 | 35 |
| `add` | zero | 31 | 35 | +4 | 34 | 36 |
| `add` | sub | 44 | 54 | +10 | 68 | 71 |
| `sub` | all | 48 | 55 | +7 | 79 | 79 |
| `sub` | **hot** | **48** | **56** | **+8** | 79 | 79 |
| `sub` | nan | 29 | 31 | +2 | 33 | 33 |
| `sub` | inf | 31 | 32 | +1 | 36 | 37 |
| `sub` | zero | 34 | 37 | +3 | 37 | 38 |
| `sub` | sub | 47 | 56 | +9 | 71 | 73 |
| `div` | all | 95 | 103 | +8 | 106 | 113 |
| `div` | **hot** | **96** | **104** | **+8** | 106 | 113 |
| `div` | nan | 27 | 37 | +10 | 31 | 39 |
| `div` | inf | 30 | 42 | +12 | 34 | 44 |
| `div` | zero | 28 | 46 | +18 | 30 | 48 |
| `div` | sub | 30 | 46 | +16 | 32 | 48 |

Div **zero/sub** ASM costs more: DAZ + specials tree vs C’s early DAZ/iszero chain — specials still rare vs hot.

### Unary / helpers

| Op | Bucket | C avg | ASM avg | ASM−C | C max | ASM max |
|----|--------|------:|--------:|------:|------:|--------:|
| `sqrt` | all | 275 | 274 | −1 | 571 | 571 |
| `sqrt` | **hot** | **276** | **275** | **−1** | 571 | 571 |
| `sqrt` | nan | 27 | 22 | −5 | 27 | 22 |
| `sqrt` | inf | 27 | 24 | −3 | 30 | 25 |
| `sqrt` | zero | 22 | 19 | −3 | 22 | 19 |
| `sqrt` | sub | 279 | 277 | −2 | 570 | 571 |
| `negate` | all/hot/… | 21 | 20 | −1 | 21 | 20 |
| `u32` | all | 22 | 21 | −1 | 27 | 26 |
| `u32` | hot (finite) | 23 | 22 | −1 | 27 | 26 |
| `u32` | nan/inf | 17 | 16 | −1 | 17 | 16 |
| `pack` | all | 20 | 18 | −2 | 20 | 18 |
| `sign`/`exp`/`mant` | all | 13–14 | 13–14 | 0 | — | — |
| `isnan`/`isinf` | all | 16 | 18 | +2 | 17 | 18 |
| `iszero` | all | 16 | 17 | +1 | 16 | 17 |

---

## Optimization guidance (from this baseline)

1. **Primary KPI:** hot-path avg instret — beat C on `add`/`sub`/`mul` (biggest gaps).  
2. **Specials:** ASM nan/inf/zero often **worse** than C (especially `mul`/`div` zero/inf). Worth a later pass on cold trees, but volume is tiny vs hot.  
3. **sqrt / u32 / negate / pack:** already ≤ C — leave unless algorithm change.  
4. Ripes 5ST remains a **separate** column later; do not mix with these instret numbers.

## Older notes

Quick edge+20k sample and early Ripes microbench history: see git history / prior sections if needed. Authoritative full-domain classed numbers are this file + `bench/results/stats_class_*.txt`.
