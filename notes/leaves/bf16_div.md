# `bf16_div` — path-class report

Canonical narrative (incl. loop/cold A/B): [`../experiments/DIV.md`](../experiments/DIV.md).

## Reproduce

```bash
cd 02-bf16/bench && source env.sh
make path_class_asm_main.o path_class_c_main.o PATH_CLASS_CFLAGS=-DPATH_CLASS_SKIP_U32
make path-class-asm path-class-c
python3 join_path_class.py results/path_class_asm.txt results/path_class_c_o2.txt results/path_class_join.md
```

- Domain: **FULL 65536²**
- ASM block: `bf16_div / bf16_div_loop / bf16_div_special`
- Tool: rv32emu, `cycle ≡ instret`, wrap included.
- Raw: [`path_class_asm.txt`](../../bench/results/path_class_asm.txt), [`path_class_c_o2.txt`](../../bench/results/path_class_c_o2.txt), [`path_class_join.md`](../../bench/results/path_class_join.md).
- ASM wall ~1686s; C wall ~1644s (`results/path_class_*.time`).
- Note: log `n`/`sum` are uint32; binary `cls=all` n printed 0 → reported as 2³².

- Hot normals: 8-iter branched soft-div; `div_renorm` = one << when quot<0x80.
- Door still uses a6/a7 copies (−2 candidate). DAZ no-op on both-hot.

## Coarse class ladder (measured)

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `all` | 4294967296 | 103 | 95 | +8 | +34359738368 | 113 | 106 |
| `hot` | 4228120576 | 104 | 96 | +8 | +33824964608 | 113 | 106 |
| `sub` | 33096708 | 46 | 30 | +16 | +529547328 | 48 | 32 |
| `nan` | 33227772 | 37 | 27 | +10 | +332277720 | 39 | 31 |
| `zero` | 261116 | 46 | 28 | +18 | +4700088 | 48 | 30 |
| `inf` | 261124 | 42 | 30 | +12 | +3133488 | 44 | 34 |


## KPI snapshot

- **all**: ASM 103 vs C 95 (+8)
- **hot**: ASM 104 vs C 96 (+8)

## Fine paths / short-circuits (measured)

Sorted by `|contrib|` where `contrib = (ASM_avg − C_avg) × n`.

| path | n | ASM avg | C avg | gap | contrib | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `div_pack` | 1602126336 | 103 | 95 | +8 | +12817010688 | 112 | 104 |
| `div_renorm` | 1577254656 | 107 | 100 | +7 | +11040782592 | 113 | 106 |
| `div_under` | 528482560 | 99 | 90 | +9 | +4756343040 | 107 | 98 |
| `div_over` | 520257024 | 104 | 95 | +9 | +4682313216 | 112 | 103 |
| `cold_sub` | 33096708 | 46 | 30 | +16 | +529547328 | 48 | 32 |
| `cold_nan` | 33227772 | 37 | 27 | +10 | +332277720 | 39 | 31 |
| `cold_zero` | 261116 | 46 | 28 | +18 | +4700088 | 48 | 30 |
| `cold_inf` | 261124 | 42 | 30 | +12 | +3133488 | 44 | 34 |


## Short-circuit map (by |contrib|)

- `div_pack`: n=1602126336, gap +8/call, contrib +12817010688 — **ASM behind**
- `div_renorm`: n=1577254656, gap +7/call, contrib +11040782592 — **ASM behind**
- `div_under`: n=528482560, gap +9/call, contrib +4756343040 — **ASM behind**
- `div_over`: n=520257024, gap +9/call, contrib +4682313216 — **ASM behind**
- `cold_sub`: n=33096708, gap +16/call, contrib +529547328 — **ASM behind**
- `cold_nan`: n=33227772, gap +10/call, contrib +332277720 — **ASM behind**
- `cold_zero`: n=261116, gap +18/call, contrib +4700088 — **ASM behind**
- `cold_inf`: n=261124, gap +12/call, contrib +3133488 — **ASM behind**

## Wins / losses

### ASM wins (gap < 0)

- (none)

### ASM losses (gap > 0)

- `div_pack`: +8 avg, contrib +12817010688
- `div_renorm`: +7 avg, contrib +11040782592
- `div_under`: +9 avg, contrib +4756343040
- `div_over`: +9 avg, contrib +4682313216
- `cold_sub`: +16 avg, contrib +529547328
- `cold_nan`: +10 avg, contrib +332277720
- `cold_zero`: +18 avg, contrib +4700088
- `cold_inf`: +12 avg, contrib +3133488

## Next cuts (from measured contrib only)

1. Largest loss: `div_pack` (contrib +12817010688, gap +8).
2. Next: `div_renorm` (contrib +11040782592).
