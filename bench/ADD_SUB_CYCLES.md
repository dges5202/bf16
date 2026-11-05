# add/sub — instret win vs Ripes cycles

Narrative chapter: [`../notes/ADD_SUB.md`](../notes/ADD_SUB.md) §9. This file is the measurement log.

Same question as the soft-mul loop: fewer instructions retired does not
automatically mean fewer 5-stage cycles.

Corpus matches [`mul_loop_ripes_main.c`](mul_loop_ripes_main.c): **12×12 edges + 8192 normal×normal** (`n=8336`).  
Hand ASM = current `bf16.S`. C = GCC `-O2` `bf16_*_ref`. Bit-exact (fail=0; same exit code).

```bash
cd bench && source env.sh
make run-add-sub-cycle    # rv32emu per-call instret
make ripes-add-sub        # Ripes RV32_5S whole-program cycles
```

## rv32emu instret (wrap included; CPI=1)

| op | ASM | C | ASM−C |
| :--- | ---: | ---: | ---: |
| add | 44 | 45 | **−1** |
| sub | 46 | 48 | **−2** |

Log: [`results/add_sub_cycle_rv32emu.txt`](results/add_sub_cycle_rv32emu.txt).  
Matches the hot samp (44/46 vs 45/48).

## Ripes RV32_5S (whole program; harness identical)

`n=8336`. Per-call Δ ≈ (ASM total − C total) / 8336.

| op | | instructions | cycles | CPI |
| :--- | :--- | ---: | ---: | ---: |
| add | ASM | 705 587 | **853 133** | 1.209 |
| add | C | 713 995 | **842 841** | 1.180 |
| add | ASM−C | **−1.01 /call** | **+1.23 /call** | |
| sub | ASM | 722 636 | **870 474** | 1.205 |
| sub | C | 739 014 | **884 622** | 1.197 |
| sub | ASM−C | **−1.96 /call** | **−1.70 /call** | |

Log: [`results/add_sub_ripes.txt`](results/add_sub_ripes.txt).

## Read (before fall-through layout)

- **add:** instret −1, but CPI 1.209 vs 1.180. Cycles went **up** ~1.2/call.
- **sub:** instret −2 survived as cycles **−1.7/call**.
- Cause: hot path took `bnez exp` into the body, and the common `|dexp|≥8` / no-carry arms were taken branches. RV32_5S charges about 2 cycles per taken branch.

## After: not-taken hot layout (`bf16.S`)

Same corpus, fail=0. Hot exits (`exp==0` / `exp==0xFF`, narrow shift, carry, left renorm) are the taken arms.

| op | ASM cycles | C cycles | ASM−C | CPI ASM | CPI C |
| :--- | ---: | ---: | ---: | ---: | ---: |
| add | **780 082** | 842 841 | **−7.5 /call** | 1.110 | 1.180 |
| sub | **797 069** | 884 622 | **−10.5 /call** | 1.108 | 1.197 |

Versus the previous ASM: about **−8.8 cycles/call** on both, instret avg still 44 / 46 (whole-program iret only ~−0.4/call). The cycle win is almost all CPI, not a shorter trace.
