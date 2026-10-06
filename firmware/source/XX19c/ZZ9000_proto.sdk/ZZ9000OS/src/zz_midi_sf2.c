/*
 * zz_midi_sf2.c -- XACP OP_MIDI_SF2 handler for ZZ9000 firmware
 *
 * Implements file playback, realtime MIDI, chunked SF2/MIDI uploads,
 * status reporting and PCM rendering.
 *
 * Depends on:
 * xil_cache.h -- Xil_DCacheInvalidateRange / Xil_DCacheFlushRange
 * xil_printf.h -- xil_printf
 * math.h -- sinf, sqrtf
 * string.h -- memset, memcpy
 * zz_midi_sf2.h -- types, constants, DDR layout
 * tsf.h -- TinySoundFont (single header)
 * tml.h -- TinyMidiLoader (single header)
 *
 * Endianness: shared DDR is big-endian.
 * All control block accesses via CTRL_RD / CTRL_WR.
 *
 * Ring PCM protocol (monotone counters):
 * available = pcm_write_total - pcm_read_total
 * free = XMID_PCM_RING_SIZE - available
 * write_pos = pcm_write_total % XMID_PCM_RING_SIZE
 * ARM only writes if free >= block_bytes.
 * Amiga increments pcm_read_total after consuming.
 *
 * ASCII-only source file -- no UTF-8 characters.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include "xil_cache.h"
#include "xil_mmu.h"
#include "xil_printf.h"
#include "xtime_l.h"

#include "zz_midi_sf2.h"

/* -----------------------------------------------------------------------
 * Big-endian helpers
 * --------------------------------------------------------------------- */
static inline uint32_t xmid_be32(uint32_t v)
{
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0xFF000000u) >> 24);
}
#define CTRL_RD(c, field)    xmid_be32(((volatile XMID_Ctrl *)(c))->field)
#define CTRL_WR(c, field, v) (((volatile XMID_Ctrl *)(c))->field = xmid_be32(v))

/* -----------------------------------------------------------------------
 * D-cache helpers
 * --------------------------------------------------------------------- */
static void xmid_invalidate(void *ptr, uint32_t len)
{
    uintptr_t s = ((uintptr_t)ptr) & ~0x1FUL;
    uintptr_t e = ((uintptr_t)ptr + (uintptr_t)len + 31UL) & ~0x1FUL;
    Xil_DCacheInvalidateRange(s, (uint32_t)(e - s));
}

static void xmid_flush(void *ptr, uint32_t len)
{
    uintptr_t s = ((uintptr_t)ptr) & ~0x1FUL;
    uintptr_t e = ((uintptr_t)ptr + (uintptr_t)len + 31UL) & ~0x1FUL;
    Xil_DCacheFlushRange(s, (uint32_t)(e - s));
}

static void xmid_invalidate_large(void *ptr, uint32_t len)
{
    uint8_t  *p   = (uint8_t *)ptr;
    uint32_t  rem = len;
    uint32_t  chunk;
    while (rem > 0u) {
        chunk = (rem > (1u * 1024u * 1024u)) ? (1u * 1024u * 1024u) : rem;
        xmid_invalidate(p, chunk);
        p   += chunk;
        rem -= chunk;
    }
}

/* -----------------------------------------------------------------------
 * Private heap (bump allocator, lives in DDR framebuffer)
 * --------------------------------------------------------------------- */
#define XMID_HEAP_GUARD  0xA110CA7Eu

#include <setjmp.h>

typedef struct {
    uint32_t size_bytes;
    uint32_t guard;
} XmidBlockHdr;

#define XMID_BLOCK_HDR_SZ  ((uint32_t)sizeof(XmidBlockHdr))

static uint8_t  *xmid_heap_base     = NULL;
static uint32_t  xmid_heap_ptr      = 0;
static uint32_t  xmid_heap_hw       = 0;
static int       xmid_heap_overflow = 0;

/* setjmp/longjmp OOM handler.
 * xmid_oom_env is set by xmid_tsf_load() and xmid_tml_load() before
 * calling tsf_load_memory() / tml_load_memory().
 * If xmid_malloc() detects heap overflow, it does longjmp(xmid_oom_env, 1)
 * which unwinds directly back to the caller, skipping all TSF/TML internals.
 * xmid_oom_active is 1 only during a load call -- outside a load, malloc
 * returns NULL normally (should not happen in normal flow). */
static jmp_buf xmid_oom_env;
static int     xmid_oom_active = 0;

void xmid_heap_set_base(void *fb_base)
{
    /* The heap lives in the ARM-private pool at XMID_HEAP_BASE_ABS. */
    (void)fb_base;

    /* Reject private-pool bases below the safe ARM allocation range. */
    if (XMID_PRIVATE_POOL_BASE_ABS < XMID_POOL_SAFE_BASE) {
        xil_printf("[xmid] FATAL: pool base 0x%08lx < safe base 0x%08lx\r\n",
                   (unsigned long)XMID_PRIVATE_POOL_BASE_ABS,
                   (unsigned long)XMID_POOL_SAFE_BASE);
        /* Halt -- do not proceed with unsafe pool */
        while (1) { asm volatile("wfe"); }
    }

    /* Force the safe private pool to cacheable Normal Write-Back.
 * ONLY 0x22000000-0x30000000. Do NOT touch 0x20000000-0x22000000
 * (that is AmigaOS Z3 RAM). */
    static int mmu_done = 0;
    if (!mmu_done) {
        uint32_t a;
        for (a = XACP_POOL_MMU_BASE; a < XACP_POOL_MMU_END; a += 0x100000u) {
            Xil_SetTlbAttributes((INTPTR)a, NORM_WB_CACHE);
        }
        mmu_done = 1;
        xil_printf("[xmid] MMU: pool 0x%08lx-0x%08lx NORM_WB_CACHE (XX18i)\r\n",
                   (unsigned long)XACP_POOL_MMU_BASE,
                   (unsigned long)XACP_POOL_MMU_END);
    }

    xmid_heap_base = (uint8_t *)XMID_HEAP_BASE_ABS;
}

void xmid_reset_heap(void)
{
    xmid_heap_ptr     = 0;
    xmid_heap_hw      = 0;
    xmid_heap_overflow = 0;
    xmid_oom_active   = 0;
}

/* Roll the bump allocator back to the post-SF2 mark before loading a new MIDI file. */
void xmid_heap_set_ptr(uint32_t mark)
{
    xmid_heap_ptr      = mark;
    if (xmid_heap_ptr > xmid_heap_hw) xmid_heap_hw = xmid_heap_ptr;
    xmid_heap_overflow = 0;
    xmid_oom_active    = 0;
}

uint32_t xmid_heap_used(void)         { return xmid_heap_ptr; }
uint32_t xmid_heap_highwater(void)    { return xmid_heap_hw;  }
int      xmid_heap_did_overflow(void) { return xmid_heap_overflow; }

void *xmid_malloc(size_t n)
{
    uint32_t      aligned, total;
    XmidBlockHdr *hdr;
    void         *user_ptr;

    if (!xmid_heap_base || n == 0u) return NULL;

    aligned = ((uint32_t)n + 7u) & ~7u;
    total   = aligned + XMID_BLOCK_HDR_SZ;

    if (xmid_heap_ptr + total > XMID_HEAP_SIZE) {
        xmid_heap_overflow = 1;
        xil_printf("[xmid] heap OVF want=%lu used=%lu/%lu\r\n",
                   (unsigned long)total,
                   (unsigned long)xmid_heap_ptr,
                   (unsigned long)XMID_HEAP_SIZE);
        if (xmid_oom_active) {
            /* longjmp back to xmid_tsf_load() or xmid_tml_load().
 * This unwinds TSF/TML call stack cleanly without any
 * corrupt pointer being written to DDR or BSS. */
            longjmp(xmid_oom_env, 1);
        }
        /* Outside a load call: return NULL (safe, should not occur) */
        return NULL;
    }

    hdr = (XmidBlockHdr *)(xmid_heap_base + xmid_heap_ptr);
    hdr->size_bytes = aligned;
    hdr->guard      = XMID_HEAP_GUARD;
    user_ptr = (void *)(xmid_heap_base + xmid_heap_ptr + XMID_BLOCK_HDR_SZ);
    xmid_heap_ptr += total;
    if (xmid_heap_ptr > xmid_heap_hw) xmid_heap_hw = xmid_heap_ptr;
    return user_ptr;
}

void *xmid_realloc(void *old_ptr, size_t new_size)
{
    void         *np;
    XmidBlockHdr *old_hdr;
    uint32_t      copy_len;

    if (new_size == 0u) return NULL;

    np = xmid_malloc(new_size);
    if (!np) return NULL;  /* longjmp already fired inside xmid_malloc */

    if (old_ptr) {
        old_hdr = (XmidBlockHdr *)((uint8_t *)old_ptr - XMID_BLOCK_HDR_SZ);
        if (old_hdr->guard != XMID_HEAP_GUARD) {
            xil_printf("[xmid] realloc bad guard %p\r\n", old_ptr);
            return np;
        }
        copy_len = old_hdr->size_bytes;
        if ((uint32_t)new_size < copy_len) copy_len = (uint32_t)new_size;
        memcpy(np, old_ptr, copy_len);
    }
    return np;
}

void xmid_free(void *p) { (void)p; }

/* -----------------------------------------------------------------------
 * TinySoundFont
 * --------------------------------------------------------------------- */
#define TSF_MALLOC   xmid_malloc
#define TSF_REALLOC  xmid_realloc
#define TSF_FREE     xmid_free
#define TSF_MEMCPY   memcpy
#define TSF_MEMSET   memset
#define TSF_IMPLEMENTATION
#include "tsf.h"

/* -----------------------------------------------------------------------
 * TinyMidiLoader
 * --------------------------------------------------------------------- */
#define TML_MALLOC   xmid_malloc
#define TML_REALLOC  xmid_realloc
#define TML_FREE     xmid_free
#define TML_IMPLEMENTATION
#include "tml.h"

/* -----------------------------------------------------------------------
 * xmid_tsf_load(): wrapper around tsf_load_memory() with OOM protection.
 * Must appear AFTER #include "tsf.h" so that 'tsf' type is known.
 * Returns non-NULL tsf* on success, NULL on parse error or heap overflow.
 * On heap overflow, longjmp unwinds TSF internals and returns here.
 * --------------------------------------------------------------------- */
static tsf *xmid_tsf_load(const uint8_t *data, int size)
{
    tsf *result;
    xmid_oom_active = 1;
    if (setjmp(xmid_oom_env) != 0) {
        /* OOM: longjmp landed here. Heap overflow already flagged. */
        xmid_oom_active = 0;
        xil_printf("[xmid] TSF OOM via longjmp heap=%lu\r\n",
                   (unsigned long)xmid_heap_ptr);
        return NULL;
    }
    result = tsf_load_memory(data, size);
    xmid_oom_active = 0;
    return result;
}

/* -----------------------------------------------------------------------
 * xmid_tml_load(): wrapper around tml_load_memory() with OOM protection.
 * Must appear AFTER #include "tml.h" so that 'tml_message' type is known.
 * Returns non-NULL tml_message* on success, NULL on error or overflow.
 * --------------------------------------------------------------------- */
static tml_message *xmid_tml_load(const uint8_t *data, int size)
{
    tml_message *result;
    xmid_oom_active = 1;
    if (setjmp(xmid_oom_env) != 0) {
        xmid_oom_active = 0;
        xil_printf("[xmid] TML OOM via longjmp heap=%lu\r\n",
                   (unsigned long)xmid_heap_ptr);
        return NULL;
    }
    result = tml_load_memory(data, size);
    xmid_oom_active = 0;
    return result;
}

/* -----------------------------------------------------------------------
 * Engine state (persistent across idle poll calls)
 * --------------------------------------------------------------------- */
static tsf      *g_sfont       = NULL;   /* loaded SoundFont */
static tml_message *g_midi     = NULL;   /* TinyMidiLoader message list */
/* Keep the parsed SoundFont resident across MIDI changes. Reload only TML state when the SoundFont is unchanged. */
static uint32_t  g_heap_after_sf2 = 0u;  /* heap mark right after SF2 load */
static uint32_t  g_loaded_sf2_off = 0u;  /* offset of the SF2 currently in */
static uint32_t  g_loaded_sf2_sz  = 0u;  /* size of the SF2 currently in */
static int       g_sfont_ready    = 0;   /* g_sfont valid and configured */
static tml_message *g_midi_cur = NULL;   /* current playback position */
static double    g_midi_time   = 0.0;    /* ms elapsed in playback */
static uint32_t  g_total_ms    = 0;      /* total MIDI duration ms */
static uint32_t  g_sample_rate = XMID_DEFAULT_RATE;
static uint32_t  g_pcm_write_total = 0; /* monotone, ARM side */
static uint32_t  g_underruns   = 0;
static int       g_playing     = 0;      /* 1 = actively rendering */

/* SF2 chunked upload state ().
 * g_sf2_total : total SF2 size announced by SF2_BEGIN
 * g_sf2_uploaded : bytes successfully copied to private pool so far
 * g_sf2_chunks : number of SF2_CHUNK commands processed
 * g_sf2_uploading: 1 = SF2_BEGIN accepted, chunks expected */
static uint32_t  g_sf2_total    = 0u;
static uint32_t  g_sf2_uploaded = 0u;
static uint32_t  g_sf2_chunks   = 0u;
static int       g_sf2_uploading = 0;

/* MIDI chunked upload state (XX19a / XACP v1.6). The raw MIDI private
 * copy at XMID_RAW_MIDI_BASE_ABS lives OUTSIDE the TSF/TML heap, so it
 * survives an SF2 reload: a new SF2 + LOAD_MIDI does not require
 * re-uploading the MIDI file. */
static uint32_t  g_midi_total    = 0u;
static uint32_t  g_midi_uploaded = 0u;
static int       g_midi_uploading = 0;

/* ARM CAMD/realtime counters. */
static uint32_t  g_arm_event_batches = 0u;
static uint32_t  g_arm_events_seen   = 0u;
/* FIFO read index is owned by ARM and kept locally. */
static uint32_t  g_fifo_read         = 0u;
/* FIFO and realtime counters published through the control block debug fields. */
static uint32_t  g_fifo_magic_seen   = 0u;
static uint32_t  g_fifo_w_seen       = 0u;
static uint32_t  g_fifo_r_seen       = 0u;
static uint32_t  g_fifo_drained      = 0u;
static uint32_t  g_fifo_bail_reason  = 0u;
static uint32_t  g_fifo_ev0          = 0u;
static uint32_t  g_fifo_apply_count  = 0u;
static uint32_t  g_fifo_max_depth    = 0u;  /* max (w-r) seen this session */
static uint32_t  g_fifo_drain_limit_hits = 0u; /* times drain hit 512 cap */
/* Per-reason rejection counters. */
static uint32_t  g_rej_status_low    = 0u;  /* status < 0x80 */
static uint32_t  g_rej_status_sys    = 0u;  /* status >= 0xF0 */
static uint32_t  g_rej_data_high     = 0u;  /* data byte bit7 set */
static uint32_t  g_last_rej_low      = 0u;  /* last status<0x80 event */
static uint32_t  g_last_rej_sys      = 0u;  /* last status>=0xF0 event */
static uint32_t  g_last_rej_data     = 0u;  /* last data-high event */
/* Coupled drain/render counters. */
static uint32_t  g_rt_drain_calls    = 0u;
static uint32_t  g_rt_render_calls   = 0u;
static uint32_t  g_rt_apply_no_render = 0u; /* events applied w/o render after */
static uint32_t  g_rt_ring_at_apply  = 0u;  /* ring level at last apply */

/* Timestamped realtime event queue. FIFO events are applied at their target audio position. */
#define XMID_RT_QUEUE_SIZE   2048u
typedef struct {
    uint8_t  status;
    uint8_t  data1;
    uint8_t  data2;
    uint8_t  reserved;
    uint32_t sample_time;
} XMID_RT_Event;
static XMID_RT_Event g_rt_queue[XMID_RT_QUEUE_SIZE];
static uint32_t  g_rt_q_head = 0u;   /* next to apply */
static uint32_t  g_rt_q_tail = 0u;   /* next to fill */
static uint32_t  g_rt_scheduled   = 0u;
static uint32_t  g_rt_applied      = 0u;
static uint32_t  g_rt_late_events  = 0u;
static uint32_t  g_rt_max_late     = 0u;
static uint32_t  g_rt_no_progress  = 0u;  /* due group consumed nothing (guard) */
static uint32_t  g_rt_q_max_depth  = 0u;
static uint32_t  g_rt_q_dropped    = 0u;
static uint32_t  g_arm_note_on_count = 0u;
static uint32_t  g_arm_note_off_count= 0u;
static uint32_t  g_arm_last_status   = 0u;
static uint32_t  g_arm_last_data1    = 0u;
static uint32_t  g_arm_last_data2    = 0u;
static int       g_realtime_active   = 0;

/* Realtime batch counters. */
static uint32_t  g_arm_last_batch_count = 0u;
static uint32_t  g_arm_cc_count       = 0u;
static uint32_t  g_arm_pc_count       = 0u;
static uint32_t  g_arm_pb_count       = 0u;
static uint32_t  g_arm_ignored_count  = 0u;
static uint32_t  g_arm_last_event0    = 0u;
static uint32_t  g_arm_last_event1    = 0u;
static uint32_t  g_arm_last_event2    = 0u;

/* Render timing counters measured with the Cortex-A9 global timer.
 * g_render_us_last records the latest block time, g_render_us_max the peak,
 * and g_ring_level_min the lowest observed PCM ring fill. */
static uint32_t  g_render_us_last = 0;
static uint32_t  g_render_us_max  = 0;
static uint32_t  g_ring_level_min = 0xFFFFFFFFu;

/* Deferred job: 0=idle, 1=queued, 2=running */
static int xmid_job_pending = 0;

/* -----------------------------------------------------------------------
 * FPU self-test (two levels)
 * --------------------------------------------------------------------- */
static uint32_t xmid_fpu_selftest(volatile XMID_Ctrl *c)
{
    uint32_t fpu_flags = 0u;
    uint32_t bits_tmp;

    CTRL_WR(c, debug3, XMID_FPU_SENTINEL | fpu_flags);
    xmid_flush((void *)c, sizeof(XMID_Ctrl));

    /* Level 1: float only */
    {
        volatile float a = 1.5f;
        volatile float b = a * 2.0f - 1.0f;
        volatile float r = b / 2.0f;
        memcpy(&bits_tmp, (void *)&r, 4u);
        CTRL_WR(c, debug2, bits_tmp);
        if (r != r || r == 0.0f) goto l1fail;
        { volatile float d = r - 1.0f; if (d < 0.0f) d = -d; if (d > 0.001f) goto l1fail; }
        fpu_flags |= XMID_FPU_FLOAT_OK;
        goto l1done;
    l1fail:
        CTRL_WR(c, debug3, XMID_FPU_SENTINEL | fpu_flags);
        xmid_flush((void *)c, sizeof(XMID_Ctrl));
        xil_printf("[xmid] FPU level1 FAIL\r\n");
        return fpu_flags;
    }
l1done:
    xil_printf("[xmid] FPU level1 ok\r\n");

    /* Level 2: sinf (non-fatal) */
    {
        volatile float s = sinf(0.5f);
        volatile float e = s - 0.4794f;
        if (e < 0.0f) e = -e;
        uint32_t bits_s, bits_e;
        memcpy(&bits_s, (void *)&s, 4u);
        memcpy(&bits_e, (void *)&e, 4u);
        CTRL_WR(c, debug0, bits_s);
        CTRL_WR(c, debug1, bits_e);
        if (s == s && s != 0.0f && e < 0.01f) {
            fpu_flags |= XMID_FPU_SINF_OK;
            xil_printf("[xmid] FPU level2 sinf ok\r\n");
        } else {
            xil_printf("[xmid] FPU level2 sinf WARN\r\n");
        }
    }
    CTRL_WR(c, debug3, XMID_FPU_SENTINEL | fpu_flags);
    xmid_flush((void *)c, sizeof(XMID_Ctrl));
    return fpu_flags;
}

/* -----------------------------------------------------------------------
 * PCM statistics
 * --------------------------------------------------------------------- */
static void xmid_calc_stats(const int16_t *pcm, uint32_t frames,
                             uint32_t channels,
                             uint32_t *out_peak, uint32_t *out_rms)
{
    uint32_t n = frames * channels, i;
    uint32_t peak = 0u;
    uint64_t sum_sq = 0u;
    for (i = 0u; i < n; i++) {
        int32_t  s = (int32_t)pcm[i];
        uint32_t a = (s < 0) ? (uint32_t)(-s) : (uint32_t)s;
        if (a > peak) peak = a;
        sum_sq += (uint64_t)((int64_t)s * (int64_t)s);
    }
    *out_peak = peak;
    *out_rms  = (n > 0u) ? (uint32_t)sqrtf((float)(sum_sq / (uint64_t)n)) : 0u;
}

/* -----------------------------------------------------------------------
 * Shared render scratch buffer (static, in BSS -- 256 KB, safe)
 * 512 frames * 2 ch * 2 bytes = 2048 bytes per block.
 * Extra space for potential rate variation.
 * --------------------------------------------------------------------- */
static int16_t g_render_buf[XMID_RENDER_BLOCK_FRAMES * 2u];

/* -----------------------------------------------------------------------
 * SELFTEST
 * --------------------------------------------------------------------- */
static __attribute__((unused)) void xmid_do_selftest(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c       = (volatile XMID_Ctrl *)ctrl_raw;

    xil_printf("[xmid] SELFTEST start\r\n");
    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));

    uint32_t cmd_seq = CTRL_RD(c, cmd_seq);
    uint32_t sf2_off = CTRL_RD(c, sf2_offset);
    uint32_t sf2_sz  = CTRL_RD(c, sf2_size);
    uint32_t sr      = CTRL_RD(c, sample_rate);
    uint32_t vol_q16 = CTRL_RD(c, volume_q16);

    if (sr      == 0u) sr      = XMID_DEFAULT_RATE;
    if (vol_q16 == 0u) vol_q16 = XMID_DEFAULT_VOLUME;

    /* FPU */
    uint32_t fpu_flags = xmid_fpu_selftest(c);
    if (!(fpu_flags & XMID_FPU_FLOAT_OK)) {
        CTRL_WR(c, state, XMID_STATE_ERROR);
        CTRL_WR(c, error, XMID_ERR_FPU_FAIL);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        return;
    }

    /* Heap -- ARM private zone, never written by 68k, no invalidation needed */
    xmid_heap_set_base(fb_base);
    xmid_reset_heap();

    /* Validate SF2 */
#define XMID_FB_WINDOW (128UL * 1024UL * 1024UL)
    if (sf2_sz == 0u || sf2_sz > XMID_SF2_MAX_SIZE ||
        sf2_off >= XMID_FB_WINDOW ||
        (uint64_t)sf2_off + sf2_sz > XMID_FB_WINDOW) {
        CTRL_WR(c, state, XMID_STATE_ERROR);
        CTRL_WR(c, error, XMID_ERR_BAD_SF2);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] bad SF2 off=%lx sz=%lu\r\n",
                   (unsigned long)sf2_off, (unsigned long)sf2_sz);
        return;
    }

    uint8_t *sf2_ptr = (uint8_t *)fb_base + sf2_off;
    xmid_invalidate_large(sf2_ptr, sf2_sz);

    xil_printf("[xmid] tsf_load_memory %lu bytes\r\n", (unsigned long)sf2_sz);
    tsf *sfont = xmid_tsf_load(sf2_ptr, (int)sf2_sz);
    if (!sfont) {
        uint32_t ec = xmid_heap_did_overflow() ? XMID_ERR_NO_HEAP
                                               : XMID_ERR_TSF_FAIL;
        CTRL_WR(c, state,          XMID_STATE_ERROR);
        CTRL_WR(c, error,          ec);
        CTRL_WR(c, heap_used,      xmid_heap_used());
        CTRL_WR(c, heap_highwater, xmid_heap_highwater());
        CTRL_WR(c, done_seq,       cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] TSF load FAIL heap=%lu\r\n",
                   (unsigned long)xmid_heap_used());
        return;
    }

    tsf_set_output(sfont, TSF_STEREO_INTERLEAVED, (int)sr, 0.0f);
    tsf_set_volume(sfont, (float)vol_q16 / 65536.0f);
    tsf_channel_set_presetnumber(sfont, 0, 0, 0);
    tsf_channel_note_on(sfont, 0, 60, 1.0f);

    uint32_t frames    = sr / 2u;
    uint32_t max_samp  = sizeof(g_render_buf) / sizeof(g_render_buf[0]);
    if (frames * XMID_DEFAULT_CHANNELS > max_samp)
        frames = max_samp / XMID_DEFAULT_CHANNELS;
    uint32_t pcm_bytes = frames * XMID_DEFAULT_CHANNELS * 2u;

    memset(g_render_buf, 0, pcm_bytes);
    tsf_render_short(sfont, g_render_buf, (int)frames, 0);
    tsf_channel_note_off(sfont, 0, 60);
    tsf_close(sfont);

    uint32_t peak = 0u, rms = 0u;
    xmid_calc_stats(g_render_buf, frames, XMID_DEFAULT_CHANNELS, &peak, &rms);
    xil_printf("[xmid] SELFTEST peak=%lu rms=%lu heap=%lu\r\n",
               (unsigned long)peak, (unsigned long)rms,
               (unsigned long)xmid_heap_used());

    /* Write PCM to ring */
    uint8_t *pcm_ring = (uint8_t *)fb_base + XMID_PCM_RING_OFFSET;
    if (pcm_bytes > XMID_PCM_RING_SIZE) pcm_bytes = XMID_PCM_RING_SIZE;
    memcpy(pcm_ring, g_render_buf, pcm_bytes);
    xmid_flush(pcm_ring, pcm_bytes);

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    CTRL_WR(c, state,             XMID_STATE_DONE);
    CTRL_WR(c, error,             XMID_ERR_NONE);
    CTRL_WR(c, peak_abs,          peak);
    CTRL_WR(c, rms_last,          rms);
    CTRL_WR(c, rendered_samples,  frames);
    CTRL_WR(c, pcm_ring_offset,   XMID_PCM_RING_OFFSET);
    CTRL_WR(c, pcm_ring_size_bytes, XMID_PCM_RING_SIZE);
    CTRL_WR(c, pcm_write_total,   pcm_bytes);
    CTRL_WR(c, pcm_read_total,    0u);
    CTRL_WR(c, sample_rate,       sr);
    CTRL_WR(c, channels,          XMID_DEFAULT_CHANNELS);
    CTRL_WR(c, heap_used,         xmid_heap_used());
    CTRL_WR(c, heap_highwater,    xmid_heap_highwater());
    CTRL_WR(c, active_voices,     0u);
    CTRL_WR(c, done_seq,          cmd_seq);
    CTRL_WR(c, debug3,            XMID_FPU_DONE | (fpu_flags & 0xFFu));
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
}

/* =======================================================================
 * SF2 chunked upload
 *
 * Chunks pass through XMID_UPLOAD_OFFSET and are copied to the private raw
 * SF2 buffer before tsf_load_memory() parses the completed image.
 * ======================================================================= */
#if 1

/* -----------------------------------------------------------------------
 * SF2_BEGIN -- announce total SF2 size, reset upload state ()
 * Deferred via idle_poll.
 * --------------------------------------------------------------------- */
static void xmid_do_sf2_begin(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c        = (volatile XMID_Ctrl *)ctrl_raw;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    uint32_t cmd_seq   = CTRL_RD(c, cmd_seq);
    uint32_t total_sz  = CTRL_RD(c, sf2_total_size);

    xil_printf("[xmid] SF2_BEGIN total=%lu\r\n", (unsigned long)total_sz);

    /* Guard: total size must fit in private pool raw SF2 zone */
    if (total_sz == 0u || total_sz > XMID_SF2_POOL_SIZE) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_SF2_TOO_LARGE);
        CTRL_WR(c, sf2_state, XMID_SF2_STATE_ERROR);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] SF2_BEGIN reject sz=%lu max=%lu\r\n",
                   (unsigned long)total_sz,
                   (unsigned long)XMID_SF2_POOL_SIZE);
        return;
    }

    /* Reset upload state. A new SF2 invalidates the whole TSF/TML heap:
 * drop the parsed MIDI too (the raw private MIDI copy at
 * XMID_RAW_MIDI_BASE_ABS survives and can be re-committed). */
    g_playing  = 0;
    g_midi_cur = NULL;
    if (g_midi)  { tml_free(g_midi);   g_midi  = NULL; }
    if (g_sfont) { tsf_close(g_sfont); g_sfont = NULL; }
    g_sfont_ready = 0;
    g_sf2_total    = total_sz;
    g_sf2_uploaded = 0u;
    g_sf2_chunks   = 0u;
    g_sf2_uploading = 1;

    /* Reset heap for fresh SF2 load */
    xmid_heap_set_base(fb_base);
    xmid_reset_heap();

    CTRL_WR(c, sf2_total_size,     g_sf2_total);
    CTRL_WR(c, sf2_uploaded_bytes, 0u);
    CTRL_WR(c, sf2_chunk_offset,   0u);
    CTRL_WR(c, sf2_chunk_size,     0u);
    CTRL_WR(c, sf2_chunks_done,    0u);
    CTRL_WR(c, sf2_state,          XMID_SF2_STATE_UPLOADING);
    CTRL_WR(c, sf2_progress_pct,   0u);
    CTRL_WR(c, state,              XMID_STATE_EMPTY);
    CTRL_WR(c, error,              XMID_ERR_NONE);
    CTRL_WR(c, done_seq,           cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
    xil_printf("[xmid] SF2_BEGIN ready, uploading\r\n");
}

/* -----------------------------------------------------------------------
 * SF2_CHUNK -- copy one chunk from staging to private pool ()
 * Deferred via idle_poll.
 * chunk_offset and chunk_size are passed via sf2_chunk_offset/sf2_chunk_size
 * fields in the ctrl block (set by 68k before triggering).
 * --------------------------------------------------------------------- */
static void xmid_do_sf2_chunk(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c        = (volatile XMID_Ctrl *)ctrl_raw;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    uint32_t cmd_seq    = CTRL_RD(c, cmd_seq);
    uint32_t chunk_off  = CTRL_RD(c, sf2_chunk_offset);
    uint32_t chunk_sz   = CTRL_RD(c, sf2_chunk_size);

    xil_printf("[xmid] SF2_CHUNK off=%lu sz=%lu\r\n",
               (unsigned long)chunk_off, (unsigned long)chunk_sz);

    /* Guard: must be in uploading state */
    if (!g_sf2_uploading) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_STATE);
        CTRL_WR(c, sf2_state, XMID_SF2_STATE_ERROR);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] SF2_CHUNK bad state\r\n");
        return;
    }

    /* Guard: chunk size -- max 1 MB (shared upload buffer, v1.6) */
    if (chunk_sz == 0u || chunk_sz > XMID_SF2_CHUNK_MAX) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_SIZE);
        CTRL_WR(c, sf2_state, XMID_SF2_STATE_ERROR);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] SF2_CHUNK bad size=%lu\r\n", (unsigned long)chunk_sz);
        return;
    }

    /* Guard: chunk offset + size must fit within declared total */
    if ((uint64_t)chunk_off + chunk_sz > g_sf2_total) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_OFFSET);
        CTRL_WR(c, sf2_state, XMID_SF2_STATE_ERROR);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] SF2_CHUNK offset out of range\r\n");
        return;
    }

    /* Require strictly sequential chunks so a retry cannot leave holes in the uploaded file. */
    if (chunk_off != g_sf2_uploaded) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_OFFSET);
        CTRL_WR(c, sf2_state, XMID_SF2_STATE_ERROR);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] SF2_CHUNK not sequential off=%lu expect=%lu\r\n",
                   (unsigned long)chunk_off,
                   (unsigned long)g_sf2_uploaded);
        return;
    }

    /* Guard: destination in private pool raw SF2 zone */
    uint32_t dst_abs = XMID_RAW_SF2_BASE_ABS + chunk_off;
    if (dst_abs < XMID_POOL_SAFE_BASE ||
        dst_abs + chunk_sz > XMID_RAW_SF2_BASE_ABS + XMID_SF2_POOL_SIZE) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_PRIVATE_POOL_ADDR);
        CTRL_WR(c, sf2_state, XMID_SF2_STATE_ERROR);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] SF2_CHUNK bad dst addr 0x%08lx\r\n",
                   (unsigned long)dst_abs);
        return;
    }

    /* Invalidate ONLY the upload chunk the 68k just wrote.
 * Shared upload buffer at XMID_UPLOAD_OFFSET (1 MB), size = chunk_sz. */
    uint8_t *staging_ptr = (uint8_t *)fb_base + XMID_UPLOAD_OFFSET;
    xmid_invalidate_large(staging_ptr, chunk_sz);

    /* Copy staging -> private pool */
    uint8_t *dst = (uint8_t *)dst_abs;
    memcpy(dst, staging_ptr, chunk_sz);

    g_sf2_uploaded = chunk_off + chunk_sz;   /* sequential: exact position */
    g_sf2_chunks++;

    /* Update progress */
    uint32_t pct = (g_sf2_total > 0u)
                   ? (uint32_t)((uint64_t)g_sf2_uploaded * 100u / g_sf2_total)
                   : 0u;

    CTRL_WR(c, sf2_uploaded_bytes, g_sf2_uploaded);
    CTRL_WR(c, sf2_chunks_done,    g_sf2_chunks);
    CTRL_WR(c, sf2_progress_pct,   pct);
    CTRL_WR(c, sf2_state,          XMID_SF2_STATE_CHUNK_DONE);
    CTRL_WR(c, error,              XMID_ERR_NONE);
    CTRL_WR(c, done_seq,           cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
    xil_printf("[xmid] SF2_CHUNK done %lu/%lu (%lu%%)\r\n",
               (unsigned long)g_sf2_uploaded,
               (unsigned long)g_sf2_total,
               (unsigned long)pct);
}

/* -----------------------------------------------------------------------
 * SF2_COMMIT_LOAD -- tsf_load_memory from private pool ()
 * Deferred via idle_poll.
 * --------------------------------------------------------------------- */
static void xmid_do_sf2_commit(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c        = (volatile XMID_Ctrl *)ctrl_raw;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    uint32_t cmd_seq = CTRL_RD(c, cmd_seq);

    xil_printf("[xmid] SF2_COMMIT uploaded=%lu total=%lu\r\n",
               (unsigned long)g_sf2_uploaded,
               (unsigned long)g_sf2_total);

    /* Guard: upload must be complete */
    if (!g_sf2_uploading || g_sf2_uploaded != g_sf2_total) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_UPLOAD_INCOMPLETE);
        CTRL_WR(c, sf2_state, XMID_SF2_STATE_ERROR);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] SF2_COMMIT incomplete %lu/%lu\r\n",
                   (unsigned long)g_sf2_uploaded,
                   (unsigned long)g_sf2_total);
        return;
    }

    /* Verify RIFF/sfbk header at pool base before calling tsf_load_memory.
 * A corrupt upload would waste seconds in tsf then crash. */
    {
        const uint8_t *hdr = (const uint8_t *)XMID_RAW_SF2_BASE_ABS;
        int riff_ok = (hdr[0]=='R' && hdr[1]=='I' && hdr[2]=='F' && hdr[3]=='F');
        int sfbk_ok = (hdr[8]=='s' && hdr[9]=='f' && hdr[10]=='b' && hdr[11]=='k');
        if (!riff_ok || !sfbk_ok) {
            g_sf2_uploading = 0;
            CTRL_WR(c, state,    XMID_STATE_ERROR);
            CTRL_WR(c, error,    XMID_ERR_BAD_SF2);
            CTRL_WR(c, sf2_state, XMID_SF2_STATE_ERROR);
            CTRL_WR(c, done_seq, cmd_seq);
            xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
            xil_printf("[xmid] SF2_COMMIT bad RIFF header: %02x%02x%02x%02x\r\n",
                       hdr[0], hdr[1], hdr[2], hdr[3]);
            return;
        }
        xil_printf("[xmid] SF2_COMMIT RIFF/sfbk OK\r\n");
    }

    /* Call tsf_load_memory from private pool */
    xil_printf("[xmid] tsf_load_memory from 0x%08lx sz=%lu\r\n",
               (unsigned long)XMID_RAW_SF2_BASE_ABS,
               (unsigned long)g_sf2_total);

    if (g_sfont) { tsf_close(g_sfont); g_sfont = NULL; }
    g_sfont = xmid_tsf_load((uint8_t *)XMID_RAW_SF2_BASE_ABS,
                              (int)g_sf2_total);
    if (!g_sfont) {
        g_sf2_uploading = 0;
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_TSF_LOAD_FAILED);
        CTRL_WR(c, sf2_state, XMID_SF2_STATE_ERROR);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] SF2_COMMIT tsf_load FAILED\r\n");
        return;
    }

    /* Configure TSF */
    uint32_t sr = CTRL_RD(c, sample_rate);
    if (sr == 0u) sr = XMID_DEFAULT_RATE;
    tsf_set_output(g_sfont, TSF_STEREO_INTERLEAVED, (int)sr, 0.0f);
    tsf_set_max_voices(g_sfont, (int)XMID_DEFAULT_VOICES);

    /* Pre-initialize 16 MIDI channels */
    {
        int ch;
        for (ch = 0; ch < 16; ch++) {
            tsf_channel_set_presetnumber(g_sfont, ch, 0, (ch == 9));
            tsf_channel_midi_control(g_sfont, ch, 7, 100);  /* volume */
            tsf_channel_midi_control(g_sfont, ch, 10, 64);  /* pan center */
        }
    }

    /* Record SF2 residency and the post-SF2 heap mark for subsequent MIDI loads. */
    {
        uint32_t vol_q16 = CTRL_RD(c, volume_q16);
        if (vol_q16 == 0u) vol_q16 = XMID_DEFAULT_VOLUME;
        tsf_set_volume(g_sfont, (float)vol_q16 / 65536.0f);
    }
    g_sample_rate    = sr;
    g_heap_after_sf2 = xmid_heap_used();
    g_loaded_sf2_off = 0u;           /* fb staging offset: obsolete in v1.6 */
    g_loaded_sf2_sz  = g_sf2_total;
    g_sfont_ready    = 1;

    g_sf2_uploading = 0;
    CTRL_WR(c, sf2_state,          XMID_SF2_STATE_READY);
    CTRL_WR(c, sf2_progress_pct,   100u);
    CTRL_WR(c, heap_used,          xmid_heap_used());
    CTRL_WR(c, heap_highwater,     xmid_heap_highwater());
    CTRL_WR(c, state,              XMID_STATE_READY);
    CTRL_WR(c, error,              XMID_ERR_NONE);
    CTRL_WR(c, done_seq,           cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
    xil_printf("[xmid] SF2_COMMIT OK heap=%lu\r\n",
               (unsigned long)xmid_heap_used());
}


/* -----------------------------------------------------------------------
 * SF2_FAKE_COMMIT -- test command that completes without parsing the SF2.
 * --------------------------------------------------------------------- */
static void xmid_do_sf2_fake_commit(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c        = (volatile XMID_Ctrl *)ctrl_raw;
    uint32_t            cmd_seq  = CTRL_RD(c, cmd_seq);
    uint32_t            i;

    xil_printf("[xmid] SF2_FAKE_COMMIT: waiting 5s (no tsf_load)...\r\n");
    /* Busy-wait ~5 seconds using delay loops so ARM stays alive */
    for (i = 0; i < 5u; i++) {
        volatile uint32_t d = 100000000u;
        while (d--) { asm volatile("nop"); }
        xil_printf("[xmid] fake commit %lu/5\r\n", (unsigned long)(i + 1u));
    }
    xil_printf("[xmid] SF2_FAKE_COMMIT done -- reporting SF2_READY\r\n");

    CTRL_WR(c, sf2_state,        XMID_SF2_STATE_READY);
    CTRL_WR(c, sf2_progress_pct, 100u);
    CTRL_WR(c, state,            XMID_STATE_READY);
    CTRL_WR(c, error,            XMID_ERR_NONE);
    CTRL_WR(c, done_seq,         cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
}

#endif /* SF2 chunked upload -- experimental, disabled ZZMIDI v1 */

/* -----------------------------------------------------------------------
 * MIDI_BEGIN -- announce total MIDI size, reset MIDI upload state (XX19a)
 * Total size is passed in the midi_size ctrl field. Deferred via idle_poll.
 * Also configures the private-pool MMU (mirrors SF2_BEGIN) so the
 * following MIDI_CHUNK memcpy into 0x25000000 is safe.
 * --------------------------------------------------------------------- */
static void xmid_do_midi_begin(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c        = (volatile XMID_Ctrl *)ctrl_raw;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    uint32_t cmd_seq  = CTRL_RD(c, cmd_seq);
    uint32_t total_sz = CTRL_RD(c, midi_size);

    xil_printf("[xmid] MIDI_BEGIN total=%lu\r\n", (unsigned long)total_sz);

    /* Guard: total size must fit in the raw MIDI private zone */
    if (total_sz == 0u || total_sz > XMID_MIDI_POOL_SIZE) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_SIZE);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] MIDI_BEGIN reject sz=%lu max=%lu\r\n",
                   (unsigned long)total_sz,
                   (unsigned long)XMID_MIDI_POOL_SIZE);
        return;
    }

    /* MMU attributes for the private pool (idempotent). */
    xmid_heap_set_base(fb_base);

    g_midi_total     = total_sz;
    g_midi_uploaded  = 0u;
    g_midi_uploading = 1;

    CTRL_WR(c, error,    XMID_ERR_NONE);
    CTRL_WR(c, done_seq, cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
    xil_printf("[xmid] MIDI_BEGIN ready, uploading\r\n");
}

/* -----------------------------------------------------------------------
 * MIDI_CHUNK -- copy one chunk from the shared upload buffer to the raw
 * MIDI private copy (XX19a). midi_offset = destination offset in the raw
 * MIDI zone, midi_size = chunk size (<= XMID_MIDI_CHUNK_MAX = 1 MB).
 * Deferred via idle_poll.
 * --------------------------------------------------------------------- */
static void xmid_do_midi_chunk(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c        = (volatile XMID_Ctrl *)ctrl_raw;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    uint32_t cmd_seq   = CTRL_RD(c, cmd_seq);
    uint32_t chunk_off = CTRL_RD(c, midi_offset);
    uint32_t chunk_sz  = CTRL_RD(c, midi_size);

    xil_printf("[xmid] MIDI_CHUNK off=%lu sz=%lu\r\n",
               (unsigned long)chunk_off, (unsigned long)chunk_sz);

    /* Guard: must be in uploading state */
    if (!g_midi_uploading) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_STATE);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] MIDI_CHUNK bad state\r\n");
        return;
    }

    /* Guard: chunk size -- max 1 MB (shared upload buffer) */
    if (chunk_sz == 0u || chunk_sz > XMID_MIDI_CHUNK_MAX) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_SIZE);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] MIDI_CHUNK bad size=%lu\r\n",
                   (unsigned long)chunk_sz);
        return;
    }

    /* Guard: chunk offset + size must fit within declared total */
    if ((uint64_t)chunk_off + chunk_sz > g_midi_total) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_OFFSET);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] MIDI_CHUNK offset out of range\r\n");
        return;
    }

    /* Guard: strictly sequential upload (), same rule as SF2. */
    if (chunk_off != g_midi_uploaded) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_OFFSET);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] MIDI_CHUNK not sequential off=%lu expect=%lu\r\n",
                   (unsigned long)chunk_off,
                   (unsigned long)g_midi_uploaded);
        return;
    }

    /* Guard: destination inside the raw MIDI private zone */
    {
        uint32_t dst_abs = XMID_RAW_MIDI_BASE_ABS + chunk_off;
        if (dst_abs <  XMID_RAW_MIDI_BASE_ABS ||
            (uint64_t)dst_abs + chunk_sz >
                (uint64_t)XMID_RAW_MIDI_BASE_ABS + XMID_MIDI_POOL_SIZE) {
            CTRL_WR(c, state,    XMID_STATE_ERROR);
            CTRL_WR(c, error,    XMID_ERR_BAD_PRIVATE_POOL_ADDR);
            CTRL_WR(c, done_seq, cmd_seq);
            xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
            xil_printf("[xmid] MIDI_CHUNK bad dst addr 0x%08lx\r\n",
                       (unsigned long)dst_abs);
            return;
        }

        /* Invalidate ONLY the upload chunk the 68k just wrote. */
        {
            uint8_t *upload_ptr = (uint8_t *)fb_base + XMID_UPLOAD_OFFSET;
            xmid_invalidate_large(upload_ptr, chunk_sz);
            memcpy((uint8_t *)dst_abs, upload_ptr, chunk_sz);
        }
    }

    g_midi_uploaded = chunk_off + chunk_sz;  /* sequential: exact position */
    if (g_midi_uploaded == g_midi_total) {
        g_midi_uploading = 0;   /* upload complete */
        xil_printf("[xmid] MIDI upload complete %lu bytes\r\n",
                   (unsigned long)g_midi_total);
    }

    CTRL_WR(c, error,    XMID_ERR_NONE);
    CTRL_WR(c, done_seq, cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
}

/* -----------------------------------------------------------------------
 * LOAD_MIDI -- parse a completed private MIDI upload.
 *
 * The SoundFont must already be resident and the MIDI upload complete.
 * The heap is rolled back to the post-SF2 mark before tml_load_memory().
 * --------------------------------------------------------------------- */
static void xmid_do_load_midi(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c       = (volatile XMID_Ctrl *)ctrl_raw;

    xil_printf("[xmid] LOAD_MIDI start (XX19a v1.6 private-pool)\r\n");
    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));

    uint32_t cmd_seq = CTRL_RD(c, cmd_seq);
    uint32_t sr      = g_sample_rate;   /* fixed at SF2_COMMIT_LOAD */

    /* SF2 must be committed and configured. */
    if (!g_sfont || !g_sfont_ready) {
        CTRL_WR(c, state, XMID_STATE_ERROR);
        CTRL_WR(c, error, XMID_ERR_BAD_SF2);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] REJECT: no committed SF2\r\n");
        return;
    }

    /* MIDI upload must be complete and sane. */
    if (g_midi_total == 0u || g_midi_uploaded != g_midi_total ||
        g_midi_total > XMID_MIDI_MAX_SIZE) {
        CTRL_WR(c, state, XMID_STATE_ERROR);
        CTRL_WR(c, error, XMID_ERR_UPLOAD_INCOMPLETE);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] REJECT MIDI upload %lu/%lu\r\n",
                   (unsigned long)g_midi_uploaded,
                   (unsigned long)g_midi_total);
        return;
    }

    /* Reset only the per-session playback state. SF2 stays resident. */
    g_midi_cur  = NULL;
    g_midi_time = 0.0;
    g_playing   = 0;
    g_pcm_write_total = 0;
    g_underruns = 0;

    /* Heap base must be set every call (cheap, just sets a pointer). */
    xmid_heap_set_base(fb_base);

    /* Discard per-MIDI allocations while keeping the resident SoundFont. */
    xil_printf("[xmid] SF2 kept (heap rollback to %lu)\r\n",
               (unsigned long)g_heap_after_sf2);
    if (g_midi) { tml_free(g_midi); g_midi = NULL; }
    xmid_heap_set_ptr(g_heap_after_sf2);

    tsf_note_off_all(g_sfont);
    {
        int ch;
        for (ch = 0; ch < 16; ch++) {
            tsf_channel_midi_control(g_sfont, ch, 120, 0); /* sound off */
            tsf_channel_midi_control(g_sfont, ch, 123, 0); /* notes off */
            tsf_channel_midi_control(g_sfont, ch, 121, 0); /* reset ctl */
            tsf_channel_midi_control(g_sfont, ch, 7,  100);
            tsf_channel_midi_control(g_sfont, ch, 10,  64);
            tsf_channel_midi_control(g_sfont, ch, 64,   0);
        }
    }

    /* Record heap usage after SF2 parsing. */
    CTRL_WR(c, debug0, xmid_heap_used());

    /* Parse the private MIDI copy. It lives in the WB-cached private pool
 * and was written by this same core: no cache invalidate needed. */
    xil_printf("[xmid] tml_load_memory %lu bytes (private)\r\n",
               (unsigned long)g_midi_total);
    g_midi = xmid_tml_load((const uint8_t *)XMID_RAW_MIDI_BASE_ABS,
                           (int)g_midi_total);

    /* NULL means either OOM (longjmp fired, heap_overflow=1) or parse error */
    if (!g_midi) {
        uint32_t ec = xmid_heap_did_overflow() ? XMID_ERR_NO_HEAP
                                               : XMID_ERR_TML_FAIL;
        /* MIDI failed. Do NOT tsf_close here: the SF2 may be shared with a
 * later retry. Mark not-ready only if the heap overflowed (state
 * is then suspect); otherwise keep the SF2 resident. */
        if (xmid_heap_did_overflow()) {
            if (g_sfont) { tsf_close(g_sfont); g_sfont = NULL; }
            g_sfont_ready = 0;
        }
        CTRL_WR(c, state,          XMID_STATE_ERROR);
        CTRL_WR(c, error,          ec);
        CTRL_WR(c, heap_used,      xmid_heap_used());
        CTRL_WR(c, heap_highwater, xmid_heap_highwater());
        CTRL_WR(c, done_seq,       cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] TML FAIL err=%lu heap=%lu/%lu\r\n",
                   (unsigned long)ec,
                   (unsigned long)xmid_heap_used(),
                   (unsigned long)XMID_HEAP_SIZE);
        return;
    }

    /* Compute total_ms by walking the message list */
    {
        tml_message *msg = g_midi;
        uint32_t last_time = 0u;
        while (msg) {
            if (msg->time > last_time) last_time = msg->time;
            msg = msg->next;
        }
        g_total_ms = last_time + 2000u;  /* +2s tail for note releases */
    }
    xil_printf("[xmid] MIDI ok total_ms=%lu heap=%lu\r\n",
               (unsigned long)g_total_ms, (unsigned long)xmid_heap_used());

    /* Record heap usage after TML parsing in debug1.
 * debug0 = heap after TSF, debug1 = heap after TML.
 * ZZMIDIPlay displays both so user can size SF2+MIDI combinations. */
    CTRL_WR(c, debug1, xmid_heap_used());

    g_midi_cur  = g_midi;
    g_midi_time = 0.0;

    /* Reset PCM ring write counter */
    g_pcm_write_total = 0u;
    g_underruns = 0u;

    /* Update control block */
    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    CTRL_WR(c, state,             XMID_STATE_READY);
    CTRL_WR(c, error,             XMID_ERR_NONE);
    CTRL_WR(c, total_ms,          g_total_ms);
    CTRL_WR(c, play_ms,           0u);
    CTRL_WR(c, pcm_ring_offset,   XMID_PCM_RING_OFFSET);
    CTRL_WR(c, pcm_ring_size_bytes, XMID_PCM_RING_SIZE);
    CTRL_WR(c, pcm_write_total,   0u);
    CTRL_WR(c, pcm_read_total,    0u);
    CTRL_WR(c, sample_rate,       sr);
    CTRL_WR(c, channels,          XMID_DEFAULT_CHANNELS);
    CTRL_WR(c, heap_used,         xmid_heap_used());
    CTRL_WR(c, heap_highwater,    xmid_heap_highwater());
    CTRL_WR(c, rendered_samples,  0u);
    CTRL_WR(c, active_voices,     0u);
    CTRL_WR(c, underruns,         0u);
    CTRL_WR(c, done_seq,          cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
    xil_printf("[xmid] LOAD_MIDI DONE state=READY\r\n");
}

/* -----------------------------------------------------------------------
 * START
 * Transitions state READY -> PLAYING.
 * Actual rendering happens in xmid_render_block() called from idle poll.
 * --------------------------------------------------------------------- */
static void xmid_do_start(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c       = (volatile XMID_Ctrl *)ctrl_raw;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    uint32_t cmd_seq = CTRL_RD(c, cmd_seq);

    if (!g_sfont || !g_midi) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_MIDI);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] START: no SF2/MIDI loaded\r\n");
        return;
    }

    g_playing         = 1;
    g_midi_cur        = g_midi;
    g_midi_time       = 0.0;
    g_pcm_write_total = 0u;
    g_underruns       = 0u;

    /* Reset render timing counters for this playback. */
    g_render_us_last  = 0u;
    g_render_us_max   = 0u;
    g_ring_level_min  = 0xFFFFFFFFu;

    /* Read back current pcm_read_total from Amiga (should be 0 at start) */
    CTRL_WR(c, state,         XMID_STATE_PLAYING);
    CTRL_WR(c, error,         XMID_ERR_NONE);
    CTRL_WR(c, play_ms,       0u);
    CTRL_WR(c, pcm_write_total, 0u);
    CTRL_WR(c, pcm_read_total,  0u);
    CTRL_WR(c, underruns,     0u);
    CTRL_WR(c, done_seq,      cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
    xil_printf("[xmid] START playing\r\n");
}

/* -----------------------------------------------------------------------
 * STOP
 * --------------------------------------------------------------------- */
static void xmid_do_stop(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c       = (volatile XMID_Ctrl *)ctrl_raw;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    uint32_t cmd_seq = CTRL_RD(c, cmd_seq);

    g_playing = 0;
    if (g_sfont) tsf_note_off_all(g_sfont);

    CTRL_WR(c, state,    XMID_STATE_DONE);
    CTRL_WR(c, error,    XMID_ERR_NONE);
    CTRL_WR(c, done_seq, cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
    xil_printf("[xmid] STOP\r\n");
}

/* -----------------------------------------------------------------------
 * Reset all realtime state to a consistent baseline.
 *
 * This clears TSF channel state, timestamp queue state, FIFO indices and
 * counters, and PCM ring counters before a new realtime session starts.
 * --------------------------------------------------------------------- */
static void xmid_hard_reset_realtime(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c        = (volatile XMID_Ctrl *)ctrl_raw;
    uint8_t            *fifo     = (uint8_t *)fb_base + XMID_FIFO_OFFSET;

    /* 1. Stop all TSF notes and reset controllers on every channel. */
    if (g_sfont) {
        int ch;
        tsf_note_off_all(g_sfont);
        for (ch = 0; ch < 16; ch++) {
            tsf_channel_midi_control(g_sfont, ch, 120, 0); /* all sound off */
            tsf_channel_midi_control(g_sfont, ch, 123, 0); /* all notes off */
            tsf_channel_midi_control(g_sfont, ch, 121, 0); /* reset ctrl */
            tsf_channel_midi_control(g_sfont, ch, 7,  100); /* volume */
            tsf_channel_midi_control(g_sfont, ch, 11, 127); /* expression */
            tsf_channel_midi_control(g_sfont, ch, 10,  64); /* pan center */
            tsf_channel_midi_control(g_sfont, ch, 64,   0); /* sustain off */
        }
    }

    /* 2. Reset the timestamped event queue (ARM). */
    g_rt_q_head = 0u;
    g_rt_q_tail = 0u;

    /* 3. Reset the ARM-side FIFO read mirror. */
    g_fifo_read = 0u;

    /* 4. Reset the FIFO DDR header so the 68k producer and the ARM consumer
 * both start from index 0 with a valid, empty FIFO. ARM writes the
 * header; the direct producer (ZZMIDICAMDIn) only validates it. */
    {
        uint32_t magic = XMID_FIFO_MAGIC;
        uint32_t ver   = XMID_FIFO_VERSION;
        uint32_t slots = XMID_FIFO_SLOTS;
        fifo[XMID_FIFO_OFF_MAGIC+0]   = (uint8_t)((magic >> 24) & 0xFF);
        fifo[XMID_FIFO_OFF_MAGIC+1]   = (uint8_t)((magic >> 16) & 0xFF);
        fifo[XMID_FIFO_OFF_MAGIC+2]   = (uint8_t)((magic >>  8) & 0xFF);
        fifo[XMID_FIFO_OFF_MAGIC+3]   = (uint8_t)( magic        & 0xFF);
        fifo[XMID_FIFO_OFF_VERSION+0] = (uint8_t)((ver >> 24) & 0xFF);
        fifo[XMID_FIFO_OFF_VERSION+1] = (uint8_t)((ver >> 16) & 0xFF);
        fifo[XMID_FIFO_OFF_VERSION+2] = (uint8_t)((ver >>  8) & 0xFF);
        fifo[XMID_FIFO_OFF_VERSION+3] = (uint8_t)( ver        & 0xFF);
        fifo[XMID_FIFO_OFF_SIZE+0]    = (uint8_t)((slots >> 24) & 0xFF);
        fifo[XMID_FIFO_OFF_SIZE+1]    = (uint8_t)((slots >> 16) & 0xFF);
        fifo[XMID_FIFO_OFF_SIZE+2]    = (uint8_t)((slots >>  8) & 0xFF);
        fifo[XMID_FIFO_OFF_SIZE+3]    = (uint8_t)( slots        & 0xFF);
        /* dropped = 0 */
        fifo[XMID_FIFO_OFF_DROPPED+0] = 0u;
        fifo[XMID_FIFO_OFF_DROPPED+1] = 0u;
        fifo[XMID_FIFO_OFF_DROPPED+2] = 0u;
        fifo[XMID_FIFO_OFF_DROPPED+3] = 0u;
        /* write_idx = 0 */
        fifo[XMID_FIFO_OFF_WRITE_IDX+0] = 0u;
        fifo[XMID_FIFO_OFF_WRITE_IDX+1] = 0u;
        fifo[XMID_FIFO_OFF_WRITE_IDX+2] = 0u;
        fifo[XMID_FIFO_OFF_WRITE_IDX+3] = 0u;
        /* read_idx = 0 */
        fifo[XMID_FIFO_OFF_READ_IDX+0] = 0u;
        fifo[XMID_FIFO_OFF_READ_IDX+1] = 0u;
        fifo[XMID_FIFO_OFF_READ_IDX+2] = 0u;
        fifo[XMID_FIFO_OFF_READ_IDX+3] = 0u;
        /* Flush header line, write_idx line and read_idx line so the 68k
 * sees the cleaned FIFO. */
        Xil_DCacheFlushRange((INTPTR)fifo, 0x10);
        Xil_DCacheFlushRange((INTPTR)(fifo + XMID_FIFO_OFF_WRITE_IDX), 0x20);
        Xil_DCacheFlushRange((INTPTR)(fifo + XMID_FIFO_OFF_READ_IDX), 0x20);
    }

    /* 5. Reset PCM ring counters (ARM side + ctrl block). */
    g_pcm_write_total = 0u;
    g_underruns       = 0u;
    CTRL_WR(c, pcm_write_total, 0u);
    CTRL_WR(c, pcm_read_total,  0u);
    CTRL_WR(c, underruns,       0u);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
}


/* -----------------------------------------------------------------------
 * REALTIME_INIT -- prepare TSF for CAMD realtime mode (no MIDI file).
 *
 * SF2 must already be loaded (g_sfont valid from a previous LOAD_MIDI).
 * Resets TSF channel state, enables rendering, does NOT load any MIDI.
 * Called once before the first EVENT_BATCH.
 * --------------------------------------------------------------------- */
static void xmid_do_realtime_init(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c       = (volatile XMID_Ctrl *)ctrl_raw;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    uint32_t cmd_seq = CTRL_RD(c, cmd_seq);

    /* XACP v1.6: the SF2 must already be committed via SF2_BEGIN /
 * SF2_CHUNK / SF2_COMMIT_LOAD. No shared SF2 staging exists anymore,
 * so REALTIME_INIT no longer loads anything itself. */
    if (!g_sfont || !g_sfont_ready) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_SF2);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] RT_INIT: no committed SF2\r\n");
        return;
    }

    /* Reset TML file state -- realtime mode has no MIDI file */
    if (g_midi) { tml_free(g_midi); g_midi = NULL; }
    g_midi_cur  = NULL;
    g_midi_time = 0.0;

    /* HARD RESET: clear all residual realtime state (TSF notes/controllers,
 * timestamp queue, ARM fifo read, FIFO DDR header, PCM counters) so this
 * session starts from a clean baseline. This is the key fix for the
 * cumulative degradation and stale-FIFO desync between runs. */
    xmid_hard_reset_realtime(fb_base);

    /* Enable continuous rendering without MIDI file */
    g_playing     = 1;
    g_realtime_active = 1;
    g_midi_cur    = NULL;  /* signals realtime mode to render loop */
    g_pcm_write_total = 0u;
    g_underruns       = 0u;
    g_fifo_read       = 0u;  /* ARM read index resync; 68k re-inits FIFO */
    /* Reset FIFO rejection counters for the new session. */
    g_fifo_drained        = 0u;
    g_fifo_apply_count    = 0u;
    g_fifo_max_depth      = 0u;
    g_fifo_drain_limit_hits = 0u;
    g_arm_events_seen     = 0u;
    g_arm_ignored_count   = 0u;
    g_rej_status_low      = 0u;
    g_rej_status_sys      = 0u;
    g_rej_data_high       = 0u;
    g_last_rej_low        = 0u;
    g_last_rej_sys        = 0u;
    g_last_rej_data       = 0u;
    g_arm_note_on_count   = 0u;
    g_arm_note_off_count  = 0u;
    g_arm_cc_count        = 0u;
    g_arm_pc_count        = 0u;
    g_arm_pb_count        = 0u;
    g_rt_drain_calls      = 0u;
    g_rt_render_calls     = 0u;
    g_rt_apply_no_render  = 0u;
    g_rt_ring_at_apply    = 0u;
    /* Reset the timestamped queue and its counters. */
    g_rt_q_head           = 0u;
    g_rt_q_tail           = 0u;
    g_rt_scheduled        = 0u;
    g_rt_applied          = 0u;
    g_rt_late_events      = 0u;
    g_rt_max_late         = 0u;
    g_rt_no_progress      = 0u;
    g_rt_q_max_depth      = 0u;
    g_rt_q_dropped        = 0u;
    CTRL_WR(c, pcm_write_total, 0u);
    CTRL_WR(c, pcm_read_total,  0u);

    CTRL_WR(c, state,    XMID_STATE_PLAYING);
    CTRL_WR(c, error,    XMID_ERR_NONE);
    CTRL_WR(c, done_seq, cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
    xil_printf("[xmid] REALTIME_INIT ok (XX18t TIMESTAMP BLOCK=%lu RT_TARGET=%lu)\r\n",
               (unsigned long)XMID_RENDER_BLOCK_FRAMES,
               (unsigned long)XMID_RT_TARGET_FRAMES);
}

/* -----------------------------------------------------------------------
 * EVENT_BATCH -- inject a batch of MIDI events from staging into TSF
 *
 * The 68k writes XMID_Event[] at XMID_EVENT_STAGING_OFFSET, preceded
 * by a uint32_t event_count. ARM invalidates staging, reads events,
 * calls tsf_channel_* for each event, then ACKs.
 *
 * Supported status bytes:
 * 0x80-0x8F Note Off
 * 0x90-0x9F Note On
 * 0xA0-0xAF (ignored)
 * 0xB0-0xBF Control Change
 * 0xC0-0xCF Program Change
 * 0xD0-0xDF (ignored)
 * 0xE0-0xEF Pitch Bend
 * --------------------------------------------------------------------- */
/* Apply one MIDI channel event to TSF. Reject malformed channel messages
 * before they can reach the TinySoundFont channel APIs. */
static void xmid_apply_event(uint8_t status, uint8_t d1, uint8_t d2)
{
    int ch;
    int type;

    /* Status byte must have bit7 set (0x80-0xFF). Reject system/realtime
 * (0xF0-0xFF) too: we only handle channel voice messages here. */
    if (status < 0x80) {
        g_arm_ignored_count++;
        g_rej_status_low++;
        g_last_rej_low = ((uint32_t)status << 16) |
                         ((uint32_t)d1 << 8) | d2;
        return;
    }
    if (status >= 0xF0) {
        g_arm_ignored_count++;
        g_rej_status_sys++;
        g_last_rej_sys = ((uint32_t)status << 16) |
                         ((uint32_t)d1 << 8) | d2;
        return;
    }
    /* Data bytes must be 0-127. If bit7 is set, the slot is corrupt. */
    if ((d1 & 0x80) || (d2 & 0x80)) {
        g_arm_ignored_count++;
        g_rej_data_high++;
        g_last_rej_data = ((uint32_t)status << 16) |
                          ((uint32_t)d1 << 8) | d2;
        return;
    }

    ch   = status & 0x0F;
    type = status & 0xF0;
    g_fifo_apply_count++;   /* reached TSF dispatch (passed validation) */

    switch (type) {
    case 0x80:
        tsf_channel_note_off(g_sfont, ch, d1);
        g_arm_note_off_count++;
        break;
    case 0x90:
        if (d2 == 0) {
            tsf_channel_note_off(g_sfont, ch, d1);
            g_arm_note_off_count++;
        } else {
            tsf_channel_note_on(g_sfont, ch, d1, d2 / 127.0f);
            g_arm_note_on_count++;
        }
        break;
    case 0xB0:
        tsf_channel_midi_control(g_sfont, ch, d1, d2);
        g_arm_cc_count++;
        break;
    case 0xC0:
        tsf_channel_set_presetnumber(g_sfont, ch, d1, (ch == 9));
        g_arm_pc_count++;
        break;
    case 0xE0:
        tsf_channel_set_pitchwheel(g_sfont, ch, ((int)d2 << 7) | (int)d1);
        g_arm_pb_count++;
        break;
    default:
        /* 0xA0 (aftertouch), 0xD0 (channel pressure): ignore for now */
        g_arm_ignored_count++;
        break;
    }
    g_arm_last_status = status;
    g_arm_last_data1  = d1;
    g_arm_last_data2  = d2;
}

/* ---------------------------------------------------------------------
 * Drain pending events from the realtime FIFO.
 *
 * The ARM owns read_idx. Header fields and indices are validated before
 * events are consumed, and each pass is bounded to avoid runaway loops.
 * --------------------------------------------------------------------- */
static void xmid_drain_fifo(void *fb_base)
{
    uint8_t  *fifo = (uint8_t *)fb_base + XMID_FIFO_OFFSET;
    uint32_t  magic, version, size, w, r;
    uint32_t  processed = 0;

    if (!g_sfont) { g_fifo_bail_reason = 1; return; }

    /* Invalidate the header line (magic..dropped) and the write_idx line. */
    Xil_DCacheInvalidateRange((INTPTR)fifo, 0x10);                 /* header */
    Xil_DCacheInvalidateRange((INTPTR)(fifo + XMID_FIFO_OFF_WRITE_IDX), 0x20);

    magic = ((uint32_t)fifo[XMID_FIFO_OFF_MAGIC+0] << 24) |
            ((uint32_t)fifo[XMID_FIFO_OFF_MAGIC+1] << 16) |
            ((uint32_t)fifo[XMID_FIFO_OFF_MAGIC+2] <<  8) |
             (uint32_t)fifo[XMID_FIFO_OFF_MAGIC+3];
    g_fifo_magic_seen = magic;
    if (magic != XMID_FIFO_MAGIC) { g_fifo_bail_reason = 2; return; }

    version = ((uint32_t)fifo[XMID_FIFO_OFF_VERSION+0] << 24) |
              ((uint32_t)fifo[XMID_FIFO_OFF_VERSION+1] << 16) |
              ((uint32_t)fifo[XMID_FIFO_OFF_VERSION+2] <<  8) |
               (uint32_t)fifo[XMID_FIFO_OFF_VERSION+3];
    if (version != XMID_FIFO_VERSION) { g_fifo_bail_reason = 3; return; }

    size = ((uint32_t)fifo[XMID_FIFO_OFF_SIZE+0] << 24) |
           ((uint32_t)fifo[XMID_FIFO_OFF_SIZE+1] << 16) |
           ((uint32_t)fifo[XMID_FIFO_OFF_SIZE+2] <<  8) |
            (uint32_t)fifo[XMID_FIFO_OFF_SIZE+3];
    if (size != XMID_FIFO_SLOTS) { g_fifo_bail_reason = 4; return; }

    w = ((uint32_t)fifo[XMID_FIFO_OFF_WRITE_IDX+0] << 24) |
        ((uint32_t)fifo[XMID_FIFO_OFF_WRITE_IDX+1] << 16) |
        ((uint32_t)fifo[XMID_FIFO_OFF_WRITE_IDX+2] <<  8) |
         (uint32_t)fifo[XMID_FIFO_OFF_WRITE_IDX+3];

    r = g_fifo_read;
    g_fifo_w_seen = w;
    g_fifo_r_seen = r;

    /* Validate indices. If out of range, resync to a SAFE value (0 or w if
 * w is valid) -- never lock g_fifo_read on an invalid w. */
    if (w >= size) {
        g_fifo_bail_reason = 5;
        g_fifo_read = 0;          /* w invalid: reset to 0, do not lock */
        return;
    }
    if (r >= size) {
        g_fifo_bail_reason = 6;
        g_fifo_read = (w < size) ? w : 0;  /* w is valid here, resync to it */
        return;
    }

    if (r == w) { g_fifo_bail_reason = 7; return; }  /* empty */

    /* Track the maximum observed FIFO depth. */
    {
        uint32_t depth = (w >= r) ? (w - r) : (size - r + w);
        if (depth > g_fifo_max_depth) g_fifo_max_depth = depth;
    }

    Xil_DCacheInvalidateRange((INTPTR)(fifo + XMID_FIFO_EVENTS_OFF),
                              size * 8u);

    while (r != w && processed < XMID_FIFO_DRAIN_MAX) {
        volatile uint8_t *ev = fifo + XMID_FIFO_EVENTS_OFF + r * 8u;
        uint8_t  status = ev[0];
        uint8_t  d1     = ev[1];
        uint8_t  d2     = ev[2];
        uint32_t st     = ((uint32_t)ev[4] << 24) | ((uint32_t)ev[5] << 16) |
                          ((uint32_t)ev[6] <<  8) |  (uint32_t)ev[7];

        if (processed == 0)
            g_fifo_ev0 = ((uint32_t)status << 16) |
                         ((uint32_t)d1 << 8) | d2;

        g_fifo_drained++;

        /* Validate before queueing (same rules as xmid_apply_event). */
        if (status < 0x80) {
            g_arm_ignored_count++; g_rej_status_low++;
            g_last_rej_low = ((uint32_t)status << 16) | ((uint32_t)d1 << 8) | d2;
        } else if (status >= 0xF0) {
            g_arm_ignored_count++; g_rej_status_sys++;
            g_last_rej_sys = ((uint32_t)status << 16) | ((uint32_t)d1 << 8) | d2;
        } else if ((d1 & 0x80) || (d2 & 0x80)) {
            g_arm_ignored_count++; g_rej_data_high++;
            g_last_rej_data = ((uint32_t)status << 16) | ((uint32_t)d1 << 8) | d2;
        } else {
            /* Valid channel event: stage into the realtime queue with its
 * sample_time so the render applies it at the right position. */
            uint32_t next_tail = (g_rt_q_tail + 1u) % XMID_RT_QUEUE_SIZE;
            if (next_tail == g_rt_q_head) {
                g_rt_q_dropped++;   /* queue full: drop (should not happen) */
            } else {
                g_rt_queue[g_rt_q_tail].status      = status;
                g_rt_queue[g_rt_q_tail].data1       = d1;
                g_rt_queue[g_rt_q_tail].data2       = d2;
                g_rt_queue[g_rt_q_tail].reserved    = 0;
                g_rt_queue[g_rt_q_tail].sample_time = st;
                g_rt_q_tail = next_tail;
                g_rt_scheduled++;
                {
                    uint32_t depth = (g_rt_q_tail >= g_rt_q_head) ?
                                     (g_rt_q_tail - g_rt_q_head) :
                                     (XMID_RT_QUEUE_SIZE - g_rt_q_head + g_rt_q_tail);
                    if (depth > g_rt_q_max_depth) g_rt_q_max_depth = depth;
                }
            }
        }
        g_arm_events_seen++;

        r = (r + 1) % size;
        processed++;
    }

    if (processed >= XMID_FIFO_DRAIN_MAX) {
        g_fifo_bail_reason = 8;
        g_fifo_drain_limit_hits++;
    } else {
        g_fifo_bail_reason = 0;
    }

    /* Publish new read_idx on its OWN cache line (big-endian). */
    g_fifo_read = r;
    fifo[XMID_FIFO_OFF_READ_IDX+0] = (uint8_t)((r >> 24) & 0xFF);
    fifo[XMID_FIFO_OFF_READ_IDX+1] = (uint8_t)((r >> 16) & 0xFF);
    fifo[XMID_FIFO_OFF_READ_IDX+2] = (uint8_t)((r >>  8) & 0xFF);
    fifo[XMID_FIFO_OFF_READ_IDX+3] = (uint8_t)( r        & 0xFF);
    Xil_DCacheFlushRange((INTPTR)(fifo + XMID_FIFO_OFF_READ_IDX), 0x20);
}

/* Publish FIFO state and counters through the control-block debug fields. */
static void xmid_publish_fifo_diag(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c       = (volatile XMID_Ctrl *)ctrl_raw;
    uint8_t            *fifo     = (uint8_t *)fb_base + XMID_FIFO_OFFSET;
    uint32_t            dropped;

    /* Read dropped (written by 68k at offset 0x0C, big-endian) */
    Xil_DCacheInvalidateRange((INTPTR)(fifo + XMID_FIFO_OFF_DROPPED), 0x20);
    dropped = ((uint32_t)fifo[XMID_FIFO_OFF_DROPPED+0] << 24) |
              ((uint32_t)fifo[XMID_FIFO_OFF_DROPPED+1] << 16) |
              ((uint32_t)fifo[XMID_FIFO_OFF_DROPPED+2] <<  8) |
               (uint32_t)fifo[XMID_FIFO_OFF_DROPPED+3];

    CTRL_WR(c, debug0, g_fifo_magic_seen);
    CTRL_WR(c, debug1, (g_fifo_w_seen << 16) | (g_fifo_r_seen & 0xFFFF));
    CTRL_WR(c, debug2, (g_fifo_bail_reason << 24) | (g_fifo_drained & 0xFFFFFF));
    CTRL_WR(c, debug3, g_fifo_ev0);
    /* Rejection counters. In realtime/FIFO mode xmid_do_event_batch
 * never runs, so these fields are exclusively ours here. Using a mix of
 * arm_* and sf2_* fields; daemon reads the matching offsets.
 * arm_last_event0 = apply_count
 * arm_last_event1 = (rej_status_low << 16) | (rej_status_sys & 0xFFFF)
 * arm_last_event2 = rej_data_high
 * sf2_total_size = last_rej_low
 * sf2_uploaded_bytes = last_rej_sys
 * sf2_chunk_offset = last_rej_data */
    CTRL_WR(c, arm_last_event0, g_fifo_apply_count);
    CTRL_WR(c, arm_last_event1, (g_rej_status_low << 16) |
                                (g_rej_status_sys & 0xFFFF));
    CTRL_WR(c, arm_last_event2, g_rej_data_high);
    CTRL_WR(c, sf2_total_size,     g_last_rej_low);
    CTRL_WR(c, sf2_uploaded_bytes, g_last_rej_sys);
    CTRL_WR(c, sf2_chunk_offset,   g_last_rej_data);
    /* Coupled drain/render counters. */
    CTRL_WR(c, arm_cc_count,  g_rt_drain_calls);
    CTRL_WR(c, arm_pb_count,  g_rt_render_calls);
    CTRL_WR(c, arm_ignored_count, g_rt_ring_at_apply);
    /* Timestamp-queue counters reuse arm_note_* and last_batch fields. */
    CTRL_WR(c, arm_note_on_count,  g_rt_scheduled);
    CTRL_WR(c, arm_note_off_count, g_rt_applied);
    CTRL_WR(c, arm_pc_count,       (g_rt_late_events << 16) |
                                   (g_rt_q_dropped & 0xFFFF));
    CTRL_WR(c, arm_last_batch_count, (g_rt_max_late << 16) |
                                     (g_rt_q_max_depth & 0xFFFF));
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
}

static void xmid_do_event_batch(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c       = (volatile XMID_Ctrl *)ctrl_raw;
    uint8_t            *evt_raw = (uint8_t *)fb_base + XMID_EVENT_STAGING_OFFSET;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    /* Invalidate event staging (written by 68k) */
    Xil_DCacheInvalidateRange((INTPTR)evt_raw, XMID_EVENT_STAGING_SIZE);

    uint32_t cmd_seq     = CTRL_RD(c, cmd_seq);
    /* event_count: 68k writes 4 explicit big-endian bytes. Read them
 * byte-by-byte so there is no endianness ambiguity. */
    uint32_t event_count = ((uint32_t)evt_raw[0] << 24) |
                           ((uint32_t)evt_raw[1] << 16) |
                           ((uint32_t)evt_raw[2] <<  8) |
                            (uint32_t)evt_raw[3];

    if (event_count > XMID_EVENT_MAX_BATCH)
        event_count = XMID_EVENT_MAX_BATCH;

    /* ARM counters exposed through the control-block debug fields. */
    g_arm_event_batches++;
    g_arm_events_seen += event_count;
    g_arm_last_batch_count = event_count;

    if (!g_sfont) {
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        return;
    }

    {
        uint32_t i;
        for (i = 0; i < event_count; i++) {
            /* Byte-by-byte read with fixed stride 8, matching 68k layout:
 * [status][data1][data2][reserved][sample_time u32] */
            volatile uint8_t *ev = (volatile uint8_t *)(evt_raw + 4 + i * 8);
            uint8_t status = ev[0];
            uint8_t d1     = ev[1];
            uint8_t d2     = ev[2];

            if (i == 0)
                g_arm_last_event0 = ((uint32_t)status << 16) |
                                    ((uint32_t)d1 << 8) | d2;
            else if (i == 1)
                g_arm_last_event1 = ((uint32_t)status << 16) |
                                    ((uint32_t)d1 << 8) | d2;
            else if (i == 2)
                g_arm_last_event2 = ((uint32_t)status << 16) |
                                    ((uint32_t)d1 << 8) | d2;

            xmid_apply_event(status, d1, d2);
        }
    }

    /* Publish ARM counters through compatibility fields not used by the
     * render timing counters. */
    CTRL_WR(c, sf2_total_size,     g_arm_events_seen);
    CTRL_WR(c, sf2_uploaded_bytes, g_arm_note_on_count);
    CTRL_WR(c, sf2_chunk_offset,   (g_arm_last_status << 16) |
                                   (g_arm_last_data1  << 8)  |
                                    g_arm_last_data2);
    /* Realtime batch counters. */
    CTRL_WR(c, arm_last_batch_count, g_arm_last_batch_count);
    CTRL_WR(c, arm_note_on_count,    g_arm_note_on_count);
    CTRL_WR(c, arm_note_off_count,   g_arm_note_off_count);
    CTRL_WR(c, arm_pc_count,         g_arm_pc_count);
    CTRL_WR(c, arm_cc_count,         g_arm_cc_count);
    CTRL_WR(c, arm_pb_count,         g_arm_pb_count);
    CTRL_WR(c, arm_ignored_count,    g_arm_ignored_count);
    CTRL_WR(c, arm_last_event0,      g_arm_last_event0);
    CTRL_WR(c, arm_last_event1,      g_arm_last_event1);
    CTRL_WR(c, arm_last_event2,      g_arm_last_event2);
    CTRL_WR(c, done_seq, cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
}

/* -----------------------------------------------------------------------
 * ALL_NOTES_OFF -- silence all channels immediately
 * --------------------------------------------------------------------- */
static void xmid_do_all_notes_off(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c       = (volatile XMID_Ctrl *)ctrl_raw;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    uint32_t cmd_seq = CTRL_RD(c, cmd_seq);

    if (g_sfont) {
        int ch;
        tsf_note_off_all(g_sfont);
        for (ch = 0; ch < 16; ch++)
            tsf_channel_midi_control(g_sfont, ch, 123, 0);
    }

    CTRL_WR(c, done_seq, cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
    xil_printf("[xmid] ALL_NOTES_OFF\r\n");
}

/* -----------------------------------------------------------------------
 * REALTIME_STOP -- stop realtime rendering, silence
 * --------------------------------------------------------------------- */
static void xmid_do_realtime_stop(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c       = (volatile XMID_Ctrl *)ctrl_raw;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    uint32_t cmd_seq = CTRL_RD(c, cmd_seq);

    /* Stop rendering first so the reset is not racing the render loop. */
    g_playing  = 0;
    g_realtime_active = 0;
    g_midi_cur = NULL;

    /* HARD RESET on stop too: silence TSF, clear queue, clean the FIFO DDR
 * header and PCM counters. With RTOFF resetting everything, a following
 * RTON always starts from a guaranteed-clean baseline. */
    xmid_hard_reset_realtime(fb_base);

    CTRL_WR(c, state,    XMID_STATE_DONE);
    CTRL_WR(c, error,    XMID_ERR_NONE);
    CTRL_WR(c, done_seq, cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
    xil_printf("[xmid] REALTIME_STOP (hard reset)\r\n");
}


/* -----------------------------------------------------------------------
 * xmid_render_one_block()
 *
 * Renders one block of XMID_RENDER_BLOCK_FRAMES frames into the DDR ring.
 * Returns 1 if MIDI file playback is complete, 0 otherwise.
 * In realtime mode (g_midi == NULL), never returns 1.
 * Caller must verify free space before calling.
 * --------------------------------------------------------------------- */
static int xmid_render_one_block(uint8_t *pcm_ring,
                                  volatile XMID_Ctrl *c)
{
    uint32_t frames     = XMID_RENDER_BLOCK_FRAMES;
    uint32_t block_bytes = frames * XMID_DEFAULT_CHANNELS * 2u;
    double   block_ms   = (double)frames / (double)g_sample_rate * 1000.0;
    uint32_t write_pos;

    /* Process MIDI file events up to current time (file mode only) */
    for (; g_midi_cur && g_midi_cur->time <= (uint32_t)g_midi_time;
         g_midi_cur = g_midi_cur->next) {
        switch (g_midi_cur->type) {
        case TML_PROGRAM_CHANGE:
            tsf_channel_set_presetnumber(g_sfont,
                g_midi_cur->channel, g_midi_cur->program,
                (g_midi_cur->channel == 9));
            break;
        case TML_NOTE_ON:
            if (g_midi_cur->velocity > 0)
                tsf_channel_note_on(g_sfont, g_midi_cur->channel,
                    g_midi_cur->key, g_midi_cur->velocity / 127.0f);
            else
                tsf_channel_note_off(g_sfont, g_midi_cur->channel,
                    g_midi_cur->key);
            break;
        case TML_NOTE_OFF:
            tsf_channel_note_off(g_sfont, g_midi_cur->channel,
                g_midi_cur->key);
            break;
        case TML_PITCH_BEND:
            tsf_channel_set_pitchwheel(g_sfont, g_midi_cur->channel,
                g_midi_cur->pitch_bend);
            break;
        case TML_CONTROL_CHANGE:
            tsf_channel_midi_control(g_sfont, g_midi_cur->channel,
                g_midi_cur->control, g_midi_cur->control_value);
            break;
        default:
            break;
        }
    }

    /* Render audio into static buffer */
    memset(g_render_buf, 0, block_bytes);
    tsf_render_short(g_sfont, g_render_buf, (int)frames, 0);
    g_midi_time += block_ms;

    /* Write to ring with wrap handling */
    write_pos = g_pcm_write_total % XMID_PCM_RING_SIZE;
    if (write_pos + block_bytes <= XMID_PCM_RING_SIZE) {
        memcpy(pcm_ring + write_pos, g_render_buf, block_bytes);
        xmid_flush(pcm_ring + write_pos, block_bytes);
    } else {
        uint32_t part1 = XMID_PCM_RING_SIZE - write_pos;
        uint32_t part2 = block_bytes - part1;
        memcpy(pcm_ring + write_pos, g_render_buf, part1);
        xmid_flush(pcm_ring + write_pos, part1);
        memcpy(pcm_ring, (uint8_t *)g_render_buf + part1, part2);
        xmid_flush(pcm_ring, part2);
    }
    g_pcm_write_total += block_bytes;

    /* Realtime mode (g_midi == NULL): never signal done.
 * File mode: done when cursor exhausted and time past total_ms. */
    if (g_midi == NULL) return 0;
    return (g_midi_cur == NULL && g_midi_time >= (double)g_total_ms);
}

/* -----------------------------------------------------------------------
 * Apply one group of realtime events sharing the same timestamp.
 * --------------------------------------------------------------------- */
static int xmid_is_real_note_on(const XMID_RT_Event *e)
{
    return ((e->status & 0xF0u) == 0x90u && e->data2 != 0u);
}

static uint32_t xmid_apply_one_due_group(uint32_t now)
{
    uint32_t t;
    uint32_t scan, run_end;
    uint32_t consumed = 0u;

    if (g_rt_q_head == g_rt_q_tail) return 0u;

    t = g_rt_queue[g_rt_q_head].sample_time;
    if (t > now) return 0u;

    /* Collect only events with EXACTLY this sample_time. */
    scan = g_rt_q_head;
    while (scan != g_rt_q_tail && g_rt_queue[scan].sample_time == t)
        scan = (scan + 1u) % XMID_RT_QUEUE_SIZE;
    run_end = scan;

    if (now > t) {
        uint32_t late = now - t;
        g_rt_late_events++;
        if (late > g_rt_max_late) g_rt_max_late = late;
    }

    /* Pass 1: everything that is NOT a real NoteOn. */
    scan = g_rt_q_head;
    while (scan != run_end) {
        XMID_RT_Event *e = &g_rt_queue[scan];
        if (!xmid_is_real_note_on(e)) {
            xmid_apply_event(e->status, e->data1, e->data2);
            g_rt_applied++;
            consumed++;
        }
        scan = (scan + 1u) % XMID_RT_QUEUE_SIZE;
    }

    /* Pass 2: real NoteOn last, so voices are allocated after same-time
 * NoteOff has freed them. */
    scan = g_rt_q_head;
    while (scan != run_end) {
        XMID_RT_Event *e = &g_rt_queue[scan];
        if (xmid_is_real_note_on(e)) {
            xmid_apply_event(e->status, e->data1, e->data2);
            g_rt_applied++;
            consumed++;
        }
        scan = (scan + 1u) % XMID_RT_QUEUE_SIZE;
    }

    g_rt_q_head = run_end;
    return consumed;
}



/* Render `frames` of audio into the ring at the current write cursor. */
static void xmid_emit_frames(uint8_t *pcm_ring, uint32_t frames)
{
    uint32_t bytes, write_pos;
    if (frames == 0) return;
    bytes = frames * XMID_DEFAULT_CHANNELS * 2u;

    memset(g_render_buf, 0, bytes);
    tsf_render_short(g_sfont, g_render_buf, (int)frames, 0);

    write_pos = g_pcm_write_total % XMID_PCM_RING_SIZE;
    if (write_pos + bytes <= XMID_PCM_RING_SIZE) {
        memcpy(pcm_ring + write_pos, g_render_buf, bytes);
        xmid_flush(pcm_ring + write_pos, bytes);
    } else {
        uint32_t part1 = XMID_PCM_RING_SIZE - write_pos;
        uint32_t part2 = bytes - part1;
        memcpy(pcm_ring + write_pos, g_render_buf, part1);
        xmid_flush(pcm_ring + write_pos, part1);
        memcpy(pcm_ring, (uint8_t *)g_render_buf + part1, part2);
        xmid_flush(pcm_ring, part2);
    }
    g_pcm_write_total += bytes;
}

static void xmid_render_realtime_coupled(void *fb_base)
{
    uint8_t            *ctrl_raw  = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c        = (volatile XMID_Ctrl *)ctrl_raw;
    uint8_t            *pcm_ring  = (uint8_t *)fb_base + XMID_PCM_RING_OFFSET;
    uint32_t            block_frames = XMID_RENDER_BLOCK_FRAMES;
    uint32_t            block_bytes  = block_frames *
                                       XMID_DEFAULT_CHANNELS * 2u;
    uint32_t            target_bytes = XMID_RT_TARGET_FRAMES *
                                       XMID_DEFAULT_CHANNELS * 2u;
    uint32_t            pcm_read, available, free_bytes;
    uint32_t            blocks = 0u;

    /* Pull all currently-available events into the timestamped queue. */
    xmid_drain_fifo(fb_base);
    g_rt_drain_calls++;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    pcm_read  = CTRL_RD(c, pcm_read_total);
    /* Guard against unsigned underflow: if AHI has consumed more than we
 * have written (startup, or ring drained), available is 0 (empty ring),
 * NOT a huge number -- otherwise the render loop never runs and we go
 * silent. */
    available  = (g_pcm_write_total > pcm_read) ?
                 (g_pcm_write_total - pcm_read) : 0u;
    /* Defensive clamp: a corrupt ctrl read must never make available huge
 * and silently block the render. */
    if (available > XMID_PCM_RING_SIZE) available = XMID_PCM_RING_SIZE;
    free_bytes = XMID_PCM_RING_SIZE - available;
    if (available < g_ring_level_min) g_ring_level_min = available;

    XTime t_start = 0, t_end = 0;
    XTime_GetTime(&t_start);

    /* Render up to MAX_BLOCKS blocks, placing queued events at their
 * sample_time within each block. */
    while (blocks < XMID_RT_MAX_BLOCKS_PER_POLL &&
           available < target_bytes &&
           free_bytes >= block_bytes) {

        uint32_t pos       = g_pcm_write_total / 4u;          /* frame cursor */
        uint32_t block_end = pos + block_frames;

        while (pos < block_end) {
            /* Peek next due event. */
            if (g_rt_q_head == g_rt_q_tail) {
                /* Queue empty: render the rest of the block. */
                xmid_emit_frames(pcm_ring, block_end - pos);
                pos = block_end;
                break;
            }
            {
                XMID_RT_Event *ev = &g_rt_queue[g_rt_q_head];
                uint32_t st = ev->sample_time;

                if (st >= block_end) {
                    /* Next event is beyond this block: finish the block. */
                    xmid_emit_frames(pcm_ring, block_end - pos);
                    pos = block_end;
                    break;
                }

                if (st <= pos) {
                    /* Due now (or late): apply same-time groups one at a
 * time, in canonical order, until none remain due. The
 * guard prevents any infinite loop if a group consumes
 * nothing. */
                    uint32_t did = xmid_apply_one_due_group(pos);
                    if (did == 0u) {
                        g_rt_no_progress++;
                        /* Force progress: drop the stuck head event. */
                        g_rt_q_head = (g_rt_q_head + 1u) % XMID_RT_QUEUE_SIZE;
                    }
                    continue;
                }

                /* Event is in the future of this block: render up to it,
 * then apply the group at that exact frame in order. */
                xmid_emit_frames(pcm_ring, st - pos);
                pos = st;
                {
                    uint32_t did = xmid_apply_one_due_group(pos);
                    if (did == 0u) {
                        g_rt_no_progress++;
                        g_rt_q_head = (g_rt_q_head + 1u) % XMID_RT_QUEUE_SIZE;
                    }
                }
            }
        }

        g_rt_render_calls++;
        available  += block_bytes;
        free_bytes -= block_bytes;
        blocks++;
    }

    if (blocks == 0) g_underruns++;  /* ring full: backpressure */

    /* Publish the render cursor so the daemon timestamps events against the
 * SAME clock. Without this, the daemon reads a stale pcm_write_total and
 * schedules events in the past/future incorrectly. */
    CTRL_WR(c, pcm_write_total, g_pcm_write_total);
    CTRL_WR(c, underruns,       g_underruns);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));

    XTime_GetTime(&t_end);
    if (t_end > t_start)
        g_render_us_last = (uint32_t)((t_end - t_start) /
                           (COUNTS_PER_SECOND / 1000000u));
}


/* -----------------------------------------------------------------------
 * xmid_render_block() -- file mode multi-block render.
 *
 * Multi-block logic ():
 * - Read pcm_read_total from Amiga (invalidate cache first)
 * - Compute available and free bytes in ring
 * - If available >= XMID_TARGET_FRAMES*4 bytes: ring is full enough,
 * skip (ARM backpressure -- normal, not an error)
 * - Otherwise: render up to XMID_RENDER_MAX_BLOCKS_PER_POLL blocks
 * until ring reaches target OR no more free space
 *
 * Two distinct counters:
 * g_underruns : ARM backpressure skips (ring too full to write)
 * stored in XMID_Ctrl.underruns
 * (AHI underruns are counted on the 68k side in ZZMIDIPlay)
 * --------------------------------------------------------------------- */
static void xmid_render_block(void *fb_base)
{
    uint8_t            *ctrl_raw  = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c        = (volatile XMID_Ctrl *)ctrl_raw;
    uint8_t            *pcm_ring  = (uint8_t *)fb_base + XMID_PCM_RING_OFFSET;
    uint32_t            block_bytes = XMID_RENDER_BLOCK_FRAMES *
                                      XMID_DEFAULT_CHANNELS * 2u;
    /* Realtime (CAMD/FIFO) keeps very little PCM ahead so externally-timed
 * events are heard promptly. File mode keeps the large lookahead. */
    uint32_t            target_frames = g_realtime_active ?
                                        XMID_RT_TARGET_FRAMES : XMID_TARGET_FRAMES;
    uint32_t            max_blocks    = g_realtime_active ?
                                        XMID_RT_MAX_BLOCKS_PER_POLL :
                                        XMID_RENDER_MAX_BLOCKS_PER_POLL;
    uint32_t            target_bytes = target_frames *
                                       XMID_DEFAULT_CHANNELS * 2u;
    uint32_t            pcm_read, available, free_bytes;
    uint32_t            blocks = 0u;
    int                 midi_done = 0;

    /* Read Amiga read pointer -- must invalidate, Amiga writes this */
    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
    pcm_read  = CTRL_RD(c, pcm_read_total);
    available = g_pcm_write_total - pcm_read;
    free_bytes = XMID_PCM_RING_SIZE - available;

    /* Track lowest ring fill seen (0 = underrun = audible gap) */
    if (available < g_ring_level_min) g_ring_level_min = available;

    /* Ring is full enough: ARM backpressure -- skip this poll */
    if (available >= target_bytes) {
        g_underruns++;
        /* No flush needed: just a counter update, not critical */
        return;
    }

    /* Render up to XMID_RENDER_MAX_BLOCKS_PER_POLL blocks.
 * Time the whole render burst with the Global Timer. */
    XTime t_start = 0, t_end = 0;
    XTime_GetTime(&t_start);

    while (blocks < max_blocks &&
           available < target_bytes &&
           free_bytes >= block_bytes &&
           !midi_done) {

        midi_done = xmid_render_one_block(pcm_ring, c);
        available  += block_bytes;
        free_bytes -= block_bytes;
        blocks++;
    }

    if (blocks > 0u) {
        uint32_t us;
        XTime_GetTime(&t_end);
        /* Convert timer ticks to microseconds.
 * COUNTS_PER_SECOND is the Global Timer frequency. */
        us = (uint32_t)(((t_end - t_start) * 1000000ULL) /
                        (XTime)COUNTS_PER_SECOND);
        /* Per-block average so the number is comparable to the audio
 * budget of one block regardless of how many blocks we did. */
        g_render_us_last = us / blocks;
        if (g_render_us_last > g_render_us_max)
            g_render_us_max = g_render_us_last;
    }

    /* Update control block once after all blocks */
    {
        uint32_t play_ms = (uint32_t)g_midi_time;
        uint32_t peak = 0u, rms = 0u;
        xmid_calc_stats(g_render_buf, XMID_RENDER_BLOCK_FRAMES,
                        XMID_DEFAULT_CHANNELS, &peak, &rms);

        CTRL_WR(c, pcm_write_total,  g_pcm_write_total);
        CTRL_WR(c, rendered_samples, g_pcm_write_total /
                                     (XMID_DEFAULT_CHANNELS * 2u));
        CTRL_WR(c, play_ms,          play_ms);
        CTRL_WR(c, peak_abs,         peak);
        CTRL_WR(c, rms_last,         rms);
        CTRL_WR(c, underruns,        g_underruns);
        CTRL_WR(c, heap_used,        xmid_heap_used());
        CTRL_WR(c, heap_highwater,   xmid_heap_highwater());

        /* Publish render timing counters.
         * debug0 = last block render time in microseconds
         * debug1 = maximum block render time
         * debug2 = minimum PCM ring level in bytes */
        CTRL_WR(c, debug0,        g_render_us_last);
        CTRL_WR(c, debug1,        g_render_us_max);
        CTRL_WR(c, debug2,        (g_ring_level_min == 0xFFFFFFFFu)
                                  ? 0u : g_ring_level_min);

        if (midi_done) {
            g_playing = 0;
            CTRL_WR(c, state, XMID_STATE_DONE);
            CTRL_WR(c, error, XMID_ERR_NONE);
            xil_printf("[xmid] DONE at %lu ms blocks/poll=%lu\r\n",
                       (unsigned long)play_ms, (unsigned long)blocks);
        }
    }

    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
}

/* -----------------------------------------------------------------------
 * xmid_pool_memtest()
 *
 * Tests the XACP private pool at XMID_PRIVATE_POOL_BASE_ABS (v1.6:
 * absolute ARM address 0x23000000). NO TSF, NO TML, NO AHI, NO 68k reads.
 *
 * The 68k only reads the result fields after done_seq == cmd_seq.
 * The ARM writes and reads the pool itself, verifying patterns.
 *
 * Reports via XMID_Ctrl:
 * debug0 = last good address reached (or pool base)
 * debug1 = total bytes tested
 * debug2 = first failing absolute offset (0xFFFFFFFF if all OK)
 * debug3 = pattern that failed (0 if all OK)
 * error = XMID_ERR_NONE if OK, else a fail code
 *
 * Progressive 1 MB block strategy (see function body).
 * --------------------------------------------------------------------- */

static void xmid_pool_memtest(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c        = (volatile XMID_Ctrl *)ctrl_raw;
    uint32_t            cmd_seq   = CTRL_RD(c, cmd_seq);
    uint32_t            fail_off  = 0xFFFFFFFFu;
    uint32_t            fail_pat  = 0u;
    int                 ok        = 1;

    /* POOL_TEST is destructive and is only allowed while the engine is idle. */
    if (g_sfont || g_midi || g_playing ||
        g_sf2_uploading || g_midi_uploading ||
        g_sf2_total != 0u || g_midi_total != 0u) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_STATE);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] POOL_TEST refused: engine not clean\r\n");
        return;
    }

    /* Configure the private-pool MMU attributes before accessing the pool. */
    xmid_heap_set_base(fb_base);

    /* Test the private pool one 1 MB block at a time. Large single-pass
 * writes are intentionally avoided to limit bus and cache pressure.
 *
 * Each block is checked in three phases:
 * MODE 1 SPARSE : write and verify 16 distributed words.
 * MODE 2 FILL : fill and verify the complete 1 MB block.
 * MODE 3 FLUSH/INV: flush/invalidate and verify against DDR.
 *
 * debug0 = last successfully tested address.
 * debug2 = first failing absolute offset (0xFFFFFFFF if all pass).
 * debug3 = failing test pattern. */

    uint32_t pool_base  = XMID_PRIVATE_POOL_BASE_ABS;  /* 0x23000000 (v1.6) */
    uint32_t total_test = (uint32_t)(XMID_PRIVATE_POOL_END_ABS - XMID_PRIVATE_POOL_BASE_ABS);
    /* = 0x30000000 - 0x23000000 = 0x0D000000 = 208 MB */
    uint32_t mb;
    uint32_t last_good  = pool_base;

    xil_printf("[xmid] POOL_TEST XX19a base=0x%08lx total=%lu MB\r\n",
               (unsigned long)pool_base,
               (unsigned long)(total_test / (1024u*1024u)));

    for (mb = 0u; mb < total_test && ok; mb += (1024u*1024u)) {
        volatile uint32_t *blk = (volatile uint32_t *)(pool_base + mb);
        uint32_t words = (1024u*1024u) / 4u;
        uint32_t i, step;

        /* Publish progress BEFORE touching the block, so if this block
 * faults/reboots, debug0 shows the last good address. */
        CTRL_WR(c, debug0, last_good);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));

        /* --- MODE 1 SPARSE: 16 words spread across the MB --- */
        step = words / 16u;
        for (i = 0; i < words; i += step) blk[i] = 0xDEADBEEFu;
        for (i = 0; i < words; i += step) {
            if (blk[i] != 0xDEADBEEFu) {
                fail_off = pool_base + mb + i*4u; fail_pat = 0xDEADBEEFu;
                ok = 0; break;
            }
        }
        if (!ok) break;

        /* --- MODE 2 FILL: full MB, read back (cache may serve) --- */
        for (i = 0; i < words; i++) blk[i] = 0x55AA55AAu;
        for (i = 0; i < words; i += 64u) {  /* sample every 256 bytes */
            if (blk[i] != 0x55AA55AAu) {
                fail_off = pool_base + mb + i*4u; fail_pat = 0x55AA55AAu;
                ok = 0; break;
            }
        }
        if (!ok) break;

        /* --- MODE 3 FLUSH + INVALIDATE: real DDR round-trip --- */
        /* Keep cache maintenance bounded to each tested block. */
#if 0
        for (i = 0; i < words; i++) blk[i] = 0xAA55AA55u;
        Xil_DCacheFlushRange((INTPTR)blk, 1024u*1024u);
        Xil_DCacheInvalidateRange((INTPTR)blk, 1024u*1024u);
        for (i = 0; i < words; i += 64u) {
            if (blk[i] != 0xAA55AA55u) {
                fail_off = pool_base + mb + i*4u; fail_pat = 0xAA55AA55u;
                ok = 0; break;
            }
        }
        if (!ok) break;
#endif

        /* Block passed all three modes */
        last_good = pool_base + mb + (1024u*1024u) - 1u;

        /* Short pause every 16 MB to avoid sustained bus saturation */
        if ((mb % (16u*1024u*1024u)) == 0u) {
            xil_printf("[xmid] tested up to 0x%08lx\r\n",
                       (unsigned long)(pool_base + mb));
        }
    }

    /* Report */
    CTRL_WR(c, debug0, last_good);
    CTRL_WR(c, debug1, total_test);
    CTRL_WR(c, debug2, fail_off);
    CTRL_WR(c, debug3, fail_pat);
    if (ok) {
        CTRL_WR(c, state, XMID_STATE_DONE);
        CTRL_WR(c, error, XMID_ERR_NONE);
        xil_printf("[xmid] POOL_TEST ALL OK\r\n");
    } else {
        CTRL_WR(c, state, XMID_STATE_ERROR);
        CTRL_WR(c, error, XMID_ERR_NO_HEAP);
        xil_printf("[xmid] POOL_TEST FAIL off=0x%08lx pat=0x%08lx\r\n",
                   (unsigned long)fail_off, (unsigned long)fail_pat);
    }
    CTRL_WR(c, done_seq, cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
}


/* -----------------------------------------------------------------------
 * xmid_handle_opcode()
 *
 * Called from case 0x64 BEFORE "if (cmd >= 0x0100)".
 * Synchronous: RESET, STATUS.
 * Deferred: SELFTEST, LOAD_MIDI, START, STOP, POOL_TEST.
 * --------------------------------------------------------------------- */
void xmid_handle_opcode(void *fb_base)
{
    uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
    volatile XMID_Ctrl *c       = (volatile XMID_Ctrl *)ctrl_raw;

    xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));

    uint32_t magic   = CTRL_RD(c, magic);
    uint32_t subcmd  = CTRL_RD(c, subcmd);
    uint32_t cmd_seq = CTRL_RD(c, cmd_seq);

    if (magic != XMID_MAGIC) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_MAGIC);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] bad magic 0x%08lx\r\n", (unsigned long)magic);
        return;
    }

    /* Reject clients using an incompatible control-block ABI. */
    {
        uint32_t abi_version = CTRL_RD(c, version);
        uint32_t abi_ssize   = CTRL_RD(c, struct_size);
        if (abi_version != XMID_VERSION ||
            abi_ssize   != (uint32_t)sizeof(XMID_Ctrl)) {
            CTRL_WR(c, state,    XMID_STATE_ERROR);
            CTRL_WR(c, error,    XMID_ERR_BAD_ABI);
            CTRL_WR(c, done_seq, cmd_seq);
            xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
            xil_printf("[xmid] ABI reject ver=%lu size=%lu\r\n",
                       (unsigned long)abi_version,
                       (unsigned long)abi_ssize);
            return;
        }
    }

    /* SELFTEST disabled -- it still used the removed v1.5
 * shared-staging model (tsf_load from an arbitrary fb offset) and
 * reset the heap under a live g_sfont. A v1.6-aware SELFTEST may
 * return later. */
    if (subcmd == XMID_CMD_SELFTEST) {
        CTRL_WR(c, state,    XMID_STATE_ERROR);
        CTRL_WR(c, error,    XMID_ERR_BAD_STATE);
        CTRL_WR(c, done_seq, cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] SELFTEST disabled in v1.6\r\n");
        return;
    }

    if (subcmd == XMID_CMD_RESET) {
        g_playing = 0;
        if (g_sfont) { tsf_close(g_sfont); g_sfont = NULL; }
        if (g_midi)  { tml_free(g_midi);   g_midi  = NULL; }
        g_midi_cur = NULL; g_midi_time = 0.0;
        g_pcm_write_total = 0u; g_underruns = 0u;
        /* SF2 is gone after a heap reset -- force a reload next time. */
        g_sfont_ready    = 0;
        g_heap_after_sf2 = 0u;
        g_loaded_sf2_off = 0u;
        g_loaded_sf2_sz  = 0u;
        xmid_heap_set_base(fb_base);
        xmid_reset_heap();
        xmid_job_pending = 0;
        /* XX19a: clear both chunked-upload state machines */
        g_sf2_uploading  = 0;
        g_sf2_total      = 0u;
        g_sf2_uploaded   = 0u;
        g_sf2_chunks     = 0u;
        g_midi_uploading = 0;
        g_midi_total     = 0u;
        g_midi_uploaded  = 0u;
        CTRL_WR(c, state,          XMID_STATE_EMPTY);
        CTRL_WR(c, error,          XMID_ERR_NONE);
        CTRL_WR(c, done_seq,       cmd_seq);
        CTRL_WR(c, heap_used,      0u);
        CTRL_WR(c, heap_highwater, 0u);
        CTRL_WR(c, pcm_write_total, 0u);
        CTRL_WR(c, pcm_read_total,  0u);
        CTRL_WR(c, underruns,      0u);
        CTRL_WR(c, debug3,         0u);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xil_printf("[xmid] RESET\r\n");
        return;
    }

    if (subcmd == XMID_CMD_STATUS) {
        CTRL_WR(c, heap_used,      xmid_heap_used());
        CTRL_WR(c, heap_highwater, xmid_heap_highwater());
        CTRL_WR(c, underruns,      g_underruns);
        CTRL_WR(c, done_seq,       cmd_seq);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        return;
    }

    /* Deferred */
    if (subcmd == XMID_CMD_LOAD_MIDI ||
        subcmd == XMID_CMD_START ||
        subcmd == XMID_CMD_STOP ||
        subcmd == XMID_CMD_POOL_TEST ||
        subcmd == XMID_CMD_REALTIME_INIT ||
        subcmd == XMID_CMD_EVENT_BATCH ||
        subcmd == XMID_CMD_ALL_NOTES_OFF ||
        subcmd == XMID_CMD_REALTIME_STOP ||
        subcmd == XMID_CMD_SF2_BEGIN ||
        subcmd == XMID_CMD_SF2_CHUNK ||
        subcmd == XMID_CMD_SF2_COMMIT_LOAD ||
        subcmd == XMID_CMD_SF2_FAKE_COMMIT ||
        subcmd == XMID_CMD_MIDI_BEGIN ||
        subcmd == XMID_CMD_MIDI_CHUNK) {
        CTRL_WR(c, error, XMID_ERR_NONE);
        xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
        xmid_job_pending = 1;
        xil_printf("[xmid] subcmd %lu queued\r\n", (unsigned long)subcmd);
        return;
    }

    CTRL_WR(c, state,    XMID_STATE_ERROR);
    CTRL_WR(c, error,    0xFFu);
    CTRL_WR(c, done_seq, cmd_seq);
    xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
    xil_printf("[xmid] unknown subcmd %lu\r\n", (unsigned long)subcmd);
}

/* -----------------------------------------------------------------------
 * xmid_idle_poll()
 *
 * Called from the main idle loop after the streaming block.
 * - Executes any pending queued job.
 * - If PLAYING, renders one block per call (deferred job = 0 when playing).
 * --------------------------------------------------------------------- */
void xmid_idle_poll(void *fb_base)
{
    /* Execute queued one-shot job first */
    if (xmid_job_pending == 1) {
        xmid_job_pending = 2;

        uint8_t            *ctrl_raw = (uint8_t *)fb_base + XMID_CTRL_OFFSET;
        volatile XMID_Ctrl *c       = (volatile XMID_Ctrl *)ctrl_raw;
        xmid_invalidate(ctrl_raw, sizeof(XMID_Ctrl));
        uint32_t subcmd  = CTRL_RD(c, subcmd);
        uint32_t cmd_seq = CTRL_RD(c, cmd_seq);

        switch (subcmd) {
        /* XMID_CMD_SELFTEST: removed from dispatch (). subcmd is
 * re-read from DDR here, so keeping the case left a theoretical
 * window to reach the old v1.5 selftest path. */
        case XMID_CMD_LOAD_MIDI:       xmid_do_load_midi(fb_base);      break;
        case XMID_CMD_START:           xmid_do_start(fb_base);          break;
        case XMID_CMD_STOP:            xmid_do_stop(fb_base);           break;
        case XMID_CMD_POOL_TEST:       xmid_pool_memtest(fb_base);      break;
        case XMID_CMD_REALTIME_INIT:   xmid_do_realtime_init(fb_base);  break;
        case XMID_CMD_EVENT_BATCH:     xmid_do_event_batch(fb_base);    break;
        case XMID_CMD_ALL_NOTES_OFF:   xmid_do_all_notes_off(fb_base);  break;
        case XMID_CMD_REALTIME_STOP:   xmid_do_realtime_stop(fb_base);  break;
        /* XX19a XACP v1.6: chunked upload protocol */
        case XMID_CMD_SF2_BEGIN:       xmid_do_sf2_begin(fb_base);      break;
        case XMID_CMD_SF2_CHUNK:       xmid_do_sf2_chunk(fb_base);      break;
        case XMID_CMD_SF2_COMMIT_LOAD: xmid_do_sf2_commit(fb_base);     break;
        case XMID_CMD_SF2_FAKE_COMMIT: xmid_do_sf2_fake_commit(fb_base);break;
        case XMID_CMD_MIDI_BEGIN:      xmid_do_midi_begin(fb_base);     break;
        case XMID_CMD_MIDI_CHUNK:      xmid_do_midi_chunk(fb_base);     break;
        default:
            CTRL_WR(c, state,    XMID_STATE_ERROR);
            CTRL_WR(c, error,    0xFEu);
            CTRL_WR(c, done_seq, cmd_seq);
            xmid_flush(ctrl_raw, sizeof(XMID_Ctrl));
            xil_printf("[xmid] subcmd %lu not implemented\r\n",
                       (unsigned long)subcmd);
            break;
        }
        xmid_job_pending = 0;
        return;
    }

    /* Realtime mode couples FIFO draining with short render quanta.
     * File mode uses the normal drain-then-render path. */
    if (g_playing) {
        if (g_realtime_active) {
            xmid_render_realtime_coupled(fb_base);
        } else {
            xmid_drain_fifo(fb_base);
            xmid_render_block(fb_base);
        }
        xmid_publish_fifo_diag(fb_base);
    }
}
