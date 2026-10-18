#ifndef ZZVGM_ARCADE_PCM_H
#define ZZVGM_ARCADE_PCM_H
#include <stdint.h>
#include <stddef.h>
/* Experimental Core1 SegaPCM 315-5218 and OKI MSM6295 VGM backends.
 * Both chips use local copies of VGM ROM data in the private Core1 arena.
 * No allocations outside the region supplied to zz_arcade_init(). */
uint8_t *zz_arcade_alloc_rom(uint32_t size,uint8_t fill);
void zz_arcade_init(uint32_t sample_rate,uint32_t sega_clock,uint32_t sega_interface,
                    uint32_t oki_clock,uint8_t oki_pin7,
                    uint8_t *arena_begin,uint8_t *arena_end);
int zz_arcade_rom_block(uint8_t type,uint32_t rom_size,uint32_t start,
                        const uint8_t *data,uint32_t size);
void zz_arcade_sega_write(uint16_t offset,uint8_t value);
void zz_arcade_oki_write(uint8_t reg,uint8_t value);
void zz_arcade_render(int16_t *sega_stereo,int16_t *oki_stereo,uint32_t frames);
#endif
