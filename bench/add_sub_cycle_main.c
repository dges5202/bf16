/*
 * Same corpus as add_sub_ripes_main.c.
 * rv32emu instret per call: hand ASM vs GCC -O2, add and sub.
 */
#include <stdint.h>

uint64_t get_instret(void);
uint16_t bf16_add(uint16_t, uint16_t);
uint16_t bf16_sub(uint16_t, uint16_t);
uint16_t bf16_add_ref(uint16_t, uint16_t);
uint16_t bf16_sub_ref(uint16_t, uint16_t);

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

static uint32_t rng;
static uint32_t randu(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng;
}
static uint16_t rand_norm(void)
{
    uint32_t r = randu();
    uint16_t e = (uint16_t)(1 + (r & 0xFD));
    uint16_t m = (uint16_t)((r >> 8) & 0x7F);
    uint16_t s = (uint16_t)((r >> 16) & 1);
    return (uint16_t)((s << 15) | (e << 7) | m);
}

static int is_nan(uint16_t x)
{
    return ((x & 0x7F80) == 0x7F80) && (x & 0x7F);
}
static int same(uint16_t a, uint16_t b)
{
    if (a == b)
        return 1;
    return is_nan(a) && is_nan(b);
}

typedef uint16_t (*opfn)(uint16_t, uint16_t);

static uint64_t udiv64(uint64_t n, uint64_t d)
{
    uint64_t q = 0, r = 0;
    int bit;
    for (bit = 63; bit >= 0; bit--) {
        r = (r << 1) | ((n >> bit) & 1ull);
        if (r >= d) {
            r -= d;
            q |= 1ull << bit;
        }
    }
    return q;
}

static void fill(uint16_t *A, uint16_t *B, unsigned *n_out)
{
    static const uint16_t E[] = {
        0x3F80, 0x4000, 0x3F00, 0x4040, 0xBF80, 0xC000,
        0x0001, 0x7F7F, 0x3C00, 0x4200, 0x3880, 0x3F80
    };
    unsigned i, j, n = 0;
    rng = 0xC0FFEE01u;
    for (i = 0; i < 12; i++)
        for (j = 0; j < 12; j++) {
            A[n] = E[i];
            B[n] = E[j];
            n++;
        }
    for (i = 0; i < 8192; i++) {
        A[n] = rand_norm();
        B[n] = rand_norm();
        n++;
    }
    *n_out = n;
}

static void measure(const char *name, opfn fn, opfn ref, const uint16_t *A,
                    const uint16_t *B, unsigned n)
{
    uint64_t sum = 0, mx = 0;
    uint32_t i, fail = 0, sink = 0;
    for (i = 0; i < n; i++) {
        uint16_t got, want;
        uint64_t t0, d;
        t0 = get_instret();
        got = fn(A[i], B[i]);
        d = get_instret() - t0;
        sum += d;
        if (d > mx)
            mx = d;
        want = ref(A[i], B[i]);
        if (!same(got, want))
            fail++;
        sink += got;
    }
    puts0(name);
    puts0(" n=");
    put_hex32(n);
    puts0(" avg=");
    put_hex32((uint32_t)udiv64(sum, n));
    puts0(" max=");
    put_hex32((uint32_t)mx);
    puts0(" fail=");
    put_hex32(fail);
    puts0(" sink=");
    put_hex32(sink);
    puts0("\n");
}

int main(void)
{
    static uint16_t A[144 + 8192], B[144 + 8192];
    unsigned n;
    puts0("add/sub instret: same corpus as Ripes (144 edges + 8192 hot)\n");
    fill(A, B, &n);
    measure("add asm", bf16_add, bf16_add_ref, A, B, n);
    measure("add c  ", bf16_add_ref, bf16_add_ref, A, B, n);
    measure("sub asm", bf16_sub, bf16_sub_ref, A, B, n);
    measure("sub c  ", bf16_sub_ref, bf16_sub_ref, A, B, n);
    puts0("DONE\n");
    return 0;
}
