#ifndef ZZ_K053260_H
#define ZZ_K053260_H
#include <stdint.h>
/* Konami KDSC K053260 standalone bare-metal emulation, inspired by
   Ernesto Corvi / Alex W. Jackson MAME BSD-3-Clause implementation.
   Original implementation here, no external dependencies. */
#define ZZ_K053260_ROM_MAX (2u*1024u*1024u)
typedef struct {
 uint32_t pos,start; uint16_t counter,length,pitch;
 int32_t pan_gain[2]; int8_t output;
 uint8_t playing,volume,pan,loop,adpcm,reverse;
} zz_k053260_voice_t;
typedef struct {
 zz_k053260_voice_t voice[4];
 uint8_t *rom; uint32_t rom_size,rom_mask,clock,rate;
 uint8_t keyon,mode,ports[4];
 uint64_t cycle_frac;
} zz_k053260_t;
void zz_k053260_init(zz_k053260_t *c,uint32_t clock,uint32_t rate);
void zz_k053260_write(zz_k053260_t *c,uint8_t address,uint8_t value);
int zz_k053260_rom(zz_k053260_t *c,uint32_t rom_size,uint32_t offset,
                   const uint8_t *data,uint32_t size,
                   uint8_t *(*alloc_rom)(uint32_t size,uint8_t fill));
void zz_k053260_render(zz_k053260_t *c,int16_t *pcm,uint32_t frames);
#endif
