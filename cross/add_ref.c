/* Freestanding C reference for bf16_add — mirrors ../bf16.c. */
#include <stdint.h>

#define BF16_SIGN_MASK 0x8000u
#define BF16_EXP_MASK 0x7F80u
#define BF16_MANT_MASK 0x007Fu
#define BF16_EXP_BIAS 127

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
