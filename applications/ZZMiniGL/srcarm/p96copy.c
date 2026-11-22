/* ZZMiniGL A1c: bounded CPU1 DDR->DDR RGB565 presentation copier.
 * ARMv7 Normal Non-cacheable memory (GPU source and P96 destination).
 * Do not enable WB caching or NEON without independent coherence tests.
 * The 32-byte path is optional and requires 32-byte aligned source,
 * destination and pitches. Scalar is the DIAG4 reference implementation.
 */
#include "wire.h"

typedef zu32 u32;
static inline u32 swap565(u32 v) {
  return ((v & 0x00ff00ffu)<<8) | ((v & 0xff00ff00u)>>8);
}

/* Each iteration moves eight 32-bit words (16 RGB565 pixels). On ARMv7,
 * REV16 swaps the two bytes of each halfword but preserves pixel order.
 * The compiler sees all modified registers and memory barriers explicitly.
 * The host simulator uses equivalent C operations for correctness testing.
 */
static void copy32_burst(volatile u32 *dst, const volatile u32 *src,
                         u32 chunks, int be) {
#if defined(__arm__) && !defined(RIX_HOST_SIM)
  u32 *d=(u32 *)dst;
  const u32 *s=(const u32 *)src;
  if(be) {
    __asm__ volatile(
      "1:\n\t"
      "ldmia %[s]!, {r3,r4,r5,r6,r8,r9,r10,r12}\n\t"
      "rev16 r3,r3\n\t" "rev16 r4,r4\n\t"
      "rev16 r5,r5\n\t" "rev16 r6,r6\n\t"
      "rev16 r8,r8\n\t" "rev16 r9,r9\n\t"
      "rev16 r10,r10\n\t" "rev16 r12,r12\n\t"
      "stmia %[d]!, {r3,r4,r5,r6,r8,r9,r10,r12}\n\t"
      "subs %[n],%[n],#1\n\t" "bne 1b\n\t"
      : [s]"+r"(s),[d]"+r"(d),[n]"+r"(chunks)
      : : "r3","r4","r5","r6","r8","r9","r10","r12","cc","memory");
  } else {
    __asm__ volatile(
      "1:\n\t"
      "ldmia %[s]!, {r3,r4,r5,r6,r8,r9,r10,r12}\n\t"
      "stmia %[d]!, {r3,r4,r5,r6,r8,r9,r10,r12}\n\t"
      "subs %[n],%[n],#1\n\t" "bne 1b\n\t"
      : [s]"+r"(s),[d]"+r"(d),[n]"+r"(chunks)
      : : "r3","r4","r5","r6","r8","r9","r10","r12","cc","memory");
  }
#else
  u32 i;
  for(i=0;i<chunks*8u;i++){
    u32 v=src[i];dst[i]=be?swap565(v):v;
  }
#endif
}

/* Returns 1 if a burst was used, 0 if scalar fallback was required.
 * The source and destination are required to be disjoint DDR ranges,
 * validated by the caller's strict physical aperture checks.
 * width is pixels, both pitches are bytes, format is a P96 RGBFTYPE.
 */
int zz_p96_copy(volatile u32 *dst, const volatile u32 *src,
                u32 width,u32 height,u32 dst_pitch,u32 src_pitch,
                u32 format,u32 mode) {
  u32 y,x,words=width/2u;
  int be=(format==ZZ_P96_FMT_RGB565_BE), burst;
  if(!width||!height||(width&1u))return -1;
  if(format!=ZZ_P96_FMT_RGB565_PC && format!=ZZ_P96_FMT_RGB565_BE)return -1;
  if(mode!=ZZ_P96_COPY_SCALAR && mode!=ZZ_P96_COPY_BURST32)return -1;
  if(dst_pitch < width*2u || src_pitch < width*2u)return -1;
  burst=(mode==ZZ_P96_COPY_BURST32 && !(width&15u) &&
         !(((u32)(unsigned long)dst | (u32)(unsigned long)src |
            dst_pitch | src_pitch)&31u));
  for(y=0;y<height;y++){
    if(burst)copy32_burst(dst,src,width/16u,be);
    else for(x=0;x<words;x++){
      u32 v=src[x];dst[x]=be?swap565(v):v;
    }
    dst=(volatile u32 *)((volatile zu8 *)dst+dst_pitch);
    src=(const volatile u32 *)((const volatile zu8 *)src+src_pitch);
  }
  return burst;
}
