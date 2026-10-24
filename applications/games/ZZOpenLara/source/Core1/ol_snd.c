/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
/* ZZOpenLara A2.5: the PCM ring of the ZZQuake640 release, written by Core1.
 *
 * Same channel and same rules as ZZQuake2 (tgl/Core1/zzq2_snd.c, SNDDMA_Submit), nothing new on the
 * 68k side:
 *   - the launcher enables the channel (SH_PCM_ENABLE = 1 unless NOSOUND is given) and plays the
 *     ring with its AHI process;
 *   - ring at ZZQ_PCM_ARM, ZZQ_PCM_RING_SIZE bytes, 16-bit stereo little endian at ZZQ_PCM_RATE
 *     (22050 Hz); write position SH_PCM_WRITE_POS (Core1), read position SH_PCM_READ_POS (68k),
 *     both in bytes; Core1 fills up to ZZQ_PCM_TARGET_FILL bytes ahead of the reader.
 * The engine's mixer (Sound::fill, 44100 Hz) is called by Core1/ol_game.cpp, which halves the rate;
 * this file only knows the ring. SH_PCM_ENABLE is left to the launcher, as in ZZQuake2.
 * ASCII only.
 */
#include "zzquake_config.h"

typedef unsigned int u32;

extern volatile u32 *shared;

static u32 ring_wpos;
static int ring_on = -1;
static u32 ring_fill = ZZQ_PCM_TARGET_FILL;   /* A2.21.2: deeper while a film plays (ol_pcm_set_fill) */

/* A2.21.2: the advance kept in the ring, bytes (16 KiB in game, more during a film: Core1 may stall on the
   disk for a few hundred ms and the sound must not break) */
void ol_pcm_set_fill(unsigned bytes)
{
    if (bytes < ZZQ_PCM_TARGET_FILL) bytes = ZZQ_PCM_TARGET_FILL;
    if (bytes > ZZQ_PCM_RING_SIZE - 16384u) bytes = ZZQ_PCM_RING_SIZE - 16384u;
    ring_fill = bytes & ~3u;
}

/* 1 when the launcher enabled the channel; the write position continues where the ring is */
int ol_pcm_open(void)
{
    ring_on = shared[SH_PCM_ENABLE] ? 1 : 0;
    ring_wpos = shared[SH_PCM_WRITE_POS] & ~3u;
    if (ring_wpos >= ZZQ_PCM_RING_SIZE) ring_wpos = 0;
    return ring_on;
}

/* frames (22050 Hz, stereo) that can be written now without going past the fill target */
int ol_pcm_room(void)
{
    u32 rd, used;
    if (ring_on <= 0 || !shared[SH_PCM_ENABLE]) return 0;
    rd = shared[SH_PCM_READ_POS];
    if (rd >= ZZQ_PCM_RING_SIZE) return 0;
    used = (ring_wpos >= rd) ? (ring_wpos - rd) : (ZZQ_PCM_RING_SIZE - rd + ring_wpos);
    if (used >= ring_fill) return 0;
    return (int)((ring_fill - used) / 4u);
}

/* A2.21.2: 1 when the sound runs (the film's images may follow its sound) */
int ol_pcm_active(void) { return ring_on > 0 && shared[SH_PCM_ENABLE] ? 1 : 0; }

/* A2.21.2: frames written and not yet taken by the 68k (what is still to be heard, roughly) */
unsigned ol_pcm_queued(void)
{
    u32 rd, used;
    if (ring_on <= 0 || !shared[SH_PCM_ENABLE]) return 0;
    rd = shared[SH_PCM_READ_POS];
    if (rd >= ZZQ_PCM_RING_SIZE) return 0;
    used = (ring_wpos >= rd) ? (ring_wpos - rd) : (ZZQ_PCM_RING_SIZE - rd + ring_wpos);
    return used / 4u;
}

/* lr: n frames of interleaved left/right 16-bit samples */
void ol_pcm_write(const short *lr, int n)
{
    volatile unsigned char *ring = (volatile unsigned char *)ZZQ_PCM_ARM;
    int i;
    if (ring_on <= 0 || n <= 0) return;
    for (i = 0; i < n; i++) {
        short l = lr[2 * i], r = lr[2 * i + 1];
        ring[ring_wpos + 0] = (unsigned char)(l & 0xFF);
        ring[ring_wpos + 1] = (unsigned char)((l >> 8) & 0xFF);
        ring[ring_wpos + 2] = (unsigned char)(r & 0xFF);
        ring[ring_wpos + 3] = (unsigned char)((r >> 8) & 0xFF);
        ring_wpos += 4;
        if (ring_wpos >= ZZQ_PCM_RING_SIZE) ring_wpos = 0;
    }
    __asm__ volatile("dsb sy" ::: "memory");
    shared[SH_PCM_WRITE_POS] = ring_wpos;
}

/* launcher statistics of the 68k reader (underruns), for the Core1 summary */
unsigned ol_pcm_underruns(void)
{
    return shared[SH_PCM_UNDERRUNS];
}
