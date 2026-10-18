/* ZZVGMPlay A0r4e - bare-metal wrapper around Game_Music_Emu GENS YM2612.
 * The upstream GENS core is LGPL-2.1+ and is downloaded separately by the
 * supplied PowerShell helper.  This wrapper deliberately avoids a general
 * heap: upstream performs exactly one malloc() for its private state, which
 * is redirected to the static storage below at compile time.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <new>
#include "Ym2612_GENS.h"
#include "gens2612_wrap.h"

/* Generous one-shot storage for Ym2612_GENS_Impl.  It contains the FM tables
 * as well as chip state.  Keeping it in BSS costs no blob file bytes and
 * avoids dragging Newlib malloc/sbrk into the Core1 payload. */
static unsigned char g_gens_heap[512u * 1024u] __attribute__((aligned(64)));
static int g_gens_heap_used;

extern "C" void *zz_gens_malloc(size_t n)
{
    if (g_gens_heap_used || n > sizeof(g_gens_heap)) return 0;
    g_gens_heap_used = 1;
    return (void *)g_gens_heap;
}

extern "C" void zz_gens_free(void *p)
{
    (void)p;
    /* Core1 keeps the emulator resident for the payload lifetime.  Do not
     * recycle the arena: set_rate() reuses the already allocated impl. */
}

static unsigned char g_chip_storage[sizeof(Ym2612_GENS_Emu)] __attribute__((aligned(16)));
static Ym2612_GENS_Emu *g_chip;

static Ym2612_GENS_Emu *chip(void)
{
    if (!g_chip)
        g_chip = new ((void *)g_chip_storage) Ym2612_GENS_Emu();
    return g_chip;
}

extern "C" int zz_gens2612_init(uint32_t clock_hz, uint32_t sample_rate)
{
    Ym2612_GENS_Emu *c = chip();
    const char *err = c->set_rate((double)sample_rate, (double)clock_hz);
    if (err) return 0;
    c->reset();
    c->mute_voices(0);
    return 1;
}

extern "C" void zz_gens2612_reset(void)
{
    if (g_chip) g_chip->reset();
}

extern "C" void zz_gens2612_write(unsigned port, uint8_t addr, uint8_t data)
{
    Ym2612_GENS_Emu *c = chip();
    if (port & 1u) c->write1((int)addr, (int)data);
    else           c->write0((int)addr, (int)data);
}

extern "C" void zz_gens2612_render(int16_t *out_interleaved, uint32_t frames)
{
    if (!frames) return;
    memset(out_interleaved, 0, (size_t)frames * 2u * sizeof(int16_t));
    chip()->run((int)frames, (Ym2612_GENS_Emu::sample_t *)out_interleaved);
}
