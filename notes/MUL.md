# bf16_mul — experiment report

Canonical write-up for truncate BF16 **mul** hand-ASM vs GCC `-O2` on `rv32emu` (`cycle ≡ instret`).  
Folds [`MUL_HOT_PATH_AUDIT.md`](MUL_HOT_PATH_AUDIT.md), [`leaves/bf16_mul.md`](leaves/bf16_mul.md), samp rows in [`bench.md`](bench.md), and path_class join data.

| Item | Value |
| :--- | :--- |
| Guest | [`bf16.S`](../bf16.S) `bf16_mul` |
| C ref | GCC `-O2` [`bench/bf16_c_o2.c`](../bench/bf16_c_o2.c) |
| Policy | Truncate product; specials before mul; hidden 1 iff exp≠0 |
| Disasm ref | [`gcc_o2_bf16_mul_ref.dis`](gcc_o2_bf16_mul_ref.dis) |
| KPI | Hot avg instret (both finite normal) |

---

## 1. Measure (mul only)

```bash
cd 02-bf16/bench && source env.sh
make stats-samp-asm OPS=mul
make path-class OPS=mul
make path-class-join OPS=mul
```

| Mode | Command | When |
| :--- | :--- | :--- |
| **samp** | `make stats-samp-asm OPS=mul` | Every mul guest push |
| **path full** | `make path-class OPS=mul` | Mul milestones |

---

## 2. Headline results

### Samp (current, seed `0xC0FFEE`)

| Op | ASM | C | gap |
| :--- | ---: | ---: | ---: |
| mul | **116** | **102** | **+14** |

### Path-class full hot (`6c00fa9` scan)

| Bucket | ASM | C | gap |
| :--- | ---: | ---: | ---: |
| hot | **116** | **102** | **+14** |
| all | 115 | 101 | +14 |
| sub | 113 | 91 | +22 |
| nan | 32 | 24 | +8 |
| zero | 39 | 27 | +12 |
| inf | 39 | 32 | +7 |

Older full `stats_class` (pre door) had hot **118 vs 102**; door drop brought samp to 116.

---

## 3. Optimization timeline

| Commit / stage | Change | mul hot | vs C |
| :--- | :--- | ---: | ---: |
| pre | door `mv a6,a0` / `mv a7,a1` | 118 | +16 |
| `8bacc39` | drop door copies; unpack from `a0`/`a1` | **116** | **+14** |

No further mul hot cuts landed yet. Add/sub series was the focus after the door.

---

## 4. Control-flow map (current ASM)

```text
bf16_mul
  unpack signs/exps; sign_xor
  gate: exp==FF either → specialcase
  gate: mag==0 (slli 17) either → specialcase
  hot:
    mant|hid via snez(exp)<<7
    exp = ea+eb-127
    8× mask-add soft mul (branchless bit)
    bit15? >>8, exp++ : >>7
    exp<0 → signed 0; exp≥255 → Inf; else pack
  cold specialcase: mag tree (NaN / Inf×0 / Inf / ±0)
```

---

## 5. Path / short-circuit performance (full domain)

`contrib = (ASM_avg − C_avg) × n`. **Every** measured mul path is behind C.

| path | n | ASM | C | gap | contrib rank |
| :--- | ---: | ---: | ---: | ---: | :--- |
| `mul_sh8` | ~1.92e9 | 118 | 104 | **+14** | #1 loss |
| `mul_sh7` | ~1.25e9 | 116 | 101 | **+15** | #2 |
| `mul_over` | ~5.38e8 | 117 | 100 | **+17** | #3 |
| `mul_under` | ~5.11e8 | 112 | 98 | **+14** | #4 |
| `cold_sub` | ~3.3e7 | 113 | 91 | +22 | #5 |
| `cold_nan` / `zero` / `inf` | smaller | … | … | +7…+12 | cold |

Implication: the +14 is **not** a rare-path artifact — it sits on the common soft-mul + shift/pack epilogue.

Full tables: [`leaves/bf16_mul.md`](leaves/bf16_mul.md).

---

## 6. Open candidates (from audit + path contrib)

Ranked by measured impact, not guesswork:

1. **Soft 8-iter mask-add shape** vs GCC `.L30` (same 8 rounds; compare mask/`snez` setup and post-loop `>>7`/`>>8`).
2. **Gate**: ASM still `slli`+`beqz` mag×2; C uses exp-first (add already proved −2/call there).
3. **Pack / under-over**: Inf const path, `bge` vs `bltu`.
4. Cold trees (nan/zero/inf) — high gap/call, tiny `n` vs hot.

---

## 7. Status

| Goal | Status |
| :--- | :--- |
| Door −2 | **Done** (118→116) |
| Beat C on hot | **Open** (+14) |
| Path-class baseline | **Done** |

**Next:** attack `mul_sh8` / `mul_sh7` with samp `OPS=mul` after each try.
