/* ZZVGMPlay CQM fast sample-rate conversion -- public-domain integration.
   Preserves the native CQM_Generate() synthesizer and exact integer rounding.
   Requires the official, unmodified cqm.h from nukeykt/Nuked-CQM. */
#ifndef ZZ_CQM_FAST_H
#define ZZ_CQM_FAST_H
#include <stdint.h>
#include "cqm.h"
void zz_cqm_fast_reset(const cqm_t *c);
void zz_cqm_generate_fast(cqm_t *c,int16_t *out,uint32_t count);
#endif
