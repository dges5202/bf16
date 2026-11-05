/*
 * Measure per-call guest instret (rv32emu: cycle == instret, CPI=1).
 * Truncate path. Skips div / negate (asm unimp). No libgcc div.
 */
#include <stdint.h>

uint64_t get_instret(void);

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

static uint32_t rng = 0xC0FFEEu;
static uint32_t randu(void){ rng = rng * 1664525u + 1013904223u; return rng; }

typedef uint16_t (*binop)(uint16_t, uint16_t);
typedef uint16_t (*unop)(uint16_t);

static void report(const char *name, uint32_t n, uint64_t sum, uint64_t mx, uint32_t sink)
{
    uint32_t avg = (n == 0) ? 0 : (uint32_t)(sum / n); /* n fits; sum/n via compiler — avoid: use shift approx if needed */
    /* soft avg: sum is < n*4096; use restoring for 64/32 */
    {
        uint64_t q = 0, r = 0; int bit;
        for (bit = 63; bit >= 0; bit--) {
            r = (r << 1) | ((sum >> bit) & 1ull);
            if (r >= n) { r -= n; q |= (1ull << bit); }
        }
        avg = (uint32_t)q;
    }
    puts0(name);
    puts0(" n=0x"); put_hex32(n);
    puts0(" avg=0x"); put_hex32(avg);
    puts0(" max=0x"); put_hex32((uint32_t)mx);
    puts0(" sink=0x"); put_hex32(sink);
    puts0("\n");
}

static void measure2(const char *name, binop fn, unsigned samples)
{
    static const uint16_t edge[] = {
        0x0000,0x8000,0x3F80,0x4000,0x3F00,0x4040,0x7F80,0xFF80,0x7FC0,
        0xBF80,0xC000,0x7F7F,0x0001,0x007F,0x0080
    };
    uint64_t sum = 0, mx = 0; uint32_t sink = 0, n = 0, i, j;
    for (i = 0; i < sizeof edge / sizeof edge[0]; i++)
        for (j = 0; j < sizeof edge / sizeof edge[0]; j++) {
            uint64_t t0 = get_instret();
            sink += fn(edge[i], edge[j]);
            {
                uint64_t d = get_instret() - t0;
                sum += d; if (d > mx) mx = d; n++;
            }
        }
    for (i = 0; i < samples; i++) {
        uint16_t a = (uint16_t)randu(), b = (uint16_t)(randu() >> 16);
        uint64_t t0 = get_instret();
        sink += fn(a, b);
        {
            uint64_t d = get_instret() - t0;
            sum += d; if (d > mx) mx = d; n++;
        }
    }
    report(name, n, sum, mx, sink);
}

static void measure1(const char *name, unop fn)
{
    uint64_t sum = 0, mx = 0; uint32_t sink = 0, n = 0, a;
    for (a = 0; a < 0x10000u; a++) {
        uint64_t t0 = get_instret();
        sink += fn((uint16_t)a);
        {
            uint64_t d = get_instret() - t0;
            sum += d; if (d > mx) mx = d; n++;
        }
    }
    report(name, n, sum, mx, sink);
}

static void measure_u32(const char *name)
{
    uint64_t sum = 0, mx = 0; uint32_t sink = 0, n = 0, i;
    static const uint32_t fixed[] = {
        0x3F800000u, 0x40000000u, 0x7F800000u, 0x7FC00000u,
        0x3F808000u, 0x7F7FFFFFu
    };
    for (i = 0; i < sizeof fixed / sizeof fixed[0]; i++) {
        uint64_t t0 = get_instret();
        sink += U32(fixed[i]);
        { uint64_t d = get_instret() - t0; sum += d; if (d > mx) mx = d; n++; }
    }
    for (i = 0; i < 4096u; i++) {
        uint32_t x = randu();
        uint64_t t0 = get_instret();
        sink += U32(x);
        { uint64_t d = get_instret() - t0; sum += d; if (d > mx) mx = d; n++; }
    }
    report(name, n, sum, mx, sink);
}

int main(void)
{
#if defined(BENCH_ASM)
    puts0("bench=ASM truncate (hand bf16.S)\n");
#else
    puts0("bench=C truncate GCC -O2 (bf16_ref soft-mul)\n");
#endif
    puts0("note: rv32emu cycle==instret CPI=1; no LU/flush model\n");
    measure_u32("u32_to_bf16");
    measure2("mul", MUL, 20000);
    measure2("add", ADD, 20000);
    measure2("sub", SUB, 20000);
    measure1("sqrt", SQRT);
    puts0("div: SKIP (unimp)\n");
    puts0("negate: SKIP (unimp)\n");
    puts0("DONE\n");
    return 0;
}
