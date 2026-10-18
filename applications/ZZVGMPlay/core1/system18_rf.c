/* Unmodified libvgm RF5C68; platform allocation only is redirected. */
#include <stdlib.h>
#include "system18_backend.h"
#ifndef ZZ_S18_REFERENCE
#define calloc zz_s18_calloc
#define malloc zz_s18_malloc
#define free zz_s18_free
#endif
#include "../third_party/libvgm-system18/emu/cores/rf5c68.c"
void *zz_s18_rf_create(uint32_t clock) {return device_start_rf5c68(clock);}
void zz_s18_rf_reset(void *p) {device_reset_rf5c68(p);}
void zz_s18_rf_reg(void *p,uint8_t r,uint8_t v) {rf5c68_w(p,r,v);}
void zz_s18_rf_mem(void *p,uint16_t a,uint8_t v) {rf5c68_mem_w(p,a,v);}
void zz_s18_rf_block(void *p,uint32_t a,uint32_t n,const uint8_t *d) {rf5c68_write_ram(p,a,n,d);}
void zz_s18_rf_update(void *p,uint32_t n,DEV_SMPL **b) {rf5c68_update(p,n,b);}
