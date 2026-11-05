# bf16_add / bf16_sub — experiment report

Standalone record of the truncate BF16 **add/sub** hand-ASM campaign vs GCC `-O2` on `rv32emu` (`cycle ≡ instret`).  
Other leaves (mul/div/…) are out of scope here except as KPI context.

| Item | Value |
| :--- | :--- |
| ISA | `rv32i_zicsr` / `ilp32` freestanding |
| Guest | [`bf16.S`](../bf16.S) `bf16_add` / `bf16_sub` |
| C ref | GCC `-O2` [`bench/bf16_c_o2.c`](../bench/bf16_c_o2.c) |
| Policy | Truncate (no RNE); FTZ on exp&lt;0; same bit contract as [`bf16.c`](../bf16.c) |
| KPI | Hot avg instret (both finite normal), wrap included |

**Related files**

- Commit samp log: [`bench.md`](bench.md)
- Latest hot-path audit: [`ADD_HOT_PATH_AUDIT.md`](ADD_HOT_PATH_AUDIT.md)
- Full-domain path tables: [`leaves/bf16_add.md`](leaves/bf16_add.md), [`leaves/bf16_sub.md`](leaves/bf16_sub.md)
- Join dump: [`../bench/results/path_class_join.md`](../bench/results/path_class_join.md)

---

## 1. How we measure (add/sub only)

While iterating on add/sub, **do not** re-sweep mul/div/helpers.

```bash
cd 02-bf16/bench && source env.sh

# Per-opt gate (~seconds): only add+sub, ~65k hot pairs each
make stats-samp-asm OPS=add,sub
# → results/stats_samp_asm_add_sub.txt

# Optional C side-by-side
make stats-samp-c OPS=add,sub

# Milestone path full-domain (~minutes for two binaries, not ~1h all-ops)
make path-class-asm OPS=add,sub
make path-class-c   OPS=add,sub
make path-class-join OPS=add,sub
# → results/path_class_asm_add_sub.txt, path_class_c_o2_add_sub.txt,
#    results/path_class_join_add_sub.md
```

| Mode | Command | When |
| :--- | :--- | :--- |
| **samp** | `make stats-samp-asm OPS=add,sub` | Every add/sub guest push |
| **path full** | `make path-class OPS=add,sub` | Add/sub milestones / path reports |
| **all-ops** | `make stats-samp-asm` / `make path-class` | Rare; cross-leaf KPI only |

Samp with `OPS=add,sub` still advances the LCG through mul’s hot draws (no mul call), so add/sub samples match the historical `OPS=all` stream.

Hard gate for MD numbers: measured samp (or path) line must exist before writing avgs. No guesses.

`bf16_sub` is `xor b,0x8000` then fall into `bf16_add` — same hot taxonomy; trampoline is the only structural extra vs C’s `auipc/jr` style.

---

## 2. Headline results

### Samp (LCG seed `0xC0FFEE`, `N_HOT=65536`) — post `c9df043`

| Op | ASM avg | C avg | gap | ASM sum | C sum |
| :--- | ---: | ---: | ---: | ---: | ---: |
| add | **44** | **45** | **−1** | 0x2c96fd | 0x2d61f0 |
| sub | **46** | **48** | **−2** | 0x2e983e | 0x3064ec |

Cross `make test`: ALL OK; samp sinks unchanged across bit-exact opts.

### Full-domain path-class hot (scan `6c00fa9`)

| Op | ASM hot | C hot | gap |
| :--- | ---: | ---: | ---: |
| add | **44** | **45** | **−1** |
| sub | **46** | **48** | **−2** |

Matches samp direction. Cold (nan/inf/zero/sub) still slightly behind C; volume is tiny vs hot.

---

## 3. Optimization timeline (samp)

Starting point after door work: add **54** / sub **56** (+9 vs C 45/48).  
End: add **44** / sub **46** (**below** C).

| Commit | Change | add | sub | add−C | Notes |
| :--- | :--- | ---: | ---: | ---: | :--- |
| `95974ab` | drop door `mv` a6/a7 | 54 | 56 | +9 | −2 all paths |
| `24755e0` | same-sign skip left renorm | 52 | 54 | +7 | ≈ −2.0 /call |
| `7b74626` | early same/opp + align srl fall-through | 52 | 53 | +7 | small sum win |
| `cca98e2` | defer mant `andi` | 52 | 53 | +7 | hot flat |
| `b5ae676` | same-sign Inf skip if no exp++ | 51 | 53 | +6 | ≈ −0.98 /call |
| `f80519d` | exp-first zero gate | **49** | **51** | **+4** | −2.0 /call |
| `31b54da` | opp renorm: no ≥0x100 / Inf | **46** | **48** | **+1** | ≈ −2.5 /call |
| `b168a70` | opp already-norm skip `bgez` | 46 | 48 | +1 | sum ≈ −0.49 /call |
| `3d32f24` | opp `mant0` fall into renorm | 46 | 48 | +1 | ≈ flat |
| `c9df043` | opp `ea<eb`: always `mb>ma` | **44** | **46** | **−1** | ≈ −1.45 /call |

Mul door (`8bacc39`) paused add; listed in [`bench.md`](bench.md) only.

### Rejected (measured, not kept)

| Idea | Result |
| :--- | :--- |
| Align fall-through **with** moving sign tails | ~+0.44 /call regression |
| Same-sign `dexp==0` dedicated arm | ~+0.24 /call (extra `beqz` on common path) |
| Opp left renorm binary **4/2/1** | bit-exact; add sum +0x4cd (~+0.019/call); not kept |
| Opp left renorm hybrid (force-1 + ladder) | modeled ~+0.006/call; not kept |

Why multi-bit failed: samp hot almost always needs `k=0` or `k=1` left shifts; binary taxes `k=1`.

---

## 4. Control-flow map (current ASM)

```text
bf16_add
  unpack sign/exp (mant deferred)
  gate FF → special_ff
  zero gate (exp-first) → special_zero | hot
  hot: mant|0x80, li 8
       bne signs → opp | fall same
  same-sign:
       ea≥eb fallthrough align → add → bump? → pack (no left renorm)
       ea<eb → swap arm → j after
  opposite:
       ea≥eb fallthrough align → equal? → ma>mb fall renorm | mb>ma j renorm
       ea<eb → opp_lt: sign_b, mb−ma always; wide → pack; else j renorm
  renorm: already-norm → pack; else 1-bit left loop; exp<0 → +0
```

`bf16_sub`: `li t0,0x8000; xor a1,a1,t0` → fall into `bf16_add`.

---

## 5. Path-gap diagnosis (why +1 stuck, then broke)

Dual-measure path buckets (same LCG as samp) showed **all** same-sign / `opp_left` hot buckets already beating C. The residual loss was one arm:

| Bucket | Meaning | Pre-fix | Post-`c9df043` |
| :--- | :--- | :--- | :--- |
| `on_ge_ma` | ea≥eb, ma>mb, already-norm | ASM ahead | ahead |
| **`on_lt_mb`** | **ea&lt;eb, mb>ma, already-norm** | **ASM 48 vs C 44** | **ASM ahead** |

Invariant: when **ea < eb** and dexp≥1, `ma` is right-shifted (or zeroed) ⇒ **`mb > ma` always**. Equal / ma>mb compares on that arm were dead weight.

Fix: `bf16_add_opp_lt` — early `sign_b`, `sub mb−ma`, skip dead compares; `dexp≥8` → `mant=mb` pack.

---

## 6. Full-domain path summary (measured)

From path-class scan (contrib = `(ASM_avg − C_avg) × n`):

### add — hot paths all ahead of C

| path | gap/call | Role |
| :--- | ---: | :--- |
| `ss_wide` | −1 | dominant same-sign volume |
| `on_ge_ma` / `on_lt_mb` | −1 | opp already-norm |
| `ss_easy` | −8 | easy same-sign |
| `ss_bump` / `opp_left` | −6 | less frequent |
| `cold_*` | +2…+3 | rare; still ASM behind |

### sub — same story + trampoline

Hot paths ahead or tied; overall hot **46 vs 48**. Cold slightly behind like add.

Detail tables: [`leaves/bf16_add.md`](leaves/bf16_add.md), [`leaves/bf16_sub.md`](leaves/bf16_sub.md).

---

## 7. What we learned

1. **Easy arms can already beat `-O2`** (slimmer pack, specialized same/opp) while the **average** still loses — must bucket by path, not guess from disasm alone.
2. **Dead compares on rare-looking but high-volume arms** (`on_lt_mb` ~half of opp) dominate residual gaps.
3. **Multi-bit renorm** helps max/heavy cancel but loses on `k=1`-heavy mixes under CPI=1.
4. **Measure before MD**: samp sum catches sub-integer wins; path-class confirms domain-wide.

---

## 8. Status and next (add/sub)

| Goal | Status |
| :--- | :--- |
| Beat C on samp add/sub | **Done** (44/46 vs 45/48) |
| Beat C on full hot | **Done** (same) |
| Cold specials ≤ C | Open (low ROI vs mul/div) |
| Further hot scraping | Mostly mined; only pursue with path contrib evidence |

Further BF16 ROI shifts to **mul (+14)** / **div (+8)** unless a new measured add/sub loss appears.
