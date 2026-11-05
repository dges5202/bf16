#include <stdint.h>
#include "ripes_samp_data.h"

uint16_t bf16_mul_ref(uint16_t,uint16_t);
uint16_t bf16_add_ref(uint16_t,uint16_t);
uint16_t bf16_sub_ref(uint16_t,uint16_t);
uint16_t bf16_sqrt_ref(uint16_t);

#if defined(OP_MUL)
#define OP(a,b) bf16_mul_ref(a,b)
#elif defined(OP_ADD)
#define OP(a,b) bf16_add_ref(a,b)
#elif defined(OP_SUB)
#define OP(a,b) bf16_sub_ref(a,b)
#elif defined(OP_SQRT)
#define UNARY 1
#elif defined(OP_EMPTY)
#define OP(a,b) ((uint16_t)((a)+(b)))
#else
#error define OP_*
#endif

int main(void)
{
  uint32_t sink = 0;
  unsigned i;
#if defined(UNARY)
  for (i = 0; i < SAMP_UN; i++)
    sink += bf16_sqrt_ref(SAMP_U[i]);
#else
  for (i = 0; i < SAMP_N; i++)
    sink += OP(SAMP_A[i], SAMP_B[i]);
#endif
  return (int)(sink & 0xff);
}
