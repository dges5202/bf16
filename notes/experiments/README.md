# Leaf experiment reports (major ops)

Four consolidated write-ups for the large guest leaves. Each file merges KPI,
opt timeline, path/short-circuit tables, side experiments, and next cuts —
so you do not need to chase audits + leaves + bench logs separately.

| Group | Report | Guest | Hot gap (path_class full) |
| :--- | :--- | :--- | ---: |
| **add / sub** | [`ADD_SUB.md`](ADD_SUB.md) | `bf16_add`, `bf16_sub` | −1 / −2 |
| **mul** | [`MUL.md`](MUL.md) | `bf16_mul` | **+14** |
| **div** | [`DIV.md`](DIV.md) | `bf16_div` | **+8** |
| **sqrt** | [`SQRT.md`](SQRT.md) | `bf16_sqrt` | −1 |

## How to measure while editing one group

```bash
cd 02-bf16/bench && source env.sh
make stats-samp-asm OPS=add,sub    # or mul | div | sqrt
make path-class OPS=add,sub        # milestone path full for that group
make path-class-join OPS=add,sub
```

## Source material (folded into the four reports)

| Kind | Location |
| :--- | :--- |
| Samp commit log | [`../bench.md`](../bench.md) |
| Path-class per leaf | [`../leaves/`](../leaves/) |
| Path join dump | [`../../bench/results/path_class_join.md`](../../bench/results/path_class_join.md) |
| Older add audit | [`../ADD_HOT_PATH_AUDIT.md`](../ADD_HOT_PATH_AUDIT.md) → summary in ADD_SUB |
| Older mul audit | [`../MUL_HOT_PATH_AUDIT.md`](../MUL_HOT_PATH_AUDIT.md) → summary in MUL |
| Div loop / cold A/B | [`../../bench/DIV_LOOP_VARIANTS.md`](../../bench/DIV_LOOP_VARIANTS.md), [`../../bench/DIV_COLD_VARIANTS.md`](../../bench/DIV_COLD_VARIANTS.md) → DIV |
| Coarse full class | [`../../PERF_TRUNCATE.md`](../../PERF_TRUNCATE.md) |

Raw numbers in these reports are **measured** (samp or path_class). Narrative only cites those rows.
