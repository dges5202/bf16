# `bf16_exp` — path-class report

## Reproduce

```bash
cd 02-bf16/bench && source env.sh
make path_class_asm_main.o path_class_c_main.o PATH_CLASS_CFLAGS=-DPATH_CLASS_SKIP_U32
make path-class-asm path-class-c
python3 join_path_class.py results/path_class_asm.txt results/path_class_c_o2.txt results/path_class_join.md
```

- Domain: **FULL 65536**
- ASM block: `bf16_exp`
- Tool: rv32emu, `cycle ≡ instret`, wrap included.
- Raw: [`path_class_asm.txt`](../../bench/results/path_class_asm.txt), [`path_class_c_o2.txt`](../../bench/results/path_class_c_o2.txt), [`path_class_join.md`](../../bench/results/path_class_join.md).
- ASM wall ~1686s; C wall ~1644s (`results/path_class_*.time`).
- Note: log `n`/`sum` are uint32; binary `cls=all` n printed 0 → reported as 2³².

- Single body; path=`one`.

## Coarse class ladder (measured)

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `all` | 65536 | 15 | 15 | +0 | +0 | 15 | 15 |
| `hot` | 65024 | 15 | 15 | +0 | +0 | 15 | 15 |
| `nan` | 254 | 15 | 15 | +0 | +0 | 15 | 15 |
| `inf` | 2 | 15 | 15 | +0 | +0 | 15 | 15 |
| `zero` | 2 | 15 | 15 | +0 | +0 | 15 | 15 |
| `sub` | 254 | 15 | 15 | +0 | +0 | 15 | 15 |


## KPI snapshot

- **all**: ASM 15 vs C 15 (+0)
- **hot**: ASM 15 vs C 15 (+0)

## Fine paths / short-circuits (measured)

Sorted by `|contrib|` where `contrib = (ASM_avg − C_avg) × n`.

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `one` | 65536 | 15 | 15 | +0 | +0 | 15 | 15 |


## Short-circuit map (by |contrib|)

- `one`: n=65536, gap +0/call, contrib +0 — **tied**

## Wins / losses

### ASM wins (gap < 0)

- (none)

### ASM losses (gap > 0)

- (none)

## Next cuts (from measured contrib only)

- No path losses on this leaf; ROI is elsewhere (or cold-only).
