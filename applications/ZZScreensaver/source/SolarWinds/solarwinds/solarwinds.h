/* Solar Winds Amiga port, 2026 Xanxi. GPL-2.0-or-later. */
#ifndef ZZ_SOLARWINDS_H
#define ZZ_SOLARWINDS_H
#include <stdint.h>
#define SW_WIDTH 640
#define SW_HEIGHT 480
#define SW_MAX_WINDS 2
#define SW_MAX_EMITTERS 400
#define SW_MAX_PARTICLES 3000
typedef struct {
    int winds, emitters, particles, geometry, size;
    int wind_speed, emitter_speed, particle_speed, blur;
    const char *name;
} sw_preset;
extern const sw_preset sw_presets[6];
int sw_init(unsigned preset, uint32_t seed);
void sw_step(void);                      /* exactly one original 60 Hz update */
void sw_draw(float elapsed_seconds, int first_frame);
int sw_needs_history(void);
unsigned sw_active_particles(void);
unsigned sw_particle_count(void);
unsigned sw_emitter_count(void);
uint32_t sw_state_hash(void);             /* deterministic diagnostic, not a CRC */
const sw_preset *sw_current_preset(void);
#endif
