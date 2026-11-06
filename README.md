# Truncate BF16 on RV32I

Personal side project: BFloat16 arithmetic in pure integer C and hand-written
RV32I assembly, with a truncate (no-RNE / FTZ) working policy and an IEEE RNE
reference twin. Guest work targets `rv32emu` (`cycle ≡ instret`).

This tree is self-contained and relocatable: move the whole directory anywhere;
commands below are relative to this root.

## Layout

| Path | Role |
| :--- | :--- |
| [`bf16.c`](bf16.c) / [`bf16.S`](bf16.S) | Truncate working copy (host C + guest ASM) |
| [`bf16_ieee_rne.c`](bf16_ieee_rne.c) / [`bf16_ieee_rne.S`](bf16_ieee_rne.S) | IEEE RNE + subnormals archive / scaffold |
| [`bench/`](bench/) | `rv32emu` instret harness (samp / class / path) |
| [`cross/`](cross/) | Host cross-checks vs float gold |
| [`notes/`](notes/) | Commit KPI log + leaf experiment reports |
| [`PERF_TRUNCATE.md`](PERF_TRUNCATE.md) | Full-bucket tables |
| [`TRUNCATE_VS_IEEE.md`](TRUNCATE_VS_IEEE.md) | Truncate vs IEEE policy / validation |

## Quick start

```bash
make test                 # host self-test on bf16.c
cd bench && source env.sh
make stats-samp-asm       # hot KPI (~65k pairs / op)
```

Expected host test: every line `PASS`, then `0 failed`.

Ops bodies use only integer bit ops (binary32 as `uint32_t` via `memcpy`).
Host may use integer `*` for the 8-bit mantissa product; the RV32I guest has no `M`.

## Docs index

- Commit / measurement gate: [`notes/bench.md`](notes/bench.md)
- Major-leaf experiments: [`notes/EXPERIMENTS.md`](notes/EXPERIMENTS.md)
- Per-path dumps: [`notes/leaves/`](notes/leaves/)
