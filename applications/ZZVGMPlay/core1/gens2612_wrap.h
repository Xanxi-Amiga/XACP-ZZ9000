#ifndef ZZ_GENS2612_WRAP_H
#define ZZ_GENS2612_WRAP_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int  zz_gens2612_init(uint32_t clock_hz, uint32_t sample_rate);
void zz_gens2612_reset(void);
void zz_gens2612_write(unsigned port, uint8_t addr, uint8_t data);
void zz_gens2612_render(int16_t *out_interleaved, uint32_t frames);
#ifdef __cplusplus
}
#endif
#endif
