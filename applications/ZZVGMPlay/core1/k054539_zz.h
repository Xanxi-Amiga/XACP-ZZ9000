/* ZZVGMPlay A07 K054539 -- based on BSD-3-Clause MAME/libvgm reference
 * Copyright (c) 2026 Xanxi (integration), Olivier Galibert (reference).
 * The source-compatible interpretation of hardware behavior is documented
 * in README_A07_K054539.md. */
#ifndef ZZ_K054539_H
#define ZZ_K054539_H
#include <stdint.h>
#define ZZ_K054539_ROM_MAX (64u*1024u*1024u)
typedef struct {uint32_t pos;int32_t frac;int32_t value,prev;} zz_k054539_voice_t;
typedef struct {
 uint8_t regs[0x230],latch[8][3];
 /* Reverb RAM is 32 KiB, 16-bit signed native little endian on Cortex A9. */
 int16_t revram[16384];
 uint16_t revpos;
 uint32_t rom_size,rom_mask,rom_addr,cur_ptr;
 uint8_t *rom;
 uint32_t clock,rate;
 /* Ratio and two native PCM frames for linear resampling. */
 uint64_t resamp_phase;
 int32_t prev_l,prev_r,next_l,next_r;
 uint8_t primed;
 uint16_t vol_q15[256],pan_q15[15];
 zz_k054539_voice_t voice[8];
} zz_k054539_t;
void zz_k054539_init(zz_k054539_t *s,uint32_t clock,uint32_t rate);
void zz_k054539_write(zz_k054539_t *s,uint16_t reg,uint8_t data);
int zz_k054539_rom(zz_k054539_t *s,uint32_t size,uint32_t offs,
                   const uint8_t *data,uint32_t len,
                   uint8_t *(*allocate)(uint32_t size,uint8_t fill));
void zz_k054539_render(zz_k054539_t *s,int16_t *dst,uint32_t frames);
#endif
