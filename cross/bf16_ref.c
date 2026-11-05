/*
 * Freestanding C reference — mirrors ../bf16.c bit-exact.
 * No * or / : rv32i guest has no M extension and we avoid libgcc.
 */
#include <stdint.h>

#define BF16_SIGN_MASK 0x8000u
#define BF16_EXP_MASK 0x7F80u
#define BF16_MANT_MASK 0x007Fu
#define BF16_EXP_BIAS 127

static uint32_t umul(uint32_t a, uint32_t b)
{
    uint32_t r = 0;
    while (b) {
        if (b & 1u)
            r += a;
        a <<= 1;
        b >>= 1;
    }
    return r;
}

/*
 * 8-iter aligned soft-div (matches ../bf16.c / bf16.S). BRANCHED compare/sub.
 * rem=mant_a<<7, d=mant_b<<7; enough for normal×normal after DAZ.
 */
static uint32_t udiv8_aligned(uint32_t mant_a, uint32_t mant_b)
{
    uint32_t rem = mant_a << 7;
    uint32_t d = mant_b << 7;
    uint32_t q = 0;
    int i;

    for (i = 0; i < 8; i++) {
        q <<= 1;
        if (rem >= d) {
            rem -= d;
            q |= 1u;
        }
        d >>= 1;
    }
    return q;
}

static uint16_t bf16_sign(uint16_t a)
{
    return (uint16_t) ((a & BF16_SIGN_MASK) >> 15);
}

static uint16_t bf16_exp(uint16_t a)
{
    return (uint16_t) ((a & BF16_EXP_MASK) >> 7);
}

static uint16_t bf16_mant(uint16_t a)
{
    return (uint16_t) (a & BF16_MANT_MASK);
}

static uint16_t bf16_pack(uint16_t sign, uint16_t exp, uint16_t mant)
{
    return (uint16_t) ((sign << 15) | (exp << 7) | mant);
}

static int bf16_isnan(uint16_t a)
{
    return bf16_exp(a) == 0xFF && bf16_mant(a) != 0;
}

static int bf16_isinf(uint16_t a)
{
    return bf16_exp(a) == 0xFF && bf16_mant(a) == 0;
}

static int bf16_iszero(uint16_t a)
{
    return bf16_exp(a) == 0 && bf16_mant(a) == 0;
}

/* ---- exported refs (same names as asm, _ref suffix) ---- */

uint16_t bf16_sign_ref(uint16_t a)
{
    return bf16_sign(a);
}

uint16_t bf16_exp_ref(uint16_t a)
{
    return bf16_exp(a);
}

uint16_t bf16_mant_ref(uint16_t a)
{
    return bf16_mant(a);
}

uint16_t bf16_pack_ref(uint16_t sign, uint16_t exp, uint16_t mant)
{
    return bf16_pack(sign, exp, mant);
}

int bf16_isnan_ref(uint16_t a)
{
    return bf16_isnan(a);
}

int bf16_isinf_ref(uint16_t a)
{
    return bf16_isinf(a);
}

int bf16_iszero_ref(uint16_t a)
{
    return bf16_iszero(a);
}

uint16_t u32_to_bf16_ref(uint32_t f32bits)
{
    if ((f32bits & 0x7F800000u) == 0x7F800000u)
        return (uint16_t) (f32bits >> 16);
    f32bits += ((f32bits >> 16) & 1u) + 0x7FFFu;
    return (uint16_t) (f32bits >> 16);
}

uint16_t bf16_mul_ref(uint16_t a, uint16_t b)
{
    uint16_t sign = (uint16_t) (bf16_sign(a) ^ bf16_sign(b));

    if (bf16_isnan(a) || bf16_isnan(b))
        return 0x7FC0;
    if ((bf16_iszero(a) && bf16_isinf(b)) || (bf16_isinf(a) && bf16_iszero(b)))
        return 0x7FC0;
    if (bf16_isinf(a) || bf16_isinf(b))
        return bf16_pack(sign, 0xFF, 0);
    if (bf16_iszero(a) || bf16_iszero(b))
        return bf16_pack(sign, 0, 0);

    {
        int32_t exp = (int32_t) bf16_exp(a) + (int32_t) bf16_exp(b) - BF16_EXP_BIAS;
        uint32_t mant_a = bf16_mant(a);
        uint32_t mant_b = bf16_mant(b);
        uint32_t mant;

        if (bf16_exp(a) != 0)
            mant_a |= 0x80u;
        if (bf16_exp(b) != 0)
            mant_b |= 0x80u;
        mant = umul(mant_a, mant_b);

        if ((mant & 0x8000u) != 0) {
            mant = (mant >> 8) & 0x7Fu;
            exp++;
        } else {
            mant = (mant >> 7) & 0x7Fu;
        }

        if (exp >= 255)
            return bf16_pack(sign, 0xFF, 0);
        if (exp < 0)
            return bf16_pack(sign, 0, 0);
        return bf16_pack(sign, (uint16_t) exp, (uint16_t) mant);
    }
}

uint16_t bf16_sqrt_ref(uint16_t a)
{
    uint16_t sign = bf16_sign(a);

    if ((bf16_isinf(a) && sign == 0) || bf16_iszero(a) || bf16_isnan(a))
        return a;
    if (sign == 1)
        return 0x7FC0;

    {
        int32_t exp = (int32_t) bf16_exp(a) - BF16_EXP_BIAS;
        int32_t mant = (int32_t) bf16_mant(a);
        int32_t new_exp;
        int32_t low = 90;
        int32_t high = 256;
        int32_t mid;

        mant |= 0x80;
        if (exp & 1) {
            mant <<= 1;
            new_exp = ((exp - 1) >> 1) + BF16_EXP_BIAS;
        } else {
            new_exp = (exp >> 1) + BF16_EXP_BIAS;
        }

        while (low <= high) {
            int32_t sq;
            mid = (low + high) >> 1;
            /* (mid*mid)/128 — mid in [90,256], product fits uint32 */
            sq = (int32_t) (umul((uint32_t) mid, (uint32_t) mid) >> 7);
            if (sq <= mant)
                low = mid + 1;
            else
                high = mid - 1;
        }
        if (new_exp >= 255)
            return bf16_pack(sign, 0xFF, 0);
        return bf16_pack(sign, (uint16_t) new_exp, (uint16_t) (high & 0x7F));
    }
}

uint16_t bf16_add_ref(uint16_t a, uint16_t b)
{
    uint16_t sign_a = bf16_sign(a);
    uint16_t sign_b = bf16_sign(b);
    uint16_t sign;

    if (bf16_isnan(a) || bf16_isnan(b))
        return 0x7FC0;
    if (sign_a != sign_b && bf16_isinf(a) && bf16_isinf(b))
        return 0x7FC0;
    if (sign_a == sign_b && bf16_isinf(a) && bf16_isinf(b))
        return a;
    if (bf16_isinf(a) && !bf16_isinf(b))
        return a;
    if (bf16_isinf(b) && !bf16_isinf(a))
        return b;

    if (sign_a != sign_b && bf16_iszero(a) && bf16_iszero(b))
        return 0;
    if (sign_a == sign_b && bf16_iszero(a) && bf16_iszero(b))
        return a;
    if (bf16_iszero(a) && !bf16_iszero(b))
        return b;
    if (bf16_iszero(b) && !bf16_iszero(a))
        return a;

    {
        int32_t exp_a = (int32_t) bf16_exp(a);
        int32_t exp_b = (int32_t) bf16_exp(b);
        int32_t exp;
        int32_t mant_a = (int32_t) (bf16_mant(a) | 0x80);
        int32_t mant_b = (int32_t) (bf16_mant(b) | 0x80);
        int32_t mant;

        if (exp_a >= exp_b) {
            exp = exp_a;
            if (exp_a - exp_b >= 8)
                mant_b = 0;
            else
                mant_b >>= (exp_a - exp_b);
        } else {
            exp = exp_b;
            if (exp_b - exp_a >= 8)
                mant_a = 0;
            else
                mant_a >>= (exp_b - exp_a);
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
            return 0;
        }

        if (mant >= 0x100) {
            mant >>= 1;
            exp++;
        } else if (mant == 0) {
            return 0;
        }
        while (mant < 0x80) {
            mant <<= 1;
            exp--;
        }

        if (exp >= 255)
            return bf16_pack(sign, 0xFF, 0);
        if (exp < 0)
            return 0;
        return bf16_pack(sign, (uint16_t) exp, (uint16_t) (mant & 0x7F));
    }
}

uint16_t bf16_sub_ref(uint16_t a, uint16_t b)
{
    return bf16_add_ref(a, (uint16_t) (b ^ BF16_SIGN_MASK));
}

uint16_t bf16_div_ref(uint16_t a, uint16_t b)
{
    /* DAZ: subnormal -> +/-0 (matches ../bf16.c) */
    if (bf16_exp(a) == 0 && bf16_mant(a) != 0)
        a = bf16_pack(bf16_sign(a), 0, 0);
    if (bf16_exp(b) == 0 && bf16_mant(b) != 0)
        b = bf16_pack(bf16_sign(b), 0, 0);

    {
        uint16_t sign = (uint16_t) (bf16_sign(a) ^ bf16_sign(b));

        if (bf16_isnan(a) || bf16_isnan(b))
            return 0x7FC0;
        if ((bf16_iszero(a) && bf16_iszero(b)) || (bf16_isinf(a) && bf16_isinf(b)))
            return 0x7FC0;
        if (!bf16_iszero(a) && bf16_iszero(b))
            return bf16_pack(sign, 0xFF, 0);
        if (bf16_isinf(a) && !bf16_isinf(b))
            return bf16_pack(sign, 0xFF, 0);
        if (bf16_isinf(b) && !bf16_isinf(a))
            return bf16_pack(sign, 0, 0);
        if (bf16_iszero(a) && !bf16_iszero(b))
            return bf16_pack(sign, 0, 0);

        {
            int32_t exp_a = (int32_t) bf16_exp(a);
            int32_t exp_b = (int32_t) bf16_exp(b);
            uint32_t mant_a = (uint32_t) bf16_mant(a) | 0x80u;
            uint32_t mant_b = (uint32_t) bf16_mant(b) | 0x80u;
            int32_t exp = exp_a - exp_b + BF16_EXP_BIAS;
            uint32_t quot = udiv8_aligned(mant_a, mant_b);

            while (quot < 0x80u && quot != 0u) {
                quot <<= 1;
                exp--;
            }

            if (exp < 0)
                return bf16_pack(sign, 0, 0);
            if (exp >= 255)
                return bf16_pack(sign, 0xFF, 0);
            return bf16_pack(sign, (uint16_t) exp, (uint16_t) (quot & 0x7Fu));
        }
    }
}

void bf16_negate_ref(uint16_t *v, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        v[i] ^= BF16_SIGN_MASK;
}
