#ifndef ZZ_GB_DMG_WRAP_H
#define ZZ_GB_DMG_WRAP_H
#include <stdint.h>
int zz_gb_dmg_init(uint32_t clock_raw, uint32_t rate);
void zz_gb_dmg_write(uint8_t reg, uint8_t data);
void zz_gb_dmg_render(int16_t *stereo, uint32_t frames);
#endif
