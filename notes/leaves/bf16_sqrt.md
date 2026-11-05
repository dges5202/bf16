# `bf16_sqrt` — path-class report

## Reproduce

```bash
cd 02-bf16/bench && source env.sh
make path_class_asm_main.o path_class_c_main.o PATH_CLASS_CFLAGS=-DPATH_CLASS_SKIP_U32
make path-class-asm path-class-c
python3 join_path_class.py results/path_class_asm.txt results/path_class_c_o2.txt results/path_class_join.md
```

- Domain: **FULL 65536**
- ASM block: `bf16_sqrt / bf16_sqrt_bisection_loop`
- Tool: rv32emu, `cycle ≡ instret`, wrap included.
- Raw: [`path_class_asm.txt`](../../bench/results/path_class_asm.txt), [`path_class_c_o2.txt`](../../bench/results/path_class_c_o2.txt), [`path_class_join.md`](../../bench/results/path_class_join.md).
- ASM wall ~1686s; C wall ~1644s (`results/path_class_*.time`).
- Note: log `n`/`sum` are uint32; binary `cls=all` n printed 0 → reported as 2³².

- Hot even/odd unbiased exp; cold ±0 / NaN / ±Inf / neg finite.

## Coarse class ladder (measured)

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `all` | 65536 | 275 | 276 | -1 | -65536 | 572 | 572 |
| `hot` | 65024 | 276 | 277 | -1 | -65024 | 572 | 572 |
| `nan` | 254 | 23 | 28 | -5 | -1270 | 23 | 28 |
| `sub` | 254 | 278 | 280 | -2 | -508 | 572 | 571 |
| `inf` | 2 | 25 | 28 | -3 | -6 | 26 | 31 |
| `zero` | 2 | 20 | 23 | -3 | -6 | 20 | 23 |


## KPI snapshot

- **all**: ASM 275 vs C 276 (-1)
- **hot**: ASM 276 vs C 277 (-1)

## Fine paths / short-circuits (measured)

Sorted by `|contrib|` where `contrib = (ASM_avg − C_avg) × n`.

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `sqrt_hot_even` | 16256 | 523 | 527 | -4 | -65024 | 563 | 565 |
| `sqrt_hot_odd` | 16256 | 531 | 534 | -3 | -48768 | 572 | 572 |
| `sqrt_nan_neg` | 32639 | 25 | 24 | +1 | +32639 | 25 | 28 |
| `cold_nan` | 254 | 23 | 28 | -5 | -1270 | 23 | 28 |
| `cold_sub` | 127 | 531 | 532 | -1 | -127 | 572 | 571 |
| `sqrt_zero` | 2 | 20 | 23 | -3 | -6 | 20 | 23 |
| `sqrt_nan_ninf` | 1 | 26 | 31 | -5 | -5 | 26 | 31 |
| `sqrt_pinf` | 1 | 24 | 25 | -1 | -1 | 24 | 25 |


## Short-circuit map (by |contrib|)

- `sqrt_hot_even`: n=16256, gap -4/call, contrib -65024 — **ASM ahead**
- `sqrt_hot_odd`: n=16256, gap -3/call, contrib -48768 — **ASM ahead**
- `sqrt_nan_neg`: n=32639, gap +1/call, contrib +32639 — **ASM behind**
- `cold_nan`: n=254, gap -5/call, contrib -1270 — **ASM ahead**
- `cold_sub`: n=127, gap -1/call, contrib -127 — **ASM ahead**
- `sqrt_zero`: n=2, gap -3/call, contrib -6 — **ASM ahead**
- `sqrt_nan_ninf`: n=1, gap -5/call, contrib -5 — **ASM ahead**
- `sqrt_pinf`: n=1, gap -1/call, contrib -1 — **ASM ahead**

## Wins / losses

### ASM wins (gap < 0)

- `sqrt_hot_even`: -4 avg, contrib -65024
- `sqrt_hot_odd`: -3 avg, contrib -48768
- `cold_nan`: -5 avg, contrib -1270
- `cold_sub`: -1 avg, contrib -127
- `sqrt_zero`: -3 avg, contrib -6
- `sqrt_nan_ninf`: -5 avg, contrib -5
- `sqrt_pinf`: -1 avg, contrib -1

### ASM losses (gap > 0)

- `sqrt_nan_neg`: +1 avg, contrib +32639

## Next cuts (from measured contrib only)

1. Largest loss: `sqrt_nan_neg` (contrib +32639, gap +1).
