# `bf16_mul` — path-class report

Canonical narrative: [`../MUL.md`](../MUL.md).  
Soft-mul nbr vs br (+ Ripes): [`../../bench/MUL_LOOP_VARIANTS.md`](../../bench/MUL_LOOP_VARIANTS.md).

## Reproduce

```bash
cd bench && source env.sh
make path-class OPS=mul
make path-class-join OPS=mul
```

- Domain: **FULL 65536²** (branchless production baseline)
- ASM block: `bf16_mul` / `bf16_mul_mul_loop` / `bf16_mul_specialcase`
- Tool: rv32emu, `cycle ≡ instret`, wrap included.
- Raw: [`path_class_asm_mul.txt`](../../bench/results/path_class_asm_mul.txt),
  [`path_class_c_o2_mul.txt`](../../bench/results/path_class_c_o2_mul.txt),
  [`path_class_join_mul.md`](../../bench/results/path_class_join_mul.md).
- Note: log `n`/`sum` are uint32; binary `cls=all` n printed 0 → reported as 2³².

- Hot: product bit15 → `mul_sh8` else `mul_sh7`; then under/over vs normal pack.
- Cold uses exclusive class ladder (nan/inf/zero/sub).

## Coarse class ladder (measured)

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `all` | 4294967296 | 114 | 100 | +14 | +60129542144 | 117 | 107 |
| `hot` | 4228120576 | 115 | 101 | +14 | +59193688064 | 117 | 107 |
| `sub` | 33096708 | 112 | 90 | +22 | +728127576 | 115 | 103 |
| `nan` | 33227772 | 31 | 23 | +8 | +265822176 | 33 | 28 |
| `zero` | 261116 | 38 | 26 | +12 | +3133392 | 40 | 30 |
| `inf` | 261124 | 38 | 31 | +7 | +1827868 | 40 | 32 |

## KPI snapshot

- **all**: ASM 114 vs C 100 (+14)
- **hot**: ASM 115 vs C 101 (+14)

## Fine paths / short-circuits (measured)

Sorted by `|contrib|` where `contrib = (ASM_avg − C_avg) × n`.

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `mul_sh8` | 1924488720 | 117 | 103 | +14 | +26942842080 | 117 | 107 |
| `mul_sh7` | 1254740232 | 115 | 100 | +15 | +18821103480 | 115 | 104 |
| `mul_over` | 537754624 | 116 | 99 | +17 | +9141828608 | 117 | 101 |
| `mul_under` | 511137000 | 111 | 97 | +14 | +7155918000 | 112 | 99 |
| `cold_sub` | 33096708 | 112 | 90 | +22 | +728127576 | 115 | 103 |
| `cold_nan` | 33227772 | 31 | 23 | +8 | +265822176 | 33 | 28 |
| `cold_zero` | 261116 | 38 | 26 | +12 | +3133392 | 40 | 30 |
| `cold_inf` | 261124 | 38 | 31 | +7 | +1827868 | 40 | 32 |

## Short-circuit map (by |contrib|)

- `mul_sh8`: gap +14/call — **ASM behind**
- `mul_sh7`: gap +15/call — **ASM behind**
- `mul_over` / `mul_under`: +17 / +14 — **ASM behind**
- cold_* : high gap/call, small n

## Wins / losses

### ASM wins (gap < 0)

- (none)

### ASM losses (gap > 0)

- All measured paths (see table).

## Next cuts (from measured contrib only)

1. Soft-mul loop: prefer **branched** over branchless (see `MUL_LOOP_VARIANTS.md`); land in `bf16.S`.
2. Then re-check `mul_sh8` / `mul_sh7` vs C.
