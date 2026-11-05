/*
 * Guest: compare asm bf16_* vs C refs (bf16_ref.c).
 *
 * Default: fixed + stride-64 + random for binary; full unary where cheap.
 * -DCROSS_FULL: helpers/pack/sqrt/negate all 65536; u32_to_bf16 all 2^32;
 *               mul/add/sub/div each 65536^2.
 */
#include <stdint.h>

uint16_t bf16_sign_ref(uint16_t a);
uint16_t bf16_exp_ref(uint16_t a);
uint16_t bf16_mant_ref(uint16_t a);
uint16_t bf16_pack_ref(uint16_t sign, uint16_t exp, uint16_t mant);
int bf16_isnan_ref(uint16_t a);
int bf16_isinf_ref(uint16_t a);
int bf16_iszero_ref(uint16_t a);
uint16_t u32_to_bf16_ref(uint32_t x);
uint16_t bf16_mul_ref(uint16_t a, uint16_t b);
uint16_t bf16_sqrt_ref(uint16_t a);
uint16_t bf16_add_ref(uint16_t a, uint16_t b);
uint16_t bf16_sub_ref(uint16_t a, uint16_t b);
uint16_t bf16_div_ref(uint16_t a, uint16_t b);
void bf16_negate_ref(uint16_t *v, unsigned n);

uint16_t bf16_sign(uint16_t a);
uint16_t bf16_exp(uint16_t a);
uint16_t bf16_mant(uint16_t a);
uint16_t bf16_pack(uint16_t sign, uint16_t exp, uint16_t mant);
int bf16_isnan(uint16_t a);
int bf16_isinf(uint16_t a);
int bf16_iszero(uint16_t a);
uint16_t u32_to_bf16(uint32_t x);
uint16_t f32_to_bf16(uint32_t x); /* same bits as u32; float ABI = a0 */
uint16_t bf16_mul(uint16_t a, uint16_t b);
uint16_t bf16_sqrt(uint16_t a);
uint16_t bf16_add(uint16_t a, uint16_t b);
uint16_t bf16_sub(uint16_t a, uint16_t b);
uint16_t bf16_div(uint16_t a, uint16_t b);
void bf16_negate(uint16_t *v, unsigned n);

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

static void put_hex16(uint16_t x)
{
    char buf[4];
    int i;
    for (i = 3; i >= 0; i--) {
        unsigned d = (x >> (i * 4)) & 0xF;
        buf[3 - i] = (char) (d < 10 ? '0' + d : 'a' + d - 10);
    }
    write_n(buf, 4);
}

static void put_hex32(uint32_t x)
{
    char buf[8];
    int i;
    for (i = 7; i >= 0; i--) {
        unsigned d = (x >> (i * 4)) & 0xF;
        buf[7 - i] = (char) (d < 10 ? '0' + d : 'a' + d - 10);
    }
    write_n(buf, 8);
}

static unsigned g_fail;
static unsigned g_fail_total;
static unsigned g_reported;

static void fail16(const char *op, uint16_t a, uint16_t b, uint16_t got, uint16_t want)
{
    g_fail++;
    g_fail_total++;
    if (g_reported < 24) {
        puts0("FAIL ");
        puts0(op);
        puts0("(0x");
        put_hex16(a);
        if (b != 0xFFFFu || op[0] == 'm' || op[0] == 'a' || op[0] == 's') {
            /* always print b for binary; for unary pass b=0 and skip via op tag */
        }
        puts0(",0x");
        put_hex16(b);
        puts0(") got 0x");
        put_hex16(got);
        puts0(" want 0x");
        put_hex16(want);
        puts0("\n");
        g_reported++;
    }
}

static void fail_u(const char *op, uint16_t a, uint16_t got, uint16_t want)
{
    g_fail++;
    g_fail_total++;
    if (g_reported < 24) {
        puts0("FAIL ");
        puts0(op);
        puts0("(0x");
        put_hex16(a);
        puts0(") got 0x");
        put_hex16(got);
        puts0(" want 0x");
        put_hex16(want);
        puts0("\n");
        g_reported++;
    }
}

static void begin_op(const char *name)
{
    g_fail = 0;
    puts0("== ");
    puts0(name);
    puts0(" ==\n");
}

static void end_op(void)
{
    puts0("  fail=");
    put_hex32(g_fail);
    puts0(g_fail ? " BAD\n" : " OK\n");
}

static uint32_t rng;

static uint32_t randu(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

typedef uint16_t (*binop_ref_t)(uint16_t, uint16_t);
typedef uint16_t (*binop_asm_t)(uint16_t, uint16_t);

static void sweep_binop(const char *name, binop_ref_t ref, binop_asm_t asmf)
{
    unsigned ai, bi;
    uint16_t a, b, want, got;

    begin_op(name);

#ifdef CROSS_FULL
    puts0("  FULL 65536^2...\n");
    for (ai = 0; ai < 0x10000u; ai++) {
        if ((ai & 0xFF) == 0) {
            puts0("  a=0x");
            put_hex16((uint16_t) ai);
            puts0(" fail=");
            put_hex32(g_fail);
            puts0("\n");
        }
        for (bi = 0; bi < 0x10000u; bi++) {
            a = (uint16_t) ai;
            b = (uint16_t) bi;
            want = ref(a, b);
            got = asmf(a, b);
            if (got != want)
                fail16(name, a, b, got, want);
        }
    }
#else
    {
        static const uint16_t fixed[] = {
            0x0000, 0x8000, 0x3F80, 0x4000, 0x4040, 0x3F00, 0xBF80, 0xC000,
            0x7F80, 0xFF80, 0x7FC0, 0x7F81, 0xFFC0, 0x7F7F, 0xFF7F, 0x4980,
            0x0001, 0x007F, 0x0080, 0x0081, 0x00FF, 0x0100, 0x3F7F, 0x7F00,
        };
        unsigned i, j;

        for (i = 0; i < sizeof fixed / sizeof fixed[0]; i++)
            for (j = 0; j < sizeof fixed / sizeof fixed[0]; j++) {
                a = fixed[i];
                b = fixed[j];
                want = ref(a, b);
                got = asmf(a, b);
                if (got != want)
                    fail16(name, a, b, got, want);
            }
        puts0("  stride-64...\n");
        for (ai = 0; ai < 0x10000u; ai += 64)
            for (bi = 0; bi < 0x10000u; bi += 64) {
                a = (uint16_t) ai;
                b = (uint16_t) bi;
                want = ref(a, b);
                got = asmf(a, b);
                if (got != want)
                    fail16(name, a, b, got, want);
            }
        puts0("  random 200000...\n");
        for (i = 0; i < 200000u; i++) {
            a = (uint16_t) randu();
            b = (uint16_t) (randu() >> 16);
            want = ref(a, b);
            got = asmf(a, b);
            if (got != want)
                fail16(name, a, b, got, want);
        }
    }
#endif
    end_op();
}

static void sweep_unary16(const char *name,
                          uint16_t (*ref)(uint16_t),
                          uint16_t (*asmf)(uint16_t))
{
    unsigned ai;
    uint16_t a, want, got;

    begin_op(name);
    for (ai = 0; ai < 0x10000u; ai++) {
        a = (uint16_t) ai;
        want = ref(a);
        got = asmf(a);
        if (got != want)
            fail_u(name, a, got, want);
    }
    end_op();
}

static void sweep_pred(const char *name, int (*ref)(uint16_t), int (*asmf)(uint16_t))
{
    unsigned ai;
    uint16_t a;
    int want, got;

    begin_op(name);
    for (ai = 0; ai < 0x10000u; ai++) {
        a = (uint16_t) ai;
        want = ref(a);
        got = asmf(a);
        if (want != got)
            fail_u(name, a, (uint16_t) got, (uint16_t) want);
    }
    end_op();
}

static void sweep_pack(void)
{
    unsigned s, e, m;
    uint16_t want, got;

    begin_op("pack");
    for (s = 0; s < 2u; s++)
        for (e = 0; e < 256u; e++)
            for (m = 0; m < 128u; m++) {
                want = bf16_pack_ref((uint16_t) s, (uint16_t) e, (uint16_t) m);
                got = bf16_pack((uint16_t) s, (uint16_t) e, (uint16_t) m);
                if (got != want)
                    fail16("pack", (uint16_t) ((s << 8) | e), (uint16_t) m, got, want);
            }
    end_op();
}

static void sweep_negate(void)
{
    unsigned ai;
    uint16_t buf_r, buf_a, multi_r[8], multi_a[8];
    unsigned i;

    begin_op("negate");

    /* n == 0 must not touch memory */
    buf_a = 0x3F80;
    bf16_negate(&buf_a, 0);
    if (buf_a != 0x3F80)
        fail_u("negate_n0", 0x3F80, buf_a, 0x3F80);

#ifdef CROSS_FULL
    puts0("  FULL 65536 (n=1)...\n");
    for (ai = 0; ai < 0x10000u; ai++) {
        buf_r = (uint16_t) ai;
        buf_a = (uint16_t) ai;
        bf16_negate_ref(&buf_r, 1);
        bf16_negate(&buf_a, 1);
        if (buf_a != buf_r)
            fail_u("negate", (uint16_t) ai, buf_a, buf_r);
    }
#else
    for (ai = 0; ai < 0x10000u; ai++) {
        buf_r = (uint16_t) ai;
        buf_a = (uint16_t) ai;
        bf16_negate_ref(&buf_r, 1);
        bf16_negate(&buf_a, 1);
        if (buf_a != buf_r)
            fail_u("negate", (uint16_t) ai, buf_a, buf_r);
    }
#endif

    /* short multi-element */
    for (i = 0; i < 8; i++) {
        multi_r[i] = (uint16_t) (0x3F00 + i);
        multi_a[i] = multi_r[i];
    }
    bf16_negate_ref(multi_r, 8);
    bf16_negate(multi_a, 8);
    for (i = 0; i < 8; i++) {
        if (multi_a[i] != multi_r[i])
            fail_u("negate_multi", (uint16_t) i, multi_a[i], multi_r[i]);
    }

    end_op();
}

static void sweep_u32(void)
{
    uint32_t x;
    uint16_t want, got;

    begin_op("u32_to_bf16");
#ifdef CROSS_FULL
    puts0("  FULL 2^32...\n");
    x = 0;
    for (;;) {
        if ((x & 0x00FFFFFFu) == 0) {
            puts0("  x=0x");
            put_hex32(x);
            puts0(" fail=");
            put_hex32(g_fail);
            puts0("\n");
        }
        want = u32_to_bf16_ref(x);
        got = u32_to_bf16(x);
        if (got != want)
            fail_u("u32", (uint16_t) (x >> 16), got, want);
        if (x == 0xFFFFFFFFu)
            break;
        x++;
    }
#else
    {
        unsigned i;
        static const uint32_t fixed[] = {
            0x00000000u, 0x80000000u, 0x3F800000u, 0x40000000u,
            0x7F800000u, 0xFF800000u, 0x7FC00000u, 0x7F800001u,
            0x3F800001u, 0x3F7FFFFFu, 0x00000001u, 0x7F7FFFFFu,
        };
        for (i = 0; i < sizeof fixed / sizeof fixed[0]; i++) {
            want = u32_to_bf16_ref(fixed[i]);
            got = u32_to_bf16(fixed[i]);
            if (got != want)
                fail_u("u32", (uint16_t) (fixed[i] >> 16), got, want);
        }
        for (i = 0; i < 200000u; i++) {
            x = randu();
            want = u32_to_bf16_ref(x);
            got = u32_to_bf16(x);
            if (got != want)
                fail_u("u32", (uint16_t) (x >> 16), got, want);
        }
    }
#endif
    end_op();
}

int main(void)
{
    g_fail_total = 0;
    g_reported = 0;
    rng = 0xC0FFEEu;

    puts0("bf16 asm vs C golden cross-check (truncate)\n");

    sweep_unary16("sign", bf16_sign_ref, bf16_sign);
    sweep_unary16("exp", bf16_exp_ref, bf16_exp);
    sweep_unary16("mant", bf16_mant_ref, bf16_mant);
    sweep_pred("isnan", bf16_isnan_ref, bf16_isnan);
    sweep_pred("isinf", bf16_isinf_ref, bf16_isinf);
    sweep_pred("iszero", bf16_iszero_ref, bf16_iszero);
    sweep_pack();
    sweep_negate();
    sweep_u32();

    /* f32_to_bf16 shares implementation with u32_to_bf16 in asm */
    {
        uint32_t x;
        uint16_t want, got;
        unsigned i;
        begin_op("f32_to_bf16(alias)");
#ifdef CROSS_FULL
        puts0("  FULL 2^32 (same path as u32)...\n");
        x = 0;
        for (;;) {
            want = u32_to_bf16_ref(x);
            got = f32_to_bf16(x);
            if (got != want)
                fail_u("f32", (uint16_t) (x >> 16), got, want);
            if (x == 0xFFFFFFFFu)
                break;
            x++;
        }
#else
        for (i = 0; i < 4096u; i++) {
            x = randu();
            want = u32_to_bf16_ref(x);
            got = f32_to_bf16(x);
            if (got != want)
                fail_u("f32", (uint16_t) (x >> 16), got, want);
        }
#endif
        end_op();
    }

    sweep_unary16("sqrt", bf16_sqrt_ref, bf16_sqrt);
    sweep_binop("mul", bf16_mul_ref, bf16_mul);
    sweep_binop("add", bf16_add_ref, bf16_add);
    sweep_binop("sub", bf16_sub_ref, bf16_sub);
    sweep_binop("div", bf16_div_ref, bf16_div);

    puts0("TOTAL fail=");
    put_hex32(g_fail_total);
    puts0("\n");
    if (g_fail_total == 0)
        puts0("ALL OK\n");
    else
        puts0("HAS FAILURES\n");
    return g_fail_total ? 1 : 0;
}
