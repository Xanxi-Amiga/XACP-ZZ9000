/* YM2413/OPLL wrapper for ZZVGMPlay Core1.
 * Backend: digital-sound-antiques emu2413 v1.6.3 (MIT).
 */
#include <stdint.h>
#include "emu2413.h"
#include "emu2413_wrap.h"
#include "opll2413_alloc.h"

static OPLL *g_opll;

static int16_t sat16_local(int32_t x)
{
    if (x > 32767) x = 32767;
    else if (x < -32768) x = -32768;
    return (int16_t)x;
}

int zz_emu2413_init(uint32_t clock_hz, uint32_t rate_hz, int vrc7)
{
    if (!clock_hz || !rate_hz) return 0;
    if (g_opll) {
        OPLL_delete(g_opll);
        g_opll = 0;
    }
    zz_opll_pool_reset();
    g_opll = OPLL_new(clock_hz, rate_hz);
    if (!g_opll) return 0;
    if (vrc7) {
        OPLL_setChipType(g_opll, 1);
        OPLL_resetPatch(g_opll, OPLL_VRC7_TONE);
    } else {
        OPLL_setChipType(g_opll, 0);
        OPLL_resetPatch(g_opll, OPLL_2413_TONE);
    }
    return 1;
}

void zz_emu2413_write(uint8_t reg, uint8_t data)
{
    if (g_opll) OPLL_writeReg(g_opll, reg, data);
}

void zz_emu2413_render(int16_t *stereo, uint32_t frames)
{
    uint32_t i;
    if (!g_opll) {
        for (i = 0; i < frames * 2u; ++i) stereo[i] = 0;
        return;
    }
    for (i = 0; i < frames; ++i) {
        int32_t o[2];
        OPLL_calcStereo(g_opll, o);
        stereo[i * 2u + 0u] = sat16_local(o[0]);
        stereo[i * 2u + 1u] = sat16_local(o[1]);
    }
}
