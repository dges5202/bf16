# `bf16_add` — path-class report

Canonical narrative (timeline + diagnosis): [`../experiments/ADD_SUB.md`](../experiments/ADD_SUB.md).

## Reproduce

```bash
cd 02-bf16/bench && source env.sh
make path_class_asm_main.o path_class_c_main.o PATH_CLASS_CFLAGS=-DPATH_CLASS_SKIP_U32
make path-class-asm path-class-c
python3 join_path_class.py results/path_class_asm.txt results/path_class_c_o2.txt results/path_class_join.md
```

- Domain: **FULL 65536²**
- ASM block: `bf16_add / bf16_add_hot_opp / bf16_add_opp_lt`
- Tool: rv32emu, `cycle ≡ instret`, wrap included.
- Raw: [`path_class_asm.txt`](../../bench/results/path_class_asm.txt), [`path_class_c_o2.txt`](../../bench/results/path_class_c_o2.txt), [`path_class_join.md`](../../bench/results/path_class_join.md).
- ASM wall ~1686s; C wall ~1644s (`results/path_class_*.time`).
- Note: log `n`/`sum` are uint32; binary `cls=all` n printed 0 → reported as 2³².

- Hot taxonomy mirrors truncate align/add/sub/renorm control flow.
- When ea<eb (dexp≥1), ma is shifted ⇒ mb>ma always (`on_lt_mb` specialized arm).

## Coarse class ladder (measured)

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `all` | 4294967296 | 44 | 45 | -1 | -4294967296 | 69 | 76 |
| `hot` | 4228120576 | 44 | 45 | -1 | -4228120576 | 69 | 76 |
| `nan` | 33227772 | 29 | 26 | +3 | +99683316 | 31 | 30 |
| `sub` | 33096708 | 46 | 44 | +2 | +66193416 | 69 | 68 |
| `zero` | 261116 | 34 | 31 | +3 | +783348 | 36 | 34 |
| `inf` | 261124 | 30 | 28 | +2 | +522248 | 35 | 33 |


## KPI snapshot

- **all**: ASM 44 vs C 45 (-1)
- **hot**: ASM 44 vs C 45 (-1)

## Fine paths / short-circuits (measured)

Sorted by `|contrib|` where `contrib = (ASM_avg − C_avg) × n`.

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `ss_wide` | 1991049216 | 44 | 45 | -1 | -1991049216 | 45 | 46 |
| `on_ge_ma` | 1040800384 | 45 | 46 | -1 | -1040800384 | 46 | 53 |
| `on_lt_mb` | 1040800384 | 43 | 44 | -1 | -1040800384 | 47 | 48 |
| `ss_easy` | 45275776 | 43 | 51 | -8 | -362206208 | 43 | 51 |
| `ss_bump` | 32459520 | 47 | 53 | -6 | -194757120 | 49 | 55 |
| `opp_left` | 32394496 | 51 | 57 | -6 | -194366976 | 69 | 76 |
| `ss_swap` | 45275776 | 45 | 48 | -3 | -135827328 | 45 | 48 |
| `cold_nan` | 33227772 | 29 | 26 | +3 | +99683316 | 31 | 30 |
| `cold_sub` | 33096708 | 46 | 44 | +2 | +66193416 | 69 | 68 |
| `cold_zero` | 261116 | 34 | 31 | +3 | +783348 | 36 | 34 |
| `cold_inf` | 261124 | 30 | 28 | +2 | +522248 | 35 | 33 |
| `opp_zero` | 65024 | 37 | 45 | -8 | -520192 | 37 | 45 |


## Short-circuit map (by |contrib|)

- `ss_wide`: n=1991049216, gap -1/call, contrib -1991049216 — **ASM ahead**
- `on_ge_ma`: n=1040800384, gap -1/call, contrib -1040800384 — **ASM ahead**
- `on_lt_mb`: n=1040800384, gap -1/call, contrib -1040800384 — **ASM ahead**
- `ss_easy`: n=45275776, gap -8/call, contrib -362206208 — **ASM ahead**
- `ss_bump`: n=32459520, gap -6/call, contrib -194757120 — **ASM ahead**
- `opp_left`: n=32394496, gap -6/call, contrib -194366976 — **ASM ahead**
- `ss_swap`: n=45275776, gap -3/call, contrib -135827328 — **ASM ahead**
- `cold_nan`: n=33227772, gap +3/call, contrib +99683316 — **ASM behind**
- `cold_sub`: n=33096708, gap +2/call, contrib +66193416 — **ASM behind**
- `cold_zero`: n=261116, gap +3/call, contrib +783348 — **ASM behind**
- `cold_inf`: n=261124, gap +2/call, contrib +522248 — **ASM behind**
- `opp_zero`: n=65024, gap -8/call, contrib -520192 — **ASM ahead**

## Wins / losses

### ASM wins (gap < 0)

- `ss_wide`: -1 avg, contrib -1991049216
- `on_ge_ma`: -1 avg, contrib -1040800384
- `on_lt_mb`: -1 avg, contrib -1040800384
- `ss_easy`: -8 avg, contrib -362206208
- `ss_bump`: -6 avg, contrib -194757120
- `opp_left`: -6 avg, contrib -194366976
- `ss_swap`: -3 avg, contrib -135827328
- `opp_zero`: -8 avg, contrib -520192

### ASM losses (gap > 0)

- `cold_nan`: +3 avg, contrib +99683316
- `cold_sub`: +2 avg, contrib +66193416
- `cold_zero`: +3 avg, contrib +783348
- `cold_inf`: +2 avg, contrib +522248

## Next cuts (from measured contrib only)

1. Largest loss: `cold_nan` (contrib +99683316, gap +3).
2. Next: `cold_sub` (contrib +66193416).
