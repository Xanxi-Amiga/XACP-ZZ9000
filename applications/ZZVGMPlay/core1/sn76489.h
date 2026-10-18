#ifndef ZZVGM_SN76489_H
#define ZZVGM_SN76489_H
#include <stdint.h>
typedef struct {
    uint32_t clock, rate;
    uint16_t tone[3];
    uint8_t vol[4], latched, noise_ctrl, stereo;
    uint32_t phase[3], noise_phase;
    uint32_t inc[3], noise_inc;
    uint16_t lfsr, feedback, width_mask;
} zz_sn76489_t;
void zz_sn76489_reset(zz_sn76489_t *s, uint32_t clock, uint32_t rate, uint16_t feedback, uint8_t shift_width);
void zz_sn76489_write(zz_sn76489_t *s, uint8_t data);
void zz_sn76489_stereo(zz_sn76489_t *s, uint8_t mask);
void zz_sn76489_render(zz_sn76489_t *s, int16_t *stereo, uint32_t frames);
#endif
