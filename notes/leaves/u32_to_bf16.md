# `u32_to_bf16` — path-class report

This path_class run used `-DPATH_CLASS_SKIP_U32`. Coarse numbers from prior `stats_class`.

## Coarse class ladder (prior full `stats_class`)

| path | n | ASM avg | C avg | gap | ASM max | C max |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| `all` | 0 | 21 | 22 | -1 | 26 | 27 |
| `hot` | 4278190080 | 22 | 23 | -1 | 26 | 27 |
| `nan` | 16777214 | 16 | 17 | -1 | 16 | 17 |
| `inf` | 2 | 16 | 17 | -1 | 16 | 17 |

## Fine paths

Finite / NaN / Inf only in prior scan.


## Next cuts

- Re-run without `PATH_CLASS_SKIP_U32` for a fresh 2³² path table.
