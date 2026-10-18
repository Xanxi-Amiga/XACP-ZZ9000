#ifndef ZZ_K051649_ZZ_H
#define ZZ_K051649_ZZ_H
/* Konami K051649 SCC / K052539 SCC+ sound generator.
 * Adapted from the 5-voice algorithm in MAME/libvgm k051649.c,
 * BSD-3-Clause, copyright Bryan McPhail, with integer-rate PCM output.
 * Wave table memory mapper is not necessary for VGM command playback.
 */
#include <stdint.h>
#define ZZ_SCC_VOICES 5u
#define ZZ_SCC_WAVE_LEN 32u
typedef struct {
    uint32_t phase;
    uint32_t inc;
    uint16_t freq;
    uint8_t vol;
    uint8_t key;
    int8_t wave[ZZ_SCC_WAVE_LEN];
} zz_scc_voice;
typedef struct {
    zz_scc_voice voice[ZZ_SCC_VOICES];
    uint32_t clock_hz;
    uint32_t rate_hz;
    uint8_t scc_plus;
    uint8_t test_reg;
} zz_scc;
void zz_scc_init(zz_scc *s, uint32_t clock_hz, uint32_t rate_hz, int scc_plus);
void zz_scc_write(zz_scc *s, uint8_t port, uint8_t offset, uint8_t data);
void zz_scc_render(zz_scc *s, int16_t *pcm_stereo, uint32_t frames);
#endif
