# bf16_sqrt — experiment report

Canonical write-up for truncate BF16 **sqrt** hand-ASM vs GCC `-O2` on `rv32emu` (`cycle ≡ instret`).  
Folds [`leaves/bf16_sqrt.md`](leaves/bf16_sqrt.md), [`PERF_TRUNCATE.md`](../PERF_TRUNCATE.md) unary tables, and path_class join data.

| Item | Value |
| :--- | :--- |
| Guest | [`bf16.S`](../bf16.S) `bf16_sqrt` |
| C ref | GCC `-O2` [`bench/bf16_c_o2.c`](../bench/bf16_c_o2.c) |
| Policy | Bisect mid∈[90,256]; `sq=(mid*mid)/128`; truncate pack `high&0x7F` |
| KPI | Hot / all avg instret (unary full 65536) |

---

## 1. Measure (sqrt only)

```bash
cd 02-bf16/bench && source env.sh
make path-class OPS=sqrt          # unary full is fast
make path-class-join OPS=sqrt
# samp harness is binary-oriented; path-class is the primary sqrt KPI
```

| Mode | Command | When |
| :--- | :--- | :--- |
| **path full** | `make path-class OPS=sqrt` | Sqrt changes / milestones |

---

## 2. Headline results

### Path-class full 65536 (scan `6c00fa9`)

| Bucket | ASM | C | gap |
| :--- | ---: | ---: | ---: |
| all | **275** | 276 | **−1** |
| hot | **276** | 277 | **−1** |
| nan | 23 | 28 | −5 |
| sub | 278 | 280 | −2 |
| inf | 25 | 28 | −3 |
| zero | 20 | 23 | −3 |

Older `stats_class` matched: hot **275 vs 276** (−1). ASM is slightly ahead overall.

---

## 3. Optimization notes

No long samp commit series for sqrt (unlike add/sub). Current hand ASM already tracks C closely; work so far was correctness + shared soft-mul style for `mid*mid` inside the bisect.

Load-bearing gate order (must match C):

1. mag==0 → ret a (±0 keep sign) **before** sign→NaN  
2. exp==0xFF → NaN keep a / −Inf→0x7FC0 / +Inf keep a  
3. sign≠0 → 0x7FC0  
4. else hot bisect  

---

## 4. Control-flow map (current ASM)

```text
bf16_sqrt
  mv a6,a0
  unpack sign/exp/mant
  ±0 (slli 17) → ret
  exp==FF → special (NaN keep / −Inf NaN / +Inf keep)
  sign≠0 → 0x7FC0
  hot:
    unbias; mant|0x80
    odd exp: mant<<=1, exp−=1
    new_exp = (exp>>1)+127
    bisect mid in [90,256]
      each probe: soft mid*mid (mask-add, early-out), sq>>7
      adjust low/high
    pack sign=0, new_exp, high&0x7F
```

Dominating cost: **~8 bisect probes × soft mul** → hot avgs ~520–530 on even/odd paths (wrap included in path rows).

---

## 5. Path / short-circuit performance

| path | n | ASM | C | gap | tag |
| :--- | ---: | ---: | ---: | ---: | :--- |
| `sqrt_hot_even` | 16256 | 523 | 527 | **−4** | ASM ahead |
| `sqrt_hot_odd` | 16256 | 531 | 534 | **−3** | ASM ahead |
| `sqrt_nan_neg` | 32639 | 25 | 24 | **+1** | only notable loss |
| `cold_nan` | 254 | 23 | 28 | −5 | ahead |
| `sqrt_zero` / ±Inf | tiny | … | … | ≤0 | ahead |

Hot even/odd already beat C. The only path behind is **negative finite → NaN** (+1/call); volume is large in count of encodings but cheap absolute contrib vs bisect.

Full tables: [`leaves/bf16_sqrt.md`](leaves/bf16_sqrt.md).

---

## 6. Open candidates

1. **`sqrt_nan_neg` +1** — only measured loss; tiny absolute vs hot bisect. Optional polish.  
2. Soft `mid*mid` early-out already present; further cuts need measured probe/insn evidence.  
3. No need to chase hot avg unless a regression appears.

---

## 7. Status

| Goal | Status |
| :--- | :--- |
| Match / beat C on hot | **Done** (−1) |
| Path-class baseline | **Done** |
| Further ROI | Low vs mul/div |

Sqrt is in **maintenance** mode; prefer mul (+14) / div (+8) for next instret work.
