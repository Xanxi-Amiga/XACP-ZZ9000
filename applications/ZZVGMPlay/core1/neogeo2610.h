#ifndef ZZVGM_NEO2610_H
#define ZZVGM_NEO2610_H
#include <stdint.h>
#include "ay8910.h"
/* Experimental ADPCM-A/B + SSG layer for YM2610 VGM; FM remains a
   temporary mapping to the existing fast GENS core, NOT accurate YMFM. */
void zz_neo_init(uint32_t clock_hz,uint32_t output_hz);
int zz_neo_rom(uint8_t type,uint32_t rom_size,uint32_t dest,const uint8_t *data,uint32_t size);
void zz_neo_write(unsigned port,uint8_t reg,uint8_t value);
void zz_neo_render(int16_t *out_stereo,uint32_t frames);
int zz_neo_ssg_enabled(void);
void zz_neo_render_ssg(int16_t *out_stereo,uint32_t frames);
#endif
