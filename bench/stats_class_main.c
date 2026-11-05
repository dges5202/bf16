/*
 * Full-domain instret stats vs get_instret wrap (rv32emu: cycle==instret).
 * Buckets (exclusive ladder): all | hot | nan | inf | zero | sub
 *   hot  = finite normal (exp 1..254); binary: BOTH hot
 *   nan  = either NaN
 *   inf  = either Inf, neither NaN
 *   zero = either ±0 (0x0000/0x8000), neither nan/inf
 *   sub  = either subnormal (exp==0,mant!=0), else
 * Also prints ±0 split counts inside the zero bucket (p0/m0 overlap OK).
 *
 * -DBENCH_ASM: hand bf16.S; else GCC -O2 bf16_*_ref.
 */
#include <stdint.h>

uint64_t get_instret(void);

uint16_t bf16_mul_ref(uint16_t, uint16_t);
uint16_t bf16_add_ref(uint16_t, uint16_t);
uint16_t bf16_sub_ref(uint16_t, uint16_t);
uint16_t bf16_div_ref(uint16_t, uint16_t);
uint16_t bf16_sqrt_ref(uint16_t);
uint16_t u32_to_bf16_ref(uint32_t);
void bf16_negate_ref(uint16_t *, unsigned);
uint16_t bf16_sign_ref(uint16_t);
uint16_t bf16_exp_ref(uint16_t);
uint16_t bf16_mant_ref(uint16_t);
uint16_t bf16_pack_ref(uint16_t, uint16_t, uint16_t);
int bf16_isnan_ref(uint16_t);
int bf16_isinf_ref(uint16_t);
int bf16_iszero_ref(uint16_t);

#if defined(BENCH_ASM)
uint16_t bf16_mul(uint16_t, uint16_t);
uint16_t bf16_add(uint16_t, uint16_t);
uint16_t bf16_sub(uint16_t, uint16_t);
uint16_t bf16_div(uint16_t, uint16_t);
uint16_t bf16_sqrt(uint16_t);
uint16_t u32_to_bf16(uint32_t);
void bf16_negate(uint16_t *, unsigned);
uint16_t bf16_sign(uint16_t);
uint16_t bf16_exp(uint16_t);
uint16_t bf16_mant(uint16_t);
uint16_t bf16_pack(uint16_t, uint16_t, uint16_t);
int bf16_isnan(uint16_t);
int bf16_isinf(uint16_t);
int bf16_iszero(uint16_t);
#define MUL bf16_mul
#define ADD bf16_add
#define SUB bf16_sub
#define DIV bf16_div
#define SQRT bf16_sqrt
#define U32 u32_to_bf16
#define NEG bf16_negate
#define SIGN bf16_sign
#define EXP bf16_exp
#define MANT bf16_mant
#define PACK bf16_pack
#define ISNAN bf16_isnan
#define ISINF bf16_isinf
#define ISZERO bf16_iszero
#else
#define MUL bf16_mul_ref
#define ADD bf16_add_ref
#define SUB bf16_sub_ref
#define DIV bf16_div_ref
#define SQRT bf16_sqrt_ref
#define U32 u32_to_bf16_ref
#define NEG bf16_negate_ref
#define SIGN bf16_sign_ref
#define EXP bf16_exp_ref
#define MANT bf16_mant_ref
#define PACK bf16_pack_ref
#define ISNAN bf16_isnan_ref
#define ISINF bf16_isinf_ref
#define ISZERO bf16_iszero_ref
#endif

enum {
    B_ALL = 0,
    B_HOT,
    B_NAN,
    B_INF,
    B_ZERO,
    B_SUB,
    B_N
};

static const char *bname[B_N] = {
    "all", "hot", "nan", "inf", "zero", "sub"
};

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

static uint32_t soft_avg(uint64_t sum, uint64_t n)
{
    uint64_t q = 0, r = 0; int bit;
    if (n == 0) return 0;
    for (bit = 63; bit >= 0; bit--) {
        r = (r << 1) | ((sum >> bit) & 1ull);
        if (r >= n) { r -= n; q |= 1ull << bit; }
    }
    return (uint32_t)q;
}

/* exclusive unary class */
static int class1(uint16_t x)
{
    uint16_t e = (x >> 7) & 0xFF;
    uint16_t m = x & 0x7F;
    if (e == 0xFF) return m ? B_NAN : B_INF;
    if (e == 0 && m == 0) return B_ZERO; /* ±0 */
    if (e == 0) return B_SUB;
    return B_HOT;
}

static int is_p0(uint16_t x){ return x == 0x0000; }
static int is_m0(uint16_t x){ return x == 0x8000; }

/* exclusive binary class */
static int class2(uint16_t a, uint16_t b)
{
    int ca = class1(a), cb = class1(b);
    if (ca == B_NAN || cb == B_NAN) return B_NAN;
    if (ca == B_INF || cb == B_INF) return B_INF;
    if (ca == B_ZERO || cb == B_ZERO) return B_ZERO;
    if (ca == B_SUB || cb == B_SUB) return B_SUB;
    return B_HOT;
}

typedef struct {
    uint64_t sum;
    uint64_t n;
    uint64_t mx;
    uint32_t wa, wb;
    uint64_t n_p0, n_m0; /* inside zero bucket only */
} bucket_t;

static void buck_add(bucket_t *b, uint64_t d, uint32_t a, uint32_t bb)
{
    b->sum += d;
    b->n++;
    if (d > b->mx) { b->mx = d; b->wa = a; b->wb = bb; }
}

static void buck_report(const char *op, int bi, const bucket_t *b)
{
    puts0(op); puts0("."); puts0(bname[bi]);
    puts0(" n="); put_hex32((uint32_t)b->n);
    puts0(" avg="); put_hex32(soft_avg(b->sum, b->n));
    puts0(" max="); put_hex32((uint32_t)b->mx);
    puts0(" wa="); put_hex32(b->wa);
    puts0(" wb="); put_hex32(b->wb);
    if (bi == B_ZERO && (b->n_p0 | b->n_m0)) {
        puts0(" n_p0="); put_hex32((uint32_t)b->n_p0);
        puts0(" n_m0="); put_hex32((uint32_t)b->n_m0);
    }
    puts0("\n");
}

typedef uint16_t (*binop_t)(uint16_t, uint16_t);
typedef uint16_t (*unop_t)(uint16_t);

static void sweep2(const char *name, binop_t fn)
{
    bucket_t bk[B_N];
    unsigned a, b, i;
    uint32_t sink = 0;

    for (i = 0; i < B_N; i++) {
        bk[i].sum = bk[i].n = bk[i].mx = 0;
        bk[i].wa = bk[i].wb = 0;
        bk[i].n_p0 = bk[i].n_m0 = 0;
    }

    puts0("== "); puts0(name); puts0(" FULL 65536^2 ==\n");
    for (a = 0; a < 0x10000u; a++) {
        if ((a & 0x0FFFu) == 0) {
            puts0("  prog a="); put_hex32(a);
            puts0(" all.max="); put_hex32((uint32_t)bk[B_ALL].mx); puts0("\n");
        }
        for (b = 0; b < 0x10000u; b++) {
            int c = class2((uint16_t)a, (uint16_t)b);
            uint64_t t0 = get_instret();
            sink += fn((uint16_t)a, (uint16_t)b);
            {
                uint64_t d = get_instret() - t0;
                buck_add(&bk[B_ALL], d, a, b);
                buck_add(&bk[c], d, a, b);
                if (c == B_ZERO) {
                    if (is_p0((uint16_t)a) || is_p0((uint16_t)b)) bk[B_ZERO].n_p0++;
                    if (is_m0((uint16_t)a) || is_m0((uint16_t)b)) bk[B_ZERO].n_m0++;
                }
            }
        }
    }
    for (i = 0; i < B_N; i++)
        buck_report(name, (int)i, &bk[i]);
    puts0(name); puts0(" sink="); put_hex32(sink); puts0("\n");
}

static void sweep1(const char *name, unop_t fn)
{
    bucket_t bk[B_N];
    unsigned a, i;
    uint32_t sink = 0;

    for (i = 0; i < B_N; i++) {
        bk[i].sum = bk[i].n = bk[i].mx = 0;
        bk[i].wa = bk[i].wb = 0;
        bk[i].n_p0 = bk[i].n_m0 = 0;
    }

    puts0("== "); puts0(name); puts0(" FULL 65536 ==\n");
    for (a = 0; a < 0x10000u; a++) {
        int c = class1((uint16_t)a);
        uint64_t t0 = get_instret();
        sink += fn((uint16_t)a);
        {
            uint64_t d = get_instret() - t0;
            buck_add(&bk[B_ALL], d, a, 0);
            buck_add(&bk[c], d, a, 0);
            if (c == B_ZERO) {
                if (is_p0((uint16_t)a)) bk[B_ZERO].n_p0++;
                if (is_m0((uint16_t)a)) bk[B_ZERO].n_m0++;
            }
        }
    }
    for (i = 0; i < B_N; i++)
        buck_report(name, (int)i, &bk[i]);
    puts0(name); puts0(" sink="); put_hex32(sink); puts0("\n");
}

static void sweep_negate(void)
{
    bucket_t bk[B_N];
    unsigned a, i;
    uint16_t buf;
    uint32_t sink = 0;

    for (i = 0; i < B_N; i++) {
        bk[i].sum = bk[i].n = bk[i].mx = 0;
        bk[i].wa = bk[i].wb = 0;
        bk[i].n_p0 = bk[i].n_m0 = 0;
    }

    puts0("== negate FULL 65536 (n=1) ==\n");
    for (a = 0; a < 0x10000u; a++) {
        int c = class1((uint16_t)a);
        buf = (uint16_t)a;
        {
            uint64_t t0 = get_instret();
            NEG(&buf, 1);
            {
                uint64_t d = get_instret() - t0;
                sink += buf;
                buck_add(&bk[B_ALL], d, a, 0);
                buck_add(&bk[c], d, a, 0);
                if (c == B_ZERO) {
                    if (is_p0((uint16_t)a)) bk[B_ZERO].n_p0++;
                    if (is_m0((uint16_t)a)) bk[B_ZERO].n_m0++;
                }
            }
        }
    }
    /* n=0 overhead */
    {
        uint64_t t0, d;
        buf = 0x3F80;
        t0 = get_instret();
        NEG(&buf, 0);
        d = get_instret() - t0;
        puts0("negate.n0 avg="); put_hex32((uint32_t)d); puts0("\n");
    }
    for (i = 0; i < B_N; i++)
        buck_report("negate", (int)i, &bk[i]);
    puts0("negate sink="); put_hex32(sink); puts0("\n");
}

static void sweep_u32(void)
{
    /* classes by f32 bit pattern: nan / inf / finite */
    bucket_t bk[B_N];
    uint32_t x, i;
    uint32_t sink = 0;

    for (i = 0; i < B_N; i++) {
        bk[i].sum = bk[i].n = bk[i].mx = 0;
        bk[i].wa = bk[i].wb = 0;
        bk[i].n_p0 = bk[i].n_m0 = 0;
    }

    puts0("== u32_to_bf16 FULL 2^32 ==\n");
    x = 0;
    for (;;) {
        uint32_t exp = (x >> 23) & 0xFF;
        uint32_t man = x & 0x7FFFFFu;
        int c;
        if (exp == 0xFF)
            c = man ? B_NAN : B_INF;
        else
            c = B_HOT; /* finite (incl denorm/zero f32) */

        if ((x & 0x03FFFFFFu) == 0) {
            puts0("  prog x="); put_hex32(x);
            puts0(" all.max="); put_hex32((uint32_t)bk[B_ALL].mx); puts0("\n");
        }
        {
            uint64_t t0 = get_instret();
            sink += U32(x);
            {
                uint64_t d = get_instret() - t0;
                buck_add(&bk[B_ALL], d, x >> 16, x & 0xFFFF);
                buck_add(&bk[c], d, x >> 16, x & 0xFFFF);
            }
        }
        if (x == 0xFFFFFFFFu)
            break;
        x++;
    }
    buck_report("u32", B_ALL, &bk[B_ALL]);
    buck_report("u32", B_HOT, &bk[B_HOT]);
    buck_report("u32", B_NAN, &bk[B_NAN]);
    buck_report("u32", B_INF, &bk[B_INF]);
    puts0("u32 sink="); put_hex32(sink); puts0("\n");
}

static void sweep_pack(void)
{
    bucket_t all;
    unsigned s, e, m;
    uint32_t sink = 0;
    all.sum = all.n = all.mx = 0; all.wa = all.wb = 0;

    puts0("== pack FULL 2*256*128 ==\n");
    for (s = 0; s < 2u; s++)
        for (e = 0; e < 256u; e++)
            for (m = 0; m < 128u; m++) {
                uint64_t t0 = get_instret();
                sink += PACK((uint16_t)s, (uint16_t)e, (uint16_t)m);
                {
                    uint64_t d = get_instret() - t0;
                    buck_add(&all, d, (s << 8) | e, m);
                }
            }
    puts0("pack.all n="); put_hex32((uint32_t)all.n);
    puts0(" avg="); put_hex32(soft_avg(all.sum, all.n));
    puts0(" max="); put_hex32((uint32_t)all.mx); puts0("\n");
}

static void sweep_pred(const char *name, int (*fn)(uint16_t))
{
    bucket_t bk[B_N];
    unsigned a, i;
    uint32_t sink = 0;

    for (i = 0; i < B_N; i++) {
        bk[i].sum = bk[i].n = bk[i].mx = 0;
        bk[i].wa = bk[i].wb = 0;
    }
    puts0("== "); puts0(name); puts0(" FULL 65536 ==\n");
    for (a = 0; a < 0x10000u; a++) {
        int c = class1((uint16_t)a);
        uint64_t t0 = get_instret();
        sink += (uint32_t)fn((uint16_t)a);
        {
            uint64_t d = get_instret() - t0;
            buck_add(&bk[B_ALL], d, a, 0);
            buck_add(&bk[c], d, a, 0);
        }
    }
    for (i = 0; i < B_N; i++)
        buck_report(name, (int)i, &bk[i]);
}

int main(void)
{
#if defined(BENCH_ASM)
    puts0("stats_class=ASM hand bf16.S\n");
#else
    puts0("stats_class=C GCC -O2 bf16_ref\n");
#endif
    puts0("rv32emu instret (CPI=1); get_instret wrap included\n");
    puts0("buckets: all hot nan inf zero sub (exclusive ladder)\n");

    sweep1("sign", SIGN);
    sweep1("exp", EXP);
    sweep1("mant", MANT);
    sweep_pred("isnan", ISNAN);
    sweep_pred("isinf", ISINF);
    sweep_pred("iszero", ISZERO);
    sweep_pack();
    sweep_negate();
    sweep_u32();
    sweep1("sqrt", SQRT);
    sweep2("mul", MUL);
    sweep2("add", ADD);
    sweep2("sub", SUB);
    sweep2("div", DIV);

    puts0("DONE\n");
    return 0;
}
