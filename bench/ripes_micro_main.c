/*
 * Fixed truncate workload for Ripes CLI 5-stage cycle/CPI telemetry.
 * No get_instret (Ripes reports --cycles/--iret). Small corpus only.
 */
#include <stdint.h>

uint16_t bf16_mul_ref(uint16_t, uint16_t);
uint16_t bf16_add_ref(uint16_t, uint16_t);
uint16_t bf16_sub_ref(uint16_t, uint16_t);
uint16_t bf16_sqrt_ref(uint16_t);
uint16_t u32_to_bf16_ref(uint32_t);

#if defined(BENCH_ASM)
uint16_t bf16_mul(uint16_t, uint16_t);
uint16_t bf16_add(uint16_t, uint16_t);
uint16_t bf16_sub(uint16_t, uint16_t);
uint16_t bf16_sqrt(uint16_t);
uint16_t u32_to_bf16(uint32_t);
#define MUL bf16_mul
#define ADD bf16_add
#define SUB bf16_sub
#define SQRT bf16_sqrt
#define U32 u32_to_bf16
#else
#define MUL bf16_mul_ref
#define ADD bf16_add_ref
#define SUB bf16_sub_ref
#define SQRT bf16_sqrt_ref
#define U32 u32_to_bf16_ref
#endif

static void write_n(const char *s, unsigned n)
{
    register unsigned a0 asm("a0") = 1;
    register const char *a1 asm("a1") = s;
    register unsigned a2 asm("a2") = n;
    register unsigned a7 asm("a7") = 64;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}
static void puts0(const char *s){ unsigned n=0; while(s[n]) n++; write_n(s,n); }
static void put_hex32(uint32_t x)
{
    char buf[8]; int i;
    for (i = 7; i >= 0; i--) {
        unsigned d = (x >> (i * 4)) & 0xF;
        buf[7 - i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    write_n(buf, 8);
}

/* Fixed edges — keep tiny so RV32_5S finishes under CLI timeout. */
static const uint16_t edge[] = {
    0x0000, 0x3F80, 0x4000, 0x3F00, 0x4040, 0xBF80, 0x7F80, 0x7FC0
};

int main(void)
{
    uint32_t sink = 0;
    unsigned i, j;

#if defined(BENCH_ASM)
    puts0("ripes-micro ASM truncate\n");
#else
    puts0("ripes-micro C-O2 truncate\n");
#endif

    for (i = 0; i < sizeof edge / sizeof edge[0]; i++) {
        sink += U32((uint32_t)edge[i] << 16);
        sink += SQRT(edge[i]);
        for (j = 0; j < sizeof edge / sizeof edge[0]; j++) {
            sink += MUL(edge[i], edge[j]);
            sink += ADD(edge[i], edge[j]);
            sink += SUB(edge[i], edge[j]);
            /* div skipped: asm unimp */
        }
    }

    puts0("sink=0x"); put_hex32(sink); puts0("\nDONE\n");
    return (int)(sink & 0xFF);
}
