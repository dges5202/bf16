# Experiment: truncate bf16_div cold-path style A vs B.
# Same gate + hot path; only specials decision differs.
#
#   bf16_div_class  — mul-style class bits then short cascade (div rules)
#   bf16_div_tree   — add-style mag compares / branch tree (div rules)
#
# Hot path: (mant_a<<7)/mant_b soft restoring div, truncate renorm. Matches bf16.c.

    .text

# ---------------------------------------------------------------------------
    .macro  DIV_HOT
    /* Prefers: a3=sign_xor a4=exp_b t5=exp_a a5=mant_b_raw t6=mant_a_raw a6/a7 live */
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
1:
    slli    t0, t0, 1
    srli    t1, a1, 15
    andi    t1, t1, 1
    or      t0, t0, t1
    slli    a1, a1, 1
    slli    a2, a2, 1
    bltu    t0, a5, 2f
    sub     t0, t0, a5
    ori     a2, a2, 1
2:
    addi    t3, t3, -1
    bnez    t3, 1b

    li      t0, 0x100
    bltu    a2, t0, 3f
    srli    a2, a2, 1
    addi    a4, a4, 1
    j       4f
3:
    li      t0, 0x80
    bgeu    a2, t0, 4f
    beqz    a2, 4f
5:
    slli    a2, a2, 1
    addi    a4, a4, -1
    bltu    a2, t0, 5b
4:
    bgez    a4, 6f
    slli    a0, a3, 15
    ret
6:
    li      t0, 255
    blt     a4, t0, 7f
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret
7:
    slli    a0, a3, 15
    slli    a4, a4, 7
    or      a0, a0, a4
    andi    a2, a2, 0x7F
    or      a0, a0, a2
    ret
    .endm

# ========================= class-bit cold path =============================
    .globl  bf16_div_class
    .type   bf16_div_class, @function
bf16_div_class:
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
    beq     t5, t0, div_class_spec
    beq     a4, t0, div_class_spec
    slli    t0, a6, 17
    beqz    t0, div_class_spec
    slli    t0, a7, 17
    beqz    t0, div_class_spec

    DIV_HOT

div_class_spec:
    slli    a6, a6, 17
    srli    a6, a6, 17
    slli    a7, a7, 17
    srli    a7, a7, 17
    li      t0, 0x7F80

    seqz    t1, a6
    xor     t2, a6, t0
    seqz    t2, t2
    sltu    t3, t0, a6
    slli    t5, t1, 2
    slli    t1, t2, 1
    or      t5, t5, t1
    or      t5, t5, t3

    seqz    t1, a7
    xor     t2, a7, t0
    seqz    t2, t2
    sltu    t3, t0, a7
    slli    t6, t1, 2
    slli    t1, t2, 1
    or      t6, t6, t1
    or      t6, t6, t3

    or      t0, t5, t6
    andi    t1, t0, 1
    bnez    t1, div_class_nan

    li      t1, 4
    bne     t5, t1, div_class_not_00
    beq     t6, t1, div_class_nan
div_class_not_00:
    li      t1, 2
    bne     t5, t1, div_class_not_ii
    beq     t6, t1, div_class_nan
div_class_not_ii:
    andi    t1, t6, 4
    beqz    t1, div_class_try_inf_a
    andi    t1, t5, 4
    beqz    t1, div_class_inf
div_class_try_inf_a:
    andi    t1, t5, 2
    beqz    t1, div_class_zero
    andi    t1, t6, 2
    beqz    t1, div_class_inf

div_class_zero:
    slli    a0, a3, 15
    ret
div_class_nan:
    li      a0, 0x7FC0
    ret
div_class_inf:
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret
    .size   bf16_div_class, .-bf16_div_class

# ========================= branch-tree cold path ===========================
    .globl  bf16_div_tree
    .type   bf16_div_tree, @function
bf16_div_tree:
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
    beq     t5, t0, div_tree_spec
    beq     a4, t0, div_tree_spec
    slli    t0, a6, 17
    beqz    t0, div_tree_spec
    slli    t0, a7, 17
    beqz    t0, div_tree_spec

    DIV_HOT

div_tree_spec:
    slli    a6, a6, 17
    srli    a6, a6, 17
    slli    a7, a7, 17
    srli    a7, a7, 17
    li      t0, 0x7F80

    /* NaN either */
    bltu    t0, a6, div_tree_nan
    bltu    t0, a7, div_tree_nan

    /* 0/0 */
    bnez    a6, div_tree_a_nz
    beqz    a7, div_tree_nan
div_tree_a_nz:
    /* Inf/Inf */
    bne     a6, t0, div_tree_a_ninf
    beq     a7, t0, div_tree_nan
div_tree_a_ninf:
    /* nonzero / 0 -> Inf */
    bnez    a7, div_tree_b_nz
    j       div_tree_inf
div_tree_b_nz:
    /* Inf / non-Inf -> Inf */
    bne     a6, t0, div_tree_zero
    /* a Inf, b not Inf (and not NaN): Inf */
    j       div_tree_inf

div_tree_zero:
    slli    a0, a3, 15
    ret
div_tree_nan:
    li      a0, 0x7FC0
    ret
div_tree_inf:
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret
    .size   bf16_div_tree, .-bf16_div_tree
