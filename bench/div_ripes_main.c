#include <stdint.h>
uint16_t bf16_div_class(uint16_t,uint16_t);
uint16_t bf16_div_tree(uint16_t,uint16_t);
static const uint16_t E[]={0x0000,0x8000,0x7F80,0xFF80,0x7FC0,0x3F80,0x4000,0xBF80,0x0001,0x7F7F,0xFFC0,0x7F81};
#if defined(USE_TREE)
#define DIV bf16_div_tree
#else
#define DIV bf16_div_class
#endif
int main(void){
  uint32_t sink=0; unsigned i,j;
  for(i=0;i<12;i++) for(j=0;j<12;j++) sink+=DIV(E[i],E[j]);
  for(i=0;i<128;i++) sink+=DIV((uint16_t)(0x3F80+i),(uint16_t)(0x4000+(i*3)));
  return (int)(sink&0xff);
}
