/* Fast YM2151 wrapper for ZZVGMPlay.
 * Core: Jarek Burczynski YM2151, same backend validated in ZZRastan.
 * No Z80/IRQ/timer callbacks are needed for VGM playback; the core advances
 * its internal timers from generated samples when timer_cb == NULL.
 */
#include <stdint.h>
#include "ym2151.h"
#include "jarek2151_wrap.h"

static int g_inited;
static uint32_t g_clock, g_rate;
static INT16 g_l[256], g_r[256];

int zz_jarek2151_init(uint32_t clock_hz, uint32_t rate_hz)
{
    if (!clock_hz || !rate_hz) return 0;
    if (g_inited && (g_clock != clock_hz || g_rate != rate_hz)) {
        YM2151Shutdown();
        g_inited = 0;
    }
    if (!g_inited) {
        if (YM2151Init(1, 0, (int)clock_hz, (int)rate_hz, 0) != 0) return 0;
        g_inited = 1;
        g_clock = clock_hz;
        g_rate = rate_hz;
    } else {
        YM2151ResetChip(0);
    }
    return 1;
}

void zz_jarek2151_write(uint8_t reg, uint8_t data)
{
    YM2151WriteReg(0, reg, data);
}

void zz_jarek2151_render(int16_t *stereo, uint32_t frames)
{
    uint32_t done = 0;
    while (done < frames) {
        uint32_t n = frames - done, i;
        INT16 *b[2];
        if (n > 256u) n = 256u;
        b[0] = g_l; b[1] = g_r;
        YM2151UpdateOne(0, b, (int)n);
        for (i = 0; i < n; ++i) {
            stereo[(done + i) * 2u + 0u] = g_l[i];
            stereo[(done + i) * 2u + 1u] = g_r[i];
        }
        done += n;
    }
}
