/*
 * ZZSIDPlay.c - ZZReSID Core1 player, built on VALIDATED AHITest model
 *
 * Uses the exact g_acur/g_anext/WaitIO double-buffer from AHITest.c
 * (which plays clean 440Hz with responsive Ctrl-C).
 *
 * MODE 2 (default): Core1 pre-renders into ring, 68k copies one block
 *   (RING_SAMPLES frames) and LOOPS it via AHI. No real-time ring sync.
 *   Proves Core1 PCM -> AHI continuous playback.
 *
 * MODE 1 (-ahi): 68k-only 440Hz sine (same as AHITest, sanity check).
 *
 * Build:
 *   m68k-amigaos-gcc -O2 -noixemul -m68020 -Wno-pointer-sign \
 *       -o ZZSIDPlay ZZSIDPlay.c -lamiga
 *
 * ASCII-only.  Author: Xanxi / 2026
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <libraries/expansion.h>
#include <devices/ahi.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/expansion.h>
#include <string.h>
#include <stdlib.h>

#include "zzresid_shared.h"

/* ===================================================================
 * Diagnostic output.
 *
 * The engine below prints a great deal, which is exactly right for the
 * CLI build and wrong for the GUI: a program launched from Workbench has
 * no console at all, and one that opens a CLI window uninvited is a
 * nuisance. In the GUI build every Printf and PutStr in this file is
 * therefore redirected here, and goes nowhere until the user ticks
 * Project/Show debug, which opens a CON: window.
 *
 * The (LONG *)ap cast is the standard Amiga varargs bridge: on m68k a
 * va_list is a pointer to the arguments laid out contiguously on the
 * stack, which is precisely the LONG array VFPrintf wants. Every Printf
 * call in this file already casts its arguments to ULONG/LONG for the
 * dos.library convention, so they are all 32 bits wide and the layout
 * holds.
 * =================================================================== */
#ifdef ZZSID_GUI
#include <stdarg.h>

static BPTR g_dbgfh = 0;

static void dbg_printf(const char *fmt, ...)
{
    va_list ap;
    if (!g_dbgfh) return;
    va_start(ap, fmt);
    VFPrintf(g_dbgfh, (STRPTR)fmt, (LONG *)(void *)ap);
    va_end(ap);
}

static void dbg_putstr(const char *s)
{
    if (!g_dbgfh) return;
    FPuts(g_dbgfh, (STRPTR)s);
}

/* After this point "Printf" and "PutStr" in this file mean the two
 * functions above.
 *
 * The NDK's inline/dos.h already defines both as function-like macros that
 * expand to the library call, so they must be undefined first - without
 * that the compiler warns about a redefinition on every build. #undef of a
 * name that is not defined is legal and does nothing, so this is safe
 * whichever way a future NDK declares them. */
#undef  Printf
#undef  PutStr
#define Printf  dbg_printf
#define PutStr  dbg_putstr
#endif /* ZZSID_GUI */

#define ZZ9000_MANUF   0x6D6Eu
#define ZZ9000_PROD_AX 0x0Au
#define ZZ9000_PROD    0x04u
#define MNT_FB_BASE    0x00010000UL

#define ZZ_WR(b,o,v) (*((volatile UWORD *)((UBYTE *)(b)+(o))) = (UWORD)(v))

static UBYTE *g_board = NULL;
static UBYTE *g_fb    = NULL;
static volatile ULONG *g_shared = NULL;
static volatile ULONG *g_pcm    = NULL;
static volatile UBYTE *g_sidbuf = NULL;   /* .sid staging area in DDR */

/* AHI double-buffer (AHITest model) */
static struct MsgPort    *g_ahiport = NULL;
static struct AHIRequest *g_req[2]  = { NULL, NULL };
static BOOL               g_act[2]  = { FALSE, FALSE };
static int                g_acur = 0, g_anext = 1;
static ULONG              g_sr   = 44100;
static LONG               g_unit = AHI_DEFAULT_UNIT;
static int                g_noahi = 0;   /* CPU isolation runs */
static int                g_probe = 0;   /* 0 none, 1 idle, 2 drain */
static int                g_probe_sec = 10;
static int                g_mono  = 0;   /* AHIST_M16S: half the samples  */
static int                g_gm    = 0;   /* ZZDoom reply-port refill model */
static ULONG              g_ahi_writes = 0;
static ULONG              g_num_sids   = 1;       /* -2sid sets 2          */
static ULONG              g_sid1_base  = 0xD420UL;/* -sid2base overrides   */
static ULONG              g_mirror     = 0;        /* chip0 writes -> chip1 */
static int                g_mirror_force = -1;     /* -1 auto, 0 off, 1 on  */

/* One AHI buffer holds g_chunk frames. -chunk sets it independently of the
 * sample rate, which is what makes the cost-per-request question testable;
 * tying it to rate/50 kept the request count pinned at 50/s and hid the axis.
 * Buffers are AllocMem(MEMF_PUBLIC) like the validated ZZDoom code, not
 * static arrays, so the device never has to relocate them. */
#define CHUNK_MIN_FRAMES 64
#define CHUNK_MAX_FRAMES 4096            /* half the ring, so fill can wait */
static UBYTE *g_buf0 = NULL;
static UBYTE *g_buf1 = NULL;
static ULONG  g_bufbytes = 0;
static ULONG g_chunk = 882;                /* default = g_sr / ZZRESID_PAL_FPS */
#define CHUNK_FRAMES (g_chunk)
#define CHUNK_BYTES  (g_chunk * (g_mono ? 2UL : 4UL))

/* Loop buffer copied from Core1 ring (RING_SAMPLES stereo frames) */
static WORD *g_loop = NULL;
static ULONG g_loop_frames = 0;
static ULONG g_loop_pos    = 0;

/* Streaming read cursor (MODE 2 continuous) */
static ULONG g_read_pos = 0;
static ULONG g_underruns = 0;
/* Real starvation metrics. g_underruns only ever counted the 500000-spin
 * timeout, which never fires when Core1 is merely a little too slow - the
 * 68k just waits, misses the AHI deadline and the sound stutters with
 * underruns still reading 0. These two tell the truth. */
static ULONG g_ring_min  = 0xFFFFFFFFUL;  /* lowest ring fill seen */
static ULONG g_starves   = 0;             /* chunks that had to wait */
static ULONG g_spins     = 0;   /* total turns of the wait loop - if this is
                                 * huge the 68k is burning CPU busy-waiting */
static int   g_nocache   = 0;   /* -nocache: skip CacheClearE on the shared
                                 * window, which may already be uncacheable */
static ULONG g_filter_cfg = ZZRESID_FILT_NONE;
/* Default is now to stop Core1 and hand it back on exit. The blob returns
 * to the firmware's core1_loop(), which frees Core1 for any other XACP
 * application with no power cycle. -keep leaves it resident instead, which
 * only saves the residfp table rebuild when iterating. */
static int   g_stop_on_exit = 1;
static ULONG g_chip_model   = ZZRESID_CHIP_6581;
/* 0 = auto (read the PSID header), 1 = forced by -6581 / -8580. Auto is the
 * default: the file says which chip it was written for, and honouring that
 * is more useful than any fixed choice of ours. */
static int   g_model_forced = 0;
static int   g_bench        = 0;   /* -bench: measure Core1 throughput */
static const char *g_tune_path = NULL;  /* -sid <file> */

/* Flush on the ZZ9000 shared window. CacheClearE is an exec call and is not
 * free on a 68060; the window is very likely already uncacheable, in which
 * case every one of these is wasted. -nocache measures that. */
static void cache_flush(void *addr, ULONG len)
{
    if (g_nocache) return;
    CacheClearE(addr, len, CACRF_ClearD);
}

static inline ULONG sh_get(int idx) { return g_shared[idx]; }
static inline void  sh_put(int idx, ULONG v) { g_shared[idx] = v; }
static void sh_put_flush(int idx, ULONG v)
{
    g_shared[idx] = v; cache_flush((void *)&g_shared[idx], 4);
}
static ULONG sh_get_flush(int idx)
{
    cache_flush((void *)&g_shared[idx], 4); return g_shared[idx];
}
static int ctrl_c(void)
{
    return (SetSignal(0L, 0L) & SIGBREAKF_CTRL_C) ? 1 : 0;
}

/* ---- AHI (AHITest model) ---- */
static void send_ahi_chunk(int slot, UBYTE *buf, ULONG len)
{
    /* io_Flags and io_Offset set explicitly, as the validated ZZDoom driver
     * does - never inherited from whatever the previous completion left. */
    g_req[slot]->ahir_Std.io_Flags   = 0;
    g_req[slot]->ahir_Std.io_Offset  = 0;
    g_req[slot]->ahir_Std.io_Command = CMD_WRITE;
    g_req[slot]->ahir_Std.io_Data    = buf;
    g_req[slot]->ahir_Std.io_Length  = len;
    g_req[slot]->ahir_Frequency      = g_sr;
    g_req[slot]->ahir_Type           = g_mono ? AHIST_M16S : AHIST_S16S;
    g_req[slot]->ahir_Volume         = 0x10000L;
    g_req[slot]->ahir_Position       = 0x8000L;
    g_req[slot]->ahir_Link           = g_act[slot ^ 1] ? g_req[slot ^ 1] : NULL;
    SendIO((struct IORequest *)g_req[slot]);
    g_act[slot] = TRUE;
    g_ahi_writes++;
}

/* Allocated whatever the mode: the -drain probe fills a buffer too. */
static int bufs_alloc(void)
{
    g_bufbytes = g_chunk * 4UL;            /* stereo worst case */
    g_buf0 = (UBYTE *)AllocMem(g_bufbytes, MEMF_PUBLIC | MEMF_CLEAR);
    g_buf1 = (UBYTE *)AllocMem(g_bufbytes, MEMF_PUBLIC | MEMF_CLEAR);
    return (g_buf0 && g_buf1);
}

static void bufs_free(void)
{
    if (g_buf0) { FreeMem(g_buf0, g_bufbytes); g_buf0 = NULL; }
    if (g_buf1) { FreeMem(g_buf1, g_bufbytes); g_buf1 = NULL; }
}

static int ahi_open(void)
{
    int i;
    g_ahiport = CreateMsgPort();
    if (!g_ahiport) return 0;
    for (i = 0; i < 2; i++) {
        g_req[i] = (struct AHIRequest *)
                   CreateIORequest(g_ahiport, sizeof(struct AHIRequest));
        if (!g_req[i]) return 0;
        g_act[i] = FALSE;
    }
    g_req[0]->ahir_Version = 4;
    if (OpenDevice(AHINAME, g_unit,
                   (struct IORequest *)g_req[0], 0) != 0)
        return 0;
    CopyMem(g_req[0], g_req[1], sizeof(struct AHIRequest));
    return 1;
}

static void ahi_close(void)
{
    int i;
    if (g_noahi) return;
    for (i = 0; i < 2; i++) {
        if (g_req[i] && g_act[i]) {
            if (!CheckIO((struct IORequest *)g_req[i]))
                AbortIO((struct IORequest *)g_req[i]);
            WaitIO((struct IORequest *)g_req[i]);
            g_act[i] = FALSE;
        }
    }
    if (g_req[0]) CloseDevice((struct IORequest *)g_req[0]);
    for (i = 0; i < 2; i++)
        if (g_req[i]) DeleteIORequest((struct IORequest *)g_req[i]);
    if (g_ahiport) DeleteMsgPort(g_ahiport);
}

/* ---- sine for MODE 1 ---- */
static ULONG g_phase = 0;
static const WORD sine16[16] = {
        0,  3061,  5657,  7391,  8000,  7391,  5657,  3061,
        0, -3061, -5657, -7391, -8000, -7391, -5657, -3061
};

static void fill_sine(UBYTE *buf)
{
    WORD *w = (WORD *)buf;
    ULONG step = (ULONG)(((ULONG)440 * 16 * 65536) / g_sr);
    int i;
    for (i = 0; i < (int)CHUNK_FRAMES; i++) {
        WORD s = sine16[(g_phase >> 16) & 0x0F];
        if (g_mono) { w[i] = s; }
        else        { w[i*2] = s; w[i*2+1] = s; }
        g_phase += step;
    }
}

/* ---- fill from the looped Core1 buffer (unused now) ---- */
static void fill_loop(UBYTE *buf)
{
    WORD *w = (WORD *)buf;
    int i;
    for (i = 0; i < CHUNK_FRAMES; i++) {
        w[i*2]   = g_loop[g_loop_pos*2];
        w[i*2+1] = g_loop[g_loop_pos*2+1];
        g_loop_pos++;
        if (g_loop_pos >= g_loop_frames) g_loop_pos = 0;
    }
}

/* ---- fill by streaming from the Core1 ring (MODE 2 continuous) ---- */
static void fill_stream(UBYTE *buf)
{
    WORD *w = (WORD *)buf;
    ULONG need = CHUNK_FRAMES;
    ULONG timeout = 0;

    /* Wait until Core1 has produced CHUNK_FRAMES ahead of our read cursor */
    {
        int first = 1;
        for (;;) {
            ULONG wpos = sh_get_flush(SH_PCM_WRITE);
            ULONG avail = wpos - g_read_pos;   /* uint wrap-safe */
            if (first) {
                if (avail < g_ring_min) g_ring_min = avail;
                if (avail < need) g_starves++;
                first = 0;
            }
            if (avail >= need) break;
            g_spins++;
            if (ctrl_c()) return;
            if (++timeout > 500000UL) { g_underruns++; break; }
        }
    }

    /* Read need frames from the ring, flushing only what we touch.
     * The ring may wrap mid-chunk, so handle two spans. */
    {
        ULONG start = g_read_pos & ZZRESID_RING_MASK;
        ULONG first = need;
        if (start + first > ZZRESID_RING_SAMPLES)
            first = ZZRESID_RING_SAMPLES - start;

        /* The ring already holds exactly what AHIST_S16S wants: big-endian
         * L then R per frame. So this is a straight block copy - the old
         * per-sample loop with shifts was rebuilding bytes it already had,
         * 44100 times a second. */
        cache_flush((void *)&g_pcm[start], first * 4);
        if (g_mono) {
            /* Keep the left channel only. Halves what AHI has to chew on,
             * which is the point of the -mono measurement. */
            ULONG k;
            for (k = 0; k < first; k++) w[k] = (WORD)(g_pcm[start + k] >> 16);
            if (first < need) {
                ULONG rest = need - first;
                cache_flush((void *)&g_pcm[0], rest * 4);
                for (k = 0; k < rest; k++)
                    w[first + k] = (WORD)(g_pcm[k] >> 16);
            }
        } else {
            CopyMem((APTR)(ULONG)&g_pcm[start], (APTR)w, first * 4);
            if (first < need) {
                ULONG rest = need - first;
                cache_flush((void *)&g_pcm[0], rest * 4);
                CopyMem((APTR)(ULONG)&g_pcm[0], (APTR)(w + first * 2), rest * 4);
            }
        }
    }

    g_read_pos += need;
    sh_put_flush(SH_PCM_READ, g_read_pos);
}

/* ---- Core1 control ---- */
/* Is a ZZReSID blob already resident and running?
 * If so we can reconfigure it in place instead of reloading and calling
 * arm_app_run (which is not reentrant and needs a power cycle). */
static int blob_is_live(void)
{
    ULONG magic, status;
    cache_flush((void *)&g_shared[SH_MAGIC], 4);
    cache_flush((void *)&g_shared[SH_STATUS], 4);
    magic  = sh_get(SH_MAGIC);
    status = sh_get(SH_STATUS);
    return (magic == ZZRESID_MAGIC &&
            (status == ZZRESID_ST_RUNNING || status == ZZRESID_ST_READY));
}

/* Reconfigure a resident blob: push new settings, hand it our read cursor,
 * then wait for it to acknowledge by clearing SH_CMD. */
static int blob_reconfigure(void)
{
    ULONG n, gen_before;

    /* Remember the generation so we can prove the blob really applied it.
     * A cleared SH_CMD alone would not distinguish a real reconfigure from
     * a stale mailbox. */
    cache_flush((void *)&g_shared[SH_GENERATION], 4);
    gen_before = sh_get(SH_GENERATION);

    sh_put(SH_SAMPLE_RATE, g_sr);
    sh_put(SH_CHIP_MODEL, g_chip_model);
    sh_put(SH_FILTER_CFG, g_filter_cfg);
    sh_put(SH_NUM_SIDS,   g_num_sids);
    sh_put(SH_SID1_BASE,  g_sid1_base);
    sh_put(SH_SID_MIRROR, g_mirror);
    sh_put(SH_MODE, g_tune_path ? ZZRESID_MODE_TUNE : ZZRESID_MODE_TONE);
    sh_put(SH_PCM_READ, 0);
    g_read_pos = 0;
    cache_flush((void *)&g_shared[0], 8 * 4);
    sh_put_flush(SH_CMD, ZZRESID_CMD_RECONFIG);

    for (n = 0; n < 5000000UL; n++) {
        cache_flush((void *)&g_shared[SH_GENERATION], 4);
        if (sh_get(SH_GENERATION) != gen_before) return 1;
        if (ctrl_c()) return 0;
    }
    return 0;
}

/* Print what the blob ACTUALLY applied, so a clamped value can never pass
 * unnoticed. Returns 0 if the applied config differs from the request. */
static int report_active_config(void)
{
    ULONG gen, act_model, act_filter, err, req_model;
    int ok = 1;

    cache_flush((void *)&g_shared[32], 8 * 4);
    gen        = sh_get(SH_GENERATION);
    req_model  = sh_get(SH_REQUESTED_MODEL);
    act_model  = sh_get(SH_ACTIVE_MODEL);
    act_filter = sh_get(SH_ACTIVE_FILTER);
    err        = sh_get(SH_SETUP_ERROR);

    Printf("generation=%lu  model: requested=%lu active=%lu (%s)\n",
           gen, req_model, act_model,
           (ULONG)(act_model == ZZRESID_CHIP_8580 ? "MOS8580" : "MOS6581"));
    Printf("filters active=%lu (%s)  setup_error=%lu\n",
           act_filter, (ULONG)(
               act_filter == ZZRESID_FILT_NONE     ? "none" :
               act_filter == ZZRESID_FILT_EXTERNAL ? "external only" :
               act_filter == ZZRESID_FILT_INTERNAL ? "internal only" : "both"),
           err);

    {
        ULONG sctlr;
        cache_flush((void *)&g_shared[SH_SCTLR], 4);
        sctlr = sh_get(SH_SCTLR);
        Printf("SCTLR=0x%08lx  MMU=%s Dcache=%s Icache=%s\n", sctlr,
               (ULONG)((sctlr & ZZRESID_SCTLR_M) ? "ON" : "OFF"),
               (ULONG)((sctlr & ZZRESID_SCTLR_C) ? "ON" : "OFF"),
               (ULONG)((sctlr & ZZRESID_SCTLR_I) ? "ON" : "OFF"));
        if (!(sctlr & ZZRESID_SCTLR_C))
            Printf("  WARNING: D-cache is OFF - the MMU did not take\n");
    }

    if (err != ZZRESID_ERR_NONE) {
        Printf("  WARNING: blob clamped the request (error %lu)\n", err);
        ok = 0;
    }
    if (act_model != g_chip_model || act_filter != g_filter_cfg) {
        Printf("  WARNING: active config differs from request\n");
        ok = 0;
    }
    return ok;
}


/* =======================================================================
 * Embedded Core1 blob.
 *
 * Two ways in, both compile-time, so no extra tool is mandatory:
 *
 *   -DZZRESID_EMBED_OBJCOPY  link zzresid_blob.o, produced straight from
 *                            the .bin by the toolchain's own objcopy.
 *                            Nothing else to install.
 *   -DZZRESID_EMBED_ARRAY    link a generated zzresid_blob.c holding the
 *                            bytes as a C array. Fallback for a toolchain
 *                            whose objcopy refuses -I binary.
 *
 * With neither, the launcher keeps reading zzresid_core1.bin from disk,
 * exactly as before. -blob <file> always overrides an embedded blob, which
 * is what keeps blob iteration cheap during development.
 * ======================================================================= */
#if defined(ZZRESID_EMBED_OBJCOPY)
extern const UBYTE _binary_zzresid_core1_bin_start[];
extern const UBYTE _binary_zzresid_core1_bin_end[];
#define ZZRESID_EMBEDDED 1
#define ZZRESID_EMBED_PTR ((const UBYTE *)_binary_zzresid_core1_bin_start)
#define ZZRESID_EMBED_LEN ((ULONG)((const UBYTE *)_binary_zzresid_core1_bin_end - \
                                   (const UBYTE *)_binary_zzresid_core1_bin_start))
#elif defined(ZZRESID_EMBED_ARRAY)
extern const unsigned char zzresid_core1_bin[];
extern const unsigned long zzresid_core1_bin_len;
#define ZZRESID_EMBEDDED 1
#define ZZRESID_EMBED_PTR ((const UBYTE *)zzresid_core1_bin)
#define ZZRESID_EMBED_LEN ((ULONG)zzresid_core1_bin_len)
#else
#define ZZRESID_EMBEDDED 0
#endif

#if ZZRESID_EMBEDDED
static int load_blob_embedded(void)
{
    UBYTE *dst = g_fb + ZZRESID_BLOB_FB_OFFSET;
    ULONG size = ZZRESID_EMBED_LEN;
    if (size == 0 || size > 0x00100000UL) {
        Printf("embedded blob: bad size %lu\n", size);
        return 0;
    }
    CopyMem((APTR)ZZRESID_EMBED_PTR, (APTR)dst, size);
    cache_flush(dst, size);
    Printf("Blob: embedded, %lu bytes\n", size);
    return 1;
}
#endif

static int load_blob(const char *path)
{
    BPTR fh; LONG size, rd;
    UBYTE *dst = g_fb + ZZRESID_BLOB_FB_OFFSET;
    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) { Printf("Cannot open blob %s\n", (ULONG)path); return 0; }
    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_CURRENT);
    Seek(fh, 0, OFFSET_BEGINNING);
    if (size <= 0 || size > 0x00100000) { Printf("bad size\n"); Close(fh); return 0; }
    rd = Read(fh, dst, size);
    Close(fh);
    if (rd != size) { Printf("short read\n"); return 0; }
    cache_flush(dst, size);
    Printf("Blob loaded: %ld bytes\n", size);
    return 1;
}

static void launch_core1(void)
{
    int i;
    for (i = 0; i < ZZRESID_SHARED_WORDS; i++) sh_put(i, 0);
    sh_put(SH_SAMPLE_RATE, g_sr);
    sh_put(SH_CHIP_MODEL, g_chip_model);
    sh_put(SH_FILTER_CFG, g_filter_cfg);
    sh_put(SH_NUM_SIDS,   g_num_sids);
    sh_put(SH_SID1_BASE,  g_sid1_base);
    sh_put(SH_SID_MIRROR, g_mirror);
    sh_put(SH_MODE, g_tune_path ? ZZRESID_MODE_TUNE : ZZRESID_MODE_TONE);
    sh_put(SH_CMD, ZZRESID_CMD_NONE);
    sh_put(SH_STATUS, ZZRESID_ST_INIT);
    sh_put(SH_PCM_READ, 0);
    sh_put(SH_CANARY_68K, ZZRESID_CANARY_68K);
    cache_flush((void *)g_shared, ZZRESID_SHARED_WORDS * 4);
    ZZ_WR(g_board, ZZRESID_REG_ARM_RUN_HI, ZZRESID_RUN_HI_VALUE);
    ZZ_WR(g_board, ZZRESID_REG_ARM_RUN_LO, ZZRESID_RUN_LO_VALUE);
}

static int wait_status(ULONG want, ULONG timeout)
{
    ULONG n;
    for (n = 0; n < timeout; n++) {
        if (sh_get_flush(SH_STATUS) == want) return 1;
        if (ctrl_c()) return 0;
    }
    return 0;
}

/* Pack a SID register entry: (reg<<24)|(val<<16)|(cyc&0xFFFF) */
static ULONG pack_reg(UBYTE reg, UBYTE val, UWORD cyc)
{
    return ((ULONG)reg << 24) | ((ULONG)val << 16) | ((ULONG)cyc & 0xFFFF);
}

/* Send an A4 440Hz triangle note batch to Core1 (Build C reSID) */
/*
 * A4 440Hz triangle on voice 1, ROUTED THROUGH THE SID FILTER.
 *
 * The previous batch wrote neither the cutoff (0x15/0x16), nor the filter
 * routing (0x17), nor a filter mode in 0x18 - so no voice ever reached the
 * SID filter and "-filt int" could not be heard. Those three registers are
 * now set, so the internal filter is actually exercised.
 *
 * When reSID's internal filter is disabled (-filt none / ext), reSID
 * bypasses the filter entirely and these registers have no effect, so the
 * baseline stays identical to the previous runs. That identity is itself a
 * useful check.
 */
static void send_sid_note(void)
{
    ULONG i = 0;
    sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x15, 0x00, 0)); /* cutoff lo   */
    sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x16, 0x40, 0)); /* cutoff hi   */
    sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x17, 0x01, 0)); /* voice1->filt */
    sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x00, 0x44, 0)); /* freq lo     */
    sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x01, 0x1D, 0)); /* freq hi A4  */
    sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x05, 0x00, 0)); /* AD = 0      */
    sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x06, 0xF0, 0)); /* SR sustain  */
    sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x18, 0x1F, 0)); /* LP + vol 15 */
    sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x04, 0x11, 0)); /* tri + gate  */
    if (g_num_sids > 1 && !g_mirror) {
        /* Same note on chip 1, a fifth higher so left and right are told
         * apart by ear and not only by a meter. The register field is the
         * offset from $D400, so chip 1 lives at 0x20..0x3C. */
        sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x20 + 0x15, 0x00, 0));
        sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x20 + 0x16, 0x40, 0));
        sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x20 + 0x17, 0x01, 0));
        sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x20 + 0x00, 0x67, 0));
        sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x20 + 0x01, 0x2B, 0)); /* E5 */
        sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x20 + 0x05, 0x00, 0));
        sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x20 + 0x06, 0xF0, 0));
        sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x20 + 0x18, 0x1F, 0));
        sh_put(SH_REG_BATCH_BASE + i++, pack_reg(0x20 + 0x04, 0x11, 0));
    }
    cache_flush((void *)&g_shared[SH_REG_BATCH_BASE], i * 4);
    sh_put_flush(SH_REG_COUNT, i);
}



/* =======================================================================
 * PSID loading
 *
 * A .sid file is a header followed by C64 memory image + 6502 player code.
 * Header fields are big-endian, which suits the 68k. We only need five of
 * them: where the data loads, where init and play live, and how many
 * subsongs there are.
 *
 * If loadAddress in the header is 0, the real load address sits in the
 * first two bytes of the data itself, little-endian (as on a C64 PRG).
 * ======================================================================= */

struct SidInfo {
    ULONG version;
    ULONG flags, hdr_model;
    ULONG sid2, sid3;
    ULONG load, init, play, songs, startsong, dataoff, datalen;
    char  name[33], author[33], released[33];
    int   is_rsid;
};

/* Parsed once, before Core1 is configured: the header decides the stereo
 * mode, which is part of that configuration. */
static struct SidInfo g_si;
static int            g_have_tune = 0;
static LONG           g_song_req  = 0;   /* 0 = unset, else 1..songs */
static ULONG          g_song_cur  = 1;   /* 1-based, what is playing */

static UBYTE *g_sidfile = NULL;
static LONG   g_sidsize = 0;

static UWORD be16(const UBYTE *p) { return (UWORD)((p[0] << 8) | p[1]); }

static int load_sid_file(const char *path, struct SidInfo *si)
{
    BPTR fh;
    LONG n;
    UBYTE *d;

    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) { Printf("Cannot open %s\n", (ULONG)path); return 0; }
    Seek(fh, 0, OFFSET_END);
    g_sidsize = Seek(fh, 0, OFFSET_CURRENT);
    Seek(fh, 0, OFFSET_BEGINNING);
    if (g_sidsize < 0x7C || g_sidsize > 200000L) {
        Printf("Bad .sid size: %ld\n", g_sidsize); Close(fh); return 0;
    }
    g_sidfile = (UBYTE *)AllocVec(g_sidsize, MEMF_ANY);
    if (!g_sidfile) { Printf("No memory for .sid\n"); Close(fh); return 0; }
    n = Read(fh, g_sidfile, g_sidsize);
    Close(fh);
    if (n != g_sidsize) { Printf("Short read\n"); return 0; }

    d = g_sidfile;
    if (!(d[0]=='P' && d[1]=='S' && d[2]=='I' && d[3]=='D') &&
        !(d[0]=='R' && d[1]=='S' && d[2]=='I' && d[3]=='D')) {
        Printf("Not a PSID/RSID file\n"); return 0;
    }
    si->is_rsid   = (d[0] == 'R');
    si->version   = be16(d + 0x04);
    /* The flags word exists from PSID v2 onwards. Bits 4-5 name the chip
     * the tune was written for: 1 = 6581, 2 = 8580, 3 = either, 0 = unknown. */
    si->flags     = (si->version >= 2 && g_sidsize >= 0x78) ? be16(d + 0x76) : 0;
    si->dataoff   = be16(d + 0x06);
    si->load      = be16(d + 0x08);
    si->init      = be16(d + 0x0A);
    si->play      = be16(d + 0x0C);
    si->songs     = be16(d + 0x0E);
    si->startsong = be16(d + 0x10);

    CopyMem(d + 0x16, si->name,     32); si->name[32]     = 0;
    CopyMem(d + 0x36, si->author,   32); si->author[32]   = 0;
    CopyMem(d + 0x56, si->released, 32); si->released[32] = 0;

    if (si->dataoff >= (ULONG)g_sidsize) { Printf("Bad dataOffset\n"); return 0; }

    /* loadAddress 0 means the address is the first two data bytes, LE. */
    if (si->load == 0) {
        UBYTE *p = d + si->dataoff;
        si->load    = (ULONG)p[0] | ((ULONG)p[1] << 8);
        si->dataoff += 2;
    }
    si->datalen = (ULONG)g_sidsize - si->dataoff;
    if (si->datalen > ZZRESID_SID_MAX_BYTES) si->datalen = ZZRESID_SID_MAX_BYTES;

    Printf("  \"%s\"\n  by %s (%s)\n", (ULONG)si->name,
           (ULONG)si->author, (ULONG)si->released);
    Printf("  %s  load=$%04lx init=$%04lx play=$%04lx  songs=%lu\n",
           (ULONG)(si->is_rsid ? "RSID" : "PSID"),
           si->load, si->init, si->play, si->songs);
    /* PSID v3/v4: SID #2 and #3 have their own 32-byte I/O windows.
     * Valid additional SID areas are $D420-$D7E0 and $DE00-$DFE0;
     * $D800-$DDFF is NOT valid, and v5 uses another header layout.
     * Preserve the base in SidInfo so the GUI can send it to Core1. */
    si->sid2 = 0; si->sid3 = 0;
    if (si->version >= 3 && si->version <= 4 && g_sidsize >= 0x7C) {
        UBYTE b2 = d[0x7A], b3 = d[0x7B];
        if (!(b2 & 1) && ((b2 >= 0x42 && b2 <= 0x7E) ||
                           (b2 >= 0xE0 && b2 <= 0xFE)))
            si->sid2 = 0xD000UL | ((ULONG)b2 << 4);
        if (si->version >= 4 && !(b3 & 1) && b3 != b2 &&
            ((b3 >= 0x42 && b3 <= 0x7E) ||
             (b3 >= 0xE0 && b3 <= 0xFE)))
            si->sid3 = 0xD000UL | ((ULONG)b3 << 4);
    }
    Printf("  PSID v%lu", si->version);
    if (si->sid2) Printf("  2nd SID at $%04lx", si->sid2);
    if (si->sid3) Printf("  3rd SID at $%04lx", si->sid3);
    if (!si->sid2) Printf("  single SID");
    Printf("\n");
#ifndef ZZSID_GUI
    if (si->sid2 && g_num_sids < 2)
        Printf("  HINT: 2SID tune - use -2sid -sid2base 0x%04lx\n", si->sid2);
    if (si->sid2 && g_num_sids > 1 && si->sid2 != g_sid1_base)
        Printf("  WARNING: header says $%04lx but -sid2base is $%04lx\n",
               si->sid2, g_sid1_base);
#endif

    /* Model selection. The header's opinion is used unless the user forced
     * one, and what was chosen and why is always printed: a model picked
     * silently is exactly how a hard-coded MOS6581 once passed for a
     * MOS8580 in this project for weeks. */
    {
        ULONG hdr = (si->flags >> 4) & 3UL;   /* 0 unknown, 1 6581, 2 8580, 3 both */
        si->hdr_model = hdr;
        if (g_model_forced) {
            Printf("  model: forced %s\n",
                   (ULONG)(g_chip_model == ZZRESID_CHIP_8580 ? "8580" : "6581"));
        } else if (hdr == 1) {
            g_chip_model = ZZRESID_CHIP_6581;
            Printf("  model: auto -> 6581 (header says 6581)\n");
        } else if (hdr == 2) {
            g_chip_model = ZZRESID_CHIP_8580;
            Printf("  model: auto -> 8580 (header says 8580)\n");
        } else if (hdr == 3) {
            g_chip_model = ZZRESID_CHIP_6581;
            Printf("  model: auto -> 6581 (header says either; use -8580 to hear the other)\n");
        } else {
            g_chip_model = ZZRESID_CHIP_6581;
            Printf("  model: auto -> 6581 (header says nothing%s)\n",
                   (ULONG)(si->version < 2 ? ", PSID v1 has no flags" : ""));
        }
    }

    /* songs / startSong sanity. A file may declare nonsense; correct it and
     * say so rather than pass it on. */
    if (si->songs == 0) { si->songs = 1; Printf("  NOTE: songs=0, corrected to 1\n"); }
    if (si->startsong < 1 || si->startsong > si->songs) {
        Printf("  NOTE: startSong=%lu out of range, corrected to 1\n", si->startsong);
        si->startsong = 1;
    }

    /* Two chips are a property of the emulated hardware, not of the tune.
     * A 1SID tune on two chips must still come out of both, the way a
     * two-chip cartridge behaves - so it is mirrored. A tune that drives
     * the second chip itself is left alone. Never a silent right channel. */
    if (g_num_sids > 1) {
        if (g_mirror_force >= 0) g_mirror = (ULONG)g_mirror_force;
        else                     g_mirror = si->sid2 ? 0UL : 1UL;
        Printf("  stereo: %s\n", (ULONG)(g_mirror
            ? "mirror_2sid - 1SID tune cloned to both chips"
            : "independent_2sid - each chip driven by the tune"));
    } else {
        Printf("  stereo: mono_dup - one chip, duplicated to L and R\n");
    }

    if (si->is_rsid)
        Printf("  NOTE: RSID needs full C64 interrupt emulation - may not play\n");
    if (si->play == 0)
        Printf("  NOTE: play address 0 means the tune sets its own IRQ - not supported yet\n");
    return 1;
}

/* Stage the tune in DDR and have Core1 load and init it. */
static int start_tune(struct SidInfo *si, ULONG song)
{
    ULONG i, n, gen_before;

    /* Write as aligned ULONGs: byte writes do not cross the Zorro bridge
     * reliably. Core1 reads the same words back through ARM32(), so packing
     * the four bytes big-endian here puts them back in file order there. */
    {
        volatile ULONG *dst = (volatile ULONG *)g_sidbuf;
        const UBYTE *sp = g_sidfile + si->dataoff;
        ULONG words = (si->datalen + 3UL) >> 2;
        ULONG b0, b1, b2, b3;
        for (i = 0; i < words; i++) {
            ULONG k = i << 2;
            b0 = (k + 0 < si->datalen) ? sp[k + 0] : 0;
            b1 = (k + 1 < si->datalen) ? sp[k + 1] : 0;
            b2 = (k + 2 < si->datalen) ? sp[k + 2] : 0;
            b3 = (k + 3 < si->datalen) ? sp[k + 3] : 0;
            dst[i] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
        }
        cache_flush((void *)g_sidbuf, words * 4);

        /* Read back what we just wrote, from the 68k side. This separates
         * "the 68k never wrote" from "Core1 reads a different address". */
        Printf("  staged %lu bytes at fb+$%08lx (dataoff=%lu)\n",
               si->datalen, (ULONG)ZZRESID_SID_FB_OFFSET, si->dataoff);
        Printf("  68k reads back $%08lx, expected $%08lx %s\n",
               dst[0],
               ((ULONG)sp[0] << 24) | ((ULONG)sp[1] << 16) |
               ((ULONG)sp[2] << 8) | (ULONG)sp[3],
               (ULONG)((dst[0] == (((ULONG)sp[0] << 24) | ((ULONG)sp[1] << 16) |
                        ((ULONG)sp[2] << 8) | (ULONG)sp[3])) ? "OK" : "WRITE FAILED"));
    }

    sh_put(SH_SID_LOAD, si->load);
    sh_put(SH_SID_INIT, si->init);
    sh_put(SH_SID_PLAY, si->play);
    sh_put(SH_SID_SONG, song);
    sh_put(SH_SID_LEN,  si->datalen);
    cache_flush((void *)&g_shared[40], 8 * 4);

    sh_put_flush(SH_CMD, ZZRESID_CMD_LOAD_SID);
    for (n = 0; n < 5000000UL; n++) {
        cache_flush((void *)&g_shared[SH_CMD], 4);
        if (sh_get(SH_CMD) == ZZRESID_CMD_NONE) break;
        if (ctrl_c()) return 0;
    }
    if (n >= 5000000UL) { Printf("LOAD_SID: no ack\n"); return 0; }

    cache_flush((void *)&g_shared[SH_SID_FRAMES], 4);
    gen_before = sh_get(SH_SID_FRAMES);
    (void)gen_before;

    sh_put_flush(SH_CMD, ZZRESID_CMD_SID_INIT);
    for (n = 0; n < 5000000UL; n++) {
        cache_flush((void *)&g_shared[SH_CMD], 4);
        if (sh_get(SH_CMD) == ZZRESID_CMD_NONE) break;
        if (ctrl_c()) return 0;
    }
    if (n >= 5000000UL) { Printf("SID_INIT: no ack\n"); return 0; }

    /* Confirm the staged bytes actually reached Core1 before blaming audio. */
    cache_flush((void *)&g_shared[SH_SID_FIRST], 4);
    {
        ULONG got = sh_get(SH_SID_FIRST);
        ULONG want = ((ULONG)g_sidfile[si->dataoff + 0] << 24) |
                     ((ULONG)g_sidfile[si->dataoff + 1] << 16) |
                     ((ULONG)g_sidfile[si->dataoff + 2] << 8)  |
                      (ULONG)g_sidfile[si->dataoff + 3];
        Printf("  transfer check: Core1 read $%08lx, file has $%08lx %s\n",
               got, want, (ULONG)(got == want ? "OK" : "MISMATCH"));
        if (got != want)
            Printf("  the tune never reached Core1 - do not trust what follows\n");
    }

    cache_flush((void *)&g_shared[SH_SID_WRITES], 4);
    Printf("  init done, %lu SID register writes\n", sh_get(SH_SID_WRITES));

    /* A tune with no play address drives itself from its own interrupt.
     * The register-write count above says nothing about whether that is
     * working - it only counts init. This does. */
    if (g_have_tune && g_si.play == 0) {
        ULONG n, a, b, k;
        Printf("  no play address: running the tune's own interrupt\n");
        for (n = 0; n < 400000UL; n++) { if (sh_get_flush(SH_IRQ_COUNT)) break; }
        a = sh_get_flush(SH_IRQ_COUNT);
        if (!a) {
            Printf("  NO interrupt fired - the tune will stay silent\n");
        } else {
            /* The tune is already running into the ring at this point, so a
             * second sample a moment later says whether it KEEPS running.
             * A count that stops growing is the signature of a tune that
             * plays for a few seconds and then goes quiet. */
            for (k = 0; k < 4000000UL; k++) { }
            b = sh_get_flush(SH_IRQ_COUNT);
            Printf("  interrupts: %lu then %lu\n", a, b);
            if (b <= a)
                Printf("  STOPPED - the 6502 is no longer being interrupted\n");
            if (sh_get_flush(SH_IRQ_RUNAWAY))
                Printf("  CPU JAMMED %lu times, last PC $%04lx\n",
                       sh_get_flush(SH_IRQ_RUNAWAY), sh_get_flush(SH_IRQ_PC));
            else
                Printf("  cpu ok, PC $%04lx\n", sh_get_flush(SH_IRQ_PC));
        }
    }
    return 1;
}

/* Dump the SID registers as the tune last wrote them. Volume lives in the
 * low nibble of $18; if that is 0 the chip is muted no matter what else
 * happens, and the fault is in the tune or the 6502, not in reSID. */
static void dump_sid_regs(void)
{
    ULONG i, vol, mode;
    cache_flush((void *)&g_shared[SH_SID_SHADOW_BASE], 32 * 4);

    Printf("  SID regs:");
    for (i = 0; i < ZZRESID_SID_REGS; i++) {
        if ((i & 7) == 0) Printf("\n   $%02lx:", i);
        Printf(" %02lx", sh_get(SH_SID_SHADOW_BASE + i) & 0xFF);
    }
    Printf("\n");

    vol  = sh_get(SH_SID_SHADOW_BASE + 0x18) & 0x0F;
    mode = (sh_get(SH_SID_SHADOW_BASE + 0x18) >> 4) & 0x07;
    Printf("  volume=%lu/15  filter mode=%lu (1=LP 2=BP 4=HP)  routing=$%02lx\n",
           vol, mode, sh_get(SH_SID_SHADOW_BASE + 0x17) & 0xFF);
    Printf("  gates: v1=%lu v2=%lu v3=%lu  waveforms: %02lx %02lx %02lx\n",
           sh_get(SH_SID_SHADOW_BASE + 0x04) & 1,
           sh_get(SH_SID_SHADOW_BASE + 0x0B) & 1,
           sh_get(SH_SID_SHADOW_BASE + 0x12) & 1,
           (sh_get(SH_SID_SHADOW_BASE + 0x04) >> 4) & 0x0F,
           (sh_get(SH_SID_SHADOW_BASE + 0x0B) >> 4) & 0x0F,
           (sh_get(SH_SID_SHADOW_BASE + 0x12) >> 4) & 0x0F);
    if (vol == 0)
        Printf("  WARNING: volume is 0 - the chip is muted\n");
}

static void report_tune_progress(void)
{
    cache_flush((void *)&g_shared[SH_SID_WRITES], 4);
    cache_flush((void *)&g_shared[SH_SID_FRAMES], 4);
    Printf("  tune: %lu frames played, %lu SID writes\n",
           sh_get(SH_SID_FRAMES), sh_get(SH_SID_WRITES));
    if (sh_get(SH_SID_WRITES) == 0)
        Printf("  WARNING: zero SID writes - the 6502 side is the problem,\n"
               "           not the audio path\n");
}

/* ---- Measure how fast Core1 actually renders ----
 * The 68k has a clock, Core1 does not.
 *
 * The first version of this drained one 882-frame chunk per loop and called
 * DateStamp every iteration. DateStamp is far too slow for that: the 68k
 * became the bottleneck, Core1 sat in backpressure, and the figure measured
 * the 68k rather than Core1 (none and both came out within 12% of each
 * other, which cannot be true). It also left the read cursor far behind the
 * write cursor, so the playback that followed read already-overwritten ring
 * data and reported a nonsense ring_min.
 *
 * This version throws the data away by slamming the read cursor straight to
 * the write cursor, so Core1 is never held back, and only looks at the clock
 * every 256 iterations. It resynchronises cleanly on the way out.
 */
static void bench_core1(int seconds)
{
    struct DateStamp d0, d1;
    ULONG t0, t1, start_w, end_w, frames, fps, target, n = 0;

    if (seconds < 1) seconds = 3;
    target = (ULONG)seconds * 50UL;

    g_read_pos = sh_get_flush(SH_PCM_WRITE);
    sh_put_flush(SH_PCM_READ, g_read_pos);
    start_w = g_read_pos;

    DateStamp(&d0);
    t0 = (ULONG)d0.ds_Minute * 3000UL + (ULONG)d0.ds_Tick;

    for (;;) {
        /* Drain everything, instantly: never let Core1 hit backpressure. */
        g_read_pos = sh_get_flush(SH_PCM_WRITE);
        sh_put_flush(SH_PCM_READ, g_read_pos);

        if ((++n & 0xFFUL) == 0) {
            DateStamp(&d1);
            t1 = (ULONG)d1.ds_Minute * 3000UL + (ULONG)d1.ds_Tick;
            if (t1 - t0 >= target) break;
            if (ctrl_c()) { Printf("\nCtrl-C\n"); break; }
        }
    }

    end_w = sh_get_flush(SH_PCM_WRITE);
    frames = end_w - start_w;
    fps = frames / (ULONG)seconds;

    Printf("BENCH over %ld s: Core1 produced %lu frames = %lu frames/s\n",
           (LONG)seconds, frames, fps);
    Printf("      real time needs %lu -> %lu %% of real time\n",
           g_sr, fps * 100UL / g_sr);
    if (fps < g_sr)
        Printf("      TOO SLOW: playback cannot hold at %lu Hz\n", g_sr);

    /* Resync and clear the metrics so the playback figures are honest. */
    g_read_pos = sh_get_flush(SH_PCM_WRITE);
    sh_put_flush(SH_PCM_READ, g_read_pos);
    g_ring_min  = 0xFFFFFFFFUL;
    g_starves   = 0;
    g_underruns = 0;
}

/* Main double-buffer play loop (AHITest model) */
/* One implementation of the double-buffer, used by both front ends.
 *
 * player_tick() is the whole trick behind a GUI that stays responsive while
 * audio plays: it blocks on WaitIO, so AHI itself paces the caller's loop.
 * No polling, no timer, no CPU burnt - the event loop simply calls this and
 * handles window messages in between. */
static void player_start(void (*fill)(UBYTE *))
{
    fill(g_buf0);
    g_acur = 0; g_anext = 1;
    send_ahi_chunk(0, g_buf0, CHUNK_BYTES);
}

static void player_tick(void (*fill)(UBYTE *))
{
    UBYTE *buf = (g_anext == 0) ? g_buf0 : g_buf1;
    fill(buf);
    send_ahi_chunk(g_anext, buf, CHUNK_BYTES);
    if (g_act[g_acur]) {
        WaitIO((struct IORequest *)g_req[g_acur]);
        g_act[g_acur] = FALSE;
    }
    { int t = g_acur; g_acur = g_anext; g_anext = t; }
}

/* Stop the audio without closing the device, so playback can start again. */
static void player_stop(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (g_req[i] && g_act[i]) {
            if (!CheckIO((struct IORequest *)g_req[i]))
                AbortIO((struct IORequest *)g_req[i]);
            WaitIO((struct IORequest *)g_req[i]);
            g_act[i] = FALSE;
        }
    }
}

static void play_loop(int max_seconds, void (*fill)(UBYTE *))
{
    UBYTE *buf;
    ULONG frames_played = 0;
    ULONG max_frames = (ULONG)max_seconds * g_sr;

    if (g_gm) {
        /* Reply-port model, copied from the validated ZZDoom driver: both
         * requests stay in flight, and whichever completes is refilled and
         * chained behind the other. The earlier buzzing came from a broken
         * version of this, not from the approach - zz_ahi.c proves it works. */
        ULONG sigmask = 1UL << g_ahiport->mp_SigBit;
        fill(g_buf0);
        send_ahi_chunk(0, g_buf0, CHUNK_BYTES);
        fill(g_buf1);
        send_ahi_chunk(1, g_buf1, CHUNK_BYTES);
        for (;;) {
            struct AHIRequest *done;
            if (ctrl_c()) { Printf("\nCtrl-C\n"); break; }
            if (max_seconds > 0 && frames_played >= max_frames) break;
            Wait(sigmask | SIGBREAKF_CTRL_C);   /* stay responsive */
            while ((done = (struct AHIRequest *)GetMsg(g_ahiport))) {
                int slot = (done == g_req[0]) ? 0 : 1;
                g_act[slot] = FALSE;
                buf = slot ? g_buf1 : g_buf0;
                fill(buf);
                send_ahi_chunk(slot, buf, CHUNK_BYTES);
                frames_played += CHUNK_FRAMES;
            }
        }
        return;
    }

    player_start(fill);
    for (;;) {
        if (ctrl_c()) { Printf("\nCtrl-C\n"); break; }
        if (max_seconds > 0 && frames_played >= max_frames) break;
        player_tick(fill);
        frames_played += CHUNK_FRAMES;
    }
    (void)buf;
}


/* ---- Dump Core1 render diagnostics ---- */
static void dump_diags(const char *tag)
{
    ULONG i;
    /* flush the two diagnostic cache lines */
    cache_flush((void *)&g_shared[16], 8 * 4);
    cache_flush((void *)&g_shared[24], 8 * 4);

    Printf("--- diag %s ---\n", (ULONG)tag);
    Printf("  capacity=%lu delta_fed=%lu got_last=%lu produced=%lu\n",
           sh_get(SH_DIAG_CAPACITY), sh_get(SH_DIAG_DELTA_FED),
           sh_get(SH_DIAG_GOT_LAST), sh_get(SH_DIAG_PRODUCED));
    Printf("  zero_pad=%lu (MUST be 0)  clock_calls=%lu\n",
           sh_get(SH_DIAG_ZEROPAD), sh_get(SH_DIAG_CLOCKCALLS));
    Printf("  pending_cycles=%lu cycle_frac_fp=%lu\n",
           sh_get(SH_DIAG_PENDING), sh_get(SH_DIAG_FRAC_FP));
    Printf("  min_sample=%ld max_sample=%ld\n",
           (LONG)sh_get(SH_DIAG_MIN_SAMPLE), (LONG)sh_get(SH_DIAG_MAX_SAMPLE));
    Printf("  probe: osc3_peak=%lu env3_peak=%lu  (raw, pre-DAC)\n",
           sh_get(SH_SID_SHADOW_BASE + 25), sh_get(SH_SID_SHADOW_BASE + 26));
    if (sh_get(SH_ACTIVE_NUM_SIDS) > 1) {
        Printf("  R channel: min=%ld max=%ld   writes: sid0=%lu sid1=%lu\n",
               (LONG)sh_get(SH_DIAG_MIN_R), (LONG)sh_get(SH_DIAG_MAX_R),
               sh_get(SH_SID_WRITES1), sh_get(SH_SID_WRITES2));
        if (sh_get(SH_DIAG_LEN_MISMATCH))
            Printf("  WARNING len_mismatch=%lu - the two chips are drifting\n",
                   sh_get(SH_DIAG_LEN_MISMATCH));
    }
    Printf("  clip_pos=%lu clip_neg=%lu\n",
           sh_get(SH_DIAG_CLIP_POS), sh_get(SH_DIAG_CLIP_NEG));
    Printf("  ring_write before=%lu after=%lu  overruns=%lu\n",
           sh_get(SH_DIAG_RING_BEFORE), sh_get(SH_DIAG_RING_AFTER),
           sh_get(SH_DIAG_OVERRUNS));
    cache_flush((void *)&g_shared[SH_RENDER_CYCLES], 4);
    Printf("  render_cycles=%lu\n", sh_get(SH_RENDER_CYCLES));
    Printf("  68k: read_pos=%lu underruns=%lu\n", g_read_pos, g_underruns);
    Printf("  68k: spins=%lu (wait-loop turns; large = busy-wait)\n", g_spins);
    Printf("  68k: ring_min=%lu frames (%lu ms)  starves=%lu / need=%lu\n",
           (g_ring_min == 0xFFFFFFFFUL) ? 0UL : g_ring_min,
           ((g_ring_min == 0xFFFFFFFFUL) ? 0UL : g_ring_min) / (g_sr / 1000UL),
           g_starves, (ULONG)CHUNK_FRAMES);
    (void)i;
}

#ifndef ZZSID_GUI
int main(int argc, char **argv)
{
    struct Library   *ExpLib;
    struct ConfigDev *cd;
    /* NULL = use the embedded blob when one is compiled in. */
    const char *blob_path = NULL;
    int mode = 2, max_seconds = -1, i;   /* -1 = not given on the CLI */
    LONG g_user_chunk = 0;               /* 0 = derive from the rate */

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-ahi")) mode = 1;
        else if (!strcmp(argv[i], "-sec") && i+1 < argc) max_seconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-stop")) g_stop_on_exit = 1;   /* default */
        else if (!strcmp(argv[i], "-keep")) g_stop_on_exit = 0;
        else if (!strcmp(argv[i], "-bench")) g_bench = 3;
        else if (!strcmp(argv[i], "-nocache")) g_nocache = 1;
        else if (!strcmp(argv[i], "-rate") && i+1 < argc) {
            LONG v = atol(argv[++i]);
            if (v < 8000 || v > 48000) { Printf("-rate: 8000..48000\n"); return 20; }
            g_sr = (ULONG)v;
        }
        else if (!strcmp(argv[i], "-unit") && i+1 < argc) g_unit = atol(argv[++i]);
        else if (!strcmp(argv[i], "-blob") && i+1 < argc) blob_path = argv[++i];
        else if (!strcmp(argv[i], "-2sid")) g_num_sids = 2;
        else if (!strcmp(argv[i], "-mirror2sid")) g_mirror_force = 1;
        else if (!strcmp(argv[i], "-no-mirror2sid")) g_mirror_force = 0;
        else if (!strcmp(argv[i], "-sid2base") && i+1 < argc) {
            /* accepts 0xd420 or plain decimal */
            const char *v = argv[++i];
            LONG b = (v[0] == '0' && (v[1] == 'x' || v[1] == 'X'))
                   ? (LONG)strtol(v + 2, NULL, 16) : atol(v);
            if (b < 0xD420L || b > 0xDFE0L || (b & 0x1FL) != 0L) {
                Printf("-sid2base: need $D420..$DFE0, multiple of 32\n");
                return 20;
            }
            g_sid1_base = (ULONG)b;
            if (g_num_sids < 2) g_num_sids = 2;
        }
        else if (!strcmp(argv[i], "-chunk") && i+1 < argc) g_user_chunk = atol(argv[++i]);
        else if (!strcmp(argv[i], "-mono")) g_mono = 1;
        else if (!strcmp(argv[i], "-gm")) g_gm = 1;
        /* CPU isolation probes. Both skip AHI entirely so the audio driver
         * cannot be part of the measurement.
         *   -idle  N : Core1 renders, the 68k touches nothing at all.
         *   -drain N : Core1 renders, the 68k reads the ring and discards it.
         * Comparing -ahi / -idle / -drain / full playback attributes the CPU
         * load to exactly one of: the audio driver, Core1's bus traffic, the
         * Zorro read path, or our own loop. */
        else if (!strcmp(argv[i], "-idle") && i+1 < argc) {
            g_probe = 1; g_noahi = 1; g_probe_sec = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-drain") && i+1 < argc) {
            g_probe = 2; g_noahi = 1; g_probe_sec = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-sid") && i+1 < argc) g_tune_path = argv[++i];
        else if (!strcmp(argv[i], "-song") && i+1 < argc) {
            /* Kept 1-based all the way to the display. It becomes the value
             * the C64 init routine wants (A = song - 1) at the single point
             * that calls init, and nowhere else. */
            g_song_req = atol(argv[++i]);
        }
        else if (!strcmp(argv[i], "-6581")) {
            g_chip_model = ZZRESID_CHIP_6581; g_model_forced = 1;
        }
        else if (!strcmp(argv[i], "-8580")) {
            g_chip_model = ZZRESID_CHIP_8580; g_model_forced = 1;
        }
        else if (!strcmp(argv[i], "-auto")) g_model_forced = 0;
        else if (!strcmp(argv[i], "-filt") && i+1 < argc) {
            const char *v = argv[++i];
            if      (!strcmp(v, "none")) g_filter_cfg = ZZRESID_FILT_NONE;
            else if (!strcmp(v, "ext"))  g_filter_cfg = ZZRESID_FILT_EXTERNAL;
            else if (!strcmp(v, "int"))  g_filter_cfg = ZZRESID_FILT_INTERNAL;
            else if (!strcmp(v, "both")) g_filter_cfg = ZZRESID_FILT_INTERNAL |
                                                        ZZRESID_FILT_EXTERNAL;
            else { Printf("-filt: none|ext|int|both\n"); return 20; }
        }
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "?")) {
            Printf("ZZSIDPlay [blob] options:\n");
            Printf("  -sid <file>   play a .sid tune (no time limit)\n");
            Printf("  -song N       subtune to play, 1..songs (default: startSong)\n");
            Printf("  -6581 -8580   force the chip model\n");
            Printf("  -auto         take the model from the file header (default)\n");
            Printf("  -filt none|ext|int|both\n");
            Printf("  -sec N        stop after N seconds (0 = never)\n");
            Printf("  -rate N       sample rate, 8000..48000 (default 44100)\n");
            Printf("  -unit N       AHI unit (default = system default)\n");
            Printf("  -nocache      skip CacheClearE on the ZZ9000 window\n");
            Printf("  -idle N       CPU probe: Core1 renders, 68k idle, no AHI\n");
            Printf("  -drain N      CPU probe: 68k reads the ring, no AHI\n");
            Printf("  -chunk N      frames per AHI buffer, 64..4096\n");
            Printf("  -mono         AHIST_M16S instead of stereo\n");
            Printf("  -gm           reply-port refill instead of WaitIO\n");
            Printf("  -bench        measure Core1 render speed\n");
            Printf("  -ahi          68k sine only, no Core1\n");
            Printf("  -2sid         two SID chips: chip0 left, chip1 right\n");
            Printf("  -sid2base N   second chip base, default 0xd420\n");
            Printf("  -mirror2sid   force mirroring chip0 writes to chip1\n");
            Printf("  -no-mirror2sid  debug: strict routing, no mirroring\n");
            Printf("  -blob <file>  load an external blob instead of the built-in one\n");
            Printf("  -stop         stop Core1 on exit and hand it back (default)\n");
            Printf("  -keep         leave Core1 resident for the next run\n");
            return 0;
        }
        else blob_path = argv[i];
    }

    /* Default 4096 frames. The AHI cost on this machine is linear in
     * CMD_WRITE per second, not in bytes: ZZMIDIPlay went from 15-20% CPU at
     * 2048 samples/buffer to under 7% at 4096, everything else unchanged.
     * The old default of one PAL frame meant 50 writes/s and 99% CPU.
     * Going below ~5 writes/s needs a ring larger than 8192 frames. */
    /* No tune means no header to consult, so a two-chip setup mirrors -
     * same rule as a 1SID file. -no-mirror2sid gives the L/R test tone. */
    if (g_num_sids > 1 && !g_tune_path)
        g_mirror = (g_mirror_force >= 0) ? (ULONG)g_mirror_force : 1UL;

    g_chunk = (g_user_chunk > 0) ? (ULONG)g_user_chunk : 4096UL;
    if (g_chunk < CHUNK_MIN_FRAMES) g_chunk = CHUNK_MIN_FRAMES;
    if (g_chunk > CHUNK_MAX_FRAMES) g_chunk = CHUNK_MAX_FRAMES;
    if (!bufs_alloc()) { Printf("cannot allocate AHI buffers\n"); return 20; }
    Printf("AHI: %lu frames/buf, %lu Hz %s -> %lu writes/s, model=%s\n",
           g_chunk, g_sr, (ULONG)(g_mono ? "mono" : "stereo"),
           g_sr / g_chunk, (ULONG)(g_gm ? "replyport" : "waitio"));

    /* A test tone has no natural length, so the old 10 s cap made sense.
     * A tune does: it plays until Ctrl-C unless -sec says otherwise. */
    if (max_seconds < 0)
        max_seconds = g_tune_path ? 0 : 10;

    ExpLib = OpenLibrary("expansion.library", 0L);
    if (!ExpLib) { PutStr("no expansion.library\n"); return 20; }
    cd = FindConfigDev(NULL, ZZ9000_MANUF, ZZ9000_PROD_AX);
    if (!cd) cd = FindConfigDev(NULL, ZZ9000_MANUF, ZZ9000_PROD);
    if (!cd) { PutStr("ZZ9000 not found\n"); CloseLibrary(ExpLib); return 20; }

    g_board  = (UBYTE *)cd->cd_BoardAddr;
    g_fb     = g_board + MNT_FB_BASE;
    g_shared = (volatile ULONG *)(g_fb + ZZRESID_SHARED_FB_OFFSET);
    g_pcm    = (volatile ULONG *)(g_fb + ZZRESID_PCM_FB_OFFSET);
    g_sidbuf = (volatile UBYTE *)(g_fb + ZZRESID_SID_FB_OFFSET);

    if (!g_noahi && !ahi_open()) {
        Printf("ahi_open failed\n");
        bufs_free(); CloseLibrary(ExpLib); return 10;
    }

    if (mode == 1) {
        Printf("MODE 1: 68k sine 440Hz via AHI. Ctrl-C to stop.\n");
        play_loop(max_seconds, fill_sine);
        ahi_close();
        CloseLibrary(ExpLib);
        return 0;
    }

    /* MODE 2: Core1 pre-render + loop */
    Printf("MODE 2: Core1 pre-render + AHI loop\n");
    if (g_num_sids > 1)
        Printf("SIDs: 2  chip0 $D400 (left)  chip1 $%04lx (right)\n",
               g_sid1_base);
    else
        Printf("SIDs: 1  chip0 $D400 (mono, duplicated L/R)\n");
    Printf("filters: %s\n", (ULONG)(
        g_filter_cfg == ZZRESID_FILT_NONE     ? "none" :
        g_filter_cfg == ZZRESID_FILT_EXTERNAL ? "external only" :
        g_filter_cfg == ZZRESID_FILT_INTERNAL ? "internal only" : "both"));

    /* The .sid header decides the stereo mode, and the stereo mode is part
     * of the configuration handed to Core1 at launch. So the file must be
     * parsed FIRST: publishing the configuration before knowing whether the
     * tune drives the second chip left mirroring permanently off, and the
     * right channel silent on every tune. Reading the file touches nothing
     * on the card, so it is safe this early. */
    {
        struct SidInfo tmp;
        if (g_tune_path) {
            Printf("Loading %s\n", (ULONG)g_tune_path);
            if (!load_sid_file(g_tune_path, &tmp)) {
                ahi_close(); bufs_free(); CloseLibrary(ExpLib); return 10;
            }
            g_si = tmp;
            g_have_tune = 1;
        }
    }

    if (blob_is_live()) {
        ULONG abi;
        cache_flush((void *)&g_shared[SH_ABI_VERSION], 4);
        abi = sh_get(SH_ABI_VERSION);
        if (abi != ZZRESID_ABI_VERSION) {
            Printf("STALE BLOB: resident blob speaks ABI %lu, this launcher "
                   "needs %lu.\n", abi, (ULONG)ZZRESID_ABI_VERSION);
            Printf("Power cycle the Amiga (real power off, not a reset) so "
                   "the new zzresid_core1.bin is loaded.\n");
            ahi_close(); bufs_free(); CloseLibrary(ExpLib); return 10;
        }
        Printf("Core1 blob already resident (ABI %lu) - reconfiguring in place\n",
               abi);
        if (!blob_reconfigure()) {
            Printf("reconfigure failed (no ack). Power cycle needed.\n");
            ahi_close(); bufs_free(); CloseLibrary(ExpLib); return 10;
        }
        Printf("Core1 RUNNING (reconfigured)\n");
        report_active_config();
    } else {
        {
            int ok;
            if (blob_path) {
                ok = load_blob(blob_path);
            } else {
#if ZZRESID_EMBEDDED
                ok = load_blob_embedded();
#else
                ok = load_blob("zzresid_core1.bin");
#endif
            }
            if (!ok) { ahi_close(); bufs_free(); CloseLibrary(ExpLib); return 10; }
        }
        Printf("Launching Core1...\n");
        launch_core1();
        if (!wait_status(ZZRESID_ST_READY, 5000000UL)) {
            Printf("Core1 not READY status=%lu\n", sh_get(SH_STATUS));
            ahi_close(); bufs_free(); CloseLibrary(ExpLib); return 10;
        }
        Printf("Core1 READY\n");

        sh_put_flush(SH_CMD, ZZRESID_CMD_START);
        wait_status(ZZRESID_ST_RUNNING, 1000000UL);
        Printf("Core1 RUNNING\n");
        report_active_config();
    }

    /* Send an A4 440Hz triangle note (Build C reSID; ignored by Build A) */
    if (g_have_tune) {
        /* Default is startSong, NOT subtune 1: many files put the main
         * theme elsewhere. An explicit -song out of range is refused rather
         * than quietly corrected - the wrong subtune played without warning
         * is worse than an error message. */
        if (g_song_req == 0) {
            g_song_cur = g_si.startsong;
        } else if (g_song_req < 1 || (ULONG)g_song_req > g_si.songs) {
            Printf("Invalid subtune %ld, file has %lu subtunes\n",
                   g_song_req, g_si.songs);
            sh_put_flush(SH_CMD, ZZRESID_CMD_STOP);
            ahi_close(); bufs_free(); CloseLibrary(ExpLib); return 20;
        } else {
            g_song_cur = (ULONG)g_song_req;
        }
        Printf("Subtunes: %lu, default: %lu, playing: %lu (init A=%lu)\n",
               g_si.songs, g_si.startsong, g_song_cur, g_song_cur - 1);
        if (!start_tune(&g_si, g_song_cur - 1)) {
            sh_put_flush(SH_CMD, ZZRESID_CMD_STOP);
            ahi_close(); bufs_free(); CloseLibrary(ExpLib); return 10;
        }
    } else {
        send_sid_note();
        Printf("Sent SID note A4 440Hz triangle, LP filter, voice1 routed\n");
    }

    /* Pre-fill: wait for a cushion of samples before starting playback */
    {
        ULONG timeout = 0, produced = 0;
        /* Three chunks ahead where the ring allows it, but never more than
         * three quarters of the ring - otherwise a large -chunk makes this
         * unreachable and the prefill times out. */
        ULONG cushion = CHUNK_FRAMES * 3;
        ULONG ceiling = (ZZRESID_RING_SAMPLES / 4) * 3;
        if (cushion > ceiling) cushion = ceiling;
        for (;;) {
            produced = sh_get_flush(SH_PCM_WRITE);
            if (produced >= cushion) break;
            if (++timeout > 5000000UL) { Printf("prefill timeout, got %lu\n", produced); break; }
            if (ctrl_c()) { sh_put_flush(SH_CMD, ZZRESID_CMD_STOP);
                            ahi_close(); bufs_free(); CloseLibrary(ExpLib); return 0; }
        }
    }

    if (g_bench) {
        bench_core1(g_bench);
        Printf("\n");
    }

    if (max_seconds > 0)
        Printf("Streaming from Core1 ring. Ctrl-C to stop (max %ld s)\n",
               (LONG)max_seconds);
    else
        Printf("Streaming from Core1 ring. Ctrl-C to stop (no time limit)\n");
    g_read_pos = 0;
    g_underruns = 0;
    g_ring_min = 0xFFFFFFFFUL;
    g_starves = 0;
    g_spins = 0;

    dump_diags("before playback");
    if (g_probe) {
        /* No AHI, no audio. The only question is what the CPU meter says. */
        ULONG t, ticks = (ULONG)g_probe_sec * 50UL;   /* Delay() is in 1/50 s */
        /* Printf() pushes every argument as a LONG, so a %s pointer needs
         * an explicit cast or the compiler warns about int conversion. */
        Printf("%s probe: %ld s, no AHI. Watch the CPU meter now.\n",
               (ULONG)((g_probe == 1) ? "IDLE" : "DRAIN"), (LONG)g_probe_sec);
        g_read_pos = sh_get_flush(SH_PCM_WRITE);
        sh_put_flush(SH_PCM_READ, g_read_pos);
        for (t = 0; t < ticks; t++) {
            if (ctrl_c()) { Printf("\nCtrl-C\n"); break; }
            if (g_probe == 2) {
                /* Consume one PAL frame per tick: same Zorro traffic as real
                 * playback, but nothing is handed to any audio driver. */
                ULONG wpos  = sh_get_flush(SH_PCM_WRITE);
                ULONG avail = wpos - g_read_pos;
                if (avail >= CHUNK_FRAMES) fill_stream(g_buf0);
            }
            Delay(1);
        }
    } else {
        play_loop(max_seconds, fill_stream);
    }
    dump_diags("after playback");

    Printf("Done. underruns=%lu\n", g_underruns);
    if (g_tune_path) {
        report_tune_progress();
        Printf("  played subtune %lu/%lu\n", g_song_cur, g_si.songs);
        dump_sid_regs();
    }
    if (g_sidfile) { FreeVec(g_sidfile); g_sidfile = NULL; }

    if (g_stop_on_exit) {
        sh_put_flush(SH_CMD, ZZRESID_CMD_STOP);
        wait_status(ZZRESID_ST_STOPPED, 1000000UL);
        {
            ULONG hd = sh_get_flush(SH_DIAG_RESERVED31);
            Printf("Core1 released (status=%lu)  heap peak=%lu KB",
                   sh_get(SH_STATUS), (hd & 0x00FFFFFFUL) / 1024UL);
            if ((hd >> 24) & 0xFF)
                Printf("  ALLOC REFUSED x%lu - raise _heap_size", (hd >> 24) & 0xFFUL);
            Printf("\n");
        }
    } else {
        Printf("Core1 left resident (-keep) - next run reconfigures instantly\n");
    }

    Printf("AHI: %lu CMD_WRITE issued\n", g_ahi_writes);
    ahi_close();
    bufs_free();
    CloseLibrary(ExpLib);
    return 0;
}

#endif /* !ZZSID_GUI */

/* ===================================================================
 * GUI front end.  Built with -DZZSID_GUI into a separate binary.
 *
 * Same source file as the CLI on purpose: both front ends call the same
 * engine functions - load_sid_file(), start_tune(), blob_reconfigure(),
 * player_start/tick/stop() - so there is one implementation of the hard
 * parts and no chance of the two drifting apart. ZZMIDIPlay is built the
 * same way, and its structure is followed closely here: GadTools gadgets,
 * an ASL requester, GT_GetIMsg/GT_ReplyIMsg, and the main loop below.
 *
 * The main loop is the one thing worth reading twice:
 *
 *     while (!quit) {
 *         if (playing) player_tick();   <- blocks on WaitIO: AHI paces us
 *         handle_idcmp();               <- never blocks
 *         if (!playing) Wait(sigs);     <- block only when idle
 *     }
 *
 * While playing, WaitIO inside the tick is what blocks, so the loop runs
 * at exactly the audio rate and the window stays responsive with no
 * polling and no CPU burnt. While idle, Wait() on the window signal does
 * the same job. Neither state spins.
 * =================================================================== */
#ifdef ZZSID_GUI

#include <exec/lists.h>
#include <exec/nodes.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/graphics.h>
#include <stdio.h>          /* sprintf for the status lines */

/* Library bases, exactly as ZZMIDIPlay declares them - that code compiles
 * on this toolchain, which settles the question.
 *
 * proto/gadtools.h and proto/asl.h declare GadToolsBase and AslBase as
 * tentative definitions, so the program must DEFINE them here, at file
 * scope and NOT static. IntuitionBase and GfxBase are not declared at all:
 * the startup code provides them, and redeclaring them is what the first
 * build tripped over. */
struct Library *GadToolsBase = NULL;
struct Library *AslBase      = NULL;

/* ================================================================
 * Window geometry
 *
 * Two control rows rather than one. Everything wanted to be on a single
 * strip like ZZMIDIPlay's, but ZZMIDIPlay has eleven icon buttons and no
 * cycle gadgets, while this player has six buttons AND three cycles whose
 * labels have to stay readable. Fitting all nine on one row forced the
 * window past 460 pixels, which is no longer the same tool. Split in two,
 * the window stays close to the other players and nothing is cramped.
 * ================================================================ */
#define WIN_MIN_WIDTH 380        /* a little wider than the other two   */
#define TEXT_LINES   4
#define CTRL_H       12          /* one height for buttons and cycles   */
#define CTRL_GAP      2
#define BTN_MAX_W    64          /* buttons span the row, within reason */
/* Chrome a GadTools cycle gadget needs on top of its widest label: the
 * arrow image is about 20 pixels, then a bevel and a text inset on each
 * side. That is already past 32, so 28 - the first guess here - clipped
 * "Filt all" by half a character. 40 leaves real room, and the cost of
 * being generous is a few pixels while the cost of being tight is
 * unreadable text, because GadTools clips in silence. */
#define CYC_PAD      40

/* Transport buttons carry IDs 1..6 so the icon loop can walk them the way
 * ZZMIDIPlay's does. */
#define GAD_LOAD     1
#define GAD_PREV     2
#define GAD_PLAY     3
#define GAD_STOP     4
#define GAD_NEXT     5
#define GAD_PLS      6
#define GAD_BTN_N    6
#define GAD_SONG     7
#define GAD_MODEL    8
#define GAD_FILTER   9
#define GAD_SIDMODE  10
#define GAD_COUNT    11

/* Playlist window gadgets - a separate gadget context, as in ZZMIDIPlay. */
#define PLG_LIST     1
#define PLG_PLAY     2
#define PLG_ADD      3
#define PLG_DEL      4
#define PLG_UP       5
#define PLG_DOWN     6
#define PLG_CLEAR    7

#define MENU_DEBUG         1
#define MENU_QUIT          2
#define MENU_SET_DEFAULT   3
#define MENU_CLEAR_DEFAULT 4

#define MAX_PLAYLIST  256
#define PATH_BUF_SIZE 512
#define PLS_DISP_MAX   80

/* ================================================================
 * Transport button icons -- 16x8, one bitplane.
 *
 * Taken verbatim from ZZMIDIPlay so the two players look like the same
 * family of tools rather than two unrelated programs.
 * ================================================================ */
#define BTN_ICON_W  16
#define BTN_ICON_H   8

static const UWORD g_idata_open[BTN_ICON_H] =
    { 0x0000,0x1800,0x3FC0,0x2040,0x2040,0x2040,0x3FC0,0x0000 };
static const UWORD g_idata_prev[BTN_ICON_H] =
    { 0x0000,0x0408,0x0C08,0x1C08,0x1C08,0x0C08,0x0408,0x0000 };
static const UWORD g_idata_play[BTN_ICON_H] =
    { 0x1000,0x1800,0x1C00,0x1E00,0x1C00,0x1800,0x1000,0x0000 };
static const UWORD g_idata_stop[BTN_ICON_H] =
    { 0x0000,0x07E0,0x07E0,0x07E0,0x07E0,0x07E0,0x07E0,0x0000 };
static const UWORD g_idata_next[BTN_ICON_H] =
    { 0x0000,0x1080,0x1880,0x1C80,0x1C80,0x1880,0x1080,0x0000 };
static const UWORD g_idata_pls[BTN_ICON_H] =
    { 0x0000,0x3FE0,0x0000,0x3FE0,0x0000,0x3FE0,0x0000,0x0000 };

static struct Image g_btn_img[GAD_BTN_N];

static struct Screen   *g_scr   = NULL;
static APTR             g_vi    = NULL;
static struct Window   *g_win   = NULL;
static struct Gadget   *g_glist = NULL;
static struct Gadget   *g_gad[GAD_COUNT];
static struct Menu     *g_menu  = NULL;

static UWORD  g_tx, g_th, g_ty[TEXT_LINES];
static WORD   g_iw;
static int    g_quit    = 0;
static int    g_playing = 0;
static int    g_core_up = 0;
static int    g_icon_state = -1;    /* what draw_icons last drew */
static char   g_path[PATH_BUF_SIZE];
/* Persistent directory preferences, same Default/Last model as ZZVGMPlay.
 * Names refer to global ENV: and persistent ENVARC: files. */
static char   g_default_drawer[PATH_BUF_SIZE];
static char   g_last_drawer[PATH_BUF_SIZE];
static char   g_status[96];
static char   g_row[TEXT_LINES][96];

/* ---- playlist ------------------------------------------------------ */
static char          *g_playlist[MAX_PLAYLIST];
static int            g_pcount   = 0;
static int            g_cur      = -1;  /* entry currently loaded      */
static int            g_pls_sel  = -1;  /* selected row, for Del/Up/Dn */
static struct List    g_pls_execlist;
static struct Node    g_pls_nodes[MAX_PLAYLIST];
static char           g_pls_display[MAX_PLAYLIST][PLS_DISP_MAX];
static struct Window *g_plswin   = NULL;
static struct Gadget *g_plsglist = NULL;
static struct Gadget *g_plsgad   = NULL;

/* Menu. "Show debug" is the only way any diagnostic text ever appears: a
 * GUI launched from Workbench has no console, and one that opens a CLI
 * window uninvited is a nuisance. */
static struct NewMenu g_newmenu[] = {
    { NM_TITLE, (STRPTR)"Project",     NULL,           0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Show debug",  (STRPTR)"D",
                                        CHECKIT | MENUTOGGLE, 0,
                                        (APTR)MENU_DEBUG },
    { NM_ITEM,  NM_BARLABEL,           NULL,           0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Quit",        (STRPTR)"Q",    0, 0, (APTR)MENU_QUIT },
    { NM_TITLE, (STRPTR)"Settings",    NULL,           0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Set Default Drawer...", NULL, 0, 0,
                                        (APTR)MENU_SET_DEFAULT },
    { NM_ITEM,  (STRPTR)"Clear Default Drawer", NULL, 0, 0,
                                        (APTR)MENU_CLEAR_DEFAULT },
    { NM_END,   NULL,                  NULL,           0, 0, NULL }
};

/* Cycle gadget labels, written to be read rather than to fit. The window
 * is sized from them at run time by measuring them, so there is no
 * character count here to fall out of step with the strings. */
static const char *g_model_lab[]  = { "Auto", "6581", "8580", NULL };
static const char *g_filter_lab[] = { "Filt all", "Filt int", "Filt ext",
                                      "Filt off", NULL };
static const char *g_sid_lab[]    = { "1 SID", "2 SID", NULL };
static UWORD g_model_sel  = 0;
static UWORD g_filter_sel = 0;
static UWORD g_sid_sel    = 0;

static void pls_refresh_listview(void);     /* forward */
static void gui_refresh(void);              /* forward */
static void gui_song_gadget(void);          /* forward */
static int  gui_play(void);                 /* forward */
static void gui_stop(void);                 /* forward */

/* ---- debug console ------------------------------------------------ *
 * Printf and PutStr are redirected to these at the top of this file when
 * ZZSID_GUI is defined, so every diagnostic line the engine already emits
 * goes here and nowhere else. With the window closed they cost a test and
 * a return. */
static void dbg_open(void)
{
    if (g_dbgfh) return;
    g_dbgfh = Open((STRPTR)"CON:0/300/640/180/ZZSIDPlay debug/CLOSE",
                   MODE_NEWFILE);
    if (g_dbgfh) Printf("ZZReSID diagnostics\n");
}

static void dbg_close(void)
{
    if (!g_dbgfh) return;
    Close(g_dbgfh);
    g_dbgfh = 0;
}

/* Fatal errors before the window exists have nowhere to print either. */
static void gui_fatal(const char *msg)
{
    struct EasyStruct es;
    PutStr(msg);
    PutStr("\n");
    if (!IntuitionBase) return;
    es.es_StructSize   = sizeof(es);
    es.es_Flags        = 0;
    es.es_Title        = (UBYTE *)"ZZSIDPlay";
    es.es_TextFormat   = (UBYTE *)"%s";
    es.es_GadgetFormat = (UBYTE *)"OK";
    EasyRequestArgs(NULL, &es, NULL, (APTR)&msg);
}

static void gui_status(const char *msg)
{
    ULONG i = 0;
    while (msg[i] && i < sizeof(g_status) - 1) { g_status[i] = msg[i]; i++; }
    g_status[i] = 0;
}

/* ---- icons --------------------------------------------------------- */
static void init_btn_images(void)
{
    static const UWORD * const src[GAD_BTN_N] = {
        g_idata_open, g_idata_prev, g_idata_play,
        g_idata_stop, g_idata_next, g_idata_pls
    };
    int i;
    for (i = 0; i < GAD_BTN_N; i++) {
        g_btn_img[i].LeftEdge   = 0;
        g_btn_img[i].TopEdge    = 0;
        g_btn_img[i].Width      = BTN_ICON_W;
        g_btn_img[i].Height     = BTN_ICON_H;
        g_btn_img[i].Depth      = 1;
        g_btn_img[i].ImageData  = (UWORD *)src[i];
        g_btn_img[i].PlanePick  = 0x01;
        g_btn_img[i].PlaneOnOff = 0x00;
        g_btn_img[i].NextImage  = NULL;
    }
}

static void draw_icons(void)
{
    int i;
    if (!g_win) return;
    for (i = 1; i <= GAD_BTN_N; i++) {
        if (g_gad[i]) {
            WORD ix = g_gad[i]->LeftEdge +
                      (WORD)((g_gad[i]->Width  - BTN_ICON_W) / 2);
            WORD iy = g_gad[i]->TopEdge  +
                      (WORD)((g_gad[i]->Height - BTN_ICON_H) / 2);
            DrawImage(g_win->RPort, &g_btn_img[i - 1], ix, iy);
        }
    }
    /* Same state dots ZZMIDIPlay puts on its toggles. */
    if (g_gad[GAD_PLAY]) {
        WORD bx = g_gad[GAD_PLAY]->LeftEdge + g_gad[GAD_PLAY]->Width  - 5;
        WORD by = g_gad[GAD_PLAY]->TopEdge  + g_gad[GAD_PLAY]->Height - 5;
        SetAPen(g_win->RPort, g_playing ? 1 : 0);
        RectFill(g_win->RPort, bx, by, bx + 2, by + 2);
    }
    if (g_gad[GAD_PLS]) {
        WORD bx = g_gad[GAD_PLS]->LeftEdge + g_gad[GAD_PLS]->Width  - 5;
        WORD by = g_gad[GAD_PLS]->TopEdge  + g_gad[GAD_PLS]->Height - 5;
        SetAPen(g_win->RPort, g_plswin ? 1 : 0);
        RectFill(g_win->RPort, bx, by, bx + 2, by + 2);
    }
    g_icon_state = g_playing;
}

/* ---- status text -------------------------------------------------- */
/* One plain line of text, redrawn only when it actually changed - the
 * refresh runs ten times a second while playing and an unconditional
 * RectFill there would flicker.
 *
 * Formatting is done by the caller with sprintf: a hand-rolled RawDoFmt
 * output stub is the classic Amiga trick but it is fragile, and getting
 * an argument count wrong there corrupts the stack instead of printing
 * rubbish. */
static void gui_text(int n, const char *str)
{
    struct RastPort *rp;
    const char *s = (str && str[0]) ? str : "";

    if (!g_win || n < 0 || n >= TEXT_LINES) return;
    if (strcmp(g_row[n], s) == 0) return;
    strncpy(g_row[n], s, sizeof(g_row[0]) - 1);
    g_row[n][sizeof(g_row[0]) - 1] = '\0';

    rp = g_win->RPort;
    SetAPen(rp, 0);
    RectFill(rp, g_tx, g_ty[n], g_tx + g_iw - 4, g_ty[n] + g_th - 1);
    if (s[0]) {
        SetAPen(rp, 1);
        SetDrMd(rp, JAM1);
        Move(rp, g_tx + 2, g_ty[n] + rp->Font->tf_Baseline);
        Text(rp, (STRPTR)s, (LONG)strlen(s));
    }
}

static void gui_invalidate(void)
{
    int i;
    /* A one-character string no real line can equal, so the next
     * gui_refresh() redraws every row. */
    for (i = 0; i < TEXT_LINES; i++) { g_row[i][0] = '\001'; g_row[i][1] = 0; }
    g_icon_state = -1;
}

/* The subtune slider doubles as the subtune readout, which is why the
 * text block does not need a "subtune n of m" line of its own. */
static void gui_song_gadget(void)
{
    struct TagItem t[3];
    if (!g_win || !g_gad[GAD_SONG]) return;
    t[0].ti_Tag  = GTSL_Max;
    t[0].ti_Data = (g_have_tune && g_si.songs > 0) ? g_si.songs : 1UL;
    t[1].ti_Tag  = GTSL_Level;
    t[1].ti_Data = g_song_cur ? g_song_cur : 1UL;
    t[2].ti_Tag  = TAG_END;
    t[2].ti_Data = 0;
    GT_SetGadgetAttrsA(g_gad[GAD_SONG], g_win, NULL, t);
}

static void gui_refresh(void)
{
    char buf[96];
    if (!g_win) return;

    if (g_have_tune) {
        sprintf(buf, "%.44s", g_si.name[0]   ? g_si.name   : "(untitled)");
        gui_text(0, buf);
        sprintf(buf, "%.44s", g_si.author[0] ? g_si.author : "(unknown)");
        gui_text(1, buf);
        sprintf(buf, "%s v%lu   %lu/%lu   %s   %s",
                g_si.is_rsid ? "RSID" : "PSID", g_si.version,
                g_song_cur, g_si.songs,
                (sh_get(SH_ACTIVE_MODEL) == ZZRESID_CHIP_8580) ? "MOS8580"
                                                               : "MOS6581",
                (g_num_sids > 1) ? (g_mirror ? "2SID mirror" : "2SID indep")
                                 : "1SID L=R");
        gui_text(2, buf);
    } else {
        gui_text(0, "ZZSIDPlay / ZZReSID on ZZ9000 Core1");
        gui_text(1, "");
        gui_text(2, "");
    }

    /* Playlist position first, then the status, and the status is capped:
     * the two together have to stay inside the window, and the longest
     * status string here is 34 characters. */
    if (g_pcount > 0) {
        sprintf(buf, "[%d/%d] %.32s",
                (g_cur >= 0) ? g_cur + 1 : 0, g_pcount, g_status);
        gui_text(3, buf);
    } else {
        gui_text(3, g_status);
    }

    if (g_icon_state != g_playing) draw_icons();
}

/* ---- Core1 configuration ------------------------------------------ */
static int gui_bring_core_up(void)
{
    if (g_core_up) {
        if (!blob_reconfigure()) { gui_status("reconfigure failed"); return 0; }
        return 1;
    }
    if (blob_is_live()) {
        ULONG abi;
        cache_flush((void *)&g_shared[SH_ABI_VERSION], 4);
        abi = sh_get(SH_ABI_VERSION);
        if (abi != ZZRESID_ABI_VERSION) {
            gui_status("stale blob - power cycle the Amiga");
            return 0;
        }
        if (!blob_reconfigure()) { gui_status("reconfigure failed"); return 0; }
    } else {
        int ok;
#if ZZRESID_EMBEDDED
        ok = load_blob_embedded();
#else
        ok = load_blob("zzresid_core1.bin");
#endif
        if (!ok) { gui_status("cannot load the Core1 blob"); return 0; }
        launch_core1();
        if (!wait_status(ZZRESID_ST_READY, 5000000UL)) {
            gui_status("Core1 did not become ready");
            return 0;
        }
        sh_put_flush(SH_CMD, ZZRESID_CMD_START);
        wait_status(ZZRESID_ST_RUNNING, 1000000UL);
    }
    g_core_up = 1;
    return 1;
}

/* Turn the three cycle gadgets into engine settings. Called before a file
 * is parsed AND before every (re)configuration, so what is on screen is
 * what Core1 gets.
 *
 * Auto must re-read the header rather than leave g_chip_model alone:
 * after a manual 8580, switching back to Auto has to undo it. */
static void gui_apply_gadgets(void)
{
    switch (g_model_sel) {
        case 1: g_chip_model = ZZRESID_CHIP_6581; g_model_forced = 1; break;
        case 2: g_chip_model = ZZRESID_CHIP_8580; g_model_forced = 1; break;
        default: {
            ULONG hdr = (g_si.flags >> 4) & 3UL;
            g_model_forced = 0;
            if (g_have_tune) {
                if      (hdr == 1) g_chip_model = ZZRESID_CHIP_6581;
                else if (hdr == 2) g_chip_model = ZZRESID_CHIP_8580;
                /* 0 = unknown, 3 = either: keep what is already set. */
            }
            break;
        }
    }
    switch (g_filter_sel) {
        case 1: g_filter_cfg = ZZRESID_FILT_INTERNAL; break;
        case 2: g_filter_cfg = ZZRESID_FILT_EXTERNAL; break;
        case 3: g_filter_cfg = ZZRESID_FILT_NONE;     break;
        default: g_filter_cfg = ZZRESID_FILT_INTERNAL |
                                ZZRESID_FILT_EXTERNAL; break;
    }
    g_num_sids = (g_sid_sel == 1) ? 2UL : 1UL;
    /* A 1SID tune on two chips is mirrored; a real 2SID tune is not. */
    if (g_num_sids > 1) g_mirror = (g_have_tune && g_si.sid2) ? 0UL : 1UL;
    else                g_mirror = 0UL;
}

/* ---- transport ----------------------------------------------------- */
static void gui_stop(void)
{
    if (!g_playing) return;
    player_stop();
    g_playing = 0;
    gui_status("stopped");
}

static int gui_play(void)
{
    if (!g_have_tune) { gui_status("load a .sid file first"); return 0; }
    gui_stop();

    gui_apply_gadgets();
    if (!gui_bring_core_up()) return 0;

    if (!start_tune(&g_si, g_song_cur - 1)) {
        gui_status("could not start the subtune");
        return 0;
    }
    g_read_pos  = 0;
    g_underruns = 0;
    g_ring_min  = 0xFFFFFFFFUL;
    g_starves   = 0;
    g_spins     = 0;

    /* Let Core1 build a cushion before the first buffer goes out. */
    {
        ULONG timeout = 0, cushion = CHUNK_FRAMES * 2;
        ULONG ceiling = (ZZRESID_RING_SAMPLES / 4) * 3;
        if (cushion > ceiling) cushion = ceiling;
        for (;;) {
            if (sh_get_flush(SH_PCM_WRITE) >= cushion) break;
            if (++timeout > 5000000UL) break;
        }
    }

    player_start(fill_stream);
    g_playing = 1;
    gui_status("playing");
    return 1;
}

/* Signed on purpose: the callers pass g_song_cur +/- 1, and an unsigned
 * subtune 1 minus 1 wraps to 0xFFFFFFFF, which no "< 1" test catches. */
static void gui_song_set(LONG n)
{
    if (!g_have_tune) return;
    if (n < 1) n = 1;
    if (n > (LONG)g_si.songs) n = (LONG)g_si.songs;
    if ((ULONG)n == g_song_cur) return;
    g_song_cur = (ULONG)n;
    if (g_playing) gui_play();      /* clean restart on the new subtune */
    gui_song_gadget();
    gui_refresh();
}

/* ================================================================
 * Playlist
 *
 * Same model as ZZMIDIPlay: an array of full paths, a separate window
 * holding a listview and six buttons, and an exec List rebuilt from the
 * array whenever it changes. A row click only selects; Play is what
 * starts it. The transport Prev/Next step the playlist, which is what
 * they do in the other two players - subtunes have the slider.
 * ================================================================ */
static const char *pls_file_leaf(const char *p)
{
    const char *s = p;
    const char *last = p;
    if (!p) return "";
    while (*s) {
        if (*s == '/' || *s == ':') last = s + 1;
        s++;
    }
    return last;
}

/* Rebuild the exec List behind the listview, marking the entry that is
 * loaded ("*") and the one that is selected (">"). */
static void pls_rebuild_exec_list(void)
{
    int i;

    g_pls_execlist.lh_Head     = (struct Node *)&g_pls_execlist.lh_Tail;
    g_pls_execlist.lh_Tail     = NULL;
    g_pls_execlist.lh_TailPred = (struct Node *)&g_pls_execlist;

    for (i = 0; i < g_pcount; i++) {
        const char *prefix;
        if      (i == g_cur && i == g_pls_sel) prefix = "*> ";
        else if (i == g_cur)                   prefix = "*  ";
        else if (i == g_pls_sel)               prefix = ">  ";
        else                                   prefix = "   ";

        g_pls_display[i][0] = '\0';
        strncpy(g_pls_display[i], prefix, PLS_DISP_MAX - 1);
        strncpy(g_pls_display[i] + 3, pls_file_leaf(g_playlist[i]),
                PLS_DISP_MAX - 4);
        g_pls_display[i][PLS_DISP_MAX - 1] = '\0';

        g_pls_nodes[i].ln_Name = g_pls_display[i];
        g_pls_nodes[i].ln_Type = NT_USER;
        g_pls_nodes[i].ln_Pri  = 0;
        AddTail(&g_pls_execlist, &g_pls_nodes[i]);
    }
}

/* Detach the list before touching it, as GadTools requires, then put it
 * back. Safe whether or not the playlist window is open. */
static void pls_refresh_listview(void)
{
    if (g_plsgad && g_plswin)
        GT_SetGadgetAttrs(g_plsgad, g_plswin, NULL,
                          GTLV_Labels, (ULONG)~0UL, TAG_END);

    pls_rebuild_exec_list();

    if (g_plsgad && g_plswin) {
        GT_SetGadgetAttrs(g_plsgad, g_plswin, NULL,
                          GTLV_Labels, (ULONG)&g_pls_execlist, TAG_END);
        GT_RefreshWindow(g_plswin, NULL);
    }
}

static BOOL pls_append_path(const char *full)
{
    char *slot;
    if (g_pcount >= MAX_PLAYLIST) return FALSE;
    slot = (char *)AllocMem(PATH_BUF_SIZE, MEMF_ANY | MEMF_CLEAR);
    if (!slot) return FALSE;
    strncpy(slot, full, PATH_BUF_SIZE - 1);
    slot[PATH_BUF_SIZE - 1] = '\0';
    g_playlist[g_pcount++] = slot;
    return TRUE;
}

/* Load one playlist entry into the engine. Does not start playback. */
static int pls_load_entry(int idx)
{
    if (idx < 0 || idx >= g_pcount || !g_playlist[idx]) return 0;

    gui_stop();
    if (g_sidfile) { FreeVec(g_sidfile); g_sidfile = NULL; }
    g_have_tune = 0;

    strncpy(g_path, g_playlist[idx], sizeof(g_path) - 1);
    g_path[sizeof(g_path) - 1] = '\0';
    g_tune_path = g_path;

    /* The model cycle has to be in force BEFORE the header is parsed:
     * load_sid_file() only honours the header when nothing is forced. */
    gui_apply_gadgets();

    if (!load_sid_file(g_path, &g_si)) {
        gui_status("could not read that file");
        g_cur = -1;
        return 0;
    }
    g_have_tune = 1;
    g_song_cur  = g_si.startsong;
    g_cur       = idx;

    /* Before this fix, the PSID parser reported $D500 but the launcher
     * always sent $D420 (g_sid1_base) to Core1. Select the chip count and
     * actual base for EACH new tune, including playlist transitions.
     * Manual SID mode remains available until the next file is loaded. */
    g_sid_sel   = g_si.sid2 ? 1 : 0;
    g_sid1_base = g_si.sid2 ? g_si.sid2 : 0xD420UL;
    gui_apply_gadgets();
    if (g_win && g_gad[GAD_SIDMODE])
        GT_SetGadgetAttrs(g_gad[GAD_SIDMODE], g_win, NULL,
                          GTCY_Active, (ULONG)g_sid_sel, TAG_END);
    Printf("SID routing: %lu chip(s), SID2=$%04lx, mirror=%lu\n",
           g_num_sids, g_sid1_base, g_mirror);
    if (g_si.sid3) {
        Printf("WARNING: 3SID at $%04lx; third SID not supported\n", g_si.sid3);
        gui_status("3SID: only first 2 chips supported");
    } else if (g_si.version >= 5) {
        Printf("WARNING: PSID v5 multi-SID metadata is not supported\n");
        gui_status("PSID v5 multi-SID unsupported");
    } else {
        gui_status("loaded");
    }
    return 1;
}

/* Go to a playlist entry. Keeps playing if it was playing. */
static void pls_goto(int idx, int force_play)
{
    int was_playing = g_playing;
    if (idx < 0 || idx >= g_pcount) return;
    if (pls_load_entry(idx) && (was_playing || force_play))
        gui_play();
    pls_refresh_listview();
    gui_song_gadget();
    gui_refresh();
    draw_icons();
}

/* Directory preferences use ordinary AmigaDOS ENV:/ENVARC: files, as in
 * ZZVGMPlay. The preferred folder is explicit, the last opened folder is
 * saved automatically. Both persist across reboot via ENVARC:. */
#define SID_DEF_ENV   "ENV:ZZSIDPlay_DefaultDrawer"
#define SID_DEF_ARC   "ENVARC:ZZSIDPlay_DefaultDrawer"
#define SID_LAST_ENV  "ENV:ZZSIDPlay_LastDrawer"
#define SID_LAST_ARC  "ENVARC:ZZSIDPlay_LastDrawer"

static int sid_read_drawer(const char *path, char *dst)
{
    BPTR fh;
    LONG n;
    dst[0] = '\0';
    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) return 0;
    n = Read(fh, dst, PATH_BUF_SIZE - 1);
    Close(fh);
    if (n <= 0 || n >= PATH_BUF_SIZE - 1) { dst[0] = '\0'; return 0; }
    dst[n] = '\0';
    /* Also accept paths created by echo/SetEnv, with a final newline. */
    while (n > 0 && (dst[n-1] == '\n' || dst[n-1] == '\r'))
        dst[--n] = '\0';
    return (n > 0);
}

static int sid_write_drawer(const char *path, const char *drawer)
{
    BPTR fh;
    LONG size = (LONG)strlen(drawer);
    LONG done;
    if (size <= 0 || size >= PATH_BUF_SIZE) return 0;
    fh = Open((STRPTR)path, MODE_NEWFILE);
    if (!fh) return 0;
    done = Write(fh, (APTR)drawer, size);
    Close(fh);
    return done == size;
}

static int sid_drawer_exists(const char *drawer)
{
    BPTR lock;
    if (!drawer[0]) return 0;
    lock = Lock((STRPTR)drawer, ACCESS_READ);
    if (!lock) return 0;
    UnLock(lock);
    return 1;
}

static void sid_drawers_load(void)
{
    if (!sid_read_drawer(SID_DEF_ENV, g_default_drawer))
        sid_read_drawer(SID_DEF_ARC, g_default_drawer);
    if (!sid_read_drawer(SID_LAST_ENV, g_last_drawer))
        sid_read_drawer(SID_LAST_ARC, g_last_drawer);
}

static const char *sid_initial_drawer(void)
{
    if (sid_drawer_exists(g_default_drawer)) return g_default_drawer;
    if (sid_drawer_exists(g_last_drawer)) return g_last_drawer;
    return NULL;  /* let ASL choose its own initial drawer */
}

static void sid_remember_last_drawer(const char *drawer)
{
    if (!drawer || !drawer[0] || strlen(drawer) >= PATH_BUF_SIZE) return;
    strncpy(g_last_drawer, drawer, PATH_BUF_SIZE - 1);
    g_last_drawer[PATH_BUF_SIZE - 1] = '\0';
    /* Last Drawer is best-effort; an unwritable ENVARC: must not stop play. */
    sid_write_drawer(SID_LAST_ARC, g_last_drawer);
    sid_write_drawer(SID_LAST_ENV, g_last_drawer);
}

static void sid_set_default_drawer(void)
{
    struct FileRequester *fr;
    struct TagItem tags[4];
    const char *initial = sid_initial_drawer();
    tags[0].ti_Tag = ASLFR_TitleText;
    tags[0].ti_Data = (ULONG)"Set default SID drawer";
    tags[1].ti_Tag = ASLFR_DrawersOnly;
    tags[1].ti_Data = TRUE;
    tags[2].ti_Tag = initial ? ASLFR_InitialDrawer : TAG_IGNORE;
    tags[2].ti_Data = (ULONG)initial;
    tags[3].ti_Tag = TAG_END;
    tags[3].ti_Data = 0;
    fr = AllocAslRequest(ASL_FileRequest, tags);
    if (!fr) { gui_status("could not open drawer requester"); gui_refresh(); return; }
    if (AslRequest(fr, NULL) && fr->fr_Drawer && fr->fr_Drawer[0]) {
        const char *picked = fr->fr_Drawer;
        if (strlen(picked) >= PATH_BUF_SIZE) {
            gui_status("drawer path too long");
        } else if (!sid_write_drawer(SID_DEF_ARC, picked)) {
            gui_status("cannot save ENVARC: default drawer");
        } else {
            strncpy(g_default_drawer, picked, PATH_BUF_SIZE-1);
            g_default_drawer[PATH_BUF_SIZE-1] = '\0';
            sid_write_drawer(SID_DEF_ENV, g_default_drawer);
            gui_status("default SID drawer saved");
        }
    }
    FreeAslRequest(fr);
    gui_refresh();
}

static void sid_clear_default_drawer(void)
{
    g_default_drawer[0] = '\0';
    /* Removing both files is intentional; next Open/Add uses Last Drawer. */
    DeleteFile((STRPTR)SID_DEF_ARC);
    DeleteFile((STRPTR)SID_DEF_ENV);
    gui_status("default drawer cleared");
    gui_refresh();
}

/* ASL multi-select, appending every chosen file. This is what the Load
 * button does - the same as ZZMIDIPlay's OUV. If nothing was loaded yet,
 * automatically play the first newly added SID. Additional files do not
 * interrupt an already loaded or playing track. */
static void pls_add_via_asl(void)
{
    struct FileRequester *fr;
    int old_count = g_pcount;
    struct TagItem fr_tags[4];
    const char *initial = sid_initial_drawer();

    fr_tags[0].ti_Tag  = ASLFR_TitleText;     fr_tags[0].ti_Data = (ULONG)"Add SID files";
    fr_tags[1].ti_Tag  = ASLFR_DoMultiSelect; fr_tags[1].ti_Data = TRUE;
    fr_tags[2].ti_Tag  = initial ? ASLFR_InitialDrawer : TAG_IGNORE;
    fr_tags[2].ti_Data = (ULONG)initial;
    fr_tags[3].ti_Tag  = TAG_END;             fr_tags[3].ti_Data = 0;

    fr = AllocAslRequest(ASL_FileRequest, fr_tags);
    if (fr && AslRequest(fr, NULL)) {
        char full[PATH_BUF_SIZE];
        int  fi;
        sid_remember_last_drawer(fr->fr_Drawer);
        if (fr->fr_NumArgs > 0 && fr->fr_ArgList != NULL) {
            for (fi = 0; fi < (int)fr->fr_NumArgs && g_pcount < MAX_PLAYLIST;
                 fi++) {
                int dl = (int)strlen(fr->fr_Drawer);
                if (dl > PATH_BUF_SIZE - 2) dl = PATH_BUF_SIZE - 2;
                memcpy(full, fr->fr_Drawer, (size_t)dl);
                if (dl > 0 && full[dl-1] != '/' && full[dl-1] != ':')
                    full[dl++] = '/';
                strncpy(full + dl, fr->fr_ArgList[fi].wa_Name,
                        (size_t)(PATH_BUF_SIZE - 1 - dl));
                full[PATH_BUF_SIZE - 1] = '\0';
                pls_append_path(full);
            }
        } else if (fr->fr_File && fr->fr_File[0] && g_pcount < MAX_PLAYLIST) {
            int dl = (int)strlen(fr->fr_Drawer);
            if (dl > PATH_BUF_SIZE - 2) dl = PATH_BUF_SIZE - 2;
            memcpy(full, fr->fr_Drawer, (size_t)dl);
            if (dl > 0 && full[dl-1] != '/' && full[dl-1] != ':')
                full[dl++] = '/';
            strncpy(full + dl, fr->fr_File, (size_t)(PATH_BUF_SIZE - 1 - dl));
            full[PATH_BUF_SIZE - 1] = '\0';
            pls_append_path(full);
        }
    }
    if (fr) FreeAslRequest(fr);

    if (g_pls_sel < 0 && g_pcount > old_count) g_pls_sel = old_count;
    if (g_cur < 0 && g_pcount > old_count) {
        if (pls_load_entry(old_count)) gui_play();
    }

    pls_refresh_listview();
    gui_song_gadget();
    gui_refresh();
    draw_icons();
}

static void pls_delete_selected(void)
{
    int i;
    if (g_pls_sel < 0 || g_pls_sel >= g_pcount) return;

    if (g_playlist[g_pls_sel]) {
        FreeMem(g_playlist[g_pls_sel], PATH_BUF_SIZE);
        g_playlist[g_pls_sel] = NULL;
    }
    for (i = g_pls_sel; i < g_pcount - 1; i++)
        g_playlist[i] = g_playlist[i + 1];
    g_playlist[g_pcount - 1] = NULL;
    g_pcount--;

    if (g_cur == g_pls_sel)     g_cur = -1;
    else if (g_cur > g_pls_sel) g_cur--;

    if (g_pls_sel >= g_pcount) g_pls_sel = g_pcount - 1;
    pls_refresh_listview();
    gui_refresh();
}

/* Swap the selected entry with its neighbour and follow the selection, so
 * an entry can be moved repeatedly without re-selecting it. */
static void pls_move_selected(int dir)
{
    int j;
    char *tmp;
    if (g_pls_sel < 0 || g_pls_sel >= g_pcount) return;
    j = g_pls_sel + dir;
    if (j < 0 || j >= g_pcount) return;

    tmp                   = g_playlist[g_pls_sel];
    g_playlist[g_pls_sel] = g_playlist[j];
    g_playlist[j]         = tmp;

    if (g_cur == g_pls_sel)  g_cur = j;
    else if (g_cur == j)     g_cur = g_pls_sel;

    g_pls_sel = j;
    pls_refresh_listview();
}

static void pls_clear_all(void)
{
    int i;
    gui_stop();
    for (i = 0; i < g_pcount; i++) {
        if (g_playlist[i]) {
            FreeMem(g_playlist[i], PATH_BUF_SIZE);
            g_playlist[i] = NULL;
        }
    }
    g_pcount  = 0;
    g_pls_sel = -1;
    g_cur     = -1;
    pls_refresh_listview();
    gui_refresh();
}

/* ---- playlist window ----------------------------------------------- */
static void pls_window_close(void)
{
    if (g_plswin)   { CloseWindow(g_plswin);      g_plswin   = NULL; }
    if (g_plsglist) { FreeGadgets(g_plsglist);    g_plsglist = NULL; }
    g_plsgad = NULL;
}

static void pls_window_open(void)
{
    struct NewGadget ng;
    struct Gadget   *prev;
    struct TagItem   lv_tags[3];
    struct TagItem   win_tags[10];
    UWORD bx, by, lw, lh, bw, bh, btop, gap;
    int   k;

    static const char *labels[6] =
        { "Play", "Add", "Del", "Up", "Dn", "Clear" };
    static const UWORD ids[6] =
        { PLG_PLAY, PLG_ADD, PLG_DEL, PLG_UP, PLG_DOWN, PLG_CLEAR };

    if (g_plswin) return;
    if (!g_scr || !g_vi) return;

    pls_rebuild_exec_list();

    bx  = g_scr->WBorLeft;
    by  = g_scr->WBorTop + g_scr->Font->ta_YSize + 1;
    lw  = (UWORD)(320 - bx - g_scr->WBorRight);
    lh  = 120;
    gap = 4;
    bw  = (UWORD)((lw - 5 * gap) / 6);
    bh  = (UWORD)(g_scr->Font->ta_YSize + 6);
    btop = (UWORD)(by + lh + 4);

    prev = CreateContext(&g_plsglist);
    if (!prev) return;

    ng.ng_LeftEdge   = bx;
    ng.ng_TopEdge    = by;
    ng.ng_Width      = lw;
    ng.ng_Height     = lh;
    ng.ng_GadgetText = NULL;
    ng.ng_TextAttr   = g_scr->Font;
    ng.ng_GadgetID   = PLG_LIST;
    ng.ng_Flags      = 0;
    ng.ng_VisualInfo = g_vi;

    lv_tags[0].ti_Tag = GTLV_Labels;      lv_tags[0].ti_Data = (ULONG)&g_pls_execlist;
    lv_tags[1].ti_Tag = GTLV_ScrollWidth; lv_tags[1].ti_Data = 16;
    lv_tags[2].ti_Tag = TAG_END;          lv_tags[2].ti_Data = 0;

    prev = g_plsgad = CreateGadgetA(LISTVIEW_KIND, prev, &ng, lv_tags);
    if (!prev) { FreeGadgets(g_plsglist); g_plsglist = NULL; return; }

    for (k = 0; k < 6; k++) {
        ng.ng_LeftEdge   = (WORD)(bx + k * (bw + gap));
        ng.ng_TopEdge    = (WORD)btop;
        ng.ng_Width      = bw;
        ng.ng_Height     = bh;
        ng.ng_GadgetText = (UBYTE *)labels[k];
        ng.ng_GadgetID   = ids[k];
        ng.ng_Flags      = 0;
        prev = CreateGadgetA(BUTTON_KIND, prev, &ng, NULL);
        if (!prev) { FreeGadgets(g_plsglist); g_plsglist = NULL;
                     g_plsgad = NULL; return; }
    }

    win_tags[0].ti_Tag = WA_Left;      win_tags[0].ti_Data = 40;
    win_tags[1].ti_Tag = WA_Top;       win_tags[1].ti_Data = 180;
    win_tags[2].ti_Tag = WA_Width;     win_tags[2].ti_Data = 320;
    win_tags[3].ti_Tag = WA_Height;    win_tags[3].ti_Data =
                           (ULONG)(btop + bh + g_scr->WBorBottom + 2);
    win_tags[4].ti_Tag = WA_Title;     win_tags[4].ti_Data = (ULONG)"SID Playlist";
    win_tags[5].ti_Tag = WA_Gadgets;   win_tags[5].ti_Data = (ULONG)g_plsglist;
    win_tags[6].ti_Tag = WA_Flags;     win_tags[6].ti_Data =
                           WFLG_DRAGBAR | WFLG_CLOSEGADGET |
                           WFLG_DEPTHGADGET | WFLG_ACTIVATE |
                           WFLG_SMART_REFRESH;
    win_tags[7].ti_Tag = WA_IDCMP;     win_tags[7].ti_Data =
                           IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW |
                           LISTVIEWIDCMP;
    win_tags[8].ti_Tag = WA_PubScreen; win_tags[8].ti_Data = (ULONG)g_scr;
    win_tags[9].ti_Tag = TAG_END;      win_tags[9].ti_Data = 0;

    g_plswin = OpenWindowTagList(NULL, win_tags);
    if (!g_plswin) { FreeGadgets(g_plsglist); g_plsglist = NULL;
                     g_plsgad = NULL; return; }
    GT_RefreshWindow(g_plswin, NULL);
}

static void pls_idcmp(void)
{
    struct IntuiMessage *msg;
    if (!g_plswin) return;

    while ((msg = GT_GetIMsg(g_plswin->UserPort)) != NULL) {
        ULONG  cls  = msg->Class;
        UWORD  code = msg->Code;
        struct Gadget *gad = (struct Gadget *)msg->IAddress;
        UWORD  gid  = gad ? gad->GadgetID : 0xFFFF;
        GT_ReplyIMsg(msg);

        if (cls == IDCMP_REFRESHWINDOW) {
            GT_BeginRefresh(g_plswin);
            GT_EndRefresh(g_plswin, TRUE);
            continue;
        }
        if (cls == IDCMP_CLOSEWINDOW) {
            pls_window_close();
            draw_icons();
            return;
        }
        if (cls != IDCMP_GADGETUP) continue;

        /* A row click selects; it does not start playback. */
        if (gad == g_plsgad || gid == PLG_LIST) {
            if ((int)code >= 0 && (int)code < g_pcount) {
                g_pls_sel = (int)code;
                pls_refresh_listview();
            }
            continue;
        }

        switch (gid) {
            case PLG_PLAY:  pls_goto(g_pls_sel, 1);  break;
            case PLG_ADD:   pls_add_via_asl();       break;
            case PLG_DEL:   pls_delete_selected();   break;
            case PLG_UP:    pls_move_selected(-1);   break;
            case PLG_DOWN:  pls_move_selected(+1);   break;
            case PLG_CLEAR: pls_clear_all();         break;
            default: break;
        }
    }
}

/* ---- main window --------------------------------------------------- */
/* Widest label in a cycle's list, in pixels, measured in the screen's own
 * font. Counting characters and multiplying by a nominal width is close
 * enough for topaz 8 and wrong for everything else, proportional screen
 * fonts most of all. This cannot disagree with the strings. */
static UWORD label_width(const char **labs, struct Screen *scr)
{
    UWORD w = 0;
    int i;
    for (i = 0; labs[i]; i++) {
        UWORD t = (UWORD)TextLength(&scr->RastPort, (STRPTR)labs[i],
                                    (LONG)strlen(labs[i]));
        if (t > w) w = t;
    }
    return w;
}

static BOOL gui_open(void)
{
    struct NewGadget ng;
    struct Gadget *prev;
    struct TagItem win_tags[12];
    UWORD bx, by, iw, fh, sl_y, cyc_y, btn_y;
    UWORD cyc_w1, cyc_w2, cyc_w3, cyc_total;
    UWORD btn_w;
    int i, win_h, win_w;

    memset(g_gad, 0, sizeof(g_gad));
    init_btn_images();
    gui_invalidate();

    g_scr = LockPubScreen(NULL);
    if (!g_scr) return FALSE;
    g_vi = GetVisualInfoA(g_scr, NULL);
    if (!g_vi) { UnlockPubScreen(NULL, g_scr); g_scr = NULL; return FALSE; }

    fh = g_scr->Font->ta_YSize;
    bx = g_scr->WBorLeft;
    by = g_scr->WBorTop + fh + 1;

    /* The cycles are measured first and the window is sized to hold them.
     * Doing it the other way round - fixing the window, then dividing what
     * is left - is what forced the labels down to four characters. */
    cyc_w1    = (UWORD)(label_width(g_model_lab,  g_scr) + CYC_PAD);
    cyc_w2    = (UWORD)(label_width(g_filter_lab, g_scr) + CYC_PAD);
    cyc_w3    = (UWORD)(label_width(g_sid_lab,    g_scr) + CYC_PAD);
    cyc_total = (UWORD)(cyc_w1 + cyc_w2 + cyc_w3 + 2 * CTRL_GAP);

    iw = (UWORD)(cyc_total + 4);
    if (iw < (UWORD)(WIN_MIN_WIDTH - bx - g_scr->WBorRight))
        iw = (UWORD)(WIN_MIN_WIDTH - bx - g_scr->WBorRight);
    win_w = (int)bx + (int)iw + (int)g_scr->WBorRight;

    /* Buttons share the full width, like the strip in the other players,
     * but never grow past BTN_MAX_W: a 16-pixel icon adrift in a 90-pixel
     * button stops looking like a transport control. */
    btn_w = (UWORD)((iw - 4 - (GAD_BTN_N - 1) * CTRL_GAP) / GAD_BTN_N);
    if (btn_w > BTN_MAX_W) btn_w = BTN_MAX_W;

    g_th = fh + 2;
    g_tx = bx + 2;
    for (i = 0; i < TEXT_LINES; i++) g_ty[i] = by + 2 + (UWORD)(i * g_th);

    sl_y  = g_ty[TEXT_LINES - 1] + g_th + 2;
    cyc_y = sl_y  + CTRL_H + 3;
    btn_y = cyc_y + CTRL_H + 3;
    win_h = (int)btn_y + CTRL_H + (int)g_scr->WBorBottom + 4;

    prev = CreateContext(&g_glist);
    if (!prev) goto fail;

    ng.ng_TextAttr   = g_scr->Font;
    ng.ng_VisualInfo = g_vi;
    ng.ng_Flags      = 0;

    /* Subtune slider, full width. It is the subtune readout as well as the
     * way to jump: GTSL_MaxLevelLen draws the number beside the knob. */
    {
        struct TagItem sl[6];
        ng.ng_LeftEdge   = bx + 2;
        ng.ng_TopEdge    = sl_y;
        ng.ng_Width      = (UWORD)(iw - 4);
        ng.ng_Height     = CTRL_H;
        ng.ng_GadgetText = NULL;
        ng.ng_GadgetID   = GAD_SONG;
        sl[0].ti_Tag = GTSL_Min;         sl[0].ti_Data = 1;
        sl[1].ti_Tag = GTSL_Max;         sl[1].ti_Data = 1;
        sl[2].ti_Tag = GTSL_Level;       sl[2].ti_Data = 1;
        sl[3].ti_Tag = GTSL_MaxLevelLen; sl[3].ti_Data = 3;
        sl[4].ti_Tag = GA_RelVerify;     sl[4].ti_Data = TRUE;
        sl[5].ti_Tag = TAG_END;          sl[5].ti_Data = 0;
        prev = g_gad[GAD_SONG] = CreateGadgetA(SLIDER_KIND, prev, &ng, sl);
        if (!prev) goto fail;
    }

    /* Settings row: the three cycles, each at its own measured width. */
    {
        struct TagItem cyc[3];
        UWORD x = bx + 2;

        ng.ng_TopEdge    = cyc_y;
        ng.ng_Height     = CTRL_H;
        ng.ng_GadgetText = NULL;

        cyc[0].ti_Tag = GTCY_Labels; cyc[0].ti_Data = (ULONG)g_model_lab;
        cyc[1].ti_Tag = GTCY_Active; cyc[1].ti_Data = g_model_sel;
        cyc[2].ti_Tag = TAG_END;     cyc[2].ti_Data = 0;
        ng.ng_LeftEdge = x;  ng.ng_Width = cyc_w1;  ng.ng_GadgetID = GAD_MODEL;
        prev = g_gad[GAD_MODEL] = CreateGadgetA(CYCLE_KIND, prev, &ng, cyc);
        if (!prev) goto fail;

        x = (UWORD)(x + cyc_w1 + CTRL_GAP);
        cyc[0].ti_Data = (ULONG)g_filter_lab;
        cyc[1].ti_Data = g_filter_sel;
        ng.ng_LeftEdge = x;  ng.ng_Width = cyc_w2;  ng.ng_GadgetID = GAD_FILTER;
        prev = g_gad[GAD_FILTER] = CreateGadgetA(CYCLE_KIND, prev, &ng, cyc);
        if (!prev) goto fail;

        x = (UWORD)(x + cyc_w2 + CTRL_GAP);
        cyc[0].ti_Data = (ULONG)g_sid_lab;
        cyc[1].ti_Data = g_sid_sel;
        ng.ng_LeftEdge = x;  ng.ng_Width = cyc_w3;  ng.ng_GadgetID = GAD_SIDMODE;
        prev = g_gad[GAD_SIDMODE] = CreateGadgetA(CYCLE_KIND, prev, &ng, cyc);
        if (!prev) goto fail;
    }

    /* Transport row: six icon buttons.
     *
     * GACT_IMMEDIATE is set by hand after creation so each button reports
     * the press as well as the release. The icons are painted over the
     * gadgets with DrawImage, and GadTools repaints a button's interior
     * when it is depressed - which erases the icon for as long as the
     * mouse is held. With GADGETDOWN in hand the icon can be put straight
     * back. GadTools offers no tag for this, but the flag is plain
     * Intuition and the window is not open yet, so setting it is safe. */
    for (i = 1; i <= GAD_BTN_N; i++) {
        ng.ng_LeftEdge   = (UWORD)(bx + 2 + (i - 1) * (btn_w + CTRL_GAP));
        ng.ng_TopEdge    = btn_y;
        ng.ng_Width      = btn_w;
        ng.ng_Height     = CTRL_H;
        ng.ng_GadgetText = NULL;
        ng.ng_GadgetID   = (UWORD)i;
        prev = g_gad[i] = CreateGadgetA(BUTTON_KIND, prev, &ng, NULL);
        if (!prev) goto fail;
        g_gad[i]->Activation |= GACT_IMMEDIATE;
    }

    /* No Quit button: the close gadget is how an Amiga window closes, and
     * the menu carries Quit for the keyboard. */
    win_tags[0].ti_Tag = WA_Left;      win_tags[0].ti_Data = 40;
    win_tags[1].ti_Tag = WA_Top;       win_tags[1].ti_Data = 40;
    win_tags[2].ti_Tag = WA_Width;     win_tags[2].ti_Data = (ULONG)win_w;
    win_tags[3].ti_Tag = WA_Height;    win_tags[3].ti_Data = (ULONG)win_h;
    win_tags[4].ti_Tag = WA_Title;     win_tags[4].ti_Data =
                             (ULONG)"ZZSIDPlay v1.0 (c) Xanxi 2026";
    win_tags[5].ti_Tag = WA_Gadgets;   win_tags[5].ti_Data = (ULONG)g_glist;
    win_tags[6].ti_Tag = WA_Flags;     win_tags[6].ti_Data =
                             WFLG_DRAGBAR | WFLG_CLOSEGADGET |
                             WFLG_DEPTHGADGET | WFLG_ACTIVATE |
                             WFLG_SMART_REFRESH;
    win_tags[7].ti_Tag = WA_IDCMP;     win_tags[7].ti_Data =
                             IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW |
                             IDCMP_GADGETUP    | IDCMP_GADGETDOWN |
                             IDCMP_MENUPICK;
    win_tags[8].ti_Tag = WA_PubScreen; win_tags[8].ti_Data = (ULONG)g_scr;
    win_tags[9].ti_Tag = TAG_END;      win_tags[9].ti_Data = 0;

    g_win = OpenWindowTagList(NULL, win_tags);
    if (!g_win) goto fail;

    /* Plain menus, not GTMN_NewLookMenus. NewLook picks its pens from the
     * screen's DrawInfo, and on a screen whose pens are not what it
     * expects the result is dark text on a dark bar - unreadable. The
     * classic rendering always uses the screen's own menu pens. */
    g_menu = CreateMenusA(g_newmenu, NULL);
    if (g_menu) {
        if (LayoutMenusA(g_menu, g_vi, NULL))
            SetMenuStrip(g_win, g_menu);
        else { FreeMenus(g_menu); g_menu = NULL; }
    }

    GT_RefreshWindow(g_win, NULL);
    g_iw = (WORD)(g_win->Width - g_win->BorderLeft - g_win->BorderRight);
    draw_icons();

    UnlockPubScreen(NULL, g_scr);
    return TRUE;

fail:
    if (g_vi)    { FreeVisualInfo(g_vi); g_vi = NULL; }
    if (g_glist) { FreeGadgets(g_glist); g_glist = NULL; }
    UnlockPubScreen(NULL, g_scr);
    g_scr = NULL;
    return FALSE;
}

static void gui_close(void)
{
    pls_window_close();
    if (g_win && g_menu) ClearMenuStrip(g_win);
    if (g_menu)  { FreeMenus(g_menu);    g_menu  = NULL; }
    if (g_win)   { CloseWindow(g_win);   g_win   = NULL; }
    if (g_glist) { FreeGadgets(g_glist); g_glist = NULL; }
    if (g_vi)    { FreeVisualInfo(g_vi); g_vi    = NULL; }
    g_scr = NULL;
}

static void gui_idcmp(void)
{
    struct IntuiMessage *msg;
    while ((msg = GT_GetIMsg(g_win->UserPort)) != NULL) {
        ULONG  cls  = msg->Class;
        UWORD  code = msg->Code;
        struct Gadget *gad = (struct Gadget *)msg->IAddress;
        GT_ReplyIMsg(msg);

        if (cls == IDCMP_REFRESHWINDOW) {
            GT_BeginRefresh(g_win);
            GT_EndRefresh(g_win, TRUE);
            gui_invalidate();
            gui_refresh();
            draw_icons();
            continue;
        }
        if (cls == IDCMP_CLOSEWINDOW) { g_quit = 1; return; }

        if (cls == IDCMP_MENUPICK) {
            UWORD sel = code;
            while (sel != MENUNULL) {
                struct MenuItem *it = ItemAddress(g_menu, sel);
                if (!it) break;
                switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case MENU_DEBUG:
                        if (it->Flags & CHECKED) dbg_open();
                        else                     dbg_close();
                        break;
                    case MENU_QUIT:
                        g_quit = 1;
                        break;
                    case MENU_SET_DEFAULT:
                        sid_set_default_drawer();
                        break;
                    case MENU_CLEAR_DEFAULT:
                        sid_clear_default_drawer();
                        break;
                    default: break;
                }
                sel = it->NextSelect;
            }
            if (g_quit) return;
            continue;
        }

        /* Pressed: GadTools has just repainted the button recessed and
         * taken the icon with it. Put it back inside the depressed box. */
        if (cls == IDCMP_GADGETDOWN) {
            if (gad->GadgetID >= GAD_LOAD && gad->GadgetID <= GAD_BTN_N)
                draw_icons();
            continue;
        }

        if (cls != IDCMP_GADGETUP) continue;

        switch (gad->GadgetID) {
            case GAD_LOAD:  pls_add_via_asl();          break;
            case GAD_PLAY:  gui_play();  gui_refresh(); break;
            case GAD_STOP:  gui_stop();  gui_refresh(); break;

            /* Prev/Next walk the playlist, as they do in the other two
             * players. Subtunes have the slider. */
            case GAD_PREV:
                if (g_cur > 0) pls_goto(g_cur - 1, 0);
                break;
            case GAD_NEXT:
                if (g_pcount > 0 && g_cur < g_pcount - 1)
                    pls_goto(g_cur + 1, 0);
                break;

            case GAD_PLS:
                if (g_plswin) pls_window_close();
                else          pls_window_open();
                break;

            case GAD_SONG:  gui_song_set((LONG)code);   break;

            case GAD_MODEL:
                g_model_sel = code;
                if (g_playing) gui_play();
                gui_refresh();
                break;
            case GAD_FILTER:
                g_filter_sel = code;
                if (g_playing) gui_play();
                gui_refresh();
                break;
            case GAD_SIDMODE:
                g_sid_sel = code;
                if (g_playing) gui_play();
                gui_refresh();
                break;
            default: break;
        }

        /* GadTools repaints a button's interior on release too, so the
         * icon has to go back a second time. Leaving this out is invisible
         * on PLAY and STOP, which change g_playing and get redrawn for
         * that reason anyway, and obvious on the others: they simply go
         * blank on first use. */
        if (gad->GadgetID >= GAD_LOAD && gad->GadgetID <= GAD_BTN_N)
            draw_icons();
    }
}

int main(int argc, char **argv)
{
    struct Library   *ExpLib = NULL;
    struct ConfigDev *cd;
    ULONG  ticks = 0;
    int    rc = 0;

    (void)argc; (void)argv;

    /* NewList() is an amiga.lib helper declared in clib/alib_protos.h, which
     * this file does not include - hence the implicit-declaration warning,
     * and an undefined reference at link time if the library does not carry
     * it. It is also not needed: pls_rebuild_exec_list() sets up the list
     * head by hand, and with g_pcount at 0 that is exactly an empty list. */
    pls_rebuild_exec_list();

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
    GfxBase       = (struct GfxBase *)      OpenLibrary("graphics.library",  37);
    GadToolsBase  =                         OpenLibrary("gadtools.library",  37);
    AslBase       =                         OpenLibrary("asl.library",       37);

    if (!IntuitionBase || !GfxBase || !GadToolsBase || !AslBase) {
        gui_fatal("Needs OS 2.0 libraries: intuition, graphics, gadtools, asl");
        rc = 20; goto bye_libs;
    }

    g_chunk = 4096UL;
    if (!bufs_alloc()) {
        gui_fatal("Cannot allocate the AHI buffers");
        rc = 20; goto bye_libs;
    }

    ExpLib = OpenLibrary("expansion.library", 0L);
    if (!ExpLib) { gui_fatal("No expansion.library"); rc = 20; goto bye_bufs; }
    cd = FindConfigDev(NULL, ZZ9000_MANUF, ZZ9000_PROD_AX);
    if (!cd) cd = FindConfigDev(NULL, ZZ9000_MANUF, ZZ9000_PROD);
    if (!cd) { gui_fatal("No ZZ9000 card found"); rc = 20; goto bye_exp; }
    g_board  = (UBYTE *)cd->cd_BoardAddr;
    g_fb     = g_board + MNT_FB_BASE;
    g_shared = (volatile ULONG *)(g_fb + ZZRESID_SHARED_FB_OFFSET);
    g_pcm    = (volatile ULONG *)(g_fb + ZZRESID_PCM_FB_OFFSET);
    g_sidbuf = (volatile UBYTE *)(g_fb + ZZRESID_SID_FB_OFFSET);

    if (!ahi_open()) { gui_fatal("Cannot open ahi.device"); rc = 10; goto bye_exp; }

    sid_drawers_load();
    if (!gui_open()) { gui_fatal("Cannot open the window"); rc = 20; goto bye_ahi; }

    gui_status("ready - load a .sid file");
    gui_refresh();

    while (!g_quit) {
        if (g_playing) {
            player_tick(fill_stream);          /* WaitIO paces this loop */
            /* Status is worth about ten refreshes a second, not fifty. */
            if ((++ticks & 3) == 0) gui_refresh();
        }

        gui_idcmp();
        if (g_quit) break;
        pls_idcmp();

        if (!g_playing && !g_quit) {
            /* Recomputed every time: the playlist window comes and goes,
             * and waiting on a signal bit from a window that has been
             * closed is how a player ends up deaf to its own gadgets. */
            ULONG sigs = 1UL << g_win->UserPort->mp_SigBit;
            if (g_plswin) sigs |= 1UL << g_plswin->UserPort->mp_SigBit;
            Wait(sigs);
        }
    }

    gui_stop();
    pls_clear_all();
    gui_close();

bye_ahi:
    ahi_close();
bye_exp:
    if (g_sidfile) { FreeVec(g_sidfile); g_sidfile = NULL; }
    if (g_core_up) {
        sh_put_flush(SH_CMD, ZZRESID_CMD_STOP);
        wait_status(ZZRESID_ST_STOPPED, 1000000UL);
    }
    if (ExpLib) CloseLibrary(ExpLib);
bye_bufs:
    bufs_free();
bye_libs:
    dbg_close();
    if (AslBase)       { CloseLibrary(AslBase);                         AslBase       = NULL; }
    if (GadToolsBase)  { CloseLibrary(GadToolsBase);                    GadToolsBase  = NULL; }
    if (GfxBase)       { CloseLibrary((struct Library *)GfxBase);       GfxBase       = NULL; }
    if (IntuitionBase) { CloseLibrary((struct Library *)IntuitionBase); IntuitionBase = NULL; }
    return rc;
}

#endif /* ZZSID_GUI */
