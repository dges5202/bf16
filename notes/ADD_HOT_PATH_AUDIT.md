# bf16_add / sub — hot path vs GCC -O2 (post-`3d32f24`)

Samp KPI (fixed seed, `N_HOT=65536`): `bench/results/stats_samp_*.txt` (re-run with this doc).

| Op | ASM avg | C avg | gap | ASM sum | C sum |
| :--- | ---: | ---: | ---: | ---: | ---: |
| add | **46** | **45** | **+1** | 0x2e0adc | 0x2d61f0 |
| sub | **48** | **48** | **0** | 0x300830 | 0x3064ec |

Sub **sum** is already below C (48.03 vs 48.39); floor hides it. Add body gap ≈ +0.66 before floor.

Disasm: hand `bf16.S`; C [`gcc_o2_bf16_add_ref.dis`](gcc_o2_bf16_add_ref.dis).  
Counts below are **guest retired in the leaf** (no `get_instret` wrap). Wrap is ~same both sides; the +1 avg gap is real mix, not wrap.

## Dynamic counts (approx)

| Scenario | ASM | GCC -O2 | Winner |
| :--- | ---: | ---: | :--- |
| Gate through `\|0x80` / `li 8` (both normal) | ~16 | ~14–16 | ≈ tie |
| Same-sign easy (`ea≥eb`, `dexp∈[1,7]`, sum `<0x100`) | **~30** | **~38** | **ASM** |
| Opposite already-norm (`ma>mb`, diff `≥0x80`) | **~32** | **~39** | **ASM** |
| Opposite left-renorm (1 iter) | ~37 | ~similar + pack tax | ≈ tie / ASM |

GCC pack (`.L118`) is **9** insn (`slli16/srli16/mv`); ours is **6**. That alone is most of the easy-path win.

**Implication:** easy same/opp arms already beat `-O2`. The residual **add +1** avg is from path mix (cancellation left-renorm, rare `dexp==0` waste), not from losing the common easy arm.

## Still different (not free wins)

1. **Same-sign `dexp==0`** — still pays `srl x0` and a failing `li 0x100; bltu`. A dedicated arm needs an extra `beqz` on the **common** `dexp≠0` path → measured **~+0.24/call regression**; **not kept**.
2. **`li t3,8`** before sign split — C loads `7` inside align. Wash / tiny.
3. **Opposite left-shift loop** — same algorithm family as C; heavy cancellation dominates avg when present. Beating C further likely needs a **multi-bit / counted** shift, not another fall-through.
4. **sub** — `xor` + fall into `bf16_add` vs C `auipc/jr` trampoline; structure already fine (tied at 48).

## Already taken (do not redo)

Door `mv` drop; same-sign skip left renorm; early same/opp split; align `srl` fall-through; defer mant `andi`; same-sign Inf skip when no `exp++`; exp-first zero gate; opp drop `≥0x100`/Inf; opp skip `bgez` when already-norm; `mant0` fall into renorm.

## To go **below** C (add ≤44, sub ≤47)

Small fall-through / skip-check ideas are largely mined. Remaining leverage:

| Idea | Risk | Notes |
| :--- | :--- | :--- |
| Faster opposite left renorm (shift by ctz-ish / binary steps) | Med–high | Must stay bit-exact vs truncate C |
| Accept add +1 | — | Easy arms already ahead of GCC |
| Milestone **full** classed | — | Confirm samp vs full domain |

## Step log (this file)

| When | Action |
| :--- | :--- |
| Earlier series | Door, renorm split, zero gate, opp slim — see `bench.md` |
| Now | Refresh audit to match post-`3d32f24` reality; re-samp |
