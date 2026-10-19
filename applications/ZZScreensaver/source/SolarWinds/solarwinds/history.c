/* 0BSD. Preserve the prior completed frame across triple buffering. */
#include "history.h"
#if defined(__arm__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif
void sw_history_copy(void *dst,const void *src,uint32_t bytes)
{
    unsigned char *d=(unsigned char*)dst;
    const unsigned char *s=(const unsigned char*)src;
    uint32_t i=0;
#if defined(__arm__) && defined(__ARM_NEON)
    for(;i+64<=bytes;i+=64) {
        uint8x16_t a=vld1q_u8(s+i),b=vld1q_u8(s+i+16);
        uint8x16_t c=vld1q_u8(s+i+32),e=vld1q_u8(s+i+48);
        vst1q_u8(d+i,vrev16q_u8(a));vst1q_u8(d+i+16,vrev16q_u8(b));
        vst1q_u8(d+i+32,vrev16q_u8(c));vst1q_u8(d+i+48,vrev16q_u8(e));
    }
#endif
    for(;i+2<=bytes;i+=2){unsigned char a=s[i],b=s[i+1];d[i]=b;d[i+1]=a;}
}
