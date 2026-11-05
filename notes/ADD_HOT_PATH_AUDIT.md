# bf16_add — same-sign hot path audit (ASM vs GCC -O2)

Measured context: samp ASM add hot **52** / C **45** (gap **+7**) at `7b74626`.  
Disasm: hand `bf16.S` @ that commit; C from `gcc_o2_bf16_add_ref.dis`.

Scenario counted below (both finite normal, **same sign**, `exp_a >= exp_b`,
`dexp < 8`, sum `< 0x100`, result exp `< 255`): the common “easy” arm.

## Dynamic insn count (retired)

| Stage | ASM (hand) | GCC -O2 | Δ (ASM−C) |
| :--- | ---: | ---: | ---: |
| Unpack + FF gate | 11 | 9 | +2 |
| Zero / nonzero gate | 4 (`slli`+`beqz`×2) | 2 (`bnez exp`×2 on hot) | **+2** |
| Sign split + `\|0x80` | 4 | 4 | 0 |
| Align (ea≥eb, dexp&lt;8) | 4 | ~8 (swap block + `j`) | −4 |
| Add + overflow test | 3 | 3 | 0 |
| Pack (+ Inf test) | 8 | 9 (`slli16/srli16/mv`) | −1 |
| **Total this scenario** | **~34** | **~35** | **≈ 0** |

Wrap (`get_instret`) sits on top of both; gap in **avg** is not explained by
this easy same-sign arm alone — opposite-sign + distribution dominate the +7.

## Where ASM still pays (candidates)

1. **Zero gate** — **done** (exp-first `bnez`); hot normals pay 2 not 4.
2. **Eager `andi` mant** — 2 insn before we know we are hot; C extracts mant after
   exp/sign gates. → **step 2**.
3. **Same-sign pack** — `li 255` twice on Inf path; finite path is 6 insn vs C’s 9
   (we are already leaner). Possible: share Inf const / fall-through into pack_fin. → **step 3**.
4. **Opposite-sign renorm** — **done**: drop ≥0x100 / Inf (diff ∈ [1,0xFF]); keep
   left loop + `bgez`. Residual add gap ~+1 vs C.

## Already taken (do not redo)

- Door `mv` drop, same-sign skip left renorm, early same/opp split, align `srl` fall-through.

## Step plan (this series)

| Step | Action | Push rule |
| :---: | :--- | :--- |
| 1 | This audit (no guest change) | samp confirm flat |
| 2 | Defer `andi` mant until hot / FF | samp: **hot flat** (andi still on hot; zero/FF cold skip them) |
| 3 | Slim same-sign pack | samp + MD |
| 4 | Pause add; start mul (+16) | samp + MD |
