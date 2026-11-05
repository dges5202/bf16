# BF16: Truncate (AI-style) vs IEEE RNE Gold

Date measured: 2026-11-05  
Host: Linux (WSL2), C99 `-O2`

## Files

| Role | C | ASM |
|------|---|-----|
| **IEEE RNE + subnormals** (gold) | [`bf16_ieee_rne.c`](bf16_ieee_rne.c) | [`bf16_ieee_rne.S`](bf16_ieee_rne.S) (scaffold; not yet bit-matched) |
| **AI truncate** (working) | [`bf16.c`](bf16.c) | [`bf16.S`](bf16.S) |

### Truncate policy (working)

- No RNE: chop after align / multiply / divide (`>>` + `& 0x7F`).
- No gradual underflow: `exp < 0` → ±0 (add flushes to `+0`); `exp >= 255` → ±Inf.
- Hidden bit only when `exp != 0` on mul/div; add/sqrt always `| 0x80` on the hot path (denorms not gradually handled).
- **Div DAZ:** subnormal inputs (`exp==0 && mant!=0`) flush to ±0 before specials/hot; soft-div is **8-iter aligned** (normal×normal contract after DAZ).

### IEEE policy (gold)

- Integer algorithms matching `RNE(f32(a) op f32(b))` (sqrt: `RNE(sqrtf(f32))`).
- Guard/round/sticky + subnormal in/out.

---

## 1. Per-file validation

### Built-in self-tests (`main`)

| Binary | Result |
|--------|--------|
| `bf16.c` (truncate) | **0 failed** |
| `bf16_ieee_rne.c` (IEEE) | **0 failed** |

### IEEE C vs host float gold (exhaustive)

Gold model: promote bf16→f32 (`bits << 16`), IEEE op in `float`, convert back with the same RNE `u32_to_bf16`.

| Op | Space | Failures |
|----|-------|----------|
| add | 65536² | **0** |
| sub | 65536² | **0** |
| mul | 65536² | **0** |
| div | 65536² | **0** |
| sqrt | 65536 | **0** |

**Conclusion:** `bf16_ieee_rne.c` is bit-exact against the RNE float gold on the full domain.

### Truncate C vs same float gold

Truncate is **not** intended to match IEEE gold. Exhaustive disagree rates vs `RNE(f32 op f32)` are identical to the trunc↔IEEE-C table below (because IEEE-C ≡ gold):

| Op | Disagree vs gold |
|----|------------------|
| add / sub | ~**3.27%** |
| mul | ~**36.96%** |
| div | ~**47.50%** |
| sqrt | ~**8.33%** |

---

## 2. Truncate C vs IEEE C — full domain

Compared `bf16.c` ops to `bf16_ieee_rne.c` ops over the full bf16×bf16 grid (sqrt: full 65536).

**ulp** = distance along the signed bf16 encoding line  
(`mag` with sign: `±(bits & 0x7FFF)`), ignoring NaN-vs-NaN (counted equal).

**flush_mismatch** = one result is ±0 and the other is a nonzero finite (typical flush-to-zero vs gradual underflow / tiny).

### Class tags (inputs)

| Tag | Meaning |
|-----|---------|
| `normal_x_normal` | both exponents in `1..254` |
| `normal_x_sub` / `sub_x_normal` | one normal, one **subnormal** (`exp==0 && mant!=0`) |
| `sub_x_sub` | both subnormal |
| `involves_special` | either side is ±0 / ±Inf / NaN |
| `ALL` | entire domain |

For **sqrt** (unary): `normal` / `subnormal` / `special`.

### Summary tables

Columns: **n** pairs, **differ %**, **exact**, counts at **1 / 2 / ≥3 ulp**, **max ulp**, **flush mismatches**.

#### add (same as sub)

| Class | n | Differ % | Exact | 1 ulp | 2 ulp | ≥3 ulp | max ulp | flush |
|-------|--:|---------:|------:|------:|------:|-------:|--------:|------:|
| ALL | 4 294 967 296 | 3.2737 | 4 154 361 886 | 134 605 482 | 4 843 246 | 1 156 682 | 128 | 108 200 |
| **normal × normal** | 4 228 120 576 | **3.3047** | 4 088 393 428 | 134 333 228 | 4 762 240 | 631 680 | 128 | 59 560 |
| normal × sub | 16 516 096 | 2.4643 | 16 109 096 | 135 872 | 40 246 | 230 882 | 127 | 8 318 |
| sub × normal | 16 516 096 | 2.4643 | 16 109 096 | 135 872 | 40 246 | 230 882 | 127 | 8 318 |
| **sub × sub** | 64 516 | **99.6063** | 254 | 510 | 514 | 63 238 | 127 | 32 004 |
| involves special | 33 750 012 | **0** | 33 750 012 | 0 | 0 | 0 | 0 | 0 |

#### mul

| Class | n | Differ % | Exact | 1 ulp | 2 ulp | ≥3 ulp | max ulp | flush |
|-------|--:|---------:|------:|------:|------:|-------:|--------:|------:|
| ALL | 4 294 967 296 | 36.9565 | 2 707 698 064 | 1 518 758 308 | 6 024 492 | 62 486 432 | 769 | 56 637 580 |
| **normal × normal** | 4 228 120 576 | **37.1292** | 2 658 254 104 | 1 518 378 540 | 5 844 868 | 45 643 064 | **64** | 55 774 068 |
| normal × sub | 16 516 096 | 52.6842 | 7 814 716 | 189 884 | 89 812 | 8 421 684 | 769 | 431 756 |
| sub × normal | 16 516 096 | 52.6842 | 7 814 716 | 189 884 | 89 812 | 8 421 684 | 769 | 431 756 |
| sub × sub | 64 516 | **0** | 64 516 | 0 | 0 | 0 | 0 | 0 |
| involves special | 33 750 012 | **0** | 33 750 012 | 0 | 0 | 0 | 0 | 0 |

#### div

| Class | n | Differ % | Exact | 1 ulp | 2 ulp | ≥3 ulp | max ulp | flush |
|-------|--:|---------:|------:|------:|------:|-------:|--------:|------:|
| ALL | 4 294 967 296 | 47.5043 | 2 254 675 132 | 1 580 607 280 | 396 649 604 | 63 035 280 | 895 | 57 245 432 |
| **normal × normal** | 4 228 120 576 | **47.8560** | 2 204 709 116 | 1 580 458 780 | 396 563 552 | 46 389 128 | **65** | 56 723 720 |
| normal × sub | 16 516 096 | 49.7582 | 8 297 992 | 24 448 | 31 524 | 8 162 132 | 766 | 0 |
| sub × normal | 16 516 096 | 52.1762 | 7 898 624 | 102 320 | 48 012 | 8 467 140 | 895 | 521 712 |
| **sub × sub** | 64 516 | **69.9485** | 19 388 | 21 732 | 6 516 | 16 880 | 764 | 0 |
| involves special | 33 750 012 | **0** | 33 750 012 | 0 | 0 | 0 | 0 | 0 |

#### sqrt

| Class | n | Differ % | Exact | 1 ulp | 2 ulp | ≥3 ulp | max ulp | flush |
|-------|--:|---------:|------:|------:|------:|-------:|--------:|------:|
| ALL | 65 536 | 8.3313 | 60 076 | 5 336 | 2 | 122 | 385 | 0 |
| **normal** | 65 024 | **8.2031** | 59 690 | 5 334 | 0 | 0 | **1** | 0 |
| **subnormal** | 254 | **49.6063** | 128 | 2 | 2 | 122 | 385 | 0 |
| special | 258 | **0** | 258 | 0 | 0 | 0 | 0 | 0 |

---

## 3. How to read the error

1. **Specials agree** on this codebase: NaN / Inf / zero paths match between truncate and IEEE (`involves_special` differ = 0).
2. **Normals (main AI path):**  
   - **add/sub:** ~3.3% disagree; when they disagree, mostly **1 ulp**, but max can reach **128 ulp** (overflow / flush edges).  
   - **mul:** ~37% disagree; vast majority of diffs are **exactly 1 ulp**; max **64 ulp** on normal×normal.  
   - **div:** ~48% disagree; many **1 ulp**, also a large **2 ulp** bucket; max **65 ulp** on normal×normal.  
   - **sqrt:** ~8.2% disagree on normals, and those diffs are **always 1 ulp** (max = 1).
3. **Subnormals are a different story** (policy, not “±1 rounding”):  
   - add/sub **sub×sub** almost always disagree (~99.6%).  
   - mul/div with a subnormal operand often differ by **large ulp** / flush.  
   - Do **not** quote “≤1 ulp” for denorm-involving cases.

### One-paragraph disclaimer (for reports)

> The truncate (AI-style) BF16 ops are **not bit-exact** relative to the IEEE-style RNE gold (`bf16_ieee_rne.c`, verified fail=0 on full 65536² / 65536). On **normal × normal** inputs, disagreement is typically **0 or 1 ulp**, with higher rates for mul/div than add; larger gaps appear near overflow/underflow. **Subnormal** inputs and flush-to-zero vs gradual underflow can produce much larger encoding distances and must be called out separately.

---

## 4. Cross-check (truncate asm vs truncate C)

Guest `cross/` harness (`bf16.S` vs `cross/bf16_ref.c`, truncate semantics): **TOTAL fail = 0** (includes mul/add/sub/div/sqrt/negate), default stride+random sweep.

IEEE asm (`bf16_ieee_rne.S`) is still a scaffold and is **out of scope** for this report.
