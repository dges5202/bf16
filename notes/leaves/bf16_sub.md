# `bf16_sub` — path-class report

Canonical narrative (timeline + diagnosis): [`../experiments/ADD_SUB.md`](../experiments/ADD_SUB.md).

## Reproduce

```bash
cd 02-bf16/bench && source env.sh
make path_class_asm_main.o path_class_c_main.o PATH_CLASS_CFLAGS=-DPATH_CLASS_SKIP_U32
make path-class-asm path-class-c
python3 join_path_class.py results/path_class_asm.txt results/path_class_c_o2.txt results/path_class_join.md
```

- Domain: **FULL 65536²**
- ASM block: `bf16_sub (xor 0x8000) → fall into bf16_add`
- Tool: rv32emu, `cycle ≡ instret`, wrap included.
- Raw: [`path_class_asm.txt`](../../bench/results/path_class_asm.txt), [`path_class_c_o2.txt`](../../bench/results/path_class_c_o2.txt), [`path_class_join.md`](../../bench/results/path_class_join.md).
- ASM wall ~1686s; C wall ~1644s (`results/path_class_*.time`).
- Note: log `n`/`sum` are uint32; binary `cls=all` n printed 0 → reported as 2³².

- Same path IDs as add on (a, −b); ASM pays trampoline into shared add body.
- Path table uses add taxonomy on flipped b.

## Coarse class ladder (measured)

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `all` | 4294967296 | 46 | 48 | -2 | -8589934592 | 71 | 79 |
| `hot` | 4228120576 | 46 | 48 | -2 | -8456241152 | 71 | 79 |
| `nan` | 33227772 | 31 | 29 | +2 | +66455544 | 33 | 33 |
| `sub` | 33096708 | 48 | 47 | +1 | +33096708 | 71 | 71 |
| `zero` | 261116 | 36 | 34 | +2 | +522232 | 38 | 37 |
| `inf` | 261124 | 32 | 31 | +1 | +261124 | 37 | 36 |


## KPI snapshot

- **all**: ASM 46 vs C 48 (-2)
- **hot**: ASM 46 vs C 48 (-2)

## Fine paths / short-circuits (measured)

Sorted by `|contrib|` where `contrib = (ASM_avg − C_avg) × n`.

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `on_ge_ma` | 1040800384 | 45 | 49 | -4 | -4163201536 | 49 | 58 |
| `ss_wide` | 1991049216 | 46 | 48 | -2 | -3982098432 | 49 | 53 |
| `ss_easy` | 45275776 | 47 | 56 | -9 | -407481984 | 69 | 79 |
| `ss_bump` | 32459520 | 51 | 58 | -7 | -227216640 | 71 | 79 |
| `opp_left` | 32394496 | 47 | 54 | -7 | -226761472 | 51 | 58 |
| `ss_swap` | 45275776 | 48 | 51 | -3 | -135827328 | 70 | 74 |
| `cold_nan` | 33227772 | 31 | 29 | +2 | +66455544 | 33 | 33 |
| `cold_sub` | 33096708 | 48 | 47 | +1 | +33096708 | 71 | 71 |
| `opp_zero` | 65024 | 48 | 57 | -9 | -585216 | 49 | 58 |
| `cold_zero` | 261116 | 36 | 34 | +2 | +522232 | 38 | 37 |
| `cold_inf` | 261124 | 32 | 31 | +1 | +261124 | 37 | 36 |
| `on_lt_mb` | 1040800384 | 47 | 47 | +0 | +0 | 51 | 54 |


## Short-circuit map (by |contrib|)

- `on_ge_ma`: n=1040800384, gap -4/call, contrib -4163201536 — **ASM ahead**
- `ss_wide`: n=1991049216, gap -2/call, contrib -3982098432 — **ASM ahead**
- `ss_easy`: n=45275776, gap -9/call, contrib -407481984 — **ASM ahead**
- `ss_bump`: n=32459520, gap -7/call, contrib -227216640 — **ASM ahead**
- `opp_left`: n=32394496, gap -7/call, contrib -226761472 — **ASM ahead**
- `ss_swap`: n=45275776, gap -3/call, contrib -135827328 — **ASM ahead**
- `cold_nan`: n=33227772, gap +2/call, contrib +66455544 — **ASM behind**
- `cold_sub`: n=33096708, gap +1/call, contrib +33096708 — **ASM behind**
- `opp_zero`: n=65024, gap -9/call, contrib -585216 — **ASM ahead**
- `cold_zero`: n=261116, gap +2/call, contrib +522232 — **ASM behind**
- `cold_inf`: n=261124, gap +1/call, contrib +261124 — **ASM behind**
- `on_lt_mb`: n=1040800384, gap +0/call, contrib +0 — **tied**

## Wins / losses

### ASM wins (gap < 0)

- `on_ge_ma`: -4 avg, contrib -4163201536
- `ss_wide`: -2 avg, contrib -3982098432
- `ss_easy`: -9 avg, contrib -407481984
- `ss_bump`: -7 avg, contrib -227216640
- `opp_left`: -7 avg, contrib -226761472
- `ss_swap`: -3 avg, contrib -135827328
- `opp_zero`: -9 avg, contrib -585216

### ASM losses (gap > 0)

- `cold_nan`: +2 avg, contrib +66455544
- `cold_sub`: +1 avg, contrib +33096708
- `cold_zero`: +2 avg, contrib +522232
- `cold_inf`: +1 avg, contrib +261124

## Next cuts (from measured contrib only)

1. Largest loss: `cold_nan` (contrib +66455544, gap +2).
2. Next: `cold_sub` (contrib +33096708).
