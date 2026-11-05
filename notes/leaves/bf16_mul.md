# `bf16_mul` — path-class report

Canonical narrative: [`../MUL.md`](../MUL.md).

## Reproduce

```bash
cd 02-bf16/bench && source env.sh
make path_class_asm_main.o path_class_c_main.o PATH_CLASS_CFLAGS=-DPATH_CLASS_SKIP_U32
make path-class-asm path-class-c
python3 join_path_class.py results/path_class_asm.txt results/path_class_c_o2.txt results/path_class_join.md
```

- Domain: **FULL 65536²**
- ASM block: `bf16_mul / bf16_mul_mul_loop / bf16_mul_specialcase`
- Tool: rv32emu, `cycle ≡ instret`, wrap included.
- Raw: [`path_class_asm.txt`](../../bench/results/path_class_asm.txt), [`path_class_c_o2.txt`](../../bench/results/path_class_c_o2.txt), [`path_class_join.md`](../../bench/results/path_class_join.md).
- ASM wall ~1686s; C wall ~1644s (`results/path_class_*.time`).
- Note: log `n`/`sum` are uint32; binary `cls=all` n printed 0 → reported as 2³².

- Hot: product bit15 → `mul_sh8` else `mul_sh7`; then under/over vs normal pack.
- Cold uses exclusive class ladder (nan/inf/zero/sub).

## Coarse class ladder (measured)

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `all` | 4294967296 | 115 | 101 | +14 | +60129542144 | 122 | 108 |
| `hot` | 4228120576 | 116 | 102 | +14 | +59193688064 | 122 | 108 |
| `sub` | 33096708 | 113 | 91 | +22 | +728127576 | 116 | 100 |
| `nan` | 33227772 | 32 | 24 | +8 | +265822176 | 34 | 29 |
| `zero` | 261116 | 39 | 27 | +12 | +3133392 | 41 | 31 |
| `inf` | 261124 | 39 | 32 | +7 | +1827868 | 41 | 33 |


## KPI snapshot

- **all**: ASM 115 vs C 101 (+14)
- **hot**: ASM 116 vs C 102 (+14)

## Fine paths / short-circuits (measured)

Sorted by `|contrib|` where `contrib = (ASM_avg − C_avg) × n`.

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `mul_sh8` | 1924488720 | 118 | 104 | +14 | +26942842080 | 118 | 108 |
| `mul_sh7` | 1254740232 | 116 | 101 | +15 | +18821103480 | 116 | 101 |
| `mul_over` | 537754624 | 117 | 100 | +17 | +9141828608 | 122 | 102 |
| `mul_under` | 511137000 | 112 | 98 | +14 | +7155918000 | 117 | 100 |
| `cold_sub` | 33096708 | 113 | 91 | +22 | +728127576 | 116 | 100 |
| `cold_nan` | 33227772 | 32 | 24 | +8 | +265822176 | 34 | 29 |
| `cold_zero` | 261116 | 39 | 27 | +12 | +3133392 | 41 | 31 |
| `cold_inf` | 261124 | 39 | 32 | +7 | +1827868 | 41 | 33 |


## Short-circuit map (by |contrib|)

- `mul_sh8`: n=1924488720, gap +14/call, contrib +26942842080 — **ASM behind**
- `mul_sh7`: n=1254740232, gap +15/call, contrib +18821103480 — **ASM behind**
- `mul_over`: n=537754624, gap +17/call, contrib +9141828608 — **ASM behind**
- `mul_under`: n=511137000, gap +14/call, contrib +7155918000 — **ASM behind**
- `cold_sub`: n=33096708, gap +22/call, contrib +728127576 — **ASM behind**
- `cold_nan`: n=33227772, gap +8/call, contrib +265822176 — **ASM behind**
- `cold_zero`: n=261116, gap +12/call, contrib +3133392 — **ASM behind**
- `cold_inf`: n=261124, gap +7/call, contrib +1827868 — **ASM behind**

## Wins / losses

### ASM wins (gap < 0)

- (none)

### ASM losses (gap > 0)

- `mul_sh8`: +14 avg, contrib +26942842080
- `mul_sh7`: +15 avg, contrib +18821103480
- `mul_over`: +17 avg, contrib +9141828608
- `mul_under`: +14 avg, contrib +7155918000
- `cold_sub`: +22 avg, contrib +728127576
- `cold_nan`: +8 avg, contrib +265822176
- `cold_zero`: +12 avg, contrib +3133392
- `cold_inf`: +7 avg, contrib +1827868

## Next cuts (from measured contrib only)

1. Largest loss: `mul_sh8` (contrib +26942842080, gap +14).
2. Next: `mul_sh7` (contrib +18821103480).
