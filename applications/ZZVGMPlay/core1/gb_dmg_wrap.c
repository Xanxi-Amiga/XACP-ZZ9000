#include <stdint.h>
#include "mamedef.h"
#include "gb.h"
#include "gb_dmg_wrap.h"

/* Legacy GB core lets the host force its render rate through these globals. */
UINT8 CHIP_SAMPLING_MODE = 0x02;
INT32 CHIP_SAMPLE_RATE = 44100;
UINT32 SampleRate = 44100;
stream_sample_t* DUMMYBUF[2] = { 0, 0 };

static stream_sample_t g_l[256];
static stream_sample_t g_r[256];

static int16_t sat16_gb(INT32 x)
{
    if (x > 32767) x = 32767;
    else if (x < -32768) x = -32768;
    return (int16_t)x;
}

int zz_gb_dmg_init(uint32_t clock_raw, uint32_t rate)
{
    CHIP_SAMPLE_RATE = (INT32)rate;
    SampleRate = rate;
    if (!device_start_gameboy_sound(0, (int)clock_raw))
        return 0;
    device_reset_gameboy_sound(0);
    return 1;
}

void zz_gb_dmg_write(uint8_t reg, uint8_t data)
{
    /* bit 7 selects the second chip in VGM; A0r4h supports chip #0 only. */
    if (reg & 0x80) return;
    gb_sound_w(0, reg & 0x7f, data);
}

void zz_gb_dmg_render(int16_t *stereo, uint32_t frames)
{
    stream_sample_t *out[2] = { g_l, g_r };
    uint32_t i;
    if (frames > 256) frames = 256;
    gameboy_update(0, out, (int)frames);
    for (i = 0; i < frames; ++i) {
        stereo[i * 2 + 0] = sat16_gb(g_l[i]);
        stereo[i * 2 + 1] = sat16_gb(g_r[i]);
    }
}
