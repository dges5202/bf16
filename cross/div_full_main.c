/* Full 65536^2 asm bf16_div vs bf16_div_ref. */
#include <stdint.h>

uint16_t bf16_div_ref(uint16_t, uint16_t);
uint16_t bf16_div(uint16_t, uint16_t);

static void write_n(const char *s, unsigned n)
{
    register unsigned a0 asm("a0") = 1;
    register const char *a1 asm("a1") = s;
    register unsigned a2 asm("a2") = n;
    register unsigned a7 asm("a7") = 64;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}
static void puts0(const char *s){ unsigned n=0; while(s[n]) n++; write_n(s,n); }
static void put_hex16(uint16_t x)
{
    char b[4]; int i;
    for (i = 3; i >= 0; i--) {
        unsigned d = (x >> (i * 4)) & 0xF;
        b[3 - i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    write_n(b, 4);
}
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

int main(void)
{
    uint32_t fail = 0, reported = 0;
    unsigned ai, bi;

    puts0("div FULL 65536^2 asm vs ref\n");
    for (ai = 0; ai < 0x10000u; ai++) {
        if ((ai & 0xFF) == 0) {
            puts0("a="); put_hex16((uint16_t)ai);
            puts0(" fail="); put_hex32(fail); puts0("\n");
        }
        for (bi = 0; bi < 0x10000u; bi++) {
            uint16_t a = (uint16_t)ai, b = (uint16_t)bi;
            uint16_t want = bf16_div_ref(a, b);
            uint16_t got = bf16_div(a, b);
            if (!same(got, want)) {
                if (reported < 16) {
                    puts0("FAIL a="); put_hex16(a);
                    puts0(" b="); put_hex16(b);
                    puts0(" got="); put_hex16(got);
                    puts0(" want="); put_hex16(want); puts0("\n");
                    reported++;
                }
                fail++;
            }
        }
    }
    puts0("TOTAL fail="); put_hex32(fail); puts0("\n");
    puts0(fail ? "HAS FAILURES\n" : "ALL OK\n");
    return fail ? 1 : 0;
}
