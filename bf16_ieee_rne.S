# Guest RV32I BFloat16 — IEEE-style (subnormals + RNE).
# Pair with bf16_ieee_rne.c (verified C gold). AI/truncate asm: bf16.S.
#
# Starting scaffold from the older truncate asm; port hot paths to match
# bf16_ieee_rne.c (add/mul/div/sqrt). Not yet exhaustively verified.
#
# Guest link (start.S / linker.ld / ecall) not wired yet.
#
# ILP32 ABI:
#   args a0-a7, 32-bit return a0
#   caller-saved: ra, t0-t6, a0-a7
#   callee-saved: sp, s0-s11
#   sp 16-byte aligned at every call boundary
#   no mul / div / rem; logical shifts; unsigned compares on bit-patterns
#   (sign/exp/mant); signed ops only where unbiased exp is two's complement
#
# Values: bf16_t is the 16-bit pattern, zero-extended in a0 / a1.
# bf16_negate(a0=&v[0], a1=n) uses a different map (not the two-operand grid).
#
# Two-operand leaf (mul/add/sub/div), after the ABI door:
#   mv a6, a0          # original a   (return-a / NaN / Inf)
#   mv a7, a1          # original b
#   t4 = sign_a        t5 = exp_a         t6 = mant_a
#   a3 = sign_b        a4 = exp_b         a5 = mant_b
#   scratch: a0 a1 a2 t0 t1 t2 t3
#   a0 at ret = packed result (build at the epilogue, then ret)
#   a6/a7 may be reused after specials return
#
# Naming: a3-a5 hold *b*, t4-t6 hold *a*.
# mul overwrites a3 with sign_xor after unpack; add keeps a3 = sign_b.
# sqrt is unary — register map at bf16_sqrt.
# add/sub — register map at bf16_add.
#
# pack (inline at each leaf; no jal bf16_pack):
#   slli a0, <sign>, 15 ;  slli tmp, <exp>, 7 ;  or a0, a0, tmp ;  or a0, a0, <mant>
#
# mul / div / sqrt scratch: a0 a1 a2 t0 t1 t2 t3 per block, short live
# ranges.  After specials, a6 a7 are free on the hot path.
#
# Guest ecalls (planned): write=64, exit=93.

    .text

    .globl  bf16_sign
    .type   bf16_sign, @function
bf16_sign:
    /* a0 = bits  ->  a0 = 0 or 1 */
    srli    a0, a0, 15                                  # bit15 -> bit0; 16-bit pattern so result is 0/1
    ret
    .size   bf16_sign, .-bf16_sign


    .globl  bf16_exp
    .type   bf16_exp, @function
bf16_exp:
    /* a0 = bits  ->  a0 = 8-bit stored exp (0..255) */
    srli    a0, a0, 7                                   # orig bit7 at bit0; sign sits in bit8
    andi    a0, a0, 0xFF                                # drop sign; keep orig bits 14..7
    ret
    .size   bf16_exp, .-bf16_exp


    .globl  bf16_mant
    .type   bf16_mant, @function
bf16_mant:
    /* a0 = bits  ->  a0 = 7-bit stored mant (0..127) */
    andi    a0, a0, 0x7F                                # orig bits 6..0; 0x7F fits andi
    ret
    .size   bf16_mant, .-bf16_mant


    .globl  bf16_pack
    .type   bf16_pack, @function
bf16_pack:
    /* a0=sign(0/1) a1=exp(8-bit) a2=mant(7-bit)  ->  a0 = bits */
    slli    a0, a0, 15                                  # sign -> bit15
    slli    a1, a1, 7                                   # exp  -> bits 14..7
    or      a0, a0, a1
    or      a0, a0, a2                                  # mant already in bits 6..0
    ret
    .size   bf16_pack, .-bf16_pack


    .globl  bf16_isnan
    .type   bf16_isnan, @function
bf16_isnan:
    /* NaN <=> exp==0xFF && mant!=0.
     * Later: (bits 14..0) > 0x7F80 is the same, still branchless (sltu),
     * ~2 fewer insns, but 0x7F80 needs lui+addi.  Keep this form until measured. */
    srli    t5, a0, 7
    andi    t5, t5, 0xFF                                # t5 = 8-bit exp
    andi    t6, a0, 0x7F                                # t6 = 7-bit mant
    xori    t0, t5, 0xFF                                # 0 iff exp == 0xFF
    seqz    t0, t0                                      # t0 = 1 iff exp all ones
    snez    t1, t6                                      # t1 = 1 iff mant != 0
    and     a0, t0, t1                                  # NaN = both
    ret
    .size   bf16_isnan, .-bf16_isnan


    .globl  bf16_isinf
    .type   bf16_isinf, @function
bf16_isinf:
    /* Inf <=> exp==0xFF && mant==0.  Same split as isnan; seqz on mant, not snez. */
    srli    t5, a0, 7
    andi    t5, t5, 0xFF                                # t5 = 8-bit exp
    andi    t6, a0, 0x7F                                # t6 = 7-bit mant
    xori    t0, t5, 0xFF                                # 0 iff exp == 0xFF
    seqz    t0, t0                                      # t0 = 1 iff exp all ones
    seqz    t1, t6                                      # t1 = 1 iff mant == 0
    and     a0, t0, t1                                  # Inf = both
    ret
    .size   bf16_isinf, .-bf16_isinf


    .globl  bf16_iszero
    .type   bf16_iszero, @function
bf16_iszero:
    /* +0 and -0: exp==0 && mant==0 (ignore sign).
     * Later: slli 17 drops bit15 (sign) off the 32-bit register; bits 14..0
     * land in 31..17.  seqz then tests magnitude==0.  Two insns, both signs.
     * Keep this split until measured. */
    srli    t5, a0, 7
    andi    t5, t5, 0xFF                                # t5 = 8-bit exp
    andi    t6, a0, 0x7F                                # t6 = 7-bit mant
    seqz    t0, t5                                      # t0 = 1 iff exp == 0
    seqz    t1, t6                                      # t1 = 1 iff mant == 0
    and     a0, t0, t1
    ret
    .size   bf16_iszero, .-bf16_iszero


    .globl  f32_to_bf16
    .type   f32_to_bf16, @function
f32_to_bf16:
    /* Same 32 bits as u32_to_bf16; only the C types differ (float vs uint32_t).
     * Host C needs memcpy so the assignment does not convert the value.
     * ILP32 with no F: the float arrives in a0 as that bit pattern already,
     * so this label falls through into u32_to_bf16 and saves a `j`. */


    .globl  u32_to_bf16
    .type   u32_to_bf16, @function
u32_to_bf16:
    /* a0 = f32 bits  ->  a0 = bf16 bits
     *
     * Inf/NaN (exp bits == 0x7F800000): copy the top 16 bits, no rounding.
     * Else: round-to-nearest-even,
     *   x += ((x >> 16) & 1) + 0x7FFF, then x >> 16.
     *
     * Rounding is the fall-through arm: finite inputs are the common case,
     * and rv32emu still charges 1 for a taken beq.  Inf/NaN is beq away.
     * Inf/NaN is rare, so there is no branchless mux: the two arms do
     * different work, and a mask-select would cost more than this beq.
     * (The label name is leftover from when this arm was the taken path.)
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
    /* a0 = a, a1 = b  ->  a0 = a*b
     *
     * Architecture (A layout + B only in the cold path):
     *
     *   ABI door: a0/a1, then mv a6=a a7=b.  Unpack once:
     *     t4=sign_a t5=exp_a t6=mant_a
     *     a3=sign_b a4=exp_b a5=mant_b
     *
     *   Gate (every call, cheap): special if exp==0xFF or (exp==0 &&
     *   mant==0), either side.  Built from t5/a4 already in registers —
     *   and slli 17 for ±0.  No 0x7F80 on the hot gate.
     *     beq  mul_specials          rare
     *     fall through               both finite nonzero
     *
     *   Hot path: implicit 1, 8x8 shift-and-add, exp_a+exp_b-bias,
     *   renormalize, XOR signs, pack into a0, ret.
     *
     *   TODO rounding: do NOT truncate with >>7/>>8 & 0x7F.  Same RNE as
     *   u32_to_bf16 (round-to-nearest-even / 四捨六入五成雙): keep the bits
     *   shifted off the product, apply an RNE bias (tie-to-even on LSB),
     *   then take the 7-bit field.  Plain chop is wrong.
     *
     *   TODO subnormals: exp==0 && mant!=0 -> no hidden 1 (already).  On
     *   underflow, gradual subnormal (right-shift, exp=0) instead of
     *   flushing every exp < 0 straight to ±0.
     *
     *   Cold path (code sits after that ret): classify by magnitude,
     *     mag = bits 14..0 = slli 17 / srli 17  (NOT slli 1 / srli 1)
     *     class bits: NaN=1, Inf=2, zero=4  (per side; or into t0)
     *
     *   Decision fall-through (label order is load-bearing):
     *     (t0 & 1) != 0     -> nan
     *     t0 == 6           -> nan          (Inf*0 / 0*Inf; not 4&2)
     *     (t0 & 2) != 0     -> inf
     *     else fall into bf16_mul_zero      (no andi 4 / bnez)
     *
     *   Why the last arm can fall through: gate only enters specials when
     *   a side is 0xFF or ±0, so after NaN / Inf*0 / Inf are peeled off the
     *   only remainder is a zero product.  bf16_mul_zero must sit immediately
     *   after the Inf beq; nan/inf labels are out-of-line rets.
     *
     *   Do not multiply first and "fix" later: Inf*0 would not become NaN.
     *   Do not run B on the gate: 1*2 would pay lui 0x7F80 every time.
     */
    mv      a6, a0                                      # a6 = a
    mv      a7, a1                                      # a7 = b

    srli    t4, a6, 15                                  # t4 = sign_a
    srli    t5, a6, 7
    andi    t5, t5, 0xFF                                # t5 = 8-bit exp_a

    srli    a3, a7, 15                                  # a3 = sign_b
    srli    a4, a7, 7
    andi    a4, a4, 0xFF                                # a4 = 8-bit exp_b
    xor     a3, t4, a3                                  # a3 = sign_xor

    # decision exp == 0xFF
    li      t0, 0xFF
    beq     t5, t0, bf16_mul_specialcase
    beq     a4, t0, bf16_mul_specialcase

    # decision exp == 0 && mant == 0 (±0 via slli 17)
    slli    t0, a6, 17                                  # t0 = (a << 17)
    beqz    t0, bf16_mul_specialcase
    slli    t0, a7, 17                                  # t0 = (b << 17)
    beqz    t0, bf16_mul_specialcase

    # Normal case — attach implicit 1 when exp != 0
    andi    t6, a6, 0x7F                                # t6 = 7-bit mant_a
    snez    t0, t5                                      # t0 = 1 if exp != 0
    slli    t0, t0, 7                                   # t0 = 0 or 0x80
    or      t6, t6, t0                                  # if exp_a != 0, mant_a | 0x80

    andi    a5, a7, 0x7F                                # a5 = 7-bit mant_b
    snez    t0, a4                                      # t0 = 1 if exp != 0
    slli    t0, t0, 7                                   # t0 = 0 or 0x80
    or      a5, a5, t0                                  # if exp_b != 0, mant_b | 0x80

    add     a4, a4, t5                                  # a4 = exp_a + exp_b
    addi    a4, a4, -127                                # a4 = exp_temp = exp_a + exp_b - 127

    # After gate + this setup (hot path only):
    #   live:  a3=sign_xor  a4=exp_a+exp_b-127  a5=mant_b  t6=mant_a
    #          a6=orig a    a7=orig b   (drop when implicit-1 is on)
    #   free:  a0 a1 a2  t0 t1 t2 t3 t4 t5

    # mul_loop: mant_a * mant_b (8-bit, branchless bit via mask)
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

bf16_mul_specialcase:
    # a3 = sign_xor; strip signs into magnitude-only a6/a7
    slli    a6, a6, 17                                  # a6 = (a << 17)
    srli    a6, a6, 17                                  # a6 = mag_a
    slli    a7, a7, 17                                  # a7 = (b << 17)
    srli    a7, a7, 17                                  # a7 = mag_b

    li      t0, 0x7F80

    # class_a: zero=4, Inf=2, NaN=1
    seqz    t1, a6                                      # t1 = 1 if a6 == 0
    xor     t2, a6, t0                                  # t2 = (a6 ^ 0x7F80)
    seqz    t2, t2                                      # t2 = 1 if Inf
    sltu    t3, t0, a6                                  # t3 = 1 if NaN (mag > 0x7F80)
    slli    t5, t1, 2                                   # t5 = class_a
    slli    t1, t2, 1
    or      t5, t5, t1
    or      t5, t5, t3

    # class_b
    seqz    t1, a7                                      # t1 = 1 if a7 == 0
    xor     t2, a7, t0                                  # t2 = (a7 ^ 0x7F80)
    seqz    t2, t2                                      # t2 = 1 if Inf
    sltu    t3, t0, a7                                  # t3 = 1 if NaN
    slli    t6, t1, 2                                   # t6 = class_b
    slli    t1, t2, 1
    or      t6, t6, t1
    or      t6, t6, t3

    # decision — fall-through into zero (see bf16_mul header)
    or      t0, t5, t6                                  # t0 = (t5 | t6)
    andi    t1, t0, 1                                   # NaN bit
    bnez    t1, bf16_mul_nan
    li      t1, 6                                       # Inf|zero == 6
    beq     t1, t0, bf16_mul_nan
    andi    t1, t0, 2                                   # Inf present (not Inf*0)
    bnez    t1, bf16_mul_inf
    # fall through: zero product (signed zero via a3)

bf16_mul_zero:
    slli    a0, a3, 15
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
     * bf16_sqrt — unary; mirrors bf16.c.
     *
     * Algorithm choice (current): binary search on mid in [90, 256] with
     * sq = (mid*mid)/128 each probe.  No mul insn → each square is a
     * software mask-mul (~15–20 instret); ~8 probes → often 150+ instret
     * on the hot path.  Kept because it matches bf16.c bit-exactly and is
     * easy to audit against the host tests (full 16-bit space checked).
     *
     * Alternate (not implemented): digit-recurrence / non-restoring
     * integer square root on the normalized significand — ~7–8
     * shift+add/sub steps, no multiply; likely 3–4× fewer instret on
     * rv32emu.  Requires C and asm to move together or gold is no longer
     * bit-exact with bf16.c; asm-only rewrite diverges ulps.
     *
     * Rationale (no soft-float lib):
     *   RV32I has no fpu and no mul/div.  Host C uses integer mid*mid and
     *   a fixed-point binary search; asm mirrors that with shift-and-add.
     *
     * Gate order is load-bearing (matches C: return-a before sign->NaN):
     *   1) ±0 via slli 17          -> ret a0 unchanged (-0 keeps sign)
     *   2) exp==0xFF               -> specials (NaN / ±Inf)
     *   3) sign!=0                 -> canonical NaN (negative finite)
     *   4) else                    -> hot path
     * Putting sign before 0xFF would turn -NaN into 0x7FC0; C returns a.
     *
     * +------+---------------------------+----------------------------------+
     * | reg  | gate / unpack             | hot                              |
     * +------+---------------------------+----------------------------------+
     * | a0   | ABI in; keep on ret-a     | pack at epilogue only            |
     * | a1   | —                         | mid*mid product, then sq = >>7   |
     * | a2   | —                         | mul: mid bits (right-shift)      |
     * | a3   | —                         | mul: mid addend (left-shift)     |
     * | a4   | —                         | high (init 256; pack high&0x7F)  |
     * | a5   | —                         | low  (init 90)                   |
     * | a6   | original a                | mid each bisect (never mul-clob) |
     * | a7   | — (no orig b)             | new_exp until pack               |
     * | t0-2 | gate / mul scratch        | mask-mul temps                   |
     * | t4   | sign_a                    | stays 0 on hot; pack as sign     |
     * | t5   | exp → unbiased            | free after new_exp (not a count) |
     * | t6   | mant 7-bit                | |0x80; odd<<1; bisect cmp target |
     * +------+---------------------------+----------------------------------+
     *
     * Hot math (same as C):
     *   Unbias exp, attach implicit 1.  Odd unbiased exp: mant<<=1 and
     *   exp-=1 so the remaining exp is even; then
     *     new_exp = (exp >> 1) + 127
     *   Odd and even share that formula — only the mant shift differs —
     *   so even beqz-skips the shift/addi and falls into one srai+addi.
     *   Use srai (not srli): unbiased exp can be negative (e.g. 0.5 -> -1).
     *
     *   Bisect mid in [90, 256] with sq = (mid*mid)/128 against mant.
     *   sq <= mant -> low = mid+1; else high = mid-1.  Result mant is
     *   high&0x7F (C).  Unsigned compares (bltu/bgeu): all values >= 0.
     *
     *   mid*mid: same branchless mask idiom as bf16_mul, but keep a6=mid
     *   (need mid±1 after).  Work on a2/a3 copies.  Stop with bnez a2
     *   (no fixed 9-count) — remaining zero bits cannot change the product.
     *
     *   Compare fall-through: common arm (sq <= mant) falls into low=mid+1;
     *   rare arm branches to high=mid-1.  Still one j to join.
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
    /* a0 = &v[0], a1 = n */
    unimp
    .size   bf16_negate, .-bf16_negate


    .globl  bf16_sub
    .type   bf16_sub, @function
bf16_sub:
    /* a - b == a + (-b): flip b sign, fall into bf16_add. */
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
     * Cold does not use mul's class-bit machine (NaN=1,Inf=2,zero=4).  Add
     * specials need same/opposite Inf and signed-zero rules; field checks
     * plus short-circuit branches match bf16.c with less encoding.
     *
     * Cold ff:  NaN; both Inf (same -> a, opposite -> NaN); one Inf -> that op.
     * Cold zero: opposite ±0 -> +0; same ±0 -> a; only a zero -> b; only b -> a.
     *
     * Hot (both finite nonzero): |0x80, align by |dexp| (>=8 -> small mant 0),
     * same-sign add / opposite sub (equal mags -> +0), renormalize, pack.
     * Possible micro-opt: swap so the larger exp is always in the a slots.
     *
     * TODO rounding: align/renorm currently drop bits; pack is mant&0x7F
     * (truncate).  Keep G/R/S through the right-shifts and add/sub, then
     * round-to-nearest-even like u32_to_bf16 (四捨六入五成雙 / tie-to-even),
     * not a plain chop.
     *
     * TODO subnormals: do NOT ori 0x80 when exp==0 (denorm has no hidden 1).
     * After renorm, exp<=0 should produce a subnormal when the significand
     * still has bits (exp=0, no hidden 1), not flush every exp < 0 to +0.
     *
     * Unpack once before the gate so ff / zero / hot share t4-t6 and a3-a5.
     *
     * +------+---------------------------+----------------------------------+
     * | reg  | gate / unpack             | hot (after specials)             |
     * +------+---------------------------+----------------------------------+
     * | a0   | ABI a; pack at epilogue   | result / scratch                 |
     * | a1   | ABI b; sub may clobber    | result mant (after add/sub)      |
     * | a2   | —                         | scratch (dexp, renorm, …)        |
     * | a3   | sign_b                    | sign_b until result sign chosen  |
     * | a4   | exp_b                     | exp_b / then free or dexp        |
     * | a5   | mant_b                    | mant_b |0x80, then aligned       |
     * | a6   | original a (cold ret-a)   | free — reuse as scratch          |
     * | a7   | original b (cold ret-b)   | free — reuse as scratch          |
     * | t0-3 | gate / hot scratch        | align, compare, renorm           |
     * | t4   | sign_a                    | -> result sign                   |
     * | t5   | exp_a                     | -> result exp (max, then renorm) |
     * | t6   | mant_a                    | mant_a |0x80, then aligned       |
     * +------+---------------------------+----------------------------------+
     */

    # unpack — shared by gate, special_ff, special_zero, hot
    mv      a6, a0                                      # a6 = a
    mv      a7, a1                                      # a7 = b

    srli    t4, a6, 15                                  # t4 = sign_a
    srli    t5, a6, 7
    andi    t5, t5, 0xFF                                # t5 = 8-bit exp_a
    andi    t6, a6, 0x7F                                # t6 = 7-bit mant_a

    srli    a3, a7, 15                                  # a3 = sign_b (kept; not xor)
    srli    a4, a7, 7
    andi    a4, a4, 0xFF                                # a4 = 8-bit exp_b
    andi    a5, a7, 0x7F                                # a5 = 7-bit mant_b

    # gate
    li      t0, 0xFF
    beq     t5, t0, bf16_add_special_ff
    beq     a4, t0, bf16_add_special_ff

    slli    t0, a6, 17                                  # mag_a == 0? (±0)
    beqz    t0, bf16_add_special_zero
    slli    t0, a7, 17                                  # mag_b == 0?
    beqz    t0, bf16_add_special_zero

bf16_add_hot:
    /* align / add-or-sub / renorm / pack — not filled yet */
    ori     t6, t6, 0x80
    ori     a5, a5, 0x80
    li      t3, 8

    bltu    t5, a4, bf16_add_exp
    # exp_a >= exp_b                                    #exp = exp_a
    sub     t0, t5, a4
    bgeu    t0, t3, bf16_add_exp_shift_b
    srl     a5, a5, t0
    j       bf16_add_exp_after

bf16_add_exp_shift_b:
    li      a5, 0
    j       bf16_add_exp_after

bf16_add_exp:
    sub     t0, a4, t5
    mv      t5, a4                                      # exp = exp_b
    bgeu    t0, t3, bf16_add_exp_shift_a
    srl     t6, t6, t0
    j       bf16_add_exp_after

bf16_add_exp_shift_a:
    li      t6, 0

bf16_add_exp_after:

    bne     t4, a3, bf16_add_sign_diff
    # t4 = sign = sign_a
    add     t6, t6, a5                              # t6 = mant = mant_a + mant_b
    j       bf16_add_mant_after

bf16_add_sign_diff:
    bne     a5, t6, bf16_add_mant0
    # mant_a == mant_b
    li      a0, 0
    ret

bf16_add_mant0:
    bltu    t6, a5, bf16_add_mant1
    # mant_a > mant_b
    # t4 = sign = sign_a
    sub     t6, t6, a5                              # t6 = mant = mant_a - mant_b
    j       bf16_add_mant_after
bf16_add_mant1:
    # mant_a < mant_b
    mv      t4, a3                                  # t4 = sign = sign_b
    sub     t6, a5, t6                              # t6 = mant = mant_b - mant_a

bf16_add_mant_after:
    
    # renorm
    bnez    t6 , bf16_add_renorm_ue0
    li      a0, 0
    ret

bf16_add_renorm_ue0:
    li      t0, 0x100                               # t0 = threshold
    bltu    t6, t0, bf16_add_renorm_less
    # mant >= 0x100
    srli    t6, t6, 1
    addi    t5, t5, 1

bf16_add_renorm_less:
    li      t0, 0x80                                # t0 = threshold
    bgeu    t6, t0, bf16_add_renorm_norm
    # mant < 0x80
bf16_add_renorm_less_loop:
    slli    t6, t6, 1
    addi    t5, t5, -1
    bltu    t6, t0, bf16_add_renorm_less_loop

bf16_add_renorm_norm:
    bgez    t5, bf16_add_renorm_exp
    li      a0, 0
    ret

bf16_add_renorm_exp:
    li      t0, 255
    bltu    t5, t0, bf16_add_renorm_exp_after
    slli    a0, t4, 15
    li      t0, 0xFF
    slli    t0, t0, 7
    or      a0, a0, t0
    ret

bf16_add_renorm_exp_after:
    slli    a0, t4, 15
    slli    t0, t5, 7
    or      a0, a0, t0
    andi    t0, t6, 0x7F
    or      a0, a0, t0
    ret

    /*
     * bf16_add_special_ff — revision notes
     *
     * Goal (bf16.c): any NaN -> 0x7FC0; both Inf same sign -> a; both Inf
     * opposite -> NaN; exactly one Inf -> that operand.
     *
     * Attempt 1 — mag via slli 17 / or == 0xFF000000; NaN as mant==0xFF:
     *   mant is 7-bit, never equals 0xFF, so NaN never fired.
     *   Inf|finite often still or'd to 0xFF000000, so "both Inf" false-hit.
     *   one-Inf arm compared seqz flag to ABI a1 (stale).  Rejected.
     *
     * Attempt 2 — boolean isnan (xori/seqz/snez/and); both-Inf as
     *   (exp_a & exp_b) != 0 plus mant==0:
     *   bitwise and of exps is not "both == 0xFF" (e.g. 0xFF & 0x01).
     *   Branch targets inverted (both-Inf path vs one-Inf).  Rejected.
     *
     * Attempt 3 — both Inf: xori each exp with 0xFF, or, beqz (zero => both
     *   FF).  one Inf: xori exp_a, bnez -> ret b else ret a.  Correct.
     *
     * Attempt 4 — keep Attempt-3 results in a1/a2 to reuse on one-Inf.
     *   Works; temps optional once short-circuit lands.
     *
     * Attempt 5 — 0x7F80 magnitude class (mul-style) considered for ff:
     *   equivalent, but needs strip-sign + load 0x7F80.  Field checks on
     *   already-unpacked exp/mant are enough here; not adopted.
     *
     * Attempt 6 (current) — short-circuit on fields, no class bits:
     *   NaN: bne exp,0xFF skip; bnez mant -> nan (per side).
     *   Then: bne exp_a,FF -> only b Inf; bne exp_b,FF -> only a Inf;
     *   else both Inf: same sign -> ret a, else fall into nan.
     *   Drops a1/a2 xori temps; li t0,0xFF shared.  Opposite both-Inf
     *   falls through into bf16_add_nan (label order load-bearing).
     */
bf16_add_special_ff:
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
    mv      a0, a7                                      # only b Inf
    ret

bf16_add_special_ret_a:
    mv      a0, a6
    ret

    /*
     * bf16_add_special_zero — mirrors bf16.c zero arms.
     * Entry: at least one ±0; neither Inf/NaN (FF already gated).
     *
     * Mag via slli 17 / srli 17 (drop sign): a1 = mag_a, a2 = mag_b.
     *
     *   or a1|a2
     *     == 0  -> both zero
     *         t4^a3 == 0  -> same sign -> j/fall to same_sign: ret a
     *         else        -> +0 (li a0, 0)
     *     != 0  -> exactly one zero -> single
     *         a1 == 0     -> only a zero -> ret_b
     *         else        -> only b zero -> fall through into same_sign
     *                        (same two insns: mv a0, a6; ret)
     *
     * Label same_sign is shared: both-zero same-sign and only-b-zero
     * both need "return a".  Entry invariant: not both nonzero.
     */
bf16_add_special_zero:
    slli    a1, a6, 17
    srli    a1, a1, 17                                  # a1 = mag_a

    slli    a2, a7, 17
    srli    a2, a2, 17                                  # a2 = mag_b

    or      t3, a1, a2
    bnez    t3, bf16_add_special_zero_single            # one side nonzero

    xor     t3, t4, a3                                  # both zero: sign_a ^ sign_b
    beqz    t3, bf16_add_special_zero_same_sign         # same -> a
    li      a0, 0                                       # opposite -> +0
    ret

bf16_add_special_zero_single:
    beqz    a1, bf16_add_special_ret_b                  # only a zero -> b
    # only b zero: fall through -> return a

bf16_add_special_zero_same_sign:
    mv      a0, a6                                      # return a (both-zero same, or only-b-zero)
    ret

bf16_add_special_ret_b:
    mv      a0, a7
    ret

    .size   bf16_add, .-bf16_add
    .size   bf16_sub, .-bf16_sub


    .globl  bf16_div
    .type   bf16_div, @function
bf16_div:
    unimp
    .size   bf16_div, .-bf16_div
