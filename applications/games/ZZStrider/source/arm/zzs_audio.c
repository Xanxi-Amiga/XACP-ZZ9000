/*
 * zzs_audio.c - ZZStrider PCM ring transport (Core1 -> AHI on the 68k).
 *
 * This is the ZZRastan 1.0 transport (zzrastan_audio.c, "LAT1" state) with the test tone and
 * the native-rate decimator removed: the YM2151 backend and the MSM6295 already produce at the
 * ring rate, so the pump is a 1:1 copy from the FIFO filled on emulated time (zzs_sound.c).
 *
 * Ring layout: bytes, S16 little-endian mono, at ARM 0x05700000 (Non-Cacheable, no cache ops).
 * pcm_write (ARM) chases pcm_read (68k). The ring is NEVER used as a backlog: it is levelled to
 * a small cushion (ZZRastan measurements: an unbounded ring carried 420 ms of latency).
 *     TARGET 1024 bytes = 512 samples = 23.2 ms      MAX 1536 bytes = 34.8 ms
 *
 * ASCII only.
 */
#include "zzs.h"

#define ZZS_RING_TARGET_BYTES 1024u
#define ZZS_RING_MAX_BYTES    1536u
#define ZZS_PCM_MAX_CHUNK     1024u

static int     g_audio_ready;
static zzs_u64 g_ring_post_sum;
static zzs_u32 g_ring_post_n;

void zzs_audio_reset(void)
{
    ZZS_SH(ZZS_SH_PCM_BASE) = ZZS_ARM_PCM;
    ZZS_SH(ZZS_SH_PCM_SIZE) = ZZS_PCM_SIZE;
    ZZS_SH(ZZS_SH_PCM_WRITE) = 0;
    ZZS_SH(ZZS_SH_PCM_READ) = 0;
    ZZS_SH(ZZS_SH_PCM_RATE) = ZZS_PCM_RATE;
    ZZS_SH(ZZS_SH_PCM_UNDERRUNS) = 0;
    ZZS_SH(ZZS_SH_PCM_ENABLE) = 0;       /* the launcher sets 1 once AHI is up */
    g_ring_post_sum = 0;
    g_ring_post_n = 0;
    g_audio_ready = 1;
}

static zzs_u32 g_ring_used;      /* bytes queued in the ring at the last pump */
static int     g_ahi_started;    /* AHI moved PCM_READ since PCM was enabled */
static zzs_u32 g_rd_at_enable = 0xFFFFFFFFu;

int zzs_audio_ahi_started(void) { return g_ahi_started; }
static zzs_u32 g_pump_calls;

zzs_u32 zzs_audio_ring_used(void) { return g_ring_used; }

/* FIFO -> ring. Called all through the frame: after every 68000 slice, during the picture
   conversion and from the pacer wait loop (about 2000 times a second), so that the ring is
   topped up within a millisecond of every AHI pull whatever the frame is doing. */
void zzs_audio_pump(void)
{
    volatile zzs_u8 *ring = (volatile zzs_u8 *)ZZS_ARM_PCM;
    zzs_u32 wr, rd, used, space, produce, fifo_bytes, i;

    if (!g_audio_ready) return;
    zzs_vlock_poll();                    /* cheap NC read, called ~2000 times a second */
    if (!ZZS_SH(ZZS_SH_PCM_ENABLE)) { g_ring_used = 0; g_ahi_started = 0; g_rd_at_enable = 0xFFFFFFFFu; return; }
    g_pump_calls++;

    wr = ZZS_SH(ZZS_SH_PCM_WRITE);       /* ARM's own value: one atomic store, no tear */
    /* pcm_read is written by the 68k in two 16-bit halves -> read until stable */
    {
        zzs_u32 a = ZZS_SH(ZZS_SH_PCM_READ), c; int tries = 4;
        while (tries--) { c = ZZS_SH(ZZS_SH_PCM_READ); if (c == a) break; a = c; }
        rd = a;
    }
    if (rd >= ZZS_PCM_SIZE || (rd & 1u) != 0u) return;      /* torn or foreign value */
    if (!g_ahi_started) {
        if (g_rd_at_enable == 0xFFFFFFFFu) g_rd_at_enable = rd;
        else if (rd != g_rd_at_enable) {
            g_ahi_started = 1;
            ZZS_SH(ZZS_SH_AHI_START_FRAME) = g_zzs_frame_no;
        }
    }
    if (wr >= ZZS_PCM_SIZE) wr = 0;

    used = (wr >= rd) ? (wr - rd) : (ZZS_PCM_SIZE - rd + wr);
    space = ZZS_PCM_SIZE - used - 2u;    /* 2-byte guard: wr never meets rd on a full ring */
    g_ring_used = used;

    if (used >= ZZS_RING_TARGET_BYTES) return;               /* enough queued: add nothing */
    fifo_bytes = ((g_fm_wr - g_fm_rd) & (ZZS_FM_FIFO_SAMPLES - 1u)) * 2u;
    produce = ZZS_RING_TARGET_BYTES - used;
    if (produce > fifo_bytes) produce = fifo_bytes;
    if (used + produce > ZZS_RING_MAX_BYTES) produce = ZZS_RING_MAX_BYTES - used;
    if (produce > space) produce = space;
    if (produce > ZZS_PCM_MAX_CHUNK) produce = ZZS_PCM_MAX_CHUNK;
    produce &= ~1u;
    if (produce == 0u) return;

    for (i = 0; i < produce; i += 2u) {
        zzs_s16 v = g_fm_fifo[g_fm_rd];
        g_fm_rd = (g_fm_rd + 1u) & (ZZS_FM_FIFO_SAMPLES - 1u);
        ring[wr]      = (zzs_u8)(v & 0xFF);
        ring[wr + 1u] = (zzs_u8)((v >> 8) & 0xFF);
        wr += 2u;
        if (wr >= ZZS_PCM_SIZE) wr = 0;
    }
    __asm__ volatile("dsb" ::: "memory");
    ZZS_SH(ZZS_SH_PCM_WRITE) = wr;

    {
        zzs_u32 post = (wr >= rd) ? (wr - rd) : (ZZS_PCM_SIZE - rd + wr);
        g_ring_used = post;
        ZZS_SH(ZZS_SH_PUMP_CALLS) = g_pump_calls;
        g_ring_post_sum += post;
        g_ring_post_n++;
        ZZS_SH(ZZS_SH_RING_POST_AVG) = (zzs_u32)(g_ring_post_sum / g_ring_post_n);
    }
}
