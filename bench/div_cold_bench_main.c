/*
 * Compare bf16_div_class vs bf16_div_tree (same hot, different specials).
 * 1) Correctness vs bf16_div_ref on specials + mixed samples
 * 2) rv32emu instret avg/max (get_instret)
 * Ripes 5ST measured separately via make targets on samp ELFs.
 */
#include <stdint.h>

uint64_t get_instret(void);
uint16_t bf16_div_ref(uint16_t, uint16_t);
uint16_t bf16_div_class(uint16_t, uint16_t);
uint16_t bf16_div_tree(uint16_t, uint16_t);

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

static uint32_t rng = 0xC0FFEEu;
static uint32_t randu(void){ rng = rng * 1664525u + 1013904223u; return rng; }

typedef uint16_t (*divfn)(uint16_t, uint16_t);

static void check_vs_ref(const char *name, divfn fn, const uint16_t *A, const uint16_t *B, unsigned n)
{
    unsigned i, fail = 0;
    for (i = 0; i < n; i++) {
        uint16_t want = bf16_div_ref(A[i], B[i]);
        uint16_t got = fn(A[i], B[i]);
        if (!same(got, want)) {
            if (fail < 8) {
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

static void measure(const char *name, divfn fn, const uint16_t *A, const uint16_t *B, unsigned n)
{
    uint64_t sum = 0, mx = 0; uint32_t sink = 0, i, wa = 0, wb = 0;
    for (i = 0; i < n; i++) {
        uint64_t t0 = get_instret();
        sink += fn(A[i], B[i]);
        {
            uint64_t d = get_instret() - t0;
            sum += d;
            if (d > mx) { mx = d; wa = A[i]; wb = B[i]; }
        }
    }
    {
        uint64_t q = 0, r = 0; int bit;
        for (bit = 63; bit >= 0; bit--) {
            r = (r << 1) | ((sum >> bit) & 1ull);
            if (r >= n) { r -= n; q |= 1ull << bit; }
        }
        puts0(name); puts0(" n="); put_hex32(n);
        puts0(" avg="); put_hex32((uint32_t)q);
        puts0(" max="); put_hex32((uint32_t)mx);
        puts0(" wa="); put_hex32(wa);
        puts0(" wb="); put_hex32(wb);
        puts0(" sink="); put_hex32(sink); puts0("\n");
    }
}

enum { NS = 64, NM = 512, NT = NS * NS + NM };

int main(void)
{
    static uint16_t A[NT], B[NT];
    static const uint16_t edge[] = {
        0x0000, 0x8000, 0x7F80, 0xFF80, 0x7FC0, 0xFFC0,
        0x7F81, 0x3F80, 0x4000, 0xBF80, 0x0001, 0x7F7F
    };
    unsigned i, j, n = 0;

    puts0("div cold variants: class vs tree\n");

    /* specials-heavy: edge x edge */
    for (i = 0; i < sizeof edge / sizeof edge[0]; i++)
        for (j = 0; j < sizeof edge / sizeof edge[0]; j++) {
            A[n] = edge[i]; B[n] = edge[j]; n++;
        }
    /* mixed random */
    for (i = 0; i < NM; i++) {
        A[n] = (uint16_t)randu();
        B[n] = (uint16_t)(randu() >> 16);
        if (B[n] == 0) B[n] = 1;
        n++;
    }

    check_vs_ref("class", bf16_div_class, A, B, n);
    check_vs_ref("tree", bf16_div_tree, A, B, n);

    /* specials-only measure: first NS*NS */
    unsigned ns = (sizeof edge / sizeof edge[0]) * (sizeof edge / sizeof edge[0]);
    puts0("--- instret specials-heavy ---\n");
    measure("class", bf16_div_class, A, B, ns);
    measure("tree", bf16_div_tree, A, B, ns);

    puts0("--- instret mixed ---\n");
    measure("class", bf16_div_class, A, B, n);
    measure("tree", bf16_div_tree, A, B, n);

    puts0("DONE\n");
    return 0;
}
