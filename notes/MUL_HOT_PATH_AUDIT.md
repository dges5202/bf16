# bf16_mul — hot path notes (ASM vs GCC -O2)

Add/sub paused at samp **51 / 53** (gap +6 / +5). Focus shifts here: mul was
**118 vs 102 (+16)** before the first cut below.

Disasm: [`gcc_o2_bf16_mul_ref.dis`](gcc_o2_bf16_mul_ref.dis).

## First cut

| Commit | Change | mul hot | vs C |
| :--- | :--- | ---: | ---: |
| *(pre)* | door `mv a6,a0` / `mv a7,a1` | 118 | +16 |
| *(this series)* | drop door copies; unpack/cold from `a0`/`a1` | **116** | **+14** |

Same −2/call pattern as add’s door drop.

## Residual (+14) — next candidates

- Soft 8-iter mask-add loop shape vs GCC `.L30` (both ~8 rounds; compare
  mask/`snez` hidden-bit setup and post-loop `>>7`/`>>8` branch).
- Gate: ASM still `slli`+`beqz` mag×2; C uses `beqz exp` then mant (see add audit).
- Pack / under-over: Inf const path, `bge` vs `bltu`.

Do not full-class until a mul milestone.
