/* ZZVGMPlay RF5C68 (c) Xanxi 2026. Behavior based on MAME/libvgm
 * rf5c68.c (c) Olivier Galibert, Aaron Giles, BSD-3-Clause. */
#ifndef ZZ_RF5C68_H
#define ZZ_RF5C68_H
#include <stdint.h>
#define ZZ_RF5C68_RAM_SIZE 65536u
typedef struct {
    uint8_t enabled,env,pan,start;
    uint16_t step,loop;
    uint32_t addr;
} zz_rf5c68_voice_t;
typedef struct {
    uint8_t ram[ZZ_RF5C68_RAM_SIZE];
    zz_rf5c68_voice_t v[8];
    uint8_t enabled,channel_bank,memory_bank;
    uint32_t native_rate,sample_rate,tick_phase;
    int32_t left,right;
} zz_rf5c68_t;
void zz_rf5c68_reset(zz_rf5c68_t *s,uint32_t clock,uint32_t sample_rate);
void zz_rf5c68_write(zz_rf5c68_t *s,uint8_t reg,uint8_t val);
void zz_rf5c68_ram_byte(zz_rf5c68_t *s,uint16_t banked_offset,uint8_t val);
void zz_rf5c68_ram_block(zz_rf5c68_t *s,uint32_t offset,const uint8_t *src,uint32_t count);
void zz_rf5c68_render(zz_rf5c68_t *s,int16_t *out,uint32_t frames);
/* Full 32-bit per-voice accumulator, for the final multichip mixer. */
void zz_rf5c68_render32(zz_rf5c68_t *s,int32_t *out,uint32_t frames);
#endif
