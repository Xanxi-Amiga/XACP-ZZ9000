#ifndef ZZVGM_AY2149_REF_H
#define ZZVGM_AY2149_REF_H
/* Mono AY/YM engine based on emu2149 v1.42 (Mitsutaka Okazaki, MIT).
 * Reuse of the envelope/noise/tone/volume algorithms of that reference.
 * Adaptation: static state and area-weighted downsampler for Cortex-A9.
 * https://github.com/digital-sound-antiques/emu2149
 */
#include <stdint.h>
typedef struct {
    uint8_t reg[16];
    uint16_t count[3],freq[3];
    uint8_t volume[3],edge[3],tmask[3],nmask[3];
    uint8_t env_ptr,env_face,env_continue,env_alternate,env_hold,env_pause;
    uint8_t noise_scaler,noise_count,noise_freq;
    uint32_t env_count,env_freq,noise_seed;
    uint32_t native_rate,output_rate,sub_units;
    int16_t native_value;
    uint8_t is_ym;
} zz_ay2149_t;
void zz_ay2149_reset(zz_ay2149_t *p,uint32_t clock,uint32_t rate,uint8_t chip_type,uint8_t chip_flags);
void zz_ay2149_write(zz_ay2149_t *p,uint8_t reg,uint8_t data);
void zz_ay2149_render(zz_ay2149_t *p,int16_t *stereo,uint32_t count);
#endif
