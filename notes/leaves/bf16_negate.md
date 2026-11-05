# `bf16_negate` — path-class report

## Reproduce

```bash
cd 02-bf16/bench && source env.sh
make path_class_asm_main.o path_class_c_main.o PATH_CLASS_CFLAGS=-DPATH_CLASS_SKIP_U32
make path-class-asm path-class-c
python3 join_path_class.py results/path_class_asm.txt results/path_class_c_o2.txt results/path_class_join.md
```

- Domain: **FULL 65536 (n=1) + n=0**
- ASM block: `bf16_negate`
- Tool: rv32emu, `cycle ≡ instret`, wrap included.
- Raw: [`path_class_asm.txt`](../../bench/results/path_class_asm.txt), [`path_class_c_o2.txt`](../../bench/results/path_class_c_o2.txt), [`path_class_join.md`](../../bench/results/path_class_join.md).
- ASM wall ~1686s; C wall ~1644s (`results/path_class_*.time`).
- Note: log `n`/`sum` are uint32; binary `cls=all` n printed 0 → reported as 2³².

- In-place xor sign; `path=n0` is empty-loop measure.

## Coarse class ladder (measured)

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `all` | 65536 | 20 | 21 | -1 | -65536 | 20 | 21 |
| `hot` | 65024 | 20 | 21 | -1 | -65024 | 20 | 21 |
| `nan` | 254 | 20 | 21 | -1 | -254 | 20 | 21 |
| `sub` | 254 | 20 | 21 | -1 | -254 | 20 | 21 |
| `inf` | 2 | 20 | 21 | -1 | -2 | 20 | 21 |
| `zero` | 2 | 20 | 21 | -1 | -2 | 20 | 21 |


## KPI snapshot

- **all**: ASM 20 vs C 21 (-1)
- **hot**: ASM 20 vs C 21 (-1)

## Fine paths / short-circuits (measured)

Sorted by `|contrib|` where `contrib = (ASM_avg − C_avg) × n`.

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `one` | 65536 | 20 | 21 | -1 | -65536 | 20 | 21 |
| `n0` | 1 | 13 | 12 | +1 | +1 | 13 | 12 |


## Short-circuit map (by |contrib|)

- `one`: n=65536, gap -1/call, contrib -65536 — **ASM ahead**
- `n0`: n=1, gap +1/call, contrib +1 — **ASM behind**

## Wins / losses

### ASM wins (gap < 0)

- `one`: -1 avg, contrib -65536

### ASM losses (gap > 0)

- `n0`: +1 avg, contrib +1

## Next cuts (from measured contrib only)

1. Largest loss: `n0` (contrib +1, gap +1).
