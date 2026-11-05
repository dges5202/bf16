/*
 * Full-domain path-classed instret (ASM or C-O2).
 *
 * Coarse ladder (exclusive): all | hot | nan | inf | zero | sub
 * Fine paths: hot control-flow / short-circuit for add/sub/mul/div/sqrt;
 *             cold uses coarse name as path id.
 *
 * -DBENCH_ASM: hand bf16.S; else GCC -O2 refs.
 * -DPATH_CLASS_QUICK: unary full; binary stride-64 (validate IDs fast).
 * -DPATH_CLASS_SKIP_U32: skip 2^32 u32_to_bf16 sweep.
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

/* ---- fine path ids (per-op namespaces; report with op.path=) ---- */
enum {
    /* shared cold when not both-hot / not hot unary */
    P_COLD_NAN = 0,
    P_COLD_INF,
    P_COLD_ZERO,
    P_COLD_SUB,
    /* add/sub hot */
    P_SS_EASY,
    P_SS_BUMP,
    P_SS_SWAP,
    P_SS_WIDE,
    P_ON_GE_MA,
    P_ON_GE_MB,
    P_ON_LT_MA,
    P_ON_LT_MB,
    P_OPP_LEFT,
    P_OPP_ZERO,
    /* mul hot */
    P_MUL_SH7,
    P_MUL_SH8,
    P_MUL_UNDER,
    P_MUL_OVER,
    /* div hot */
    P_DIV_PACK,
    P_DIV_RENORM,
    P_DIV_UNDER,
    P_DIV_OVER,
    /* sqrt */
    P_SQRT_ZERO,
    P_SQRT_NAN_NEG,
    P_SQRT_NAN_NINF,
    P_SQRT_PINF,
    P_SQRT_HOT_EVEN,
    P_SQRT_HOT_ODD,
    /* helpers / u32 / pack / negate use coarse-only or single */
    P_ONE,
    P_N
};

static const char *pname[P_N] = {
    "cold_nan", "cold_inf", "cold_zero", "cold_sub",
    "ss_easy", "ss_bump", "ss_swap", "ss_wide",
    "on_ge_ma", "on_ge_mb", "on_lt_ma", "on_lt_mb",
    "opp_left", "opp_zero",
    "mul_sh7", "mul_sh8", "mul_under", "mul_over",
    "div_pack", "div_renorm", "div_under", "div_over",
    "sqrt_zero", "sqrt_nan_neg", "sqrt_nan_ninf", "sqrt_pinf",
    "sqrt_hot_even", "sqrt_hot_odd",
    "one",
};

typedef struct {
    uint64_t sum;
    uint64_t n;
    uint64_t mx;
    uint32_t wa, wb;
} bucket_t;

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

static void buck_clear(bucket_t *b)
{
    b->sum = b->n = b->mx = 0;
    b->wa = b->wb = 0;
}
static void buck_add(bucket_t *b, uint64_t d, uint32_t a, uint32_t bb)
{
    b->sum += d;
    b->n++;
    if (d > b->mx) {
        b->mx = d;
        b->wa = a;
        b->wb = bb;
    }
}
static void buck_report(const char *op, const char *kind, const char *name, const bucket_t *b)
{
    if (b->n == 0)
        return;
    puts0(op);
    puts0(".");
    puts0(kind);
    puts0("=");
    puts0(name);
    /* n64 / sum64: hi then lo (uint32 print alone overflows at 2^32) */
    puts0(" n_hi=");
    put_hex32((uint32_t)(b->n >> 32));
    puts0(" n=");
    put_hex32((uint32_t)b->n);
    puts0(" avg=");
    put_hex32(soft_avg(b->sum, b->n));
    puts0(" max=");
    put_hex32((uint32_t)b->mx);
    puts0(" wa=");
    put_hex32(b->wa);
    puts0(" wb=");
    put_hex32(b->wb);
    puts0(" sum_hi=");
    put_hex32((uint32_t)(b->sum >> 32));
    puts0(" sum=");
    put_hex32((uint32_t)b->sum);
    puts0("\n");
}

static int class1(uint16_t x)
{
    uint16_t e = (x >> 7) & 0xFF;
    uint16_t m = x & 0x7F;
    if (e == 0xFF)
        return m ? B_NAN : B_INF;
    if (e == 0 && m == 0)
        return B_ZERO;
    if (e == 0)
        return B_SUB;
    return B_HOT;
}

static int class2(uint16_t a, uint16_t b)
{
    int ca = class1(a), cb = class1(b);
    if (ca == B_NAN || cb == B_NAN)
        return B_NAN;
    if (ca == B_INF || cb == B_INF)
        return B_INF;
    if (ca == B_ZERO || cb == B_ZERO)
        return B_ZERO;
    if (ca == B_SUB || cb == B_SUB)
        return B_SUB;
    return B_HOT;
}

static int cold_path(int c)
{
    if (c == B_NAN)
        return P_COLD_NAN;
    if (c == B_INF)
        return P_COLD_INF;
    if (c == B_ZERO)
        return P_COLD_ZERO;
    return P_COLD_SUB;
}

/* ---- hot path classifiers (truncate semantics) ---- */

static int path_add(uint16_t a, uint16_t b)
{
    int c = class2(a, b);
    unsigned sa, sb, ma, mb, mant;
    int ea, eb, dexp;

    if (c != B_HOT)
        return cold_path(c);

    sa = a >> 15;
    sb = b >> 15;
    ea = (a >> 7) & 0xFF;
    eb = (b >> 7) & 0xFF;
    ma = (a & 0x7F) | 0x80;
    mb = (b & 0x7F) | 0x80;

    if (ea >= eb) {
        dexp = ea - eb;
        if (dexp >= 8)
            mb = 0;
        else
            mb >>= (unsigned)dexp;
    } else {
        dexp = eb - ea;
        if (dexp >= 8)
            ma = 0;
        else
            ma >>= (unsigned)dexp;
    }

    if (sa == sb) {
        mant = ma + mb;
        if (dexp >= 8)
            return P_SS_WIDE;
        if (ea < eb)
            return (mant >= 0x100u) ? P_SS_BUMP : P_SS_SWAP;
        return (mant >= 0x100u) ? P_SS_BUMP : P_SS_EASY;
    }
    if (ma == mb)
        return P_OPP_ZERO;
    mant = (ma > mb) ? (ma - mb) : (mb - ma);
    if (mant < 0x80u)
        return P_OPP_LEFT;
    if (ea >= eb)
        return (ma > mb) ? P_ON_GE_MA : P_ON_GE_MB;
    return (ma > mb) ? P_ON_LT_MA : P_ON_LT_MB;
}

/* RV32I: no M; 8x8 product for classifier only */
static uint32_t soft_mul8(uint32_t ma, uint32_t mb)
{
    uint32_t prod = 0;
    int i;
    for (i = 0; i < 8; i++) {
        if (mb & 1u)
            prod += ma;
        mb >>= 1;
        ma <<= 1;
    }
    return prod;
}

static int path_mul(uint16_t a, uint16_t b)
{
    int c = class2(a, b);
    int ea, eb, exp;
    unsigned ma, mb;
    uint32_t prod;

    if (c != B_HOT)
        return cold_path(c);

    ea = (a >> 7) & 0xFF;
    eb = (b >> 7) & 0xFF;
    ma = a & 0x7F;
    mb = b & 0x7F;
    if (ea)
        ma |= 0x80;
    if (eb)
        mb |= 0x80;
    exp = ea + eb - 127;
    prod = soft_mul8(ma, mb);
    if (prod & 0x8000u)
        exp++;
    if (exp < 0)
        return P_MUL_UNDER;
    if (exp >= 255)
        return P_MUL_OVER;
    return (prod & 0x8000u) ? P_MUL_SH8 : P_MUL_SH7;
}

static int path_div(uint16_t a, uint16_t b)
{
    int c = class2(a, b);
    int ea, eb, exp;
    unsigned ma, mb;
    uint32_t rem, d, q;
    int i, renorm;

    if (c != B_HOT)
        return cold_path(c);

    /* hot normals: DAZ no-op */
    ea = (a >> 7) & 0xFF;
    eb = (b >> 7) & 0xFF;
    ma = (a & 0x7F) | 0x80;
    mb = (b & 0x7F) | 0x80;
    exp = ea - eb + 127;
    rem = (uint32_t)ma << 7;
    d = (uint32_t)mb << 7;
    q = 0;
    for (i = 0; i < 8; i++) {
        q <<= 1;
        if (rem >= d) {
            rem -= d;
            q |= 1u;
        }
        d >>= 1;
    }
    renorm = 0;
    if (q < 0x80u && q != 0u) {
        q <<= 1;
        exp--;
        renorm = 1;
    }
    if (exp < 0)
        return P_DIV_UNDER;
    if (exp >= 255)
        return P_DIV_OVER;
    return renorm ? P_DIV_RENORM : P_DIV_PACK;
}

static int path_sqrt(uint16_t a)
{
    int c = class1(a);
    int e, exp;

    if (c == B_ZERO)
        return P_SQRT_ZERO;
    if (c == B_NAN)
        return P_COLD_NAN;
    if (c == B_INF)
        return (a >> 15) ? P_SQRT_NAN_NINF : P_SQRT_PINF;
    if (a >> 15)
        return P_SQRT_NAN_NEG;
    if (c == B_SUB)
        return P_COLD_SUB; /* still runs hot-ish in truncate; bucket as sub */

    e = (a >> 7) & 0xFF;
    exp = e - 127;
    return (exp & 1) ? P_SQRT_HOT_ODD : P_SQRT_HOT_EVEN;
}

#if defined(PATH_CLASS_QUICK)
#define BIN_STEP 64u
#else
#define BIN_STEP 1u
#endif

typedef uint16_t (*binop_t)(uint16_t, uint16_t);
typedef uint16_t (*unop_t)(uint16_t);
typedef int (*path2_t)(uint16_t, uint16_t);
typedef int (*path1_t)(uint16_t);

static void sweep2(const char *name, binop_t fn, path2_t pfn)
{
    bucket_t coarse[B_N];
    bucket_t path[P_N];
    unsigned a, b, i;
    uint32_t sink = 0;

    for (i = 0; i < B_N; i++)
        buck_clear(&coarse[i]);
    for (i = 0; i < P_N; i++)
        buck_clear(&path[i]);

    puts0("== ");
    puts0(name);
#if defined(PATH_CLASS_QUICK)
    puts0(" QUICK stride-64 ==\n");
#else
    puts0(" FULL 65536^2 ==\n");
#endif
    for (a = 0; a < 0x10000u; a += BIN_STEP) {
        if ((a & 0x0FFFu) == 0) {
            puts0("  prog a=");
            put_hex32(a);
            puts0("\n");
        }
        for (b = 0; b < 0x10000u; b += BIN_STEP) {
            int c = class2((uint16_t)a, (uint16_t)b);
            int p = pfn((uint16_t)a, (uint16_t)b);
            uint64_t t0 = get_instret();
            sink += fn((uint16_t)a, (uint16_t)b);
            {
                uint64_t d = get_instret() - t0;
                buck_add(&coarse[B_ALL], d, a, b);
                buck_add(&coarse[c], d, a, b);
                if ((unsigned)p < P_N)
                    buck_add(&path[p], d, a, b);
            }
        }
    }
    for (i = 0; i < B_N; i++)
        buck_report(name, "cls", bname[i], &coarse[i]);
    for (i = 0; i < P_N; i++)
        buck_report(name, "path", pname[i], &path[i]);
    puts0(name);
    puts0(" sink=");
    put_hex32(sink);
    puts0("\n");
}

static void sweep1(const char *name, unop_t fn, path1_t pfn)
{
    bucket_t coarse[B_N];
    bucket_t path[P_N];
    unsigned a, i;
    uint32_t sink = 0;

    for (i = 0; i < B_N; i++)
        buck_clear(&coarse[i]);
    for (i = 0; i < P_N; i++)
        buck_clear(&path[i]);

    puts0("== ");
    puts0(name);
    puts0(" FULL 65536 ==\n");
    for (a = 0; a < 0x10000u; a++) {
        int c = class1((uint16_t)a);
        int p = pfn ? pfn((uint16_t)a) : P_ONE;
        uint64_t t0 = get_instret();
        sink += fn((uint16_t)a);
        {
            uint64_t d = get_instret() - t0;
            buck_add(&coarse[B_ALL], d, a, 0);
            buck_add(&coarse[c], d, a, 0);
            if ((unsigned)p < P_N)
                buck_add(&path[p], d, a, 0);
        }
    }
    for (i = 0; i < B_N; i++)
        buck_report(name, "cls", bname[i], &coarse[i]);
    for (i = 0; i < P_N; i++)
        buck_report(name, "path", pname[i], &path[i]);
    puts0(name);
    puts0(" sink=");
    put_hex32(sink);
    puts0("\n");
}

static int path_one(uint16_t a)
{
    (void)a;
    return P_ONE;
}

static int path_by_class1(uint16_t a)
{
    int c = class1(a);
    if (c == B_HOT)
        return P_ONE;
    return cold_path(c);
}

static void sweep_negate(void)
{
    bucket_t coarse[B_N];
    bucket_t path[P_N];
    unsigned a, i;
    uint16_t buf;
    uint32_t sink = 0;

    for (i = 0; i < B_N; i++)
        buck_clear(&coarse[i]);
    for (i = 0; i < P_N; i++)
        buck_clear(&path[i]);

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
                buck_add(&coarse[B_ALL], d, a, 0);
                buck_add(&coarse[c], d, a, 0);
                buck_add(&path[P_ONE], d, a, 0);
            }
        }
    }
    {
        uint64_t t0, d;
        buf = 0x3F80;
        t0 = get_instret();
        NEG(&buf, 0);
        d = get_instret() - t0;
        puts0("negate.path=n0 n=00000001 avg=");
        put_hex32((uint32_t)d);
        puts0(" max=");
        put_hex32((uint32_t)d);
        puts0(" wa=00000000 wb=00000000 sum=");
        put_hex32((uint32_t)d);
        puts0("\n");
    }
    for (i = 0; i < B_N; i++)
        buck_report("negate", "cls", bname[i], &coarse[i]);
    for (i = 0; i < P_N; i++)
        buck_report("negate", "path", pname[i], &path[i]);
    puts0("negate sink=");
    put_hex32(sink);
    puts0("\n");
}

static void sweep_pack(void)
{
    bucket_t all;
    unsigned s, e, m;
    uint32_t sink = 0;
    buck_clear(&all);
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
    buck_report("pack", "cls", "all", &all);
    buck_report("pack", "path", "one", &all);
    puts0("pack sink=");
    put_hex32(sink);
    puts0("\n");
}

#if !defined(PATH_CLASS_SKIP_U32) && !defined(PATH_CLASS_QUICK)
static void sweep_u32(void)
{
    bucket_t coarse[B_N];
    bucket_t path[P_N];
    uint32_t x, i;
    uint32_t sink = 0;

    for (i = 0; i < B_N; i++)
        buck_clear(&coarse[i]);
    for (i = 0; i < P_N; i++)
        buck_clear(&path[i]);

    puts0("== u32_to_bf16 FULL 2^32 ==\n");
    x = 0;
    for (;;) {
        uint32_t ex = (x >> 23) & 0xFF;
        uint32_t man = x & 0x7FFFFFu;
        int c, p;
        if (ex == 0xFF)
            c = man ? B_NAN : B_INF;
        else
            c = B_HOT;
        p = (c == B_NAN) ? P_COLD_NAN : (c == B_INF ? P_COLD_INF : P_ONE);

        if ((x & 0x03FFFFFFu) == 0) {
            puts0("  prog x=");
            put_hex32(x);
            puts0("\n");
        }
        {
            uint64_t t0 = get_instret();
            sink += U32(x);
            {
                uint64_t d = get_instret() - t0;
                buck_add(&coarse[B_ALL], d, x >> 16, x & 0xFFFF);
                buck_add(&coarse[c], d, x >> 16, x & 0xFFFF);
                buck_add(&path[p], d, x >> 16, x & 0xFFFF);
            }
        }
        if (x == 0xFFFFFFFFu)
            break;
        x++;
    }
    buck_report("u32", "cls", "all", &coarse[B_ALL]);
    buck_report("u32", "cls", "hot", &coarse[B_HOT]);
    buck_report("u32", "cls", "nan", &coarse[B_NAN]);
    buck_report("u32", "cls", "inf", &coarse[B_INF]);
    for (i = 0; i < P_N; i++)
        buck_report("u32", "path", pname[i], &path[i]);
    puts0("u32 sink=");
    put_hex32(sink);
    puts0("\n");
}
#endif

static void sweep_pred(const char *name, int (*fn)(uint16_t))
{
    bucket_t coarse[B_N];
    bucket_t path[P_N];
    unsigned a, i;
    uint32_t sink = 0;

    for (i = 0; i < B_N; i++)
        buck_clear(&coarse[i]);
    for (i = 0; i < P_N; i++)
        buck_clear(&path[i]);

    puts0("== ");
    puts0(name);
    puts0(" FULL 65536 ==\n");
    for (a = 0; a < 0x10000u; a++) {
        int c = class1((uint16_t)a);
        int p = path_by_class1((uint16_t)a);
        uint64_t t0 = get_instret();
        sink += (uint32_t)fn((uint16_t)a);
        {
            uint64_t d = get_instret() - t0;
            buck_add(&coarse[B_ALL], d, a, 0);
            buck_add(&coarse[c], d, a, 0);
            buck_add(&path[p], d, a, 0);
        }
    }
    for (i = 0; i < B_N; i++)
        buck_report(name, "cls", bname[i], &coarse[i]);
    for (i = 0; i < P_N; i++)
        buck_report(name, "path", pname[i], &path[i]);
    puts0(name);
    puts0(" sink=");
    put_hex32(sink);
    puts0("\n");
}

/* OPS= filter: default all on. With -DOPS_FILTER, only -DOP_*=1 run. */
#if !defined(OPS_FILTER)
#define OP_SIGN 1
#define OP_EXP 1
#define OP_MANT 1
#define OP_ISNAN 1
#define OP_ISINF 1
#define OP_ISZERO 1
#define OP_PACK 1
#define OP_NEGATE 1
#define OP_U32 1
#define OP_SQRT 1
#define OP_MUL 1
#define OP_ADD 1
#define OP_SUB 1
#define OP_DIV 1
#endif

int main(void)
{
#if defined(BENCH_ASM)
    puts0("path_class=ASM hand bf16.S\n");
#else
    puts0("path_class=C GCC -O2 bf16_ref\n");
#endif
    puts0("rv32emu instret (CPI=1); wrap included; cls+path buckets\n");
#if defined(PATH_CLASS_QUICK)
    puts0("MODE=QUICK\n");
#endif
#if defined(OPS_FILTER)
    puts0("OPS=filtered\n");
#endif

#if OP_SIGN
    sweep1("sign", SIGN, path_one);
#endif
#if OP_EXP
    sweep1("exp", EXP, path_one);
#endif
#if OP_MANT
    sweep1("mant", MANT, path_one);
#endif
#if OP_ISNAN
    sweep_pred("isnan", ISNAN);
#endif
#if OP_ISINF
    sweep_pred("isinf", ISINF);
#endif
#if OP_ISZERO
    sweep_pred("iszero", ISZERO);
#endif
#if OP_PACK
    sweep_pack();
#endif
#if OP_NEGATE
    sweep_negate();
#endif
#if OP_U32
#if !defined(PATH_CLASS_SKIP_U32) && !defined(PATH_CLASS_QUICK)
    sweep_u32();
#else
    puts0("== u32_to_bf16 SKIPPED ==\n");
#endif
#endif
#if OP_SQRT
    sweep1("sqrt", SQRT, path_sqrt);
#endif
#if OP_MUL
    sweep2("mul", MUL, path_mul);
#endif
#if OP_ADD
    sweep2("add", ADD, path_add);
#endif
#if OP_SUB
    sweep2("sub", SUB, path_add); /* same hot taxonomy; +xor trampoline in leaf */
#endif
#if OP_DIV
    sweep2("div", DIV, path_div);
#endif

    puts0("DONE\n");
    return 0;
}
