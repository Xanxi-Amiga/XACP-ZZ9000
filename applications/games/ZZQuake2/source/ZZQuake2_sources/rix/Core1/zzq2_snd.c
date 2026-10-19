/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2_snd.c - ZZQuake2: the SNDDMA_* driver of Quake II on the PCM
 * channel of the ZZQuake640 release.
 *
 * Replaces sound/snddma_null.c of quake2generic (not linked). The
 * mixing stays the portable C code of Quake II (client/snd_dma.c,
 * snd_mix.c, snd_mem.c, unmodified).
 *
 * The release already has everything else and nothing of it changes:
 *   - the launcher enables the channel (SH_PCM_ENABLE = 1 unless the
 *     NOSOUND option is given) and plays the ring with its AHI process;
 *   - the ring: ZZQ_PCM_ARM, ZZQ_PCM_RING_SIZE bytes, 16-bit stereo
 *     little endian at ZZQ_PCM_RATE (22050 Hz), write position in
 *     SH_PCM_WRITE_POS (Core1), read position in SH_PCM_READ_POS (68k),
 *     both in bytes;
 *   - the fill target ZZQ_PCM_TARGET_FILL.
 * The logic below is the one of the release driver (snd_zz9000.c of
 * ZZQuake640, Quake I), written against the Quake II names: dma
 * instead of shm, SNDDMA_Init returning qboolean, SNDDMA_BeginPainting.
 *
 *   SNDDMA_Init      fails (no sound, Quake II carries on silently)
 *                    when the launcher did not enable the channel.
 *   SNDDMA_GetDMAPos position of the 68k reader, in the DMA buffer
 *                    of Quake II.
 *   SNDDMA_Submit    copies what Quake II has mixed since the last
 *                    call into the ring, up to the fill target.
 *   SNDDMA_Shutdown  does not clear SH_PCM_ENABLE (see below).
 *
 * ASCII only.
 */

#include "client/client.h"
#include "client/snd_loc.h"
#include "zzquake_config.h"

extern volatile unsigned int *shared;

/* The DMA buffer of Quake II has as many frames as the ring (16384,
   a power of two as snd_mix.c needs): the read position of the 68k
   then maps to ONE position of the buffer. With a smaller buffer (the
   Quake I driver of the release has 4096 frames, a quarter of the
   ring), a reader that moves by a whole buffer between two calls is
   not seen to move at all; Quake II then stops mixing (found on the
   emulator, 0.4). */
#define ZZQ2_DMA_FRAMES (ZZQ_PCM_RING_SIZE / 4u)
typedef char zzq2_dma_frames_pow2[(ZZQ2_DMA_FRAMES & (ZZQ2_DMA_FRAMES - 1u)) == 0 ? 1 : -1];
static short zzq2_dma_buf[ZZQ2_DMA_FRAMES * 2];

static unsigned int zzq2_copied_frames;
static unsigned int zzq2_ring_wpos;

qboolean SNDDMA_Init(void)
{
    if (!shared[SH_PCM_ENABLE])
        return false;

    memset((void *)&dma, 0, sizeof(dma));
    dma.channels         = 2;
    dma.samplebits       = 16;
    dma.speed            = (int)ZZQ_PCM_RATE;
    dma.samples          = ZZQ2_DMA_FRAMES * 2;    /* mono samples */
    dma.submission_chunk = 1;
    dma.buffer           = (byte *)zzq2_dma_buf;
    dma.samplepos        = 0;

    memset(zzq2_dma_buf, 0, sizeof(zzq2_dma_buf));
    zzq2_copied_frames = 0;           /* S_Init sets paintedtime to 0 */
    /* Continue the ring where this driver left it (0 at the first
       call): "snd_restart" (also sent by the sound options of the
       menu) calls SNDDMA_Shutdown then SNDDMA_Init while the 68k keeps
       reading. */
    zzq2_ring_wpos = shared[SH_PCM_WRITE_POS] & ~3u;
    if (zzq2_ring_wpos >= ZZQ_PCM_RING_SIZE)
        zzq2_ring_wpos = 0;
    Com_Printf("ZZQuake2 sound: %d Hz, 16 bits, stereo (release PCM ring)\n", dma.speed);
    return true;
}

int SNDDMA_GetDMAPos(void)
{
    unsigned int rd = shared[SH_PCM_READ_POS];
    unsigned int consumed_frames = rd / 4u;
    dma.samplepos = (int)((consumed_frames * 2u) % (unsigned int)dma.samples);
    return dma.samplepos;
}

void SNDDMA_BeginPainting(void)
{
}

void SNDDMA_Submit(void)
{
    volatile unsigned char *ring = (volatile unsigned char *)ZZQ_PCM_ARM;
    unsigned int painted = (unsigned int)paintedtime;
    unsigned int rd, used, room_frames, n, i;

    if (!shared[SH_PCM_ENABLE])
        return;
    if (painted <= zzq2_copied_frames)
        return;

    rd = shared[SH_PCM_READ_POS];
    used = (zzq2_ring_wpos >= rd) ? (zzq2_ring_wpos - rd)
                                  : (ZZQ_PCM_RING_SIZE - rd + zzq2_ring_wpos);
    if (used >= ZZQ_PCM_TARGET_FILL)
        return;
    room_frames = (ZZQ_PCM_TARGET_FILL - used) / 4u;

    n = painted - zzq2_copied_frames;
    if (n > room_frames)
        n = room_frames;

    for (i = 0; i < n; i++) {
        unsigned int f = (zzq2_copied_frames + i) & (ZZQ2_DMA_FRAMES - 1u);
        short l = zzq2_dma_buf[f * 2];
        short r = zzq2_dma_buf[f * 2 + 1];
        ring[zzq2_ring_wpos + 0] = (unsigned char)(l & 0xFF);
        ring[zzq2_ring_wpos + 1] = (unsigned char)((l >> 8) & 0xFF);
        ring[zzq2_ring_wpos + 2] = (unsigned char)(r & 0xFF);
        ring[zzq2_ring_wpos + 3] = (unsigned char)((r >> 8) & 0xFF);
        zzq2_ring_wpos += 4;
        if (zzq2_ring_wpos >= ZZQ_PCM_RING_SIZE)
            zzq2_ring_wpos = 0;
    }
    zzq2_copied_frames += n;

    __asm__ volatile("dsb sy" ::: "memory");
    shared[SH_PCM_WRITE_POS] = zzq2_ring_wpos;
}

/* Unlike the Quake I driver of the release, SH_PCM_ENABLE is left to
   the launcher (it clears it itself at the end of the session): a
   "snd_restart" must find the channel still enabled. */
void SNDDMA_Shutdown(void)
{
}
