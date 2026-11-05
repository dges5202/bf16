/*
 * Compare branched vs branchless soft-div inner step (same gate/specials/renorm).
 * Corpus: edges + 40000 random. Correctness vs bf16_div_ref; instret via CSR.
 * Also report hot-only subset (finite nonzero both sides) so loop cost is visible.
 */
#include <stdint.h>

uint64_t get_instret(void);
uint16_t bf16_div_ref(uint16_t, uint16_t);
uint16_t bf16_div_br(uint16_t, uint16_t);
uint16_t bf16_div_nbr(uint16_t, uint16_t);

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
    char b[8]; int i;
    for (i = 7; i >= 0; i--) {
        unsigned d = (x >> (i * 4)) & 0xF;
        b[7 - i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    write_n(b, 8);
}

static int is_nan(uint16_t x){ return ((x & 0x7F80) == 0x7F80) && (x & 0x7F); }
static int same(uint16_t a, uint16_t b)
{
    if (a == b) return 1;
    if (is_nan(a) && is_nan(b)) return 1;
    return 0;
}

/* finite nonzero mag → soft-div hot path (gate passes) */
static int is_hot(uint16_t x)
{
    uint16_t e = (x >> 7) & 0xFF;
    uint16_t m = x & 0x7F;
    if (e == 0xFF) return 0;
    if (e == 0 && m == 0) return 0;
    return 1;
}

static uint32_t rng = 0xA5A5F00Du;
static uint32_t randu(void){ rng = rng * 1664525u + 1013904223u; return rng; }

enum { NEDGE = 12, NRAND = 40000, NTOT = NEDGE * NEDGE + NRAND };

typedef uint16_t (*divfn)(uint16_t, uint16_t);

static void check(const char *name, divfn fn, const uint16_t *A, const uint16_t *B, unsigned n)
{
    unsigned i, fail = 0;
    for (i = 0; i < n; i++) {
        uint16_t want = bf16_div_ref(A[i], B[i]);
        uint16_t got = fn(A[i], B[i]);
        if (!same(got, want)) {
            if (fail < 6) {
                puts0("FAIL "); puts0(name);
                puts0(" a="); put_hex32(A[i]);
                puts0(" b="); put_hex32(B[i]);
                puts0(" got="); put_hex32(got);
                puts0(" want="); put_hex32(want); puts0("\n");
            }
            fail++;
        }
    }
    puts0(name); puts0(" check n="); put_hex32(n);
    puts0(" fail="); put_hex32(fail); puts0("\n");
}

static void measure(const char *name, divfn fn, const uint16_t *A, const uint16_t *B,
                    unsigned n, int hot_only)
{
    uint64_t sum = 0, mx = 0; uint32_t sink = 0, i, cnt = 0, wa = 0, wb = 0;
    for (i = 0; i < n; i++) {
        if (hot_only && !(is_hot(A[i]) && is_hot(B[i])))
            continue;
        {
            uint64_t t0 = get_instret();
            sink += fn(A[i], B[i]);
            {
                uint64_t d = get_instret() - t0;
                sum += d;
                if (d > mx) { mx = d; wa = A[i]; wb = B[i]; }
                cnt++;
            }
        }
    }
    {
        uint64_t q = 0, r = 0; int bit;
        if (cnt == 0) cnt = 1;
        for (bit = 63; bit >= 0; bit--) {
            r = (r << 1) | ((sum >> bit) & 1ull);
            if (r >= cnt) { r -= cnt; q |= 1ull << bit; }
        }
        puts0(name); puts0(" n="); put_hex32(cnt);
        puts0(" avg="); put_hex32((uint32_t)q);
        puts0(" max="); put_hex32((uint32_t)mx);
        puts0(" wa="); put_hex32(wa);
        puts0(" wb="); put_hex32(wb);
        puts0(" sink="); put_hex32(sink); puts0("\n");
    }
}

int main(void)
{
    static uint16_t A[NTOT], B[NTOT];
    static const uint16_t edge[] = {
        0x0000, 0x8000, 0x3F80, 0x4000, 0x3F00, 0x4040,
        0x7F80, 0xFF80, 0x7FC0, 0xBF80, 0x0001, 0x7F7F
    };
    unsigned i, j, n = 0;

    puts0("div loop: branched vs branchless\n");
    for (i = 0; i < NEDGE; i++)
        for (j = 0; j < NEDGE; j++) {
            A[n] = edge[i]; B[n] = edge[j]; n++;
        }
    for (i = 0; i < NRAND; i++) {
        A[n] = (uint16_t)randu();
        B[n] = (uint16_t)(randu() >> 16);
        n++;
    }

    check("br ", bf16_div_br, A, B, n);
    check("nbr", bf16_div_nbr, A, B, n);

    puts0("--- instret all ---\n");
    measure("br ", bf16_div_br, A, B, n, 0);
    measure("nbr", bf16_div_nbr, A, B, n, 0);

    puts0("--- instret hot-only ---\n");
    measure("br ", bf16_div_br, A, B, n, 1);
    measure("nbr", bf16_div_nbr, A, B, n, 1);

    puts0("DONE\n");
    return 0;
}
