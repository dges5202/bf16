/* FULL 65536^2 avg/max instret — truncate C-O2. sqrt full 65536. skip div. */
#include <stdint.h>
uint64_t get_instret(void);
uint16_t bf16_mul_ref(uint16_t,uint16_t);
uint16_t bf16_add_ref(uint16_t,uint16_t);
uint16_t bf16_sub_ref(uint16_t,uint16_t);
uint16_t bf16_sqrt_ref(uint16_t);

static void write_n(const char *s, unsigned n){
  register unsigned a0 asm("a0")=1; register const char *a1 asm("a1")=s;
  register unsigned a2 asm("a2")=n; register unsigned a7 asm("a7")=64;
  asm volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a7):"memory");
}
static void puts0(const char *s){unsigned n=0;while(s[n])n++;write_n(s,n);}
static void put_hex32(uint32_t x){
  char b[8];int i;for(i=7;i>=0;i--){unsigned d=(x>>(i*4))&0xF;b[7-i]=(char)(d<10?'0'+d:'a'+d-10);} write_n(b,8);
}

typedef uint16_t (*binop)(uint16_t,uint16_t);
static void sweep2(const char *name, binop fn){
  uint64_t sum=0, mx=0; uint32_t sink=0, wa=0, wb=0;
  uint32_t a,b;
  puts0(name); puts0(" FULL 65536^2\n");
  for(a=0;a<0x10000u;a++){
    if((a&0x0FFFu)==0){ puts0("  a=0x"); put_hex32(a); puts0(" max=0x"); put_hex32((uint32_t)mx); puts0("\n"); }
    for(b=0;b<0x10000u;b++){
      uint64_t t0=get_instret();
      sink+=fn((uint16_t)a,(uint16_t)b);
      { uint64_t d=get_instret()-t0; sum+=d; if(d>mx){mx=d;wa=a;wb=b;} }
    }
  }
  {
    uint64_t q=0,r=0; int bit; const uint64_t n=1ull<<32; /* 65536^2 */
    for(bit=63;bit>=0;bit--){ r=(r<<1)|((sum>>bit)&1ull); if(r>=n){r-=n;q|=1ull<<bit;} }
    puts0(name); puts0(" n=0x100000000 avg=0x"); put_hex32((uint32_t)q);
    puts0(" max=0x"); put_hex32((uint32_t)mx);
    puts0(" worst_a=0x"); put_hex32(wa);
    puts0(" worst_b=0x"); put_hex32(wb);
    puts0(" sink=0x"); put_hex32(sink); puts0("\n");
  }
}
static void sweep1(void){
  uint64_t sum=0, mx=0; uint32_t sink=0, wa=0, a;
  for(a=0;a<0x10000u;a++){
    uint64_t t0=get_instret();
    sink+=bf16_sqrt_ref((uint16_t)a);
    { uint64_t d=get_instret()-t0; sum+=d; if(d>mx){mx=d;wa=a;} }
  }
  {
    uint64_t q=0,r=0; int bit; const uint64_t n=65536ull;
    for(bit=63;bit>=0;bit--){ r=(r<<1)|((sum>>bit)&1ull); if(r>=n){r-=n;q|=1ull<<bit;} }
    puts0("sqrt n=0x10000 avg=0x"); put_hex32((uint32_t)q);
    puts0(" max=0x"); put_hex32((uint32_t)mx);
    puts0(" worst_a=0x"); put_hex32(wa);
    puts0(" sink=0x"); put_hex32(sink); puts0("\n");
  }
}
int main(void){
  puts0("FULL domain instret stats C-O2 truncate\n");
  sweep2("mul", bf16_mul_ref);
  sweep2("add", bf16_add_ref);
  sweep2("sub", bf16_sub_ref);
  sweep1();
  puts0("div SKIP\nDONE\n");
  return 0;
}
