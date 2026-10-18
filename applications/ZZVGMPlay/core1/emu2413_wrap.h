#ifndef ZZVGM_EMU2413_WRAP_H
#define ZZVGM_EMU2413_WRAP_H
#include <stdint.h>
int  zz_emu2413_init(uint32_t clock_hz, uint32_t rate_hz, int vrc7);
void zz_emu2413_write(uint8_t reg, uint8_t data);
void zz_emu2413_render(int16_t *stereo, uint32_t frames);
#endif
