# Classed instret — ASM vs GCC -O2 (rv32emu)
Measured from `bench/stats_class_*.txt`. Hex in logs; decimal below.
Buckets (exclusive): **hot** / **nan** / **inf** / **zero (±0)** / **sub** / **all**.
`get_instret` wrap included (~few insn overhead).

## Helpers / pack / negate / u32 / sqrt

| Op | Bucket | C avg | ASM avg | ASM−C | C max | ASM max |
|----|--------|------:|--------:|------:|------:|--------:|
| `sign` | all | 13 | 13 | +0 | 13 | 13 |
| `sign` | hot | 13 | 13 | +0 | 13 | 13 |
| `sign` | nan | 13 | 13 | +0 | 13 | 13 |
| `sign` | inf | 13 | 13 | +0 | 13 | 13 |
| `sign` | zero | 13 | 13 | +0 | 13 | 13 |
| `sign` | sub | 13 | 13 | +0 | 13 | 13 |
| `exp` | all | 14 | 14 | +0 | 14 | 14 |
| `exp` | hot | 14 | 14 | +0 | 14 | 14 |
| `exp` | nan | 14 | 14 | +0 | 14 | 14 |
| `exp` | inf | 14 | 14 | +0 | 14 | 14 |
| `exp` | zero | 14 | 14 | +0 | 14 | 14 |
| `exp` | sub | 14 | 14 | +0 | 14 | 14 |
| `mant` | all | 13 | 13 | +0 | 13 | 13 |
| `mant` | hot | 13 | 13 | +0 | 13 | 13 |
| `mant` | nan | 13 | 13 | +0 | 13 | 13 |
| `mant` | inf | 13 | 13 | +0 | 13 | 13 |
| `mant` | zero | 13 | 13 | +0 | 13 | 13 |
| `mant` | sub | 13 | 13 | +0 | 13 | 13 |
| `isnan` | all | 16 | 18 | +2 | 17 | 18 |
| `isnan` | hot | 16 | 18 | +2 | 16 | 18 |
| `isnan` | nan | 17 | 18 | +1 | 17 | 18 |
| `isnan` | inf | 17 | 18 | +1 | 17 | 18 |
| `isnan` | zero | 16 | 18 | +2 | 16 | 18 |
| `isnan` | sub | 16 | 18 | +2 | 16 | 18 |
| `isinf` | all | 16 | 18 | +2 | 17 | 18 |
| `isinf` | hot | 16 | 18 | +2 | 16 | 18 |
| `isinf` | nan | 17 | 18 | +1 | 17 | 18 |
| `isinf` | inf | 17 | 18 | +1 | 17 | 18 |
| `isinf` | zero | 16 | 18 | +2 | 16 | 18 |
| `isinf` | sub | 16 | 18 | +2 | 16 | 18 |
| `iszero` | all | 16 | 17 | +1 | 16 | 17 |
| `iszero` | hot | 16 | 17 | +1 | 16 | 17 |
| `iszero` | nan | 16 | 17 | +1 | 16 | 17 |
| `iszero` | inf | 16 | 17 | +1 | 16 | 17 |
| `iszero` | zero | 16 | 17 | +1 | 16 | 17 |
| `iszero` | sub | 16 | 17 | +1 | 16 | 17 |
| `pack` | all | 20 | 18 | -2 | 20 | 18 |
| `negate` | all | 21 | 20 | -1 | 21 | 20 |
| `negate` | hot | 21 | 20 | -1 | 21 | 20 |
| `negate` | nan | 21 | 20 | -1 | 21 | 20 |
| `negate` | inf | 21 | 20 | -1 | 21 | 20 |
| `negate` | zero | 21 | 20 | -1 | 21 | 20 |
| `negate` | sub | 21 | 20 | -1 | 21 | 20 |
| `u32` | all | 22 | 21 | -1 | 27 | 26 |
| `u32` | hot | 23 | 22 | -1 | 27 | 26 |
| `u32` | nan | 17 | 16 | -1 | 17 | 16 |
| `u32` | inf | 17 | 16 | -1 | 17 | 16 |
| `sqrt` | all | 275 | 274 | -1 | 571 | 571 |
| `sqrt` | hot | 276 | 275 | -1 | 571 | 571 |
| `sqrt` | nan | 27 | 22 | -5 | 27 | 22 |
| `sqrt` | inf | 27 | 24 | -3 | 30 | 25 |
| `sqrt` | zero | 22 | 19 | -3 | 22 | 19 |
| `sqrt` | sub | 279 | 277 | -2 | 570 | 571 |

## Binary ops (FULL 65536²)

| Op | Bucket | C avg | ASM avg | ASM−C | C max | ASM max |
|----|--------|------:|--------:|------:|------:|--------:|
| `mul` | all | 101 | 117 | +16 | 108 | 124 |
| `mul` | hot | 102 | 118 | +16 | 108 | 124 |
| `mul` | nan | 24 | 34 | +10 | 29 | 36 |
| `mul` | inf | 32 | 41 | +9 | 33 | 43 |
| `mul` | zero | 27 | 41 | +14 | 31 | 43 |
| `mul` | sub | 91 | 115 | +24 | 100 | 118 |
| `add` | all | 45 | 53 | +8 | 76 | 77 |
| `add` | hot | 45 | 54 | +9 | 76 | 77 |
| `add` | nan | 26 | 29 | +3 | 30 | 31 |
| `add` | inf | 28 | 30 | +2 | 33 | 35 |
| `add` | zero | 31 | 35 | +4 | 34 | 36 |
| `add` | sub | 44 | 54 | +10 | 68 | 71 |
| `sub` | all | 48 | 55 | +7 | 79 | 79 |
| `sub` | hot | 48 | 56 | +8 | 79 | 79 |
| `sub` | nan | 29 | 31 | +2 | 33 | 33 |
| `sub` | inf | 31 | 32 | +1 | 36 | 37 |
| `sub` | zero | 34 | 37 | +3 | 37 | 38 |
| `sub` | sub | 47 | 56 | +9 | 71 | 73 |
| `div` | all | 95 | 103 | +8 | 106 | 113 |
| `div` | hot | 96 | 104 | +8 | 106 | 113 |
| `div` | nan | 27 | 37 | +10 | 31 | 39 |
| `div` | inf | 30 | 42 | +12 | 34 | 44 |
| `div` | zero | 28 | 46 | +18 | 30 | 48 |
| `div` | sub | 30 | 46 | +16 | 32 | 48 |

## Hot-path focus (optimize here)

| Op | C hot avg | ASM hot avg | gap |
|----|----------:|------------:|----:|
| `mul` | 102 | 118 | +16 (+15.7%) |
| `add` | 45 | 54 | +9 (+20.0%) |
| `sub` | 48 | 56 | +8 (+16.7%) |
| `div` | 96 | 104 | +8 (+8.3%) |
| `sqrt` | 276 | 275 | -1 (-0.4%) |
| `u32` | 23 | 22 | -1 (-4.3%) |
