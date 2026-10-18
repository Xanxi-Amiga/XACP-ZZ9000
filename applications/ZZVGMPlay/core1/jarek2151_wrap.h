#ifndef ZZVGM_JAREK2151_WRAP_H
#define ZZVGM_JAREK2151_WRAP_H
#include <stdint.h>
int  zz_jarek2151_init(uint32_t clock_hz, uint32_t rate_hz);
void zz_jarek2151_write(uint8_t reg, uint8_t data);
void zz_jarek2151_render(int16_t *stereo, uint32_t frames);
#endif
