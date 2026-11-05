# Experiment: soft-mul loop — why not branchless?

**Baseline commit** before flipping production `bf16.S` soft-mul from
branchless mask-add → branched `beqz` skip. Production is still **nbr**.

Files: [`mul_loop_variants.S`](mul_loop_variants.S) (`bf16_mul_nbr` / `bf16_mul_br`).

Same gate, specials tree, shift/pack. Only the **8-iter soft product** differs:

- **nbr** (current production): `sub` mask / `and` / always `add`
- **br**: `beqz` skip → optional `add`

## How to run

```bash
cd bench && source env.sh
make run-mul-loop-sample   # edges+40k; vs bf16_mul_ref
make run-mul-loop          # FULL 65536² match + instret (~12 min)
make ripes-mul-loop        # Ripes RV32_5S cycles (12×12 + 8192 hot)
make stats-samp-asm OPS=mul
make path-class OPS=mul && make path-class-join OPS=mul
```

## Production baseline (branchless still in `bf16.S`)

### Samp (`N_HOT=65536`, seed draws match C)

| | ASM (nbr) | C `-O2` | gap |
| :--- | ---: | ---: | ---: |
| mul hot | **115** (0x73) | **101** (0x65) | **+14** |

Logs: [`results/stats_samp_asm_mul.txt`](results/stats_samp_asm_mul.txt),
[`results/stats_samp_c_o2_mul.txt`](results/stats_samp_c_o2_mul.txt).

### Path-class FULL 65536²

| Bucket | ASM | C | gap |
| :--- | ---: | ---: | ---: |
| hot | **115** | **101** | **+14** |
| all | 114 | 100 | +14 |
| `mul_sh8` | 117 | 103 | +14 |
| `mul_sh7` | 115 | 100 | +15 |

Join: [`results/path_class_join_mul.md`](results/path_class_join_mul.md).

## A/B: nbr vs br (variants only)

### Correctness

Against `bf16_mul_ref`: both **fail=0** (edges+40k).  
FULL domain: **br ↔ nbr match fail=0**.

### rv32emu instret (CPI=1, wrap included)

| Corpus | nbr avg | br avg | Δ (br−nbr) |
|--------|--------:|-------:|-----------:|
| sample all | 116 | **105** | −11 |
| sample hot | 117 | **106** | −11 |
| FULL all | 117 | **104** | **−13** |
| FULL hot | 118 | **105** | **−13** |

Log: [`results/mul_loop_rv32emu.txt`](results/mul_loop_rv32emu.txt).

On rv32emu, branchless costs ~**+13 insn/call** on hot — every zero mantissa bit
still pays mask/`and`/`add`.

### Ripes RV32_5S (144 edges + 8192 normal×normal)

Same corpus shape as div-loop Ripes. Log: [`results/mul_loop_ripes.txt`](results/mul_loop_ripes.txt).

| Variant | instructions | **cycles** | CPI |
|---------|-------------:|-----------:|----:|
| **br** | 1 219 015 | **1 505 731** | 1.235 |
| nbr | 1 315 320 | 1 542 802 | 1.173 |

- Branchless has **better CPI** (fewer taken/not-taken swings).
- Branched still wins **total cycles** (~**−2.5%**, −37 071 on this corpus)
  because the instruction bloat dominates the CPI win.

## Discussion — why not keep branchless?

1. **KPI today is rv32emu instret** (`cycle ≡ instret`). Branchless loses ~13/call
   on FULL hot; that alone argues against nbr for the production leaf.
2. **Even on a 5-stage + forwarding model (Ripes)**, nbr does not buy back the
   extra ALU work in wall cycles — only CPI looks prettier.
3. Same lesson as soft-div ([`DIV_LOOP_VARIANTS.md`](DIV_LOOP_VARIANTS.md)):
   skip-when-zero branched inner step beats always-do-ALU branchless here.
4. Landing `br` in `bf16.S` is expected to move production hot ~115 → ~102–105
   (still near C 101; not automatically a win over GCC — further cuts remain).

**Decision pending:** flip production loop in a follow-up commit after this baseline.
