#ifndef ZZ_ES5503_H
#define ZZ_ES5503_H
#include <stdint.h>
#define ZZ_ES5503_RAM_SIZE 131072u
typedef struct {
 uint16_t freq; uint8_t vol,control,ptr,wtsize,resolution,data,irq;
 uint32_t accum;
 uint8_t bank;
} zz_es_osc_t;
typedef struct {
 uint8_t ram[ZZ_ES5503_RAM_SIZE];
 zz_es_osc_t osc[32];
 uint32_t clock,rate,output_rate,accum_rate;
 int16_t last_l,last_r,prev_l,prev_r;
 int32_t lp1_l,lp1_r,lp2_l,lp2_r;
 uint16_t lp_alpha;
 uint8_t primed;
 uint8_t enabled,outputs;
} zz_es5503_t;
void zz_es5503_init(zz_es5503_t *c,uint32_t clock,uint32_t rate,uint8_t outputs);
void zz_es5503_write(zz_es5503_t *c,uint16_t reg,uint8_t value);
int zz_es5503_ram(zz_es5503_t *c,uint32_t off,const uint8_t *data,uint32_t size);
void zz_es5503_render(zz_es5503_t *c,int16_t *out,uint32_t n);
#endif
