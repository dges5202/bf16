/*
 * Ripes workload: add or sub, hand ASM vs GCC -O2.
 * Same shape as mul_loop_ripes: 12×12 edges + 8192 normal×normal.
 * Built four ways: BENCH_ASM × OP_SUB.
 */
#include <stdint.h>

#if defined(BENCH_ASM)
uint16_t bf16_add(uint16_t, uint16_t);
uint16_t bf16_sub(uint16_t, uint16_t);
#if defined(OP_SUB)
#define OP bf16_sub
#else
#define OP bf16_add
#endif
#else
uint16_t bf16_add_ref(uint16_t, uint16_t);
uint16_t bf16_sub_ref(uint16_t, uint16_t);
#if defined(OP_SUB)
#define OP bf16_sub_ref
#else
#define OP bf16_add_ref
#endif
#endif

static uint32_t rng = 0xC0FFEE01u;
static uint32_t randu(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

static uint16_t rand_norm(void)
{
    uint32_t r = randu();
    uint16_t e = (uint16_t)(1 + (r & 0xFD)); /* 1..254 */
    uint16_t m = (uint16_t)((r >> 8) & 0x7F);
    uint16_t s = (uint16_t)((r >> 16) & 1);
    return (uint16_t)((s << 15) | (e << 7) | m);
}

int main(void)
{
    static const uint16_t E[] = {
        0x3F80, 0x4000, 0x3F00, 0x4040, 0xBF80, 0xC000,
        0x0001, 0x7F7F, 0x3C00, 0x4200, 0x3880, 0x3F80
    };
    uint32_t sink = 0;
    unsigned i, j;

    for (i = 0; i < 12; i++)
        for (j = 0; j < 12; j++)
            sink += OP(E[i], E[j]);

    for (i = 0; i < 8192; i++)
        sink += OP(rand_norm(), rand_norm());

    return (int)(sink & 0xff);
}
