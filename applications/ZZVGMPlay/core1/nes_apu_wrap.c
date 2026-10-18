#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "mamedef.h"
#include "nes_apu.h"
#include "nes_apu_wrap.h"

static uint8_t g_nes_pool[64 * 1024] __attribute__((aligned(64)));
static uint8_t g_nes_ram[64 * 1024] __attribute__((aligned(64)));
static void *g_nes;
static stream_sample_t g_l[256];
static stream_sample_t g_r[256];
static uint32_t g_pool_used;

void zz_nes_alloc_reset(void) { g_pool_used = 0; }
void *zz_nes_malloc(size_t n)
{
    uint32_t p = (g_pool_used + 63u) & ~63u;
    if (p + n > sizeof(g_nes_pool)) return 0;
    g_pool_used = p + n;
    memset(g_nes_pool + p, 0, n);
    return g_nes_pool + p;
}

static int16_t sat16_nes(INT32 x)
{
    if (x > 32767) x = 32767;
    else if (x < -32768) x = -32768;
    return (int16_t)x;
}

int zz_nes_apu_init(uint32_t clock, uint32_t rate)
{
    zz_nes_alloc_reset();
    memset(g_nes_ram, 0, sizeof(g_nes_ram));
    g_nes = device_start_nesapu((int)(clock & 0x3fffffffu), (int)rate);
    if (!g_nes) return 0;
    device_reset_nesapu(g_nes);
    nesapu_set_rom(g_nes, g_nes_ram);
    return 1;
}

void zz_nes_apu_write(uint8_t reg, uint8_t data)
{
    if (!g_nes || (reg & 0x80)) return;
    nes_psg_w(g_nes, (offs_t)(reg & 0x7f), data);
}

void zz_nes_apu_ram_write(uint16_t addr, const uint8_t *src, uint32_t len)
{
    uint32_t n;
    if (!src) return;
    n = sizeof(g_nes_ram) - addr;
    if (len < n) n = len;
    memcpy(g_nes_ram + addr, src, n);
}

void zz_nes_apu_render(int16_t *stereo, uint32_t frames)
{
    stream_sample_t *out[2] = { g_l, g_r };
    uint32_t i;
    if (!g_nes) return;
    if (frames > 256) frames = 256;
    nes_psg_update_sound(g_nes, out, (int)frames);
    for (i = 0; i < frames; ++i) {
        stereo[i * 2 + 0] = sat16_nes(g_l[i]);
        stereo[i * 2 + 1] = sat16_nes(g_r[i]);
    }
}
