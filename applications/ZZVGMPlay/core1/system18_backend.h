#ifndef ZZ_SYSTEM18_BACKEND_H
#define ZZ_SYSTEM18_BACKEND_H
#include <stdint.h>
#include <stddef.h>
#define ZZ_S18_POOL_BYTES (256u * 1024u)
int zz_s18_init(uint32_t fm_clock, unsigned fm_count, uint32_t pcm_clock,
               uint32_t output_rate, void *pool, size_t pool_bytes);
void zz_s18_fm_write(unsigned chip, unsigned port, uint8_t reg, uint8_t value);
void zz_s18_rf_write(uint8_t reg, uint8_t value);
void zz_s18_rf_byte(uint16_t offset, uint8_t value);
void zz_s18_rf_ram(uint32_t offset, const uint8_t *data, uint32_t bytes);
uint32_t zz_s18_rf_bank(void);
void zz_s18_render(int32_t *fm0, int32_t *fm1, int32_t *pcm, uint32_t frames);
size_t zz_s18_pool_used(void);
/* Allocator used only by the imported System 18 translation units. */
void *zz_s18_malloc(size_t bytes);
void *zz_s18_calloc(size_t count, size_t bytes);
void zz_s18_free(void *ptr);
#endif
