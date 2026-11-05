/*
 * Soft-mul loop A/B: branchless (nbr) vs branched (br).
 * Same gate/specials/shift/pack; only the 8-iter product step differs.
 *
 * 1) edges + random vs bf16_mul_ref
 * 2) FULL 65536²: br vs nbr bit-match + instret avg/max (all + hot)
 *
 * rv32emu: cycle ≡ instret. Wrap included.
 */
#include <stdint.h>

uint64_t get_instret(void);
uint16_t bf16_mul_ref(uint16_t, uint16_t);
uint16_t bf16_mul_nbr(uint16_t, uint16_t);
uint16_t bf16_mul_br(uint16_t, uint16_t);

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

static int is_nan(uint16_t x)
{
    return ((x & 0x7F80) == 0x7F80) && (x & 0x7F);
}
static int same(uint16_t a, uint16_t b)
{
    if (a == b)
        return 1;
    if (is_nan(a) && is_nan(b))
        return 1;
    return 0;
}

/* finite nonzero mag → soft-mul hot path (gate passes) */
static int is_hot(uint16_t x)
{
    uint16_t e = (x >> 7) & 0xFF;
    uint16_t m = x & 0x7F;
    if (e == 0xFF)
        return 0;
    if (e == 0 && m == 0)
        return 0;
    return 1;
}

static uint32_t rng = 0xC0FFEE01u;
static uint32_t randu(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

enum { NEDGE = 12, NRAND = 40000, NTOT = NEDGE * NEDGE + NRAND };

typedef uint16_t (*mulfn)(uint16_t, uint16_t);

static void check(const char *name, mulfn fn, const uint16_t *A, const uint16_t *B,
                  unsigned n)
{
    unsigned i, fail = 0;
    for (i = 0; i < n; i++) {
        uint16_t want = bf16_mul_ref(A[i], B[i]);
        uint16_t got = fn(A[i], B[i]);
        if (!same(got, want)) {
            if (fail < 6) {
                puts0("FAIL ");
                puts0(name);
                puts0(" a=");
                put_hex32(A[i]);
                puts0(" b=");
                put_hex32(B[i]);
                puts0(" got=");
                put_hex32(got);
                puts0(" want=");
                put_hex32(want);
                puts0("\n");
            }
            fail++;
        }
    }
    puts0(name);
    puts0(" check n=");
    put_hex32(n);
    puts0(" fail=");
    put_hex32(fail);
    puts0("\n");
}

static uint64_t udiv64(uint64_t n, uint64_t d)
{
    uint64_t q = 0, r = 0;
    int bit;
    if (d == 0)
        return 0;
    for (bit = 63; bit >= 0; bit--) {
        r = (r << 1) | ((n >> bit) & 1ull);
        if (r >= d) {
            r -= d;
            q |= 1ull << bit;
        }
    }
    return q;
}

static void measure(const char *name, mulfn fn, const uint16_t *A, const uint16_t *B,
                    unsigned n, int hot_only)
{
    uint64_t sum = 0, mx = 0;
    uint32_t sink = 0, i, cnt = 0, wa = 0, wb = 0;
    for (i = 0; i < n; i++) {
        if (hot_only && !(is_hot(A[i]) && is_hot(B[i])))
            continue;
        {
            uint64_t t0 = get_instret();
            sink += fn(A[i], B[i]);
            {
                uint64_t d = get_instret() - t0;
                sum += d;
                if (d > mx) {
                    mx = d;
                    wa = A[i];
                    wb = B[i];
                }
                cnt++;
            }
        }
    }
    if (cnt == 0)
        cnt = 1;
    puts0(name);
    puts0(" n=");
    put_hex32(cnt);
    puts0(" avg=");
    put_hex32((uint32_t)udiv64(sum, cnt));
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

/* FULL 65536²: match br↔nbr; instret for both (all + hot). One timing each. */
static void full_scan(void)
{
    uint64_t sum_nbr = 0, sum_br = 0, sum_nbr_h = 0, sum_br_h = 0;
    uint64_t mx_nbr = 0, mx_br = 0, mx_nbr_h = 0, mx_br_h = 0;
    uint64_t n_all = 0, n_hot = 0;
    uint32_t fail = 0, sink = 0;
    uint32_t a, b;

    puts0("--- FULL 65536^2: match + instret ---\n");
    for (a = 0; a < 65536u; a++) {
        for (b = 0; b < 65536u; b++) {
            uint16_t ua = (uint16_t)a, ub = (uint16_t)b;
            uint16_t rn, rb;
            uint64_t t0, dn, db;
            int hot;

            t0 = get_instret();
            rn = bf16_mul_nbr(ua, ub);
            dn = get_instret() - t0;
            sum_nbr += dn;
            if (dn > mx_nbr)
                mx_nbr = dn;

            t0 = get_instret();
            rb = bf16_mul_br(ua, ub);
            db = get_instret() - t0;
            sum_br += db;
            if (db > mx_br)
                mx_br = db;

            if (!same(rn, rb)) {
                if (fail < 6) {
                    puts0("MISMATCH a=");
                    put_hex32(ua);
                    puts0(" b=");
                    put_hex32(ub);
                    puts0(" nbr=");
                    put_hex32(rn);
                    puts0(" br=");
                    put_hex32(rb);
                    puts0("\n");
                }
                fail++;
            }
            sink += (uint32_t)rn + (uint32_t)rb;
            n_all++;

            hot = is_hot(ua) && is_hot(ub);
            if (hot) {
                sum_nbr_h += dn;
                sum_br_h += db;
                if (dn > mx_nbr_h)
                    mx_nbr_h = dn;
                if (db > mx_br_h)
                    mx_br_h = db;
                n_hot++;
            }
        }
        if ((a & 0xFFF) == 0) {
            puts0("prog a=");
            put_hex32(a);
            puts0("\n");
        }
    }

    puts0("match fail=");
    put_hex32(fail);
    puts0(" sink=");
    put_hex32(sink);
    puts0("\n");

    puts0("all  n=");
    put_hex32((uint32_t)n_all); /* low 32; full is 2^32 */
    puts0(" (full 2^32)\n");
    puts0("nbr all avg=");
    put_hex32((uint32_t)udiv64(sum_nbr, n_all ? n_all : 1));
    puts0(" max=");
    put_hex32((uint32_t)mx_nbr);
    puts0("\n");
    puts0("br  all avg=");
    put_hex32((uint32_t)udiv64(sum_br, n_all ? n_all : 1));
    puts0(" max=");
    put_hex32((uint32_t)mx_br);
    puts0("\n");

    if (n_hot == 0)
        n_hot = 1;
    puts0("hot n=");
    put_hex32((uint32_t)n_hot);
    puts0("\n");
    puts0("nbr hot avg=");
    put_hex32((uint32_t)udiv64(sum_nbr_h, n_hot));
    puts0(" max=");
    put_hex32((uint32_t)mx_nbr_h);
    puts0("\n");
    puts0("br  hot avg=");
    put_hex32((uint32_t)udiv64(sum_br_h, n_hot));
    puts0(" max=");
    put_hex32((uint32_t)mx_br_h);
    puts0("\n");
}

int main(void)
{
    static uint16_t A[NTOT], B[NTOT];
    static const uint16_t edge[] = {
        0x0000, 0x8000, 0x3F80, 0x4000, 0x3F00, 0x4040,
        0x7F80, 0xFF80, 0x7FC0, 0xBF80, 0x0001, 0x7F7F
    };
    unsigned i, j, n = 0;

    puts0("mul loop: branchless(nbr) vs branched(br)\n");
    for (i = 0; i < NEDGE; i++)
        for (j = 0; j < NEDGE; j++) {
            A[n] = edge[i];
            B[n] = edge[j];
            n++;
        }
    for (i = 0; i < NRAND; i++) {
        A[n] = (uint16_t)randu();
        B[n] = (uint16_t)(randu() >> 16);
        n++;
    }

    check("nbr", bf16_mul_nbr, A, B, n);
    check("br ", bf16_mul_br, A, B, n);

    puts0("--- instret sample all ---\n");
    measure("nbr", bf16_mul_nbr, A, B, n, 0);
    measure("br ", bf16_mul_br, A, B, n, 0);

    puts0("--- instret sample hot-only ---\n");
    measure("nbr", bf16_mul_nbr, A, B, n, 1);
    measure("br ", bf16_mul_br, A, B, n, 1);

#ifndef MUL_LOOP_SKIP_FULL
    full_scan();
#endif

    puts0("DONE\n");
    return 0;
}
