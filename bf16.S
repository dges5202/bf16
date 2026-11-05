# Guest RV32I BFloat16 — AI / industrial truncate-oriented variant.
# WORKING COPY: mirrors bf16.c (not IEEE RNE). Twin: bf16_ieee_rne.S / .c
#
# Policy (see bf16.c): no RNE (chop &0x7F); exp<0 -> +/-0; exp>=255 -> Inf;
# div DAZ on subnormals; soft-div 8-iter aligned for normal x normal.
#
# ILP32 ABI
#   args a0-a7, return a0; caller-saved ra/t*/a*; callee-saved sp/s*
#   sp 16-byte aligned at calls; no M (mul/div/rem); unsigned bit compares
#   unless unbiased exp needs two's-complement (srai / bgez).
#
# Values: bf16 is the 16-bit pattern, zero-extended in a0/a1.
#
# Two-operand leaves (mul/add/sub/div) after the ABI door — common grid:
#   mv a6,a0  mv a7,a1          # keep originals for cold ret-a / ret-b
#   t4=sign_a  t5=exp_a  t6=mant_a
#   a3=sign_b  a4=exp_b  a5=mant_b     (mul/div: a3 becomes sign_xor)
#   scratch a0-a2 t0-t3; pack into a0 at each ret
# Per-leaf maps and path notes sit at each .globl.
#
# Cold specials style: mag branch-tree (see mul/div headers). Class-bit
# state machines were tried first; bench favored trees — details there.
#
    .text

    .globl  bf16_sign
    .type   bf16_sign, @function
bf16_sign:
    /* Unary field helper.
     *   in  a0 = bf16 bits
     *   out a0 = sign (0/1)
     *   clobbers: (none beyond a0)
     */
    srli    a0, a0, 15                                  # bit15 -> bit0
    ret
    .size   bf16_sign, .-bf16_sign


    .globl  bf16_exp
    .type   bf16_exp, @function
bf16_exp:
    /* Unary field helper.
     *   in  a0 = bf16 bits
     *   out a0 = stored exp (bits 14..7), 0..255
     */
    srli    a0, a0, 7
    andi    a0, a0, 0xFF                                # drop sign in bit8
    ret
    .size   bf16_exp, .-bf16_exp


    .globl  bf16_mant
    .type   bf16_mant, @function
bf16_mant:
    /* Unary field helper.
     *   in  a0 = bf16 bits
     *   out a0 = stored mant (bits 6..0), 0..127
     */
    andi    a0, a0, 0x7F
    ret
    .size   bf16_mant, .-bf16_mant


    .globl  bf16_pack
    .type   bf16_pack, @function
bf16_pack:
    /* Pack fields (leaf helpers may also pack inline — no jal).
     *   in  a0=sign(0/1)  a1=exp(8-bit)  a2=mant(7-bit)
     *   out a0 = bits; clobbers a1
     */
    slli    a0, a0, 15
    slli    a1, a1, 7
    or      a0, a0, a1
    or      a0, a0, a2
    ret
    .size   bf16_pack, .-bf16_pack


    .globl  bf16_isnan
    .type   bf16_isnan, @function
bf16_isnan:
    /* Predicate: NaN <=> exp==0xFF && mant!=0.  Branchless and of flags.
     *   in a0; out a0=0/1; scratch t0 t1 t5 t6
     * Alt (not used): mag>0x7F80 via sltu — fewer logic ops but needs 0x7F80.
     */
    srli    t5, a0, 7
    andi    t5, t5, 0xFF
    andi    t6, a0, 0x7F
    xori    t0, t5, 0xFF
    seqz    t0, t0
    snez    t1, t6
    and     a0, t0, t1
    ret
    .size   bf16_isnan, .-bf16_isnan


    .globl  bf16_isinf
    .type   bf16_isinf, @function
bf16_isinf:
    /* Predicate: Inf <=> exp==0xFF && mant==0.  Same shape as isnan (seqz mant).
     *   in a0; out a0=0/1; scratch t0 t1 t5 t6
     */
    srli    t5, a0, 7
    andi    t5, t5, 0xFF
    andi    t6, a0, 0x7F
    xori    t0, t5, 0xFF
    seqz    t0, t0
    seqz    t1, t6
    and     a0, t0, t1
    ret
    .size   bf16_isinf, .-bf16_isinf


    .globl  bf16_iszero
    .type   bf16_iszero, @function
bf16_iszero:
    /* Predicate: +/-0 <=> exp==0 && mant==0 (sign ignored).
     *   in a0; out a0=0/1; scratch t0 t1 t5 t6
     * Alt (not used): slli 17; seqz — mag==0 in two insns for both signs.
     */
    srli    t5, a0, 7
    andi    t5, t5, 0xFF
    andi    t6, a0, 0x7F
    seqz    t0, t5
    seqz    t1, t6
    and     a0, t0, t1
    ret
    .size   bf16_iszero, .-bf16_iszero


    .globl  f32_to_bf16
    .type   f32_to_bf16, @function
f32_to_bf16:
    /* Alias of u32_to_bf16: ILP32/no-F passes float bits in a0 already.
     * Fall through (no j).  Host C still memcpy's for type safety.
     */

    .globl  u32_to_bf16
    .type   u32_to_bf16, @function
u32_to_bf16:
    /*
     * f32 bits -> bf16 bits (RNE on finite; Inf/NaN copy top 16).
     *
     * Registers: in/out a0; scratch t0-t2.
     *
     * Paths:
     *   (a0 & 0x7F800000) == 0x7F800000  -> Inf/NaN: srli 16, ret
     *   else fall-through                -> RNE: x += ((x>>16)&1)+0x7FFF; >>16
     * Finite is fall-through (common); Inf/NaN is beq away.  No branchless
     * mux — arms differ; mask-select costs more than one taken beq.
     * Label u32_to_bf16_decision is the rare Inf/NaN arm.
     */
    lui     t0, 0x7F800                                 # t0 = 0x7F800000
    and     t1, a0, t0                                  # t1 = (a0 & 0x7F800000)
    beq     t0, t1, u32_to_bf16_decision                # Inf/NaN -> copy top 16
    srli    t0, a0, 16                                  # t0 = (a0 >> 16)
    andi    t1, t0, 1                                   # t1 = (a0 >> 16) & 1
    li      t2, 0x7FFF                                  # t2 = 0x7FFF
    add     t1, t1, t2                                  # t1 = RNE bias
    add     a0, a0, t1                                  # round
    srli    a0, a0, 16                                  # a0 = bf16 bits
    ret

u32_to_bf16_decision:
    srli    a0, a0, 16                                  # (f32bits >> 16)
    ret
    .size   u32_to_bf16, .-u32_to_bf16
    .size   f32_to_bf16, .-f32_to_bf16


    .globl  bf16_mul
    .type   bf16_mul, @function
bf16_mul:
    /*
     * bf16_mul — truncate product; mirrors bf16.c.
     *
     * -------------------- registers --------------------
     *   door:  a0=a a1=b kept for cold (no a6/a7 copies)
     *   unpack: t4=sign_a t5=exp_a | a3=sign_b then sign_xor; a4=exp_b
     *   hot:    t6=mant_a|hid  a5=mant_b|hid  a4=exp_a+exp_b-bias
     *           a1=product  a3=sign_xor; scratch t0-t3 t5(count)
     *   cold:   t1=mag_a t2=mag_b t0=0x7F80; a3=sign_xor; a0/a1 originals
     *
     * -------------------- paths ------------------------
     *   Gate (cheap, no 0x7F80): exp==0xFF | mag==0 (slli 17) either side
     *     -> specials; else hot.
     *   Hot: attach hidden iff exp!=0; 8x8 branchless mask-add mul;
     *        bit15? >>8,+exp : >>7; chop &0x7F; exp<0 +/-0; >=255 Inf; pack.
     *   Cold: see block above bf16_mul_specialcase (mag branch-tree).
     *
     * -------------------- algorithm --------------------
     * Soft mul (no M): 8-iter (mant_b&1) mask-add, shift mant_a.
     * Must special-case before multiply (Inf*0 -> NaN, not a finite product).
     */
    srli    t4, a0, 15                                  # t4 = sign_a
    srli    t5, a0, 7
    andi    t5, t5, 0xFF                                # t5 = 8-bit exp_a

    srli    a3, a1, 15                                  # a3 = sign_b
    srli    a4, a1, 7
    andi    a4, a4, 0xFF                                # a4 = 8-bit exp_b
    xor     a3, t4, a3                                  # a3 = sign_xor

    # decision exp == 0xFF
    li      t0, 0xFF
    beq     t5, t0, bf16_mul_specialcase
    beq     a4, t0, bf16_mul_specialcase

    # decision exp == 0 && mant == 0 (±0 via slli 17)
    slli    t0, a0, 17                                  # t0 = (a << 17)
    beqz    t0, bf16_mul_specialcase
    slli    t0, a1, 17                                  # t0 = (b << 17)
    beqz    t0, bf16_mul_specialcase

    # Normal case — attach implicit 1 when exp != 0
    andi    t6, a0, 0x7F                                # t6 = 7-bit mant_a
    snez    t0, t5                                      # t0 = 1 if exp != 0
    slli    t0, t0, 7                                   # t0 = 0 or 0x80
    or      t6, t6, t0                                  # if exp_a != 0, mant_a | 0x80

    andi    a5, a1, 0x7F                                # a5 = 7-bit mant_b
    snez    t0, a4                                      # t0 = 1 if exp != 0
    slli    t0, t0, 7                                   # t0 = 0 or 0x80
    or      a5, a5, t0                                  # if exp_b != 0, mant_b | 0x80

    add     a4, a4, t5                                  # a4 = exp_a + exp_b
    addi    a4, a4, -127                                # a4 = exp_temp = exp_a + exp_b - 127

    /* hot live: a3=sign_xor a4=exp t6/a5=mants; free a0-a2 t0-t4 */
    # 8x8 product (branchless bit via mask)
    li      t5, 8                                       # t5 = count = 8
    li      a1, 0                                       # a1 = product

bf16_mul_mul_loop:
    andi    t0, a5, 1                                   # t0 = mant_b & 1
    srli    a5, a5, 1
    sub     t1, x0, t0                                  # mask = 0 or -1
    and     t2, t6, t1                                  # t2 = mant_a & mask
    add     a1, a1, t2                                  # a1 += layer
    slli    t6, t6, 1
    addi    t5, t5, -1
    bne     t5, x0, bf16_mul_mul_loop

    li      t0, 0x8000
    and     t1, a1, t0                                  # t1 = a1 & 0x8000
    beq     t1, x0, bf16_mul_shift
    srli    a1, a1, 8                                   # a1 = mant >> 8
    andi    a1, a1, 0x7F                                # a1 = (mant >> 8) & 0x7F
    addi    a4, a4, 1                                   # a4 = exp ++
    j       bf16_mul_shift_out

bf16_mul_shift:
    srli    a1, a1, 7                                   # a1 = mant >> 7
    andi    a1, a1, 0x7F                                # a1 = (mant >> 7) & 0x7F

bf16_mul_shift_out:
    bge     a4, x0, bf16_mul_overflow1
    slli    a0, a3, 15                                  # underflow -> signed zero
    ret

bf16_mul_overflow1:
    li      t0, 255
    blt     a4, t0, bf16_mul_overflow2
    slli    a0, a3, 15                                  # overflow -> signed Inf
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret

bf16_mul_overflow2:
bf16_mul_pack:
    slli    a0, a3, 15                                  # a0 = sign << 15
    slli    a4, a4, 7                                   # a4 = exp << 7
    or      a0, a0, a4                                  # a0 = sign | exp
    or      a0, a0, a1                                  # a0 = sign | exp | mant
    ret

    /*
     * --- cold specials: mag branch-tree (not class state machine) -------
     *
     * History:
     *   v1 — class-bit "state machine": per side encode NaN=1, Inf=2, zero=4,
     *        OR into t0, then cascade (t0&1)->nan, t0==6->nan (Inf*0),
     *        (t0&2)->inf, else fall-through zero.  Fall-through last arm
     *        relied on gate invariants; label order was load-bearing.
     *   Why change — bench/div_cold_variants.S + DIV_COLD_VARIANTS.md
     *        (same idea applied to div, then mul specials aligned):
     *        specials-heavy: tree ~67 vs class ~82 avg instret (~18%);
     *        Ripes 5ST total cycles also favored tree.  Mixed/hot ≈ tied,
     *        so hot cost dominates overall, but cold is cheaper as a tree.
     *   v2 (current) — strip sign (slli 17/srli 17), compare mag to 0x7F80:
     *        mag > 0x7F80 -> NaN; Inf*0 / 0*Inf -> NaN; either Inf -> Inf;
     *        else +/-0 with sign_xor.  Same rules as C, fewer encode insns.
     *
     * Entry: a3=sign_xor; a0/a1 originals (pre-hot).  Out-of-line nan/inf rets.
     */
bf16_mul_specialcase:
    li      t0, 0x7F80
    slli    t1, a0, 17
    srli    t1, t1, 17                                  # t1 = mag_a
    slli    t2, a1, 17
    srli    t2, t2, 17                                  # t2 = mag_b
    
    bltu    t0, t1, bf16_mul_nan
    bltu    t0, t2, bf16_mul_nan

    bne     t1, t0, bf16_mul_skip1
    beqz    t2, bf16_mul_nan        # inf/0

bf16_mul_skip1:
    bne     t2, t0, bf16_mul_skip2
    beqz    t1, bf16_mul_nan        # 0/Inf
    
bf16_mul_skip2:
    beq     t1, t0, bf16_mul_inf        # /0 → Inf
    beq     t2, t0, bf16_mul_inf    # Inf/… → Inf
    slli    a0, a3, 15              # ±0
    ret

bf16_mul_nan:
    li      a0, 0x7FC0
    ret

bf16_mul_inf:
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret
    .size   bf16_mul, .-bf16_mul


    .globl  bf16_sqrt
    .type   bf16_sqrt, @function
bf16_sqrt:
    /*
     * bf16_sqrt — unary; mirrors bf16.c (bisect + soft mid*mid).
     *
     * -------------------- registers --------------------
     *   a0 ABI in / pack out (keep for ret-a cold)
     *   a6 = orig a, then mid;  a7 = new_exp
     *   a4=high a5=low; a1=sq product; a2/a3 = mul copies of mid
     *   t4=sign  t5=exp->unbiased  t6=mant(|0x80; cmp target)
     *   t0-t2 gate / mask-mul scratch
     *
     * -------------------- paths (order load-bearing) --------------------
     *   1) mag==0 (slli 17) -> ret a   (+/-0 keeps sign; before sign->NaN)
     *   2) exp==0xFF        -> NaN keep a; -Inf->0x7FC0; +Inf keep a
     *   3) sign!=0          -> 0x7FC0 (negative finite)
     *   4) else hot
     *   Sign before 0xFF would turn -NaN into 0x7FC0; C returns a.
     *
     * -------------------- algorithm --------------------
     * Bisect mid in [90,256]; sq=(mid*mid)/128 vs mant.  No M: each square
     * is branchless mask-mul (~8 probes, often 150+ instret).  Matches C
     * bit-exact.  Alt digit-recurrence would need C+asm move together.
     * Odd unbiased exp: mant<<=1, exp-=1; new_exp=(exp>>1)+127 via srai.
     * mid*mid keeps a6=mid (need mid+/-1); early-out bnez a2 on mul loop.
     */
    mv      a6, a0                                      # a6 = original a (ret-a paths)

    srli    t4, a6, 15                                  # t4 = sign_a
    srli    t5, a6, 7
    andi    t5, t5, 0xFF                                # t5 = 8-bit exp_a
    andi    t6, a6, 0x7F                                # t6 = 7-bit mant_a

    # --- gate ---------------------------------------------------------------
    # ±0 first so -0 is not treated as negative finite
    slli    t0, a6, 17                                  # drop sign; mag==0?
    beqz    t0, bf16_sqrt_zero

    li      t0, 0xFF
    beq     t5, t0, bf16_sqrt_specialcase               # Inf / NaN

    bnez    t4, bf16_sqrt_nan                           # negative finite -> NaN

    # --- hot: unbias + implicit 1 ------------------------------------------
    # a6 may be overwritten from here (only mid uses it after this)
    addi    t5, t5, -127                                # t5 = unbiased exp
    ori     t6, t6, 0x80                                # t6 = mant | implicit 1

    # odd: mant<<=1, exp-=1; even: skip into shared new_exp
    andi    t0, t5, 1
    beqz    t0, bf16_sqrt_newexp_even
    slli    t6, t6, 1                                   # odd: fold the extra 2^1 into mant
    addi    t5, t5, -1                                  # odd: make exp even

bf16_sqrt_newexp_even:
    srai    a7, t5, 1                                   # arithmetic: exp may be negative
    addi    a7, a7, 127                                 # a7 = new_exp

    # --- binary search for fixed-point sqrt(mant) --------------------------
    li      a5, 90                                      # low
    li      a4, 256                                     # high (inclusive range as in C)

bf16_sqrt_bisection_loop:
    add     a6, a4, a5
    srli    a6, a6, 1                                   # a6 = mid = (low+high)>>1

    # mid*mid with mask adds; a2=bits, a3=shifted addend; a6 stays mid
    li      a1, 0                                       # product
    mv      a2, a6
    mv      a3, a6

bf16_sqrt_mul_loop:
    andi    t0, a2, 1                                   # next multiplier bit
    srli    a2, a2, 1
    sub     t1, x0, t0                                  # 0 or 0xFFFFFFFF
    and     t2, a3, t1                                  # addend or 0
    add     a1, a1, t2
    slli    a3, a3, 1                                   # next power of mid
    bnez    a2, bf16_sqrt_mul_loop                      # early out when bits gone

    srli    a1, a1, 7                                   # sq = (mid*mid)/128
    bltu    t6, a1, bf16_sqrt_bisection_loop_high       # mant < sq -> shrink high
    addi    a5, a6, 1                                   # fall-through: low = mid+1
    j       bf16_sqrt_bisection_loop_cont

bf16_sqrt_bisection_loop_high:
    addi    a4, a6, -1                                  # high = mid-1

bf16_sqrt_bisection_loop_cont:
    bgeu    a4, a5, bf16_sqrt_bisection_loop            # while high >= low

    # pack: sign (0 on hot), new_exp, high&0x7F
    slli    a0, t4, 15
    slli    t0, a7, 7
    or      a0, a0, t0
    andi    t0, a4, 0x7F
    or      a0, a0, t0
    ret

    # --- cold: exp==0xFF ---------------------------------------------------
    # NaN (mant!=0): return original a (payload / sign), same as +Inf path.
    # -Inf: produce 0x7FC0.  +Inf falls through to ret with a0 unchanged.
bf16_sqrt_specialcase:
    bnez    t6, bf16_sqrt_ret_orig                      # NaN -> keep a0
    bnez    t4, bf16_sqrt_nan                           # -Inf -> NaN; +Inf falls

bf16_sqrt_ret_orig:
    ret

bf16_sqrt_nan:
    li      a0, 0x7FC0
    ret

bf16_sqrt_zero:
    ret                                                 # ±0: a0 still original
    .size   bf16_sqrt, .-bf16_sqrt


    .globl  bf16_negate
    .type   bf16_negate, @function
bf16_negate:
    /*
     * In-place sign flip on a bf16 array (not the two-operand grid).
     *   a0 = &v[0] (uint16 elements)   a1 = n
     *   scratch t1=0x8000 t2=load; n==0 -> immediate ret
     *   each step: lhu; xor sign bit; sh; a0+=2; a1--
     */
    li      t1, 0x8000
bf16_negate_loop:
    beqz    a1, bf16_negate_loop_end
    lhu     t2, 0(a0)
    xor     t2, t2, t1
    sh      t2, 0(a0)
    addi    a0, a0, 2
    addi    a1, a1, -1
    bnez    a1, bf16_negate_loop
bf16_negate_loop_end:
    ret
    .size   bf16_negate, .-bf16_negate


    .globl  bf16_sub
    .type   bf16_sub, @function
bf16_sub:
    /* a-b == a+(-b): xor b with 0x8000, fall into bf16_add (shared body). */
    li      t0, 0x8000
    xor     a1, a1, t0
    .globl  bf16_add
    .type   bf16_add, @function
bf16_add:
    /*
     * bf16_add — binary; mirrors bf16.c.
     *
     * Signs are not xor'd at the door (contrast mul).  sign_a / sign_b stay
     * until the same-sign vs opposite-sign decision on the hot path.
     *
     * Gate (every call):
     *   exp_a==0xFF | exp_b==0xFF  ->  bf16_add_special_ff
     *   mag_a==0    | mag_b==0     ->  bf16_add_special_zero  (slli 17)
     *   else                       ->  hot
     * FF is checked before ±0 so Inf+0 enters special_ff, not special_zero.
     *
     * Cold: field short-circuit (not class bits / mag-OR).  Need same/opposite
     * Inf and signed-zero rules — see notes above bf16_add_special_ff / _zero.
     *
     * Cold ff:  NaN; both Inf (same -> a, opposite -> NaN); one Inf -> that op.
     * Cold zero: opposite ±0 -> +0; same ±0 -> a; only a zero -> b; only b -> a.
     *
     * Hot (both finite nonzero): |0x80, align by |dexp| (>=8 -> small mant 0),
     * same-sign add / opposite sub (equal mags -> +0), renormalize, pack
     * mant&0x7F (truncate).  exp<0 -> +0; exp>=255 -> Inf.
     * (AI truncate; IEEE RNE twin is bf16_ieee_rne.S)
     *
     * Unpack once before the gate so ff / zero / hot share t4-t6 and a3-a5.
     *
     * +------+---------------------------+----------------------------------+
     * | reg  | gate / unpack             | hot (after specials)             |
     * +------+---------------------------+----------------------------------+
     * | a0   | ABI a; keep for cold      | pack result                      |
     * |      | ret-a (no door mv)        |                                  |
     * | a1   | ABI b; keep for cold      | free after gate (hot unused)     |
     * |      | ret-b (sub: already -b)   |                                  |
     * | a2   | —                         | scratch                          |
     * | a3   | sign_b                    | until result sign chosen         |
     * | a4   | exp_b                     | then free / dexp                 |
     * | a5   | mant_b                    | |0x80, then aligned              |
     * | a6/7 | free scratch              | free scratch                     |
     * | t0-3 | gate / hot / cold scratch | align, compare, renorm           |
     * | t4   | sign_a                    | -> result sign                   |
     * | t5   | exp_a                     | -> result exp                    |
     * | t6   | mant_a                    | |0x80, then aligned              |
     * +------+---------------------------+----------------------------------+
     *
     * Door: no mv a6/a7 (-2 all paths).  Cold ret-a = ret; ret-b = mv a0,a1.
     */
    # unpack sign/exp only; mant deferred to hot / special_ff (GCC-style)
    srli    t4, a0, 15                                  # t4 = sign_a
    srli    t5, a0, 7
    andi    t5, t5, 0xFF                                # t5 = 8-bit exp_a

    srli    a3, a1, 15                                  # a3 = sign_b (kept; not xor)
    srli    a4, a1, 7
    andi    a4, a4, 0xFF                                # a4 = 8-bit exp_b

    # gate: hot (exp!=0xFF and exp!=0) falls through. Taken branches are the rare
    # exits. RV32_5S charges ~2 cycles per taken branch; the old bnez-to-hot
    # pair was taken on every normal.
    li      t0, 0xFF
    beq     t5, t0, bf16_add_special_ff
    beq     a4, t0, bf16_add_special_ff
    beqz    t5, bf16_add_exp_a_zero
    beqz    a4, bf16_add_exp_b_zero

bf16_add_hot:
    /*
     * Hot body. Ripes RV32_5S: not-taken is free, taken/j costs a flush.
     * Common cases fall through:
     *   same sign, |ea-eb|>=8 (mb or ma becomes 0), sum < 0x100 → pack
     *   opposite, already-norm diff → pack
     * Narrow align, carry, and left-renorm are the taken tails.
     */
    andi    t6, a0, 0x7F
    andi    a5, a1, 0x7F
    ori     t6, t6, 0x80
    ori     a5, a5, 0x80
    li      t3, 8
    bne     t4, a3, bf16_add_hot_opp

    # -------- same sign, ea >= eb falls through --------
    bltu    t5, a4, bf16_add_ss_eb_gt
    sub     t0, t5, a4
    bltu    t0, t3, bf16_add_ss_srl_b                   # dexp < 8 (uncommon)
    li      a5, 0                                       # |dexp|>=8 → small mant 0
bf16_add_ss_sum:
    add     t6, t6, a5                                  # sum in [0x80, 0x1fe]
    li      t0, 0x100
    bgeu    t6, t0, bf16_add_ss_bump                    # carry is the rare arm
bf16_add_ss_pack:
    slli    a0, t4, 15
    slli    t0, t5, 7
    or      a0, a0, t0
    andi    t0, t6, 0x7F
    or      a0, a0, t0
    ret

bf16_add_ss_bump:
    srli    t6, t6, 1
    addi    t5, t5, 1
    li      t0, 255
    beq     t5, t0, bf16_add_ss_inf
    j       bf16_add_ss_pack

bf16_add_ss_inf:
    slli    a0, t4, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret

bf16_add_ss_srl_b:
    srl     a5, a5, t0
    j       bf16_add_ss_sum

bf16_add_ss_eb_gt:
    sub     t0, a4, t5
    mv      t5, a4
    bltu    t0, t3, bf16_add_ss_srl_a
    li      t6, 0
    j       bf16_add_ss_sum

bf16_add_ss_srl_a:
    srl     t6, t6, t0
    j       bf16_add_ss_sum

    # -------- opposite sign --------
    /* ea<eb: ma shifted ⇒ mb>ma, sign=sign_b. Wide (|dexp|>=8) falls through. */
bf16_add_hot_opp:
    bltu    t5, a4, bf16_add_opp_lt
    sub     t0, t5, a4
    bltu    t0, t3, bf16_add_opp_srl_b
    li      a5, 0

bf16_add_opp_after:
    beq     a5, t6, bf16_add_opp_ret0                   # equal mag → +0 (rare)
    bltu    t6, a5, bf16_add_opp_mant1
    sub     t6, t6, a5                                  # sign stays sign_a
bf16_add_opp_renorm:
    /*
     * Diff of two |0x80 mants is in [1, 0xFF]. Already-norm (>=0x80) is the
     * common arm and falls into pack. Left renorm stays 1-bit.
     */
    li      t0, 0x80
    bltu    t6, t0, bf16_add_opp_renorm_loop
bf16_add_opp_pack_fin:
    slli    a0, t4, 15
    slli    t0, t5, 7
    or      a0, a0, t0
    andi    t0, t6, 0x7F
    or      a0, a0, t0
    ret

bf16_add_opp_ret0:
    li      a0, 0
    ret

bf16_add_opp_renorm_loop:
    slli    t6, t6, 1
    addi    t5, t5, -1
    bltu    t6, t0, bf16_add_opp_renorm_loop
    bgez    t5, bf16_add_opp_pack_fin
    li      a0, 0
    ret

bf16_add_opp_mant1:
    mv      t4, a3
    sub     t6, a5, t6
    j       bf16_add_opp_renorm

bf16_add_opp_srl_b:
    srl     a5, a5, t0
    j       bf16_add_opp_after

bf16_add_opp_lt:
    sub     t0, a4, t5
    mv      t5, a4
    mv      t4, a3
    bltu    t0, t3, bf16_add_opp_lt_srl
    mv      t6, a5                                      # dexp>=8: result = mb
    j       bf16_add_opp_pack_fin

bf16_add_opp_lt_srl:
    srl     t6, t6, t0
    sub     t6, a5, t6
    j       bf16_add_opp_renorm

bf16_add_exp_a_zero:
    slli    t0, a0, 17
    beqz    t0, bf16_add_special_zero
    beqz    a4, bf16_add_exp_b_zero
    j       bf16_add_hot

bf16_add_exp_b_zero:
    slli    t0, a1, 17
    beqz    t0, bf16_add_special_zero
    j       bf16_add_hot

    /*
     * bf16_add_special_ff — field short-circuit (not class-bit machine).
     *
     * Rules: any NaN->0x7FC0; both Inf same sign->a; opposite->NaN;
     * exactly one Inf->that operand.
     *
     * History (condensed): mag|==0xFF000000 / mant==0xFF / exp_a&exp_b all
     * false-hit NaN or both-Inf.  Current: short-circuit on unpacked
     * exp/mant; opposite both-Inf falls into bf16_add_nan (label order
     * load-bearing).  Add keeps field checks (same/opposite Inf + signed
     * zero); mul/div cold moved class->mag-tree after DIV_COLD_VARIANTS.md.
     */
bf16_add_special_ff:
    andi    t6, a0, 0x7F                                # mant deferred from door
    andi    a5, a1, 0x7F
    li      t0, 0xFF
    bne     t5, t0, bf16_add_special_skip1              # a not FF -> not NaN(a)
    bnez    t6, bf16_add_nan                            # a is NaN

bf16_add_special_skip1:
    bne     a4, t0, bf16_add_special_skip2              # b not FF -> not NaN(b)
    bnez    a5, bf16_add_nan                            # b is NaN

bf16_add_special_skip2:
    # past NaN: any FF side is Inf.  Disambiguate one vs both.
    bne     t5, t0, bf16_add_special_one_inf            # a not Inf -> only b Inf
    bne     a4, t0, bf16_add_special_ret_a              # b not Inf -> only a Inf
    # both Inf
    xor     t3, t4, a3                                  # t3 = sign_a ^ sign_b
    beqz    t3, bf16_add_special_ret_a                  # same sign -> a
    # opposite signs fall through to nan

bf16_add_nan:
    li      a0, 0x7FC0
    ret

bf16_add_special_one_inf:
    mv      a0, a1                                      # only b Inf (a1 still ABI b / sub:-b)
    ret

bf16_add_special_ret_a:
    ret                                                 # a0 still ABI a

    /*
     * bf16_add_special_zero — mirrors bf16.c zero arms.
     * Entry: at least one ±0; neither Inf/NaN (FF already gated).
     * a0/a1 still originals — mag in t0/t1 (do not clobber a1).
     *
     *   or t0|t1
     *     == 0  -> both zero: same sign -> ret a; else +0
     *     != 0  -> one zero: t0==0 -> ret b; else ret a
     */
bf16_add_special_zero:
    slli    t0, a0, 17
    srli    t0, t0, 17                                  # t0 = mag_a
    slli    t1, a1, 17
    srli    t1, t1, 17                                  # t1 = mag_b

    or      t3, t0, t1
    bnez    t3, bf16_add_special_zero_single            # one side nonzero

    xor     t3, t4, a3                                  # both zero: sign_a ^ sign_b
    beqz    t3, bf16_add_special_ret_a                  # same -> a
    li      a0, 0                                       # opposite -> +0
    ret

bf16_add_special_zero_single:
    beqz    t0, bf16_add_special_ret_b                  # only a zero -> b
    ret                                                 # only b zero -> a (a0 unchanged)

bf16_add_special_ret_b:
    mv      a0, a1
    ret

    .size   bf16_add, .-bf16_add
    .size   bf16_sub, .-bf16_sub


    .globl  bf16_div
    .type   bf16_div, @function
bf16_div:
    /*
     * bf16_div — truncate quotient; mirrors bf16.c (DAZ + 8-iter soft-div).
     *
     * -------------------- registers --------------------
     *   door:   a0=a a1=b -> a6=a a7=b
     *   unpack: t4=sign_a t5=exp_a | a3=sign_b (then sign_xor) a4=exp_b
     *   DAZ:    exp==0 -> force +/-0 into a6/a7 (keeps sign); covers subnormals
     *   hot:    t6=mant_a|hid a5=mant_b|hid a4=exp_a-exp_b+bias
     *           a2=quot  t3=count; rem uses t6; scratch t0 t1
     *   cold:   t1=mag_a t2=mag_b t0=0x7F80; a3=sign_xor
     *
     * -------------------- paths ------------------------
     *   1) DAZ   2) gate FF / +/-0 -> specials   3) else hot
     *   Hot: soft-div -> renorm (one << if quot<0x80) -> exp flush -> pack
     *   Cold: mag branch-tree (below).  Contract after DAZ: normal x normal
     *   (+ Inf/NaN/0 rules).  Full gradual subnormals: bf16_ieee_rne.*.
     *
     * -------------------- soft-div algorithm --------------------
     * quot ~= (mant_a<<7)/mant_b via 8-iter *aligned* long division:
     *   rem=mant_a<<7, d=mant_b<<7; each step quot<<=1; if rem>=d sub, bit=1;
     *   d>>=1.  BRANCHED skip (not sltu/mask): DIV_LOOP_VARIANTS.md —
     *   fewer instret and 5ST cycles than always-on mask.  8 bits enough
     *   for normal x normal (quot in [0x40,0xFF]); DAZ removes tiny divisors.
     */
    # unpack
    mv      a6, a0                                      # a6 = a
    mv      a7, a1                                      # a7 = b

    srli    t4, a6, 15                                  # t4 = sign_a
    srli    t5, a6, 7
    andi    t5, t5, 0xFF                                # t5 = 8-bit exp_a

    srli    a3, a7, 15                                  # a3 = sign_b (before xor)
    srli    a4, a7, 7
    andi    a4, a4, 0xFF                                # a4 = 8-bit exp_b

    # DAZ: exp==0 -> +/-0 (covers true zero and subnormal)
    bnez    t5, bf16_div_daz_a_done
    slli    a6, t4, 15
bf16_div_daz_a_done:
    bnez    a4, bf16_div_daz_b_done
    slli    a7, a3, 15
bf16_div_daz_b_done:

    xor     a3, a3, t4                                  # a3 = sign_xor

    # gate
    # decision exp == 0xFF
    li      t0, 0xFF
    beq     t5, t0, bf16_div_special
    beq     a4, t0, bf16_div_special

    # decision exp == 0 && mant == 0 (±0 via slli 17)
    slli    t0, a6, 17                                  # t0 = (a << 17)
    beqz    t0, bf16_div_special
    slli    t0, a7, 17                                  # t0 = (b << 17)
    beqz    t0, bf16_div_special

    # normal case
    andi    t6, a6, 0x7F                                # t6 = 7-bit mant_a
    snez    t0, t5                                      # exp != 0 ?
    slli    t0, t0, 7                                   # 0 or 0x80
    or      t6, t6, t0

    andi    a5, a7, 0x7F                                # a5 = 7-bit mant_b
    snez    t0, a4                                      # exp != 0 ?
    slli    t0, t0, 7                                   # 0 or 0x80
    or      a5, a5, t0

    sub     a4, t5, a4
    addi    a4, a4, 127                                 # a4 = exp_a - exp_b + 127

    /* soft-div body (see leaf header) */
    slli    t6, t6, 7
    slli    a5, a5, 7
    li      t3, 8
    li      a2, 0

bf16_div_loop:
    slli    a2, a2, 1
    bltu    t6, a5, bf16_div_skip       # rem < aligned divisor -> no sub
    sub     t6, t6, a5
    addi    a2, a2, 1
bf16_div_skip:
    srli    a5, a5, 1
    addi    t3, t3, -1
    bnez    t3, bf16_div_loop

    /* renorm: at most one << for normal x normal; then exp flush + pack */
    li      t0, 0x80
    bgeu    a2, t0, bf16_div_renorm_less
    beqz    a2, bf16_div_renorm_less
    slli    a2, a2, 1
    addi    a4, a4, -1

bf16_div_renorm_less:
    bgez    a4, bf16_div_renorm_exp1
    slli    a0, a3, 15
    ret

bf16_div_renorm_exp1:
    li      t0, 255
    bltu    a4, t0, bf16_div_renorm_exp1_after
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0    
    ret

bf16_div_renorm_exp1_after:
    slli    a0, a3, 15
    slli    t0, a4, 7
    or      a0, a0, t0
    andi    t0, a2, 0x7F
    or      a0, a0, t0
    ret

    /*
     * --- cold specials: mag branch-tree --------------------------------
     *
     * History (same arc as mul; measured on div first):
     *   v1 — class-bit state machine (NaN=1, Inf=2, zero=4, OR, cascade).
     *        Attractive fall-through encoding; more ALU to build classes.
     *   Bench — bench/div_cold_variants.S + DIV_COLD_VARIANTS.md:
     *        specials-heavy instret tree ~67 vs class ~82; 5ST cycles also
     *        tree-win.  Mixed corpus nearly tied (hot dominates).
     *   v2 (current) — mag tree: strip sign, bltu vs 0x7F80 for NaN;
     *        0/0 & Inf/Inf -> NaN; /0 or Inf/finite -> Inf; else +/-0.
     *        Matches C; fewer encode insns on the cold path.
     *
     * Entry after DAZ+gate: a3=sign_xor; a6/a7 may already be flushed +/-0.
     */
bf16_div_special:
    li      t0, 0x7F80
    slli    t1, a6, 17  
    srli    t1, t1, 17                                  # t1 = mag_a
    slli    t2, a7, 17
    srli    t2, t2, 17                                  # t2 = mag_b
    
    bltu    t0, t1, bf16_div_nan
    bltu    t0, t2, bf16_div_nan

    bnez    t1, bf16_div_skip1
    beqz    t2, bf16_div_nan        # 0/0

bf16_div_skip1:
    bne     t1, t0, bf16_div_skip2
    beq     t2, t0, bf16_div_nan    # Inf/Inf
    
bf16_div_skip2:
    beqz    t2, bf16_div_inf        # /0 → Inf
    beq     t1, t0, bf16_div_inf    # Inf/… → Inf
    slli    a0, a3, 15              # ±0
    ret

bf16_div_nan:
    li      a0, 0x7FC0
    ret

bf16_div_inf:
    slli    a0, a3, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret
    .size   bf16_div, .-bf16_div
