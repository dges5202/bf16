# bf16_add / sub — hot path vs GCC -O2 (post opp `ea<eb` specialize)

Samp KPI (fixed seed, `N_HOT=65536`): `bench/results/stats_samp_*.txt`.

| Op | ASM avg | C avg | gap | ASM sum | C sum |
| :--- | ---: | ---: | ---: | ---: | ---: |
| add | **44** | **45** | **−1** | 0x2c96fd | 0x2d61f0 |
| sub | **46** | **48** | **−2** | 0x2e983e | 0x3064ec |

Both avgs and sums beat C. Cross `make test`: ALL OK (same sink as pre-change on samp).

## Path-gap finding (why +1 lingered)

Per-bucket instret (ASM vs C, same LCG) showed **every** same-sign / `opp_left` bucket already ahead of C. The whole residual loss was:

| Bucket | n (≈) | Pre | Issue |
| :--- | ---: | :--- | :--- |
| `on_ge_ma` (ea≥eb, ma>mb, already-norm) | 16k | ASM ahead | fall-through align+sub |
| **`on_lt_mb` (ea<eb, mb>ma, already-norm)** | **16k** | **ASM 48 vs C 44** | paid `j` into shared after + equal/ma compares |

When **ea < eb** and dexp≥1, `ma` is right-shifted (or zero if dexp≥8) ⇒ **`mb > ma` always**. Equal and ma>mb arms are dead on that side.

## Fix

Specialize `bf16_add_opp_lt`: set `sign_b` early, `sub mb-ma`, skip equal/ma>mb; `dexp≥8` → `mant=mb`, pack (skip renorm).

Post path-gap: `on_lt_mb` ASM **42** vs C **44** (ahead). Total sum_gap strongly negative.

## Already taken (do not redo)

Door `mv`; same-sign skip left renorm; early same/opp split; align `srl` fall-through; defer mant; Inf skip; exp-first zero; opp drop ≥0x100/Inf; skip `bgez`; `mant0` fall; **opp `ea<eb` specialize**; rejected: `dexp==0` arm, multi-bit left renorm, align+moved tails.

## Optional further

Add/sub already below C on samp. Next ROI is **mul (+14)** / **div (+8)**. Milestone full classed when ready.
