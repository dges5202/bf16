# `bf16_isnan` — path-class report

## Reproduce

```bash
cd bench && source env.sh
make path_class_asm_main.o path_class_c_main.o PATH_CLASS_CFLAGS=-DPATH_CLASS_SKIP_U32
make path-class-asm path-class-c
python3 join_path_class.py results/path_class_asm.txt results/path_class_c_o2.txt results/path_class_join.md
```

- Domain: **FULL 65536**
- ASM block: `bf16_isnan`
- Tool: rv32emu, `cycle ≡ instret`, wrap included.
- Raw: [`path_class_asm.txt`](../../bench/results/path_class_asm.txt), [`path_class_c_o2.txt`](../../bench/results/path_class_c_o2.txt), [`path_class_join.md`](../../bench/results/path_class_join.md).
- ASM wall ~1686s; C wall ~1644s (`results/path_class_*.time`).
- Note: log `n`/`sum` are uint32; binary `cls=all` n printed 0 → reported as 2³².

- Path by operand class.

## Coarse class ladder (measured)

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `all` | 65536 | 20 | 18 | +2 | +131072 | 20 | 19 |
| `hot` | 65024 | 20 | 18 | +2 | +130048 | 20 | 18 |
| `sub` | 254 | 20 | 18 | +2 | +508 | 20 | 18 |
| `nan` | 254 | 20 | 19 | +1 | +254 | 20 | 19 |
| `zero` | 2 | 20 | 18 | +2 | +4 | 20 | 18 |
| `inf` | 2 | 20 | 19 | +1 | +2 | 20 | 19 |


## KPI snapshot

- **all**: ASM 20 vs C 18 (+2)
- **hot**: ASM 20 vs C 18 (+2)

## Fine paths / short-circuits (measured)

Sorted by `|contrib|` where `contrib = (ASM_avg − C_avg) × n`.

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `one` | 65024 | 20 | 18 | +2 | +130048 | 20 | 18 |
| `cold_sub` | 254 | 20 | 18 | +2 | +508 | 20 | 18 |
| `cold_nan` | 254 | 20 | 19 | +1 | +254 | 20 | 19 |
| `cold_zero` | 2 | 20 | 18 | +2 | +4 | 20 | 18 |
| `cold_inf` | 2 | 20 | 19 | +1 | +2 | 20 | 19 |


## Short-circuit map (by |contrib|)

- `one`: n=65024, gap +2/call, contrib +130048 — **ASM behind**
- `cold_sub`: n=254, gap +2/call, contrib +508 — **ASM behind**
- `cold_nan`: n=254, gap +1/call, contrib +254 — **ASM behind**
- `cold_zero`: n=2, gap +2/call, contrib +4 — **ASM behind**
- `cold_inf`: n=2, gap +1/call, contrib +2 — **ASM behind**

## Wins / losses

### ASM wins (gap < 0)

- (none)

### ASM losses (gap > 0)

- `one`: +2 avg, contrib +130048
- `cold_sub`: +2 avg, contrib +508
- `cold_nan`: +1 avg, contrib +254
- `cold_zero`: +2 avg, contrib +4
- `cold_inf`: +1 avg, contrib +2

## Next cuts (from measured contrib only)

1. Largest loss: `one` (contrib +130048, gap +2).
2. Next: `cold_sub` (contrib +508).
