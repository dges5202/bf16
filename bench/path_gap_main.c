/*
 * One-shot: same samp LCG, classify hot bf16_add paths, measure ASM vs C-O2
 * instret per bucket (wrap included). Find which mix still loses to C.
 */
#include <stdint.h>

uint64_t get_instret(void);
uint16_t bf16_add(uint16_t, uint16_t);
uint16_t bf16_add_ref(uint16_t, uint16_t);

#ifndef N_HOT
#define N_HOT 65536u
#endif

enum {
    B_SS_EASY = 0,
    B_SS_BUMP,
    B_SS_SWAP,
    B_SS_WIDE,
    B_ON_GE_MA, /* opp norm ea>=eb ma>mb (fallthrough sub) */
    B_ON_GE_MB, /* opp norm ea>=eb mb>ma (mv sign + j) */
    B_ON_LT_MA, /* opp norm ea<eb ma>mb */
    B_ON_LT_MB, /* opp norm ea<eb mb>ma */
    B_OPP_LEFT,
    B_OPP_ZERO,
    B_N
};

static const char *names[B_N] = {
    "ss_easy", "ss_bump", "ss_swap", "ss_wide",
    "on_ge_ma", "on_ge_mb", "on_lt_ma", "on_lt_mb",
    "opp_left", "opp_zero",
};

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
static int is_hot(uint16_t x)
{
    uint16_t e = (x >> 7) & 0xFF;
    return e != 0 && e != 0xFF;
}

static int classify(uint16_t a, uint16_t b)
{
    unsigned sa = a >> 15, sb = b >> 15;
    int ea = (a >> 7) & 0xFF, eb = (b >> 7) & 0xFF;
    unsigned ma = (a & 0x7F) | 0x80, mb = (b & 0x7F) | 0x80;
    int dexp;
    unsigned mant;

    if (ea >= eb) {
        dexp = ea - eb;
        if (dexp >= 8)
            mb = 0;
        else
            mb >>= dexp;
    } else {
        dexp = eb - ea;
        if (dexp >= 8)
            ma = 0;
        else
            ma >>= dexp;
    }

    if (sa == sb) {
        mant = ma + mb;
        if (dexp >= 8)
            return B_SS_WIDE;
        if (ea < eb)
            return (mant >= 0x100) ? B_SS_BUMP : B_SS_SWAP;
        return (mant >= 0x100) ? B_SS_BUMP : B_SS_EASY;
    }
    if (ma == mb)
        return B_OPP_ZERO;
    mant = (ma > mb) ? (ma - mb) : (mb - ma);
    if (mant < 0x80)
        return B_OPP_LEFT;
    if (ea >= eb)
        return (ma > mb) ? B_ON_GE_MA : B_ON_GE_MB;
    return (ma > mb) ? B_ON_LT_MA : B_ON_LT_MB;
}

int main(void)
{
    uint64_t n[B_N], sum_a[B_N], sum_c[B_N];
    uint32_t sink = 0, got = 0;
    int zi;

    for (zi = 0; zi < B_N; zi++) {
        n[zi] = 0;
        sum_a[zi] = 0;
        sum_c[zi] = 0;
    }

    rng = 0xC0FFEEu;
    puts0("path_gap add ASM vs C-O2; N_HOT=");
    put_hex32(N_HOT);
    puts0("\n");

    while (got < N_HOT) {
        uint32_t r = randu();
        uint16_t a = (uint16_t)r, b = (uint16_t)(r >> 16);
        int buck;
        uint64_t t0, d;
        if (!is_hot(a) || !is_hot(b))
            continue;
        buck = classify(a, b);
        n[buck]++;

        t0 = get_instret();
        sink += bf16_add(a, b);
        d = get_instret() - t0;
        sum_a[buck] += d;

        t0 = get_instret();
        sink += bf16_add_ref(a, b);
        d = get_instret() - t0;
        sum_c[buck] += d;

        got++;
    }

    {
        int i;
        uint64_t ta = 0, tc = 0;
        for (i = 0; i < B_N; i++) {
            int32_t gap = (int32_t)(sum_a[i] - sum_c[i]);
            puts0(names[i]);
            puts0(" n=");
            put_hex32((uint32_t)n[i]);
            puts0(" asm_avg=");
            put_hex32(soft_avg(sum_a[i], n[i]));
            puts0(" c_avg=");
            put_hex32(soft_avg(sum_c[i], n[i]));
            puts0(" sum_gap=");
            if (gap < 0) {
                puts0("-");
                put_hex32((uint32_t)(-gap));
            } else
                put_hex32((uint32_t)gap);
            puts0("\n");
            ta += sum_a[i];
            tc += sum_c[i];
        }
        puts0("TOTAL sum_gap=");
        {
            int32_t g = (int32_t)(ta - tc);
            if (g < 0) {
                puts0("-");
                put_hex32((uint32_t)(-g));
            } else
                put_hex32((uint32_t)g);
        }
        puts0(" asm_avg=");
        put_hex32(soft_avg(ta, N_HOT));
        puts0(" c_avg=");
        put_hex32(soft_avg(tc, N_HOT));
        puts0(" sink=");
        put_hex32(sink);
        puts0("\nDONE\n");
    }
    return 0;
}
