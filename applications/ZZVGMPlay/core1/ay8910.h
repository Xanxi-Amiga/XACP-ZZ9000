#ifndef ZZVGM_AY8910_H
#define ZZVGM_AY8910_H
#include <stdint.h>
typedef struct {
    uint32_t clock,rate;
    uint8_t reg[16],stereo;
    uint32_t phase[3],inc[3],noise_phase,noise_inc,env_phase,env_inc;
    uint32_t lfsr;
    int env_step,env_dir,env_hold,env_alt,env_attack,env_continue,env_holding;
} zz_ay8910_t;
void zz_ay8910_reset(zz_ay8910_t *a,uint32_t clock,uint32_t rate);
void zz_ay8910_write(zz_ay8910_t *a,uint8_t reg,uint8_t data);
void zz_ay8910_stereo(zz_ay8910_t *a,uint8_t mask);
void zz_ay8910_render(zz_ay8910_t *a,int16_t *stereo,uint32_t frames);
#endif
