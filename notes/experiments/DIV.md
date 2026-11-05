# bf16_div — experiment report

Canonical write-up for truncate BF16 **div** hand-ASM vs GCC `-O2` on `rv32emu` (`cycle ≡ instret`).  
Folds [`DIV_LOOP_VARIANTS.md`](../../bench/DIV_LOOP_VARIANTS.md), [`DIV_COLD_VARIANTS.md`](../../bench/DIV_COLD_VARIANTS.md), [`leaves/bf16_div.md`](../leaves/bf16_div.md), and path_class join data.

| Item | Value |
| :--- | :--- |
| Guest | [`bf16.S`](../../bf16.S) `bf16_div` |
| C ref | GCC `-O2` [`bench/bf16_c_o2.c`](../../bench/bf16_c_o2.c) |
| Policy | DAZ on subnormals; 8-iter aligned soft-div; truncate renorm/pack |
| KPI | Hot avg instret (both finite normal) |

---

## 1. Measure (div only)

```bash
cd 02-bf16/bench && source env.sh
make stats-samp-asm OPS=div
make path-class OPS=div
make path-class-join OPS=div
```

| Mode | Command | When |
| :--- | :--- | :--- |
| **samp** | `make stats-samp-asm OPS=div` | Every div guest push |
| **path full** | `make path-class OPS=div` | Div milestones |

---

## 2. Headline results

### Samp (current)

| Op | ASM | C | gap |
| :--- | ---: | ---: | ---: |
| div | **104** | **96** | **+8** |

### Path-class full

| Bucket | ASM | C | gap |
| :--- | ---: | ---: | ---: |
| hot | **104** | **96** | **+8** |
| all | 103 | 95 | +8 |
| sub | 46 | 30 | +16 |
| nan | 37 | 27 | +10 |
| zero | 46 | 28 | +18 |
| inf | 42 | 30 | +12 |

Cold zero/sub are expensive on ASM (DAZ + specials tree) but rare vs hot.

---

## 3. Side experiments (kept decisions)

### Soft-div loop: branched vs branchless

Source: [`bench/DIV_LOOP_VARIANTS.md`](../../bench/DIV_LOOP_VARIANTS.md) (`bf16_div_br` / `bf16_div_nbr`).

Same gate/specials/renorm; only the compare/subtract step differs.

| Corpus | br avg | nbr avg |
| :--- | ---: | ---: |
| hot-only (~39713) | **210** | 282 (~+34%) |

Ripes RV32_5S: branchless better CPI but **more total cycles** (~+15%).

**Decision: keep branched skip** (current `bf16_div_loop`).

### Cold specials: class-bit encode vs mag branch tree

Source: [`bench/DIV_COLD_VARIANTS.md`](../../bench/DIV_COLD_VARIANTS.md).

| Corpus | class avg | tree avg |
| :--- | ---: | ---: |
| specials-heavy | 82 | **67** (~−18%) |
| mixed | ~tied (hot dominates) | |

Ripes 5S: tree still fewer cycles.

**Decision: keep mag branch tree** (current `bf16_div_special`).

---

## 4. Control-flow map (current ASM)

```text
bf16_div
  door: mv a6,a0 / mv a7,a1          ← still present (−2 candidate)
  unpack; DAZ (exp==0 → ±0 keep sign)
  gate FF / ±0 → special
  hot:
    mant|hid; exp = ea−eb+127
    rem=ma<<7, d=mb<<7
    8×: quot<<=1; if rem≥d sub + bit; d>>=1   (branched)
    renorm: at most one << if quot<0x80
    exp flush → pack
  cold: mag tree (NaN / 0/0 / Inf/Inf / /0 / Inf / ±0)
```

---

## 5. Path / short-circuit performance (full domain)

All measured hot/cold paths behind C; largest contrib on common epilogues:

| path | n | ASM | C | gap | note |
| :--- | ---: | ---: | ---: | ---: | :--- |
| `div_pack` | ~1.60e9 | 103 | 95 | **+8** | #1 loss |
| `div_renorm` | ~1.58e9 | 107 | 100 | **+7** | #2 (one <<) |
| `div_under` | ~5.28e8 | 99 | 90 | +9 | |
| `div_over` | ~5.20e8 | 104 | 95 | +9 | |
| `cold_*` | smaller | … | … | +10…+18 | DAZ/specials |

Full tables: [`leaves/bf16_div.md`](../leaves/bf16_div.md).

---

## 6. Open candidates (measured order)

1. **Door `mv` a6/a7** — same −2 pattern as add/mul (not done on div yet).
2. **Hot soft-div / pack / renorm** — dominate `div_pack` + `div_renorm` contrib.
3. **Gate** exp-first (add proved useful) after door.
4. Cold trees — optional; high gap/call, low volume.

---

## 7. Status

| Goal | Status |
| :--- | :--- |
| Branched soft-div vs branchless | **Settled** (keep br) |
| Cold tree vs class | **Settled** (keep tree) |
| Door −2 | **Open** |
| Beat C on hot (+8) | **Open** |

**Next:** door drop with `OPS=div` samp; then soft-div/pack against path contrib.
