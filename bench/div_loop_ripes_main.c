/*
 * Ripes workload: hot-path heavy (soft-div). Built twice with USE_NBR.
 * ~8192 normal/normal pairs + small edge set.
 */
#include <stdint.h>

uint16_t bf16_div_br(uint16_t, uint16_t);
uint16_t bf16_div_nbr(uint16_t, uint16_t);

#if defined(USE_NBR)
#define DIV bf16_div_nbr
#else
#define DIV bf16_div_br
#endif

static uint32_t rng = 0xA5A5F00Du;
static uint32_t randu(void){ rng = rng * 1664525u + 1013904223u; return rng; }

static uint16_t rand_norm(void)
{
    /* exp 1..254, mant 0..127 */
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
            sink += DIV(E[i], E[j]);

    for (i = 0; i < 8192; i++)
        sink += DIV(rand_norm(), rand_norm());

    return (int)(sink & 0xff);
}
