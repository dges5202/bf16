# bf16_add / bf16_sub — experiment report

Canonical experiment report for truncate BF16 **add/sub** hand-ASM campaign vs GCC `-O2` on `rv32emu` (`cycle ≡ instret`).  
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
- Join dump: [`path_class_join.md`](../bench/results/path_class_join.md)
- 5-stage cycle chapter logs: [`../bench/ADD_SUB_CYCLES.md`](../bench/ADD_SUB_CYCLES.md)
- Truncate-vs-IEEE full-domain ulps: [`../TRUNCATE_VS_IEEE.md`](../TRUNCATE_VS_IEEE.md)

---

## 1. How we measure (add/sub only)

While iterating on add/sub, **do not** re-sweep mul/div/helpers.

```bash
cd bench && source env.sh

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

Instret is not the 5-stage story. That measurement and the layout change that followed are [chapter 9](#9-five-stage-cycles-ripes-rv32_5s).

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

Hot normals fall through. Taken branches are the uncommon exits (Ripes charges those).

```text
bf16_add
  unpack sign/exp (mant deferred)
  beq exp==0xFF → special_ff          # not taken on hot
  beqz exp==0   → zero / subnorm tail # not taken on hot
  hot: mant|0x80, li 8
       bne signs → opp                 # same-sign falls through
  same-sign:
       bltu ea<eb → swap arm
       bltu |dexp|<8 → srl             # wide (|dexp|≥8) falls through, small mant = 0
       bgeu sum≥0x100 → carry/Inf      # no-carry falls into pack
  opposite:
       same wide-vs-narrow split
       beq mags equal → +0
       bltu diff<0x80 → 1-bit left renorm
       already-norm falls into pack
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
| Beat C on Ripes 5ST cycles | **Done** (chapter 9: add −7.5, sub −10.5 /call) |
| Cold specials ≤ C | Open (low ROI vs mul/div) |
| Further hot scraping | Instret mostly mined; cycle layout is the chapter 9 cut |

Further BF16 instret ROI shifts to **mul (+14)** / **div (+8)** unless a new measured add/sub loss appears.

---

## 9. Five-stage cycles (Ripes RV32_5S)

rv32emu reports `cycle ≡ instret` (CPI = 1). That KPI said add/sub already beat GCC `-O2` by 1–2 instructions. On a 5-stage pipeline with forwarding the same binaries did not: **add was slower in cycles**.

Logs and commands: [`../bench/ADD_SUB_CYCLES.md`](../bench/ADD_SUB_CYCLES.md).

### 9.1 What Ripes charges

Processor: `RV32_5S` (5-stage, forwarding, hazard detection).  
Not-taken branches fall through at 1 cycle per instruction. A **taken** branch or unconditional `j` flushes the two younger stages, about **+2 cycles**.

So a hot path that is one instruction shorter can still lose if it takes more branches than GCC.

### 9.2 Corpus

Same shape as the mul Ripes sample: **12×12 edges + 8192 normal×normal** (`n = 8336`).  
Harness is identical on the ASM and C ELFs, so

`per-call Δ ≈ (ASM total − C total) / 8336`.

Bit-exact against `bf16_*_ref` (`fail = 0`, same process exit code).

```bash
cd bench && source env.sh
make run-add-sub-cycle    # rv32emu instret on that corpus
make ripes-add-sub        # Ripes cycles
```

### 9.3 Before the layout change

Instret on this corpus matched the hot samp: add **44 vs 45**, sub **46 vs 48**.

| op | instructions ASM−C | cycles ASM−C | CPI ASM | CPI C |
| :--- | ---: | ---: | ---: | ---: |
| add | −1.01 /call | **+1.23 /call** | 1.209 | 1.180 |
| sub | −1.96 /call | −1.70 /call | 1.205 | 1.197 |

Add retired fewer instructions and still spent more cycles. Two taken branches on every normal explain it:

1. `bnez exp_a` / `bnez exp_b` jumped **into** the hot body. On a normal both are taken.
2. The common arms were also taken: `|dexp| ≥ 8` (most uniform exponent pairs) went through `bgeu` + `j`, and a no-carry sum took `bltu` to pack.

### 9.4 What changed in `bf16.S`

Semantics unchanged. Only which arm falls through:

| Check | Now falls through | Taken only when |
| :--- | :--- | :--- |
| exp | hot | `exp == 0` or `exp == 0xFF` |
| align | `\|dexp\| ≥ 8` (small mant = 0) | `\|dexp\| < 8` (actual `srl`) |
| same-sign sum | pack | carry (`sum ≥ 0x100`) or Inf |
| opposite diff | pack | `diff < 0x80` (left renorm) or equal mag |

`bf16_sub` is still `xor` the sign and fall into `bf16_add`, so it picks up the same layout plus the trampoline.

### 9.5 After

| op | ASM cycles | C cycles | ASM−C | CPI ASM | CPI C |
| :--- | ---: | ---: | ---: | ---: | ---: |
| add | **780 082** | 842 841 | **−7.5 /call** | 1.110 | 1.180 |
| sub | **797 069** | 884 622 | **−10.5 /call** | 1.108 | 1.197 |

Against the previous ASM that is about **−8.8 cycles/call** on both ops. Whole-program retired instructions moved only about **−0.4/call**; the per-call instret averages stayed **44 / 46**. The cycle win is CPI, not a shorter trace.

### 9.6 Takeaway

Under CPI = 1, “delete an instruction” and “delete a taken branch” look the same. On RV32_5S they do not. The branch to keep on the hot path is the one that is **not taken**.

---

## 10. Truncation error on real weights (GPT-2)

Chapter 9 is about cycles. This chapter is about **value** error of the working policy: chop extra bits toward zero, no round-to-nearest-even. Full-domain ulp rates against IEEE RNE are in [`../TRUNCATE_VS_IEEE.md`](../TRUNCATE_VS_IEEE.md) (add/sub disagree on ~3.3% of normal×normal pairs, usually 1 ulp). Those rates treat every bf16 pattern as equally likely. Trained weights are not.

### 10.1 What was measured

Source: OpenAI GPT-2 FP32 checkpoint (`openai-community/gpt2`, `model.safetensors`).  
**124 412 160** trained parameters. The causal `attn.bias` masks are not weights and were left out.

Two different questions:

| Question | Method |
| :--- | :--- |
| How far is a stored weight from the FP32 original? | Chop the low 16 bits of the FP32 word (trunc BF16) vs round-to-nearest-even into BF16 |
| How far is one real matmul from an FP32 dot product? | Token embedding × `h.0.attn.c_attn.weight` (`K=768`, `N=2304`) |

The matmul is reported two ways. **Cast + FP32 acc** only chops the inputs, then sums in FP32 (the usual GPU BF16 pattern). **Trunc mul/add** is this tree’s policy: every product and every running add is `bf16_mul` / `bf16_add` from [`bf16.c`](../bf16.c). Self-check of that port: `add(1,1)=2`, `mul(1,2)=2`, `add(1,−1)=0`.

Relative error of a weight is `|cast − fp32| / |fp32|`. Output error is RMS of the residual divided by RMS of the FP32 result. Five embedding rows were used as inputs (token ids 0, 262, 464, 1000, 50000).

### 10.2 Stored weights

Chopping to BF16 moves a normal by at most one BF16 ulp. One ulp at the 7-bit mantissa is `2^−7 ≈ 0.781%` relative.

| | Trunc (chop) | RNE |
| :--- | ---: | ---: |
| Mean relative error | **0.281%** | 0.142% |
| Median (hist edge) | ≤ 0.391% | |
| p99 / p99.9 | ≤ 0.781% | |
| Max relative | **0.775%** | |
| Already exact in BF16 | 0.0015% of weights | |
| Trunc code ≠ RNE code | **49.9%** of weights | |

So on this checkpoint the chop never exceeds 1 ulp, and it roughly doubles the average storage error of RNE. The full-domain “128 ulp” add outliers do not show up: those need overflow, flush, or subnormals, and these weights sit in the normal bin.

### 10.3 One real matmul

`y = embedding @ c_attn`, compared with the FP32 dot product.

| Accumulation | relRMS vs FP32 | cosine |
| :--- | ---: | ---: |
| Trunc-cast inputs, FP32 sum | **0.58%–0.63%** | 0.999998 |
| RNE-cast inputs, FP32 sum | 0.17%–0.21% | |
| Trunc `bf16_mul` + `bf16_add` | **6.4%–7.8%** | 0.9985–0.9990 |

The five tokens land in those bands; none is an outlier. Cosine stays high because the error is a small relative perturbation, not a rotation of the vector.

### 10.4 Why the MAC is worse than the cast

A BF16 mantissa has 7 bits. Casting a weight throws away bits once. The trunc accumulator throws them away **on every add**. The dot is 768 terms long, so the partial sum’s low bits never stick. That is an accumulation-width limit, not a sign that the weights themselves were far from BF16.

FP32 accumulation after a trunc cast stays near the storage error (~0.6% RMS). That is the number to quote for “store the matrix in trunc BF16.” The 6–8% RMS is the number to quote for “reduce the dot with `bf16_add`.”

### 10.5 What this does not say

This is one layer and one matmul, not a full GPT-2 forward. Later layers can compound. It also does not re-measure add/sub instret or Ripes cycles; the arithmetic here is the C policy, bit-identical to the guest contract.
