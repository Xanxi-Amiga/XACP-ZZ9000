#ifndef ZZ_HUC6280_WRAP_H
#define ZZ_HUC6280_WRAP_H
#include <stdint.h>
#include <stddef.h>
int zz_huc6280_init(uint32_t clock_raw, uint32_t rate);
void zz_huc6280_write(uint8_t reg, uint8_t data);
void zz_huc6280_render(int16_t *stereo, uint32_t frames);
void *zz_pce_malloc(size_t n);
void zz_pce_free(void *p);
void zz_pce_alloc_reset(void);
#endif
