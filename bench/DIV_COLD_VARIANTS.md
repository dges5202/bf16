# Experiment: `bf16_div` cold path — class encode vs branch tree

> Folded into the canonical div report: [`../notes/experiments/DIV.md`](../notes/experiments/DIV.md).

Files: [`div_cold_variants.S`](div_cold_variants.S) (`bf16_div_class` / `bf16_div_tree`), same gate+hot, different specials.

## Correctness

Against `bf16_div_ref` (truncate C): **both fail=0** on specials×specials edges + 512 random.

## rv32emu instret (includes `get_instret` wrap)

| Corpus | class avg | tree avg | class max | tree max |
|--------|----------:|---------:|----------:|---------:|
| specials-heavy (12×12 edges) | **82** (0x52) | **67** (0x43) | 218 | 218 |
| mixed (+512 random) | 180 (0xb4) | 176 (0xb0) | 219 | 219 |

On **specials**, tree wins ~**18%** fewer instructions. On **mixed**, hot path dominates → nearly tied.

## Ripes (same 144 specials + 128 normals workload)

| Variant | SS cycles (=iret) | **5S cycles** | 5S CPI |
|---------|------------------:|--------------:|-------:|
| class | 37650 | **49402** | 1.312 |
| tree | **35377** | **47129** | 1.332 |

Tree still fewer **total 5ST cycles**; CPI slightly higher (fewer insn, similar stalls).

## Takeaway

Do **not** assume class/fall-through is faster. For this div specials corpus, **branch tree is better** on both instret and 5ST cycles. Class encoding remains fine if you want style parity with `mul`; performance edge goes to the tree here.
