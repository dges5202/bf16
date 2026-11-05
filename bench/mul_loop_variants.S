# Truncate bf16_mul A/B: same gate / specials / shift / pack.
# Only the 8-iter soft-mul inner step differs:
#   bf16_mul_nbr — current production: branchless mask-add
#   bf16_mul_br  — branched: beqz skip add when (mant_b & 1) == 0
#
# Copied from ../bf16.S bf16_mul; do not link both into production.

    .text

# =========================================================================
    .globl  bf16_mul_nbr
    .type   bf16_mul_nbr, @function
bf16_mul_nbr:
    srli    t4, a0, 15
    srli    t5, a0, 7
    andi    t5, t5, 0xFF
    srli    a3, a1, 15
    srli    a4, a1, 7
    andi    a4, a4, 0xFF
    xor     a3, t4, a3
    li      t0, 0xFF
    beq     t5, t0, mul_nbr_spec
    beq     a4, t0, mul_nbr_spec
    slli    t0, a0, 17
    beqz    t0, mul_nbr_spec
    slli    t0, a1, 17
    beqz    t0, mul_nbr_spec

    andi    t6, a0, 0x7F
    snez    t0, t5
    slli    t0, t0, 7
    or      t6, t6, t0
    andi    a5, a1, 0x7F
    snez    t0, a4
    slli    t0, t0, 7
    or      a5, a5, t0
    add     a4, a4, t5
    addi    a4, a4, -127

    li      t5, 8
    li      a1, 0
mul_nbr_loop:
    andi    t0, a5, 1
    srli    a5, a5, 1
    sub     t1, x0, t0              # mask = 0 / -1
    and     t2, t6, t1
    add     a1, a1, t2
    slli    t6, t6, 1
    addi    t5, t5, -1
    bne     t5, x0, mul_nbr_loop
    j       mul_nbr_shift

mul_nbr_spec:
    li      t0, 0x7F80
    slli    t1, a0, 17
    srli    t1, t1, 17
    slli    t2, a1, 17
    srli    t2, t2, 17
    bltu    t0, t1, mul_nbr_nan
    bltu    t0, t2, mul_nbr_nan
    bne     t1, t0, mul_nbr_s1
    beqz    t2, mul_nbr_nan
mul_nbr_s1:
    bne     t2, t0, mul_nbr_s2
    beqz    t1, mul_nbr_nan
mul_nbr_s2:
    beq     t1, t0, mul_nbr_inf
    beq     t2, t0, mul_nbr_inf
    slli    a0, a3, 15
    ret
mul_nbr_nan:
    li      a0, 0x7FC0
    ret
mul_nbr_inf:
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret

mul_nbr_shift:
    li      t0, 0x8000
    and     t1, a1, t0
    beq     t1, x0, mul_nbr_sh7
    srli    a1, a1, 8
    andi    a1, a1, 0x7F
    addi    a4, a4, 1
    j       mul_nbr_out
mul_nbr_sh7:
    srli    a1, a1, 7
    andi    a1, a1, 0x7F
mul_nbr_out:
    bge     a4, x0, mul_nbr_ov1
    slli    a0, a3, 15
    ret
mul_nbr_ov1:
    li      t0, 255
    blt     a4, t0, mul_nbr_pack
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret
mul_nbr_pack:
    slli    a0, a3, 15
    slli    a4, a4, 7
    or      a0, a0, a4
    or      a0, a0, a1
    ret
    .size   bf16_mul_nbr, .-bf16_mul_nbr

# =========================================================================
    .globl  bf16_mul_br
    .type   bf16_mul_br, @function
bf16_mul_br:
    srli    t4, a0, 15
    srli    t5, a0, 7
    andi    t5, t5, 0xFF
    srli    a3, a1, 15
    srli    a4, a1, 7
    andi    a4, a4, 0xFF
    xor     a3, t4, a3
    li      t0, 0xFF
    beq     t5, t0, mul_br_spec
    beq     a4, t0, mul_br_spec
    slli    t0, a0, 17
    beqz    t0, mul_br_spec
    slli    t0, a1, 17
    beqz    t0, mul_br_spec

    andi    t6, a0, 0x7F
    snez    t0, t5
    slli    t0, t0, 7
    or      t6, t6, t0
    andi    a5, a1, 0x7F
    snez    t0, a4
    slli    t0, t0, 7
    or      a5, a5, t0
    add     a4, a4, t5
    addi    a4, a4, -127

    li      t5, 8
    li      a1, 0
mul_br_loop:
    andi    t0, a5, 1
    srli    a5, a5, 1
    beqz    t0, mul_br_skip         # bit clear → skip add
    add     a1, a1, t6
mul_br_skip:
    slli    t6, t6, 1
    addi    t5, t5, -1
    bne     t5, x0, mul_br_loop
    j       mul_br_shift

mul_br_spec:
    li      t0, 0x7F80
    slli    t1, a0, 17
    srli    t1, t1, 17
    slli    t2, a1, 17
    srli    t2, t2, 17
    bltu    t0, t1, mul_br_nan
    bltu    t0, t2, mul_br_nan
    bne     t1, t0, mul_br_s1
    beqz    t2, mul_br_nan
mul_br_s1:
    bne     t2, t0, mul_br_s2
    beqz    t1, mul_br_nan
mul_br_s2:
    beq     t1, t0, mul_br_inf
    beq     t2, t0, mul_br_inf
    slli    a0, a3, 15
    ret
mul_br_nan:
    li      a0, 0x7FC0
    ret
mul_br_inf:
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret

mul_br_shift:
    li      t0, 0x8000
    and     t1, a1, t0
    beq     t1, x0, mul_br_sh7
    srli    a1, a1, 8
    andi    a1, a1, 0x7F
    addi    a4, a4, 1
    j       mul_br_out
mul_br_sh7:
    srli    a1, a1, 7
    andi    a1, a1, 0x7F
mul_br_out:
    bge     a4, x0, mul_br_ov1
    slli    a0, a3, 15
    ret
mul_br_ov1:
    li      t0, 255
    blt     a4, t0, mul_br_pack
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret
mul_br_pack:
    slli    a0, a3, 15
    slli    a4, a4, 7
    or      a0, a0, a4
    or      a0, a0, a1
    ret
    .size   bf16_mul_br, .-bf16_mul_br
