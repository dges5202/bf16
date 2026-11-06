/*
 * Truncate BF16 — host C working copy (pair: bf16.S guest ASM).
 * IEEE subnormals+RNE twin: bf16_ieee_rne.c (archive; do not edit for this path).
 *
 * Policy (hot paths):
 *   - No RNE: chop with shifts and & 0x7F (drop G/R/S).
 *   - No gradual underflow: exp < 0 -> +/-0 (add flushes to +0); exp >= 255 -> +/-Inf.
 *   - Hidden bit only when exp != 0 (mul/div); add/sqrt attach |0x80 like the
 *     matching truncate asm (denorms not gradually handled).
 *   - Div: DAZ — subnormal inputs (exp==0 && mant!=0) flush to +/-0 before
 *     specials/hot; soft-div is 8-bit aligned (normal×normal contract). See bf16_div.
 *
 * No float arithmetic in the op bodies: binary32 is a uint32_t bit pattern
 * (memcpy, not a pointer cast). Host integer `*` is fine for mant products;
 * the RV32I guest has no mul.
 *
 *   make test
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t bits;
} bf16_t;

#define BF16_SIGN_MASK 0x8000U
#define BF16_EXP_MASK  0x7F80U
#define BF16_MANT_MASK 0x007FU
#define BF16_EXP_BIAS  127

#define BF16_NAN()  ((bf16_t) {.bits = 0x7FC0})
#define BF16_ZERO() ((bf16_t) {.bits = 0x0000})
#define BF16_NEG_ZERO() ((bf16_t) {.bits = 0x8000})
#define BF16_INF()  ((bf16_t) {.bits = 0x7F80})
#define BF16_NEG_INF()  ((bf16_t) {.bits = 0xFF80})

/* ========== bf16 ops ========== */

static uint16_t bf16_sign(bf16_t a)
{
    /* bits 15 */
    return (a.bits & BF16_SIGN_MASK) >> 15;
}

static uint16_t bf16_exp(bf16_t a)
{
    /* bits 14..7, mask 0xFF */

    return (a.bits & BF16_EXP_MASK) >> 7;
}

static uint16_t bf16_mant(bf16_t a)
{
    /* bits 6..0, mask 0x7F */
    return (a.bits & BF16_MANT_MASK);
}

static bf16_t bf16_pack(uint16_t sign, uint16_t exp, uint16_t mant)
{
    /* (sign << 15) | (exp << 7) | mant */
    return (bf16_t) {.bits = (sign << 15) | (exp << 7) | mant};
}

static int bf16_isnan(bf16_t a)
{
    /* exp all ones AND mant nonzero */
    
    return (bf16_exp(a) == 0xFF) && (bf16_mant(a) != 0);
}

static int bf16_isinf(bf16_t a)
{
    /* exp all ones AND mant zero.  == BF16_EXP_MASK, not a nonzero mask. */
    return (bf16_exp(a) == 0xFF) && (bf16_mant(a) == 0);
}

static int bf16_iszero(bf16_t a)
{
    /* ignore sign: bits 14..0 are zero */
    return (bf16_exp(a) == 0) && (bf16_mant(a) == 0);
}

static bf16_t u32_to_bf16(uint32_t f32bits)
{
    /* inf/nan: copy top 16 bits, skip rounding
     * else: round-to-nearest-even
     *   f32bits += ((f32bits >> 16) & 1) + 0x7FFF
     *   return f32bits >> 16
     */
    if ((f32bits & 0x7F800000) == 0x7F800000) {
        return (bf16_t) {.bits = (uint16_t)(f32bits >> 16)};
    }
    else {
        f32bits += ((f32bits >> 16) & 1) + 0x7FFF;
        return (bf16_t) {.bits = f32bits >> 16};
    }
}

static bf16_t f32_to_bf16(float val)
{
    /* memcpy bits, then u32_to_bf16.  no float arithmetic. */
    uint32_t f32bits;
    memcpy(&f32bits, &val, sizeof(float));

    return u32_to_bf16(f32bits);
}

static bf16_t bf16_mul(bf16_t a, bf16_t b)
{
    /* Truncate mul: XOR sign; specials; hidden 1 iff exp!=0;
     * 8x8 product; if bit15 set >>8 else >>7 & 0x7F; flush exp<0 / >=255.
     */
    uint16_t sign = bf16_sign(a) ^ bf16_sign(b);

    if (bf16_isnan(a) || bf16_isnan(b))
        return BF16_NAN();
    else if ((bf16_iszero(a) && bf16_isinf(b)) ||
             (bf16_isinf(a) && bf16_iszero(b)))
        return BF16_NAN();
    else if (bf16_isinf(a) || bf16_isinf(b))
        return bf16_pack(sign, 0xFF, 0);
    else if (bf16_iszero(a) || bf16_iszero(b))
        return bf16_pack(sign, 0, 0);

    int32_t exp_a = bf16_exp(a);
    int32_t exp_b = bf16_exp(b);
    int32_t mant_a = bf16_mant(a);
    int32_t mant_b = bf16_mant(b);

    if (exp_a)
        mant_a |= 0x80;
    if (exp_b)
        mant_b |= 0x80;

    int32_t exp = exp_a + exp_b - BF16_EXP_BIAS;
    uint32_t mant = (uint32_t)mant_a * (uint32_t)mant_b;

    if (mant & 0x8000u) {
        mant = (mant >> 8) & 0x7Fu;
        exp++;
    } else {
        mant = (mant >> 7) & 0x7Fu;
    }

    if (exp < 0)
        return bf16_pack(sign, 0, 0);
    if (exp >= 255)
        return bf16_pack(sign, 0xFF, 0);
    return bf16_pack(sign, (uint16_t)exp, (uint16_t)mant);
}

static bf16_t bf16_sqrt(bf16_t a)
{
    /* Truncate sqrt: bisect mid in [90,256], sq=(mid*mid)/128; pack high&0x7F.
     * No remainder rounding. Always |0x80 (no denorm normalize).
     */
    uint16_t sign = bf16_sign(a);

    if ((bf16_isinf(a) && sign == 0) || bf16_iszero(a) || bf16_isnan(a))
        return a;
    else if (sign == 1)
        return BF16_NAN();

    int32_t e = bf16_exp(a);
    int32_t mant = bf16_mant(a) | 0x80;
    int32_t exp = e - BF16_EXP_BIAS;
    int32_t new_exp;

    if (exp & 1) {
        mant <<= 1;
        new_exp = ((exp - 1) >> 1) + BF16_EXP_BIAS;
    } else {
        new_exp = (exp >> 1) + BF16_EXP_BIAS;
    }

    int32_t low = 90;
    int32_t high = 256;
    while (low <= high) {
        int32_t mid = (low + high) >> 1;
        int32_t sq = (mid * mid) / 128;
        if (sq <= mant)
            low = mid + 1;
        else
            high = mid - 1;
    }

    if (new_exp >= 255)
        return bf16_pack(sign, 0xFF, 0);
    return bf16_pack(sign, (uint16_t)new_exp, (uint16_t)(high & 0x7F));
}

static void bf16_negate(bf16_t *v, size_t n)
{
    /* v[i].bits ^= BF16_SIGN_MASK */
    for(size_t i = 0; i < n; i++) {
        v[i].bits ^= BF16_SIGN_MASK;
    }
}

static bf16_t bf16_add(bf16_t a, bf16_t b)
{
    /* Truncate add: specials; always |0x80; align with plain >> (dexp>=8 -> 0);
     * same-sign add / opposite sub; renorm; exp<0 -> +0; exp>=255 -> Inf; chop &0x7F.
     */
    uint16_t sign_a = bf16_sign(a);
    uint16_t sign_b = bf16_sign(b);
    uint16_t sign;

    if (bf16_isnan(a) || bf16_isnan(b))
        return BF16_NAN();
    else if (sign_a != sign_b && (bf16_isinf(a) && bf16_isinf(b)))
        return BF16_NAN();
    else if (sign_a == sign_b && (bf16_isinf(a) && bf16_isinf(b)))
        return a;
    else if (bf16_isinf(a) && !bf16_isinf(b))
        return a;
    else if (bf16_isinf(b) && !bf16_isinf(a))
        return b;
    else if (sign_a != sign_b && (bf16_iszero(a) && bf16_iszero(b)))
        return BF16_ZERO();
    else if (sign_a == sign_b && bf16_iszero(a) && bf16_iszero(b))
        return a;
    else if (bf16_iszero(a) && !bf16_iszero(b))
        return b;
    else if (bf16_iszero(b) && !bf16_iszero(a))
        return a;

    int32_t exp_a = bf16_exp(a);
    int32_t exp_b = bf16_exp(b);
    int32_t exp;
    int32_t mant_a = bf16_mant(a) | 0x80;
    int32_t mant_b = bf16_mant(b) | 0x80;
    int32_t mant;

    if (exp_a >= exp_b) {
        exp = exp_a;
        int32_t dexp = exp_a - exp_b;
        if (dexp >= 8)
            mant_b = 0;
        else
            mant_b >>= dexp;
    } else {
        exp = exp_b;
        int32_t dexp = exp_b - exp_a;
        if (dexp >= 8)
            mant_a = 0;
        else
            mant_a >>= dexp;
    }

    if (sign_a == sign_b) {
        sign = sign_a;
        mant = mant_a + mant_b;
    } else if (mant_a > mant_b) {
        sign = sign_a;
        mant = mant_a - mant_b;
    } else if (mant_a < mant_b) {
        sign = sign_b;
        mant = mant_b - mant_a;
    } else {
        return BF16_ZERO();
    }

    if (mant == 0)
        return BF16_ZERO();
    if (mant >= 0x100) {
        mant >>= 1;
        exp++;
    }
    while (mant < 0x80) {
        mant <<= 1;
        exp--;
    }

    if (exp < 0)
        return BF16_ZERO();
    if (exp >= 255)
        return bf16_pack(sign, 0xFF, 0);
    return bf16_pack(sign, (uint16_t)exp, (uint16_t)(mant & 0x7F));
}

static bf16_t bf16_sub(bf16_t a, bf16_t b)
{
    /* flip b's sign, add */
    bf16_t b_neg = b;
    b_neg.bits ^= BF16_SIGN_MASK;
    return bf16_add(a, b_neg);
}

/*
 * Soft-div for truncate bf16_div — matches bf16.S (8-iter aligned, BRANCHED).
 *
 * Binary long division with both operands <<7, then divisor >>=1 each step:
 *   rem = mant_a<<7;  d = mant_b<<7;  8 steps → quot bits.
 * Enough for normal×normal (hidden mant in [0x80,0xFF] ⇒ quot < 256).
 *
 * Why branched (not sltu/mask): bench/DIV_LOOP_VARIANTS.md —
 * hot avg instret ~210 vs ~282; fewer Ripes 5ST cycles too.
 */
static uint32_t bf16_soft_div_u(uint32_t mant_a, uint32_t mant_b)
{
    uint32_t rem = mant_a << 7;
    uint32_t d = mant_b << 7;
    uint32_t q = 0;
    int i;

    for (i = 0; i < 8; i++) {
        q <<= 1;
        if (rem >= d) { /* branched skip when rem < aligned divisor */
            rem -= d;
            q |= 1u;
        }
        d >>= 1;
    }
    return q;
}

static bf16_t bf16_div(bf16_t a, bf16_t b)
{
    /*
     * Truncate / AI-style div.
     *
     * Subnormals (exp==0 && mant!=0): DAZ — flush to +/-0 (keep sign) before
     * any specials or hot path.  Not IEEE gradual denorms; contract is
     * finite normal×normal after DAZ (+ Inf/NaN/0 rules).  Output still
     * FTZ on exp<0.  Full subnormal math: bf16_ieee_rne.c.
     *
     * Hot: XOR sign; specials; hidden 1 (exps nonzero after DAZ);
     * soft_div 8-iter aligned branched; renorm; flush; chop &0x7F.
     */
    /* DAZ */
    if (bf16_exp(a) == 0 && bf16_mant(a) != 0)
        a = bf16_pack(bf16_sign(a), 0, 0);
    if (bf16_exp(b) == 0 && bf16_mant(b) != 0)
        b = bf16_pack(bf16_sign(b), 0, 0);

    uint16_t sign = bf16_sign(a) ^ bf16_sign(b);

    if (bf16_isnan(a) || bf16_isnan(b))
        return BF16_NAN();
    else if ((bf16_iszero(a) && bf16_iszero(b)) ||
             (bf16_isinf(a) && bf16_isinf(b)))
        return BF16_NAN();
    else if (!bf16_iszero(a) && bf16_iszero(b))
        return bf16_pack(sign, 0xFF, 0);
    else if (bf16_isinf(a) && !bf16_isinf(b))
        return bf16_pack(sign, 0xFF, 0);
    else if (bf16_isinf(b) && !bf16_isinf(a))
        return bf16_pack(sign, 0, 0);
    else if (bf16_iszero(a) && !bf16_iszero(b))
        return bf16_pack(sign, 0, 0);

    int32_t exp_a = bf16_exp(a);
    int32_t exp_b = bf16_exp(b);
    int32_t mant_a = bf16_mant(a) | 0x80; /* normals only after DAZ */
    int32_t mant_b = bf16_mant(b) | 0x80;

    int32_t exp = exp_a - exp_b + BF16_EXP_BIAS;
    uint32_t quot = bf16_soft_div_u((uint32_t)mant_a, (uint32_t)mant_b);

    /* normal×normal quot is in [0x40,0xFF]; no >=0x100 path needed */
    while (quot < 0x80u && quot != 0u) {
        quot <<= 1;
        exp--;
    }

    if (exp < 0)
        return bf16_pack(sign, 0, 0);
    if (exp >= 255)
        return bf16_pack(sign, 0xFF, 0);
    return bf16_pack(sign, (uint16_t)exp, (uint16_t)(quot & 0x7Fu));
}

/* ========== tests ========== */

static int check16(const char *name, bf16_t got, uint16_t expect)
{
    if (got.bits == expect) {
        printf("PASS  %s  0x%04X\n", name, got.bits);
        return 0;
    }
    printf("FAIL  %s  got 0x%04X  expect 0x%04X\n", name, got.bits, expect);
    return 1;
}

static int checki(const char *name, int got, int expect)
{
    if (got == expect) {
        printf("PASS  %s  %d\n", name, got);
        return 0;
    }
    printf("FAIL  %s  got %d  expect %d\n", name, got, expect);
    return 1;
}

static bf16_t B(uint16_t bits)
{
    return (bf16_t) {.bits = bits};
}

/* Independent round-to-nearest-even, not a copy of u32_to_bf16. */
static uint16_t rne_ref(uint32_t x)
{
    uint16_t kept;
    uint32_t disc;

    if ((x & 0x7F800000u) == 0x7F800000u)
        return (uint16_t) (x >> 16);
    kept = (uint16_t) (x >> 16);
    disc = x & 0xFFFFu;
    if (disc > 0x8000u || (disc == 0x8000u && (kept & 1u)))
        kept++;
    return kept;
}

int main(void)
{
    int fail = 0;
    bf16_t arr[2];

    /* 0x4049 -> e=1, M=73, value 3.140625.  1.0 is 0x3F80. */
    fail += checki("sign(0x4049)", (int) bf16_sign(B(0x4049)), 0);
    fail += checki("exp(0x4049)", (int) bf16_exp(B(0x4049)), 128);
    fail += checki("mant(0x4049)", (int) bf16_mant(B(0x4049)), 73);
    fail += check16("pack(0,128,73)", bf16_pack(0, 128, 73), 0x4049);
    fail += check16("pack(0,127,0)", bf16_pack(0, 127, 0), 0x3F80);

    fail += checki("isinf(+inf)", bf16_isinf(B(0x7F80)), 1);
    fail += checki("isinf(-inf)", bf16_isinf(B(0xFF80)), 1);
    fail += checki("isnan(+inf)", bf16_isnan(B(0x7F80)), 0);
    fail += checki("isnan(qnan)", bf16_isnan(B(0x7FC0)), 1);
    fail += checki("isinf(qnan)", bf16_isinf(B(0x7FC0)), 0);
    fail += checki("iszero(+0)", bf16_iszero(B(0x0000)), 1);
    fail += checki("iszero(-0)", bf16_iszero(B(0x8000)), 1);
    fail += checki("iszero(1.0)", bf16_iszero(B(0x3F80)), 0);

    fail += check16("u32_to_bf16(1.0)", u32_to_bf16(0x3F800000u), 0x3F80);
    fail += check16("u32_to_bf16(2.0)", u32_to_bf16(0x40000000u), 0x4000);
    fail += check16("u32_to_bf16(0.5)", u32_to_bf16(0x3F000000u), 0x3F00);
    fail += check16("u32_to_bf16(-1.0)", u32_to_bf16(0xBF800000u), 0xBF80);
    fail += check16("u32_to_bf16(+inf)", u32_to_bf16(0x7F800000u), 0x7F80);
    fail += check16("u32_to_bf16(qnan)", u32_to_bf16(0x7FC00000u), 0x7FC0);
    /* largest finite f32 rounds up to +inf */
    fail += check16("u32_to_bf16(0x7F7FFFFF)", u32_to_bf16(0x7F7FFFFFu), 0x7F80);
    /* ties to even: discarded field == 0x8000 */
    fail += check16("tie even stay", u32_to_bf16(0x3F808000u), 0x3F80);
    fail += check16("tie even up", u32_to_bf16(0x3F818000u), 0x3F82);

    {
        float one = 1.0f;
        fail += check16("f32_to_bf16(1.0f)", f32_to_bf16(one), 0x3F80);
    }

    {
        uint32_t bit16, low, nfail = 0;

        for (bit16 = 0; bit16 < 2; bit16++) {
            for (low = 0; low < 65536u; low++) {
                uint32_t x = 0x3F800000u | (bit16 << 16) | low;
                uint16_t got = u32_to_bf16(x).bits;
                uint16_t expect = rne_ref(x);

                if (got != expect) {
                    if (nfail < 4)
                        printf("FAIL  rne  x=0x%08X got 0x%04X expect 0x%04X\n",
                               x, got, expect);
                    nfail++;
                }
            }
        }
        fail += checki("rne sweep 131072", (int) nfail, 0);
    }

    fail += check16("mul(1,1)", bf16_mul(B(0x3F80), B(0x3F80)), 0x3F80);
    fail += check16("mul(1,2)", bf16_mul(B(0x3F80), B(0x4000)), 0x4000);
    fail += check16("mul(2,2)", bf16_mul(B(0x4000), B(0x4000)), 0x4080);
    fail += check16("mul(3,4)", bf16_mul(B(0x4040), B(0x4080)), 0x4140);
    fail += check16("mul(0.5,0.5)", bf16_mul(B(0x3F00), B(0x3F00)), 0x3E80);
    fail += check16("mul(0.5,2)", bf16_mul(B(0x3F00), B(0x4000)), 0x3F80);
    fail += check16("mul(-1,2)", bf16_mul(B(0xBF80), B(0x4000)), 0xC000);
    fail += check16("mul(-1,-2)", bf16_mul(B(0xBF80), B(0xC000)), 0x4000);
    fail += check16("mul(0,5)", bf16_mul(B(0x0000), B(0x40A0)), 0x0000);
    fail += check16("mul(-0,1)", bf16_mul(B(0x8000), B(0x3F80)), 0x8000);
    fail += check16("mul(inf,0)", bf16_mul(B(0x7F80), B(0x0000)), 0x7FC0);
    fail += check16("mul(inf,2)", bf16_mul(B(0x7F80), B(0x4000)), 0x7F80);
    fail += check16("mul(-inf,2)", bf16_mul(B(0xFF80), B(0x4000)), 0xFF80);
    fail += check16("mul(nan,1)", bf16_mul(B(0x7FC0), B(0x3F80)), 0x7FC0);
    fail += check16("mul(max,max)", bf16_mul(B(0x7F7F), B(0x7F7F)), 0x7F80);

    fail += check16("sqrt(0)", bf16_sqrt(B(0x0000)), 0x0000);
    fail += check16("sqrt(-0)", bf16_sqrt(B(0x8000)), 0x8000);
    fail += check16("sqrt(0.25)", bf16_sqrt(B(0x3E80)), 0x3F00);
    fail += check16("sqrt(1)", bf16_sqrt(B(0x3F80)), 0x3F80);
    fail += check16("sqrt(4)", bf16_sqrt(B(0x4080)), 0x4000);
    fail += check16("sqrt(9)", bf16_sqrt(B(0x4110)), 0x4040);
    fail += check16("sqrt(16)", bf16_sqrt(B(0x4180)), 0x4080);
    fail += check16("sqrt(+inf)", bf16_sqrt(B(0x7F80)), 0x7F80);
    fail += check16("sqrt(-1)", bf16_sqrt(B(0xBF80)), 0x7FC0);
    fail += check16("sqrt(-inf)", bf16_sqrt(B(0xFF80)), 0x7FC0);
    fail += check16("sqrt(nan)", bf16_sqrt(B(0x7FC0)), 0x7FC0);

    arr[0] = B(0x3F80);
    arr[1] = B(0x4000);
    bf16_negate(arr, 2);
    fail += check16("neg[0]", arr[0], 0xBF80);
    fail += check16("neg[1]", arr[1], 0xC000);
    arr[0] = B(0x0000);
    bf16_negate(arr, 1);
    fail += check16("neg(+0)", arr[0], 0x8000);

    fail += check16("add(1,1)", bf16_add(B(0x3F80), B(0x3F80)), 0x4000);
    fail += check16("add(1,2)", bf16_add(B(0x3F80), B(0x4000)), 0x4040);
    fail += check16("add(0.5,0.5)", bf16_add(B(0x3F00), B(0x3F00)), 0x3F80);
    fail += check16("add(1,0)", bf16_add(B(0x3F80), B(0x0000)), 0x3F80);
    fail += check16("add(1,-0)", bf16_add(B(0x3F80), B(0x8000)), 0x3F80);
    fail += check16("add(+0,+0)", bf16_add(B(0x0000), B(0x0000)), 0x0000);
    fail += check16("add(-0,-0)", bf16_add(B(0x8000), B(0x8000)), 0x8000);
    fail += check16("add(+0,-0)", bf16_add(B(0x0000), B(0x8000)), 0x0000);
    fail += check16("add(2,-1)", bf16_add(B(0x4000), B(0xBF80)), 0x3F80);
    fail += check16("add(1,-1)", bf16_add(B(0x3F80), B(0xBF80)), 0x0000);
    fail += check16("add(-1,-1)", bf16_add(B(0xBF80), B(0xBF80)), 0xC000);
    fail += check16("add(3,-2)", bf16_add(B(0x4040), B(0xC000)), 0x3F80);
    fail += check16("add(1,2^20)", bf16_add(B(0x3F80), B(0x4980)), 0x4980);
    fail += check16("add(inf,1)", bf16_add(B(0x7F80), B(0x3F80)), 0x7F80);
    fail += check16("add(inf,inf)", bf16_add(B(0x7F80), B(0x7F80)), 0x7F80);
    fail += check16("add(-inf,-inf)", bf16_add(B(0xFF80), B(0xFF80)), 0xFF80);
    fail += check16("add(inf,-inf)", bf16_add(B(0x7F80), B(0xFF80)), 0x7FC0);
    fail += check16("add(nan,1)", bf16_add(B(0x7FC0), B(0x3F80)), 0x7FC0);
    fail += check16("add(max,max)", bf16_add(B(0x7F7F), B(0x7F7F)), 0x7F80);
    fail += check16("add(-max,-max)", bf16_add(B(0xFF7F), B(0xFF7F)), 0xFF80);

    fail += check16("sub(2,1)", bf16_sub(B(0x4000), B(0x3F80)), 0x3F80);
    fail += check16("sub(1,1)", bf16_sub(B(0x3F80), B(0x3F80)), 0x0000);
    fail += check16("sub(1,2)", bf16_sub(B(0x3F80), B(0x4000)), 0xBF80);
    fail += check16("sub(0,1)", bf16_sub(B(0x0000), B(0x3F80)), 0xBF80);

    fail += check16("div(1,1)", bf16_div(B(0x3F80), B(0x3F80)), 0x3F80);
    fail += check16("div(1,2)", bf16_div(B(0x3F80), B(0x4000)), 0x3F00);
    fail += check16("div(4,2)", bf16_div(B(0x4080), B(0x4000)), 0x4000);
    fail += check16("div(8,2)", bf16_div(B(0x4100), B(0x4000)), 0x4080);
    fail += check16("div(10,2)", bf16_div(B(0x4120), B(0x4000)), 0x40A0);
    fail += check16("div(3,2)", bf16_div(B(0x4040), B(0x4000)), 0x3FC0);
    fail += check16("div(-10,2)", bf16_div(B(0xC120), B(0x4000)), 0xC0A0);
    fail += check16("div(2,1)", bf16_div(B(0x4000), B(0x3F80)), 0x4000);
    fail += check16("div(0,1)", bf16_div(B(0x0000), B(0x3F80)), 0x0000);
    fail += check16("div(0,-1)", bf16_div(B(0x0000), B(0xBF80)), 0x8000);
    fail += check16("div(1,0)", bf16_div(B(0x3F80), B(0x0000)), 0x7F80);
    fail += check16("div(-1,0)", bf16_div(B(0xBF80), B(0x0000)), 0xFF80);
    fail += check16("div(0,0)", bf16_div(B(0x0000), B(0x0000)), 0x7FC0);
    fail += check16("div(inf,inf)", bf16_div(B(0x7F80), B(0x7F80)), 0x7FC0);
    fail += check16("div(inf,2)", bf16_div(B(0x7F80), B(0x4000)), 0x7F80);
    fail += check16("div(2,inf)", bf16_div(B(0x4000), B(0x7F80)), 0x0000);
    fail += check16("div(0,inf)", bf16_div(B(0x0000), B(0x7F80)), 0x0000);
    fail += check16("div(nan,1)", bf16_div(B(0x7FC0), B(0x3F80)), 0x7FC0);
    /* DAZ: subnormal -> +/-0 before specials */
    fail += check16("div(sub,1)", bf16_div(B(0x0001), B(0x3F80)), 0x0000);
    fail += check16("div(1,sub)", bf16_div(B(0x3F80), B(0x0001)), 0x7F80);
    fail += check16("div(-sub,1)", bf16_div(B(0x8001), B(0x3F80)), 0x8000);

    printf("%d failed\n", fail);
    return fail ? 1 : 0;
}
