#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "mamedef.h"
#include "Ootake_PSG.h"
#include "huc6280_wrap.h"

static uint8_t g_pce_pool[64 * 1024] __attribute__((aligned(64)));
static uint32_t g_pce_pool_used;
static void *g_pce;
static Sint32 g_l[256];
static Sint32 g_r[256];

void zz_pce_alloc_reset(void) { g_pce_pool_used = 0; }
void *zz_pce_malloc(size_t n)
{
    uint32_t p = (g_pce_pool_used + 63u) & ~63u;
    if (p + n > sizeof(g_pce_pool)) return 0;
    g_pce_pool_used = p + n;
    memset(g_pce_pool + p, 0, n);
    return g_pce_pool + p;
}
void zz_pce_free(void *p) { (void)p; }

static int16_t sat16_pce(Sint32 x)
{
    if (x > 32767) x = 32767;
    else if (x < -32768) x = -32768;
    return (int16_t)x;
}

int zz_huc6280_init(uint32_t clock_raw, uint32_t rate)
{
    zz_pce_alloc_reset();
    g_pce = PSG_Init((Sint32)clock_raw, (Sint32)rate);
    return g_pce != 0;
}

void zz_huc6280_write(uint8_t reg, uint8_t data)
{
    if (!g_pce || (reg & 0x80)) return;
    PSG_Write(g_pce, reg & 0x0f, data);
}

void zz_huc6280_render(int16_t *stereo, uint32_t frames)
{
    Sint32 *out[2] = { g_l, g_r };
    uint32_t i;
    if (!g_pce) return;
    if (frames > 256) frames = 256;
    PSG_Mix(g_pce, out, (Sint32)frames);
    for (i = 0; i < frames; ++i) {
        stereo[i * 2 + 0] = sat16_pce(g_l[i]);
        stereo[i * 2 + 1] = sat16_pce(g_r[i]);
    }
}
