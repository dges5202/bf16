# `bf16_pack` — path-class report

## Reproduce

```bash
cd 02-bf16/bench && source env.sh
make path_class_asm_main.o path_class_c_main.o PATH_CLASS_CFLAGS=-DPATH_CLASS_SKIP_U32
make path-class-asm path-class-c
python3 join_path_class.py results/path_class_asm.txt results/path_class_c_o2.txt results/path_class_join.md
```

- Domain: **FULL 2×256×128**
- ASM block: `bf16_pack`
- Tool: rv32emu, `cycle ≡ instret`, wrap included.
- Raw: [`path_class_asm.txt`](../../bench/results/path_class_asm.txt), [`path_class_c_o2.txt`](../../bench/results/path_class_c_o2.txt), [`path_class_join.md`](../../bench/results/path_class_join.md).
- ASM wall ~1686s; C wall ~1644s (`results/path_class_*.time`).
- Note: log `n`/`sum` are uint32; binary `cls=all` n printed 0 → reported as 2³².

- Single body; path=`one`.

## Coarse class ladder (measured)

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `all` | 65536 | 19 | 21 | -2 | -131072 | 19 | 21 |


## KPI snapshot

- **all**: ASM 19 vs C 21 (-2)

## Fine paths / short-circuits (measured)

Sorted by `|contrib|` where `contrib = (ASM_avg − C_avg) × n`.

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `one` | 65536 | 19 | 21 | -2 | -131072 | 19 | 21 |


## Short-circuit map (by |contrib|)

- `one`: n=65536, gap -2/call, contrib -131072 — **ASM ahead**

## Wins / losses

### ASM wins (gap < 0)

- `one`: -2 avg, contrib -131072

### ASM losses (gap > 0)

- (none)

## Next cuts (from measured contrib only)

- No path losses on this leaf; ROI is elsewhere (or cold-only).
