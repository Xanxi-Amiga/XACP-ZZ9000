#ifndef ZZ_NES_APU_WRAP_H
#define ZZ_NES_APU_WRAP_H
#include <stdint.h>
#include <stddef.h>
int zz_nes_apu_init(uint32_t clock, uint32_t rate);
void zz_nes_apu_write(uint8_t reg, uint8_t data);
void zz_nes_apu_ram_write(uint16_t addr, const uint8_t *src, uint32_t len);
void zz_nes_apu_render(int16_t *stereo, uint32_t frames);
void *zz_nes_malloc(size_t n);
void zz_nes_alloc_reset(void);
#endif
