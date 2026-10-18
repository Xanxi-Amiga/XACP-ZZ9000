#ifndef ZZVGM_QSOUND_PCM_H
#define ZZVGM_QSOUND_PCM_H
#include <stdint.h>
void zz_qsound_init(uint32_t clock_hz, uint32_t output_hz);
int zz_qsound_rom(uint32_t size, uint32_t off, const uint8_t *src, uint32_t len);
void zz_qsound_write(uint8_t reg, uint16_t value);
void zz_qsound_render(int16_t *stereo, uint32_t frames);
#endif
