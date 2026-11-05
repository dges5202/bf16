/*
 * Fast per-opt instret sample (not full-domain).
 *
 * Draws random bf16 pairs until N_HOT hot samples (both finite normal),
 * plus a small mixed stream for all-bucket smoke.  Use between commits;
 * run stats_class_* only at milestones.
 *
 * -DBENCH_ASM: hand bf16.S; else GCC -O2 ref.
 */
#include <stdint.h>

uint64_t get_instret(void);

uint16_t bf16_mul_ref(uint16_t, uint16_t);
uint16_t bf16_add_ref(uint16_t, uint16_t);
uint16_t bf16_sub_ref(uint16_t, uint16_t);
uint16_t bf16_div_ref(uint16_t, uint16_t);

#if defined(BENCH_ASM)
uint16_t bf16_mul(uint16_t, uint16_t);
uint16_t bf16_add(uint16_t, uint16_t);
uint16_t bf16_sub(uint16_t, uint16_t);
uint16_t bf16_div(uint16_t, uint16_t);
#define MUL bf16_mul
#define ADD bf16_add
#define SUB bf16_sub
#define DIV bf16_div
#else
#define MUL bf16_mul_ref
#define ADD bf16_add_ref
#define SUB bf16_sub_ref
#define DIV bf16_div_ref
#endif

#ifndef N_HOT
#define N_HOT 65536u
#endif

/* OPS= filter: default all on. With -DOPS_FILTER, only -DOP_*=1 run. */
#if !defined(OPS_FILTER)
#define OP_MUL 1
#define OP_ADD 1
#define OP_SUB 1
#define OP_DIV 1
#endif

typedef uint16_t (*binop)(uint16_t, uint16_t);

static void write_n(const char *s, unsigned n)
{
    register unsigned a0 asm("a0") = 1;
    register const char *a1 asm("a1") = s;
    register unsigned a2 asm("a2") = n;
    register unsigned a7 asm("a7") = 64;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}
static void puts0(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    write_n(s, n);
}
static void put_hex32(uint32_t x)
{
    char b[8];
    int i;
    for (i = 7; i >= 0; i--) {
        unsigned d = (x >> (i * 4)) & 0xF;
        b[7 - i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    write_n(b, 8);
}
static uint32_t soft_avg(uint64_t sum, uint64_t n)
{
    uint64_t q = 0, r = 0;
    int bit;
    if (n == 0)
        return 0;
    for (bit = 63; bit >= 0; bit--) {
        r = (r << 1) | ((sum >> bit) & 1ull);
        if (r >= n) {
            r -= n;
            q |= 1ull << bit;
        }
    }
    return (uint32_t)q;
}

static uint32_t rng;

static uint32_t randu(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

/* finite normal: exp in 1..254 */
static int is_hot(uint16_t x)
{
    uint16_t e = (x >> 7) & 0xFF;
    return e != 0 && e != 0xFF;
}

/* Advance LCG past N_HOT hot pairs (no leaf / instret) so OPS= filters
 * keep later ops on the same sample stream as OPS=all. */
static void burn_hot(void)
{
    uint32_t n = 0;
    while (n < N_HOT) {
        uint16_t a = (uint16_t)randu();
        uint16_t b = (uint16_t)(randu() >> 16);
        if (!is_hot(a) || !is_hot(b))
            continue;
        n++;
    }
}

static void measure_hot(const char *name, binop fn)
{
    uint64_t sum = 0, mx = 0;
    uint32_t sink = 0, n = 0, draws = 0;
    uint32_t wa = 0, wb = 0;

    while (n < N_HOT) {
        uint16_t a = (uint16_t)randu();
        uint16_t b = (uint16_t)(randu() >> 16);
        uint64_t t0, d;
        draws++;
        if (!is_hot(a) || !is_hot(b))
            continue;
        t0 = get_instret();
        sink += fn(a, b);
        d = get_instret() - t0;
        sum += d;
        if (d > mx) {
            mx = d;
            wa = a;
            wb = b;
        }
        n++;
    }

    puts0(name);
    puts0(".hot n=");
    put_hex32(n);
    puts0(" draws=");
    put_hex32(draws);
    puts0(" avg=");
    put_hex32(soft_avg(sum, n));
    puts0(" sum=");
    put_hex32((uint32_t)sum); /* low 32; N_HOT*~200 fits */
    puts0(" max=");
    put_hex32((uint32_t)mx);
    puts0(" wa=");
    put_hex32(wa);
    puts0(" wb=");
    put_hex32(wb);
    puts0(" sink=");
    put_hex32(sink);
    puts0("\n");
}

int main(void)
{
    rng = 0xC0FFEEu;

#if defined(BENCH_ASM)
    puts0("stats_samp=ASM hand bf16.S\n");
#else
    puts0("stats_samp=C GCC -O2 bf16_ref\n");
#endif
    puts0("rv32emu instret (CPI=1); hot-only sample; hex avgs; N_HOT=");
    put_hex32(N_HOT);
    puts0("\n");
#if defined(OPS_FILTER)
    puts0("OPS=filtered\n");
#endif

#if OP_MUL
    measure_hot("mul", MUL);
#else
    burn_hot();
#endif
#if OP_ADD
    measure_hot("add", ADD);
#else
    burn_hot();
#endif
#if OP_SUB
    measure_hot("sub", SUB);
#else
    burn_hot();
#endif
#if OP_DIV
    measure_hot("div", DIV);
#else
    burn_hot();
#endif

    puts0("DONE\n");
    return 0;
}
