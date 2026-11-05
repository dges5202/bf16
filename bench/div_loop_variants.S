# Truncate bf16_div: same gate/specials/renorm; soft-div inner step differs.
# Soft-div = 16-iter restoring (mant_a<<7)/mant_b — same as div_cold DIV_HOT.
#   bf16_div_br   — branched skip when rem < divisor
#   bf16_div_nbr  — branchless sltu + mask and

    .text

# =========================================================================
    .globl  bf16_div_br
    .type   bf16_div_br, @function
bf16_div_br:
    mv      a6, a0
    mv      a7, a1
    srli    t4, a6, 15
    srli    t5, a6, 7
    andi    t5, t5, 0xFF
    srli    a3, a7, 15
    srli    a4, a7, 7
    andi    a4, a4, 0xFF
    xor     a3, t4, a3
    li      t0, 0xFF
    beq     t5, t0, div_br_spec
    beq     a4, t0, div_br_spec
    slli    t0, a6, 17
    beqz    t0, div_br_spec
    slli    t0, a7, 17
    beqz    t0, div_br_spec

    andi    t6, a6, 0x7F
    snez    t0, t5
    slli    t0, t0, 7
    or      t6, t6, t0
    andi    a5, a7, 0x7F
    snez    t0, a4
    slli    t0, t0, 7
    or      a5, a5, t0
    sub     a4, t5, a4
    addi    a4, a4, 127

    slli    a1, t6, 7
    li      t0, 0
    li      a2, 0
    li      t3, 16
div_br_loop:
    slli    t0, t0, 1
    srli    t1, a1, 15
    andi    t1, t1, 1
    or      t0, t0, t1
    slli    a1, a1, 1
    slli    a2, a2, 1
    bltu    t0, a5, div_br_skip
    sub     t0, t0, a5
    ori     a2, a2, 1
div_br_skip:
    addi    t3, t3, -1
    bnez    t3, div_br_loop
    j       div_br_renorm

div_br_spec:
    li      t0, 0x7F80
    slli    t1, a6, 17
    srli    t1, t1, 17
    slli    t2, a7, 17
    srli    t2, t2, 17
    bltu    t0, t1, div_br_nan
    bltu    t0, t2, div_br_nan
    bnez    t1, div_br_s1
    beqz    t2, div_br_nan
div_br_s1:
    bne     t1, t0, div_br_s2
    beq     t2, t0, div_br_nan
div_br_s2:
    beqz    t2, div_br_inf
    beq     t1, t0, div_br_inf
    slli    a0, a3, 15
    ret
div_br_nan:
    li      a0, 0x7FC0
    ret
div_br_inf:
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret

div_br_renorm:
    li      t0, 0x100
    bltu    a2, t0, div_br_r1
    srli    a2, a2, 1
    addi    a4, a4, 1
    j       div_br_r2
div_br_r1:
    li      t0, 0x80
    bgeu    a2, t0, div_br_r2
    beqz    a2, div_br_r2
div_br_r1l:
    slli    a2, a2, 1
    addi    a4, a4, -1
    bltu    a2, t0, div_br_r1l
div_br_r2:
    bgez    a4, div_br_r3
    slli    a0, a3, 15
    ret
div_br_r3:
    li      t0, 255
    blt     a4, t0, div_br_pack
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret
div_br_pack:
    slli    a0, a3, 15
    slli    a4, a4, 7
    or      a0, a0, a4
    andi    a2, a2, 0x7F
    or      a0, a0, a2
    ret
    .size   bf16_div_br, .-bf16_div_br

# =========================================================================
    .globl  bf16_div_nbr
    .type   bf16_div_nbr, @function
bf16_div_nbr:
    mv      a6, a0
    mv      a7, a1
    srli    t4, a6, 15
    srli    t5, a6, 7
    andi    t5, t5, 0xFF
    srli    a3, a7, 15
    srli    a4, a7, 7
    andi    a4, a4, 0xFF
    xor     a3, t4, a3
    li      t0, 0xFF
    beq     t5, t0, div_nbr_spec
    beq     a4, t0, div_nbr_spec
    slli    t0, a6, 17
    beqz    t0, div_nbr_spec
    slli    t0, a7, 17
    beqz    t0, div_nbr_spec

    andi    t6, a6, 0x7F
    snez    t0, t5
    slli    t0, t0, 7
    or      t6, t6, t0
    andi    a5, a7, 0x7F
    snez    t0, a4
    slli    t0, t0, 7
    or      a5, a5, t0
    sub     a4, t5, a4
    addi    a4, a4, 127

    slli    a1, t6, 7
    li      t0, 0
    li      a2, 0
    li      t3, 16
div_nbr_loop:
    slli    t0, t0, 1
    srli    t1, a1, 15
    andi    t1, t1, 1
    or      t0, t0, t1
    slli    a1, a1, 1
    slli    a2, a2, 1
    sltu    t1, t0, a5
    xori    t1, t1, 1
    or      a2, a2, t1
    sub     t2, x0, t1
    and     t2, a5, t2
    sub     t0, t0, t2
    addi    t3, t3, -1
    bnez    t3, div_nbr_loop
    j       div_nbr_renorm

div_nbr_spec:
    li      t0, 0x7F80
    slli    t1, a6, 17
    srli    t1, t1, 17
    slli    t2, a7, 17
    srli    t2, t2, 17
    bltu    t0, t1, div_nbr_nan
    bltu    t0, t2, div_nbr_nan
    bnez    t1, div_nbr_s1
    beqz    t2, div_nbr_nan
div_nbr_s1:
    bne     t1, t0, div_nbr_s2
    beq     t2, t0, div_nbr_nan
div_nbr_s2:
    beqz    t2, div_nbr_inf
    beq     t1, t0, div_nbr_inf
    slli    a0, a3, 15
    ret
div_nbr_nan:
    li      a0, 0x7FC0
    ret
div_nbr_inf:
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret

div_nbr_renorm:
    li      t0, 0x100
    bltu    a2, t0, div_nbr_r1
    srli    a2, a2, 1
    addi    a4, a4, 1
    j       div_nbr_r2
div_nbr_r1:
    li      t0, 0x80
    bgeu    a2, t0, div_nbr_r2
    beqz    a2, div_nbr_r2
div_nbr_r1l:
    slli    a2, a2, 1
    addi    a4, a4, -1
    bltu    a2, t0, div_nbr_r1l
div_nbr_r2:
    bgez    a4, div_nbr_r3
    slli    a0, a3, 15
    ret
div_nbr_r3:
    li      t0, 255
    blt     a4, t0, div_nbr_pack
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret
div_nbr_pack:
    slli    a0, a3, 15
    slli    a4, a4, 7
    or      a0, a0, a4
    andi    a2, a2, 0x7F
    or      a0, a0, a2
    ret
    .size   bf16_div_nbr, .-bf16_div_nbr
