# perfcounter.S: read the 64-bit cycle and instret counters on RV32.
#
#   uint64_t get_cycles(void);
#   uint64_t get_instret(void);
#
# ILP32 returns a 64-bit integer in a1:a0 (a1 = high word, a0 = low word).
# rv32emu has no timing model: cycle and instret count the same guest
# instructions. Read high, low, high again; retry if the high words differ.

    .text

    .globl  get_cycles
    .type   get_cycles, @function
    .align  2
get_cycles:
    csrr    a1, cycleh
    csrr    a0, cycle
    csrr    a2, cycleh
    bne     a1, a2, get_cycles
    ret
    .size   get_cycles, .-get_cycles

    .globl  get_instret
    .type   get_instret, @function
    .align  2
get_instret:
    csrr    a1, instreth
    csrr    a0, instret
    csrr    a2, instreth
    bne     a1, a2, get_instret
    ret
    .size   get_instret, .-get_instret
