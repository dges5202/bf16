# Experiment: soft-div loop — branched vs branchless

Files: [`div_loop_variants.S`](div_loop_variants.S) (`bf16_div_br` / `bf16_div_nbr`).

Same gate, specials tree, renorm/pack. Soft-div is the same 16-iter restoring `(mant_a<<7)/mant_b`; only the **compare/subtract** step differs:

- **br**: `bltu` skip → optional `sub` + `ori`
- **nbr**: `sltu`/`xori` → `or` quot bit + mask-`and` subtract (always fall-through)

## Correctness

Against `bf16_div_ref` (truncate C): **both fail=0** on 12×12 edges + 40000 random (`n=40144`).

## rv32emu instret (includes `get_instret` wrap)

| Corpus | br avg | nbr avg | br max | nbr max |
|--------|-------:|--------:|-------:|--------:|
| all (40144) | **208** (0xd0) | 279 (0x117) | 225 | 303 |
| hot-only (~39713) | **210** (0xd2) | 282 (0x11a) | 225 | 303 |

Branchless costs ~**+34%** instructions on the hot path (extra ALU every iter; never skips).

## Ripes RV32_5S (144 edges + 8192 normal/normal)

| Variant | instructions | **cycles** | CPI |
|---------|-------------:|-----------:|----:|
| br | 2086299 | **2655357** | 1.273 |
| nbr | 2686385 | **3055485** | 1.137 |

Branchless has **better CPI** (fewer mispredict/bubble opportunities) but still **more total 5ST cycles** (~+15%) because the instruction bloat dominates.

## Takeaway

For this soft-div loop on 5ST+forwarding: **keep the branched skip**. Branchless is not winning on instret or cycles here; only CPI looks prettier.
