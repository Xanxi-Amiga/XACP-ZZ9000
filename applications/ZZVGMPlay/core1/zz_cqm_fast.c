/* ZZVGMPlay -- a bit-exact replacement for the two per-frame soft divisions in
   Nuked-CQM's CQM_GenerateResampled, targeted at Cortex-A9 (no HW UDIV/SDIV).
   Does not alter CQM_Generate: 49716 actual native frames/s stay intact.
   Signed integer divisions round toward zero exactly as in upstream.
   Unsigned q = floor(a/d) via fixed reciprocal + one correction.
   For 0<=a<=INT32_MAX and 1<=d<=1024, floor(2^32/d) reciprocal gives a
   quotient which is exact or one less, never more; corrected by remainder.
   Frequency/remainder ownership is the unchanged upstream cqm_t.
   License: MIT-0; no changes to the Nuked-CQM LGPL upstream files.
 */
#include <stdint.h>
#include "zz_cqm_fast.h"

static uint32_t zz_cqm_divisor;
static uint32_t zz_cqm_reciprocal;

void zz_cqm_fast_reset(const cqm_t *c)
{
    uint32_t r = (c->rateratio > 0) ? (uint32_t)c->rateratio : 0u;
    /* Safely use fallback for unexpected output/native sample ratios. */
    if(r >= 1u && r <= 1024u) {
        zz_cqm_divisor = r;
        zz_cqm_reciprocal = (uint32_t)(0x100000000ULL / r);
    } else {
        zz_cqm_divisor = 0u;
        zz_cqm_reciprocal = 0u;
    }
}

static inline int32_t zz_cqm_divide_fast(int32_t a)
{
    uint32_t neg = (a < 0);
    uint32_t n = neg ? (uint32_t)(-(int64_t)a) : (uint32_t)a;
    uint32_t q = (uint32_t)(((uint64_t)n * zz_cqm_reciprocal) >> 32);
    uint32_t r = n - q * zz_cqm_divisor;
    if(r >= zz_cqm_divisor) ++q;
    return neg ? -(int32_t)q : (int32_t)q;
}

void zz_cqm_generate_fast(cqm_t *c,int16_t *out,uint32_t count)
{
    uint32_t i;
    int32_t denom = c->rateratio;
    if(!zz_cqm_divisor || denom != (int32_t)zz_cqm_divisor) {
        CQM_GenerateStream(c,out,count);
        return;
    }
    for(i=0;i<count;++i) {
        int32_t p,a,b;
        while(c->samplecnt >= denom) {
            c->oldsamples[0]=c->samples[0];
            c->oldsamples[1]=c->samples[1];
            CQM_Generate(c,c->samples);
            c->samplecnt-=denom;
        }
        p=c->samplecnt;
        a=(int32_t)c->oldsamples[0]*(denom-p)+(int32_t)c->samples[0]*p;
        b=(int32_t)c->oldsamples[1]*(denom-p)+(int32_t)c->samples[1]*p;
        out[2u*i]=(int16_t)zz_cqm_divide_fast(a);
        out[2u*i+1u]=(int16_t)zz_cqm_divide_fast(b);
        c->samplecnt+=1 << 10;
    }
}
