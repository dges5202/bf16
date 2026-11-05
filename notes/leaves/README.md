# Leaf path-class reports

Full-domain **measured** ASM vs GCC `-O2` instret by control-flow path.

Join: [`../../bench/results/path_class_join.md`](../../bench/results/path_class_join.md).

While iterating on one leaf family, use `OPS=` so you do not re-sweep everything:

```bash
make path-class OPS=add,sub
make path-class-join OPS=add,sub
```

Canonical experiment reports (merged narrative + paths): [`../`](../).


## Leaves

- [`bf16_add.md`](bf16_add.md)
- [`bf16_sub.md`](bf16_sub.md)
- [`bf16_mul.md`](bf16_mul.md)
- [`bf16_div.md`](bf16_div.md)
- [`bf16_sqrt.md`](bf16_sqrt.md)
- [`bf16_sign.md`](bf16_sign.md)
- [`bf16_exp.md`](bf16_exp.md)
- [`bf16_mant.md`](bf16_mant.md)
- [`bf16_isnan.md`](bf16_isnan.md)
- [`bf16_isinf.md`](bf16_isinf.md)
- [`bf16_iszero.md`](bf16_iszero.md)
- [`bf16_pack.md`](bf16_pack.md)
- [`bf16_negate.md`](bf16_negate.md)
- [`u32_to_bf16.md`](u32_to_bf16.md)

## Method

1. Path id = pure function of operands (same IDs on ASM and C).
2. Each call: `get_instret` → leaf → `cls=*` and `path=*`.
3. `contrib = (ASM_avg − C_avg) × n` ranks impact (no guessing).
4. This scan: binaries + unary full; **u32 2³² skipped** (`PATH_CLASS_SKIP_U32`).
5. Binary `cls=all` log n overflows uint32 (2³²); reports use n=4294967296.
