/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzqgl_present.c - ZZQuakeGL: the GPU's RGB565 frame into the P96 buffer
 * the launcher granted, with the release triple-buffer handshake.
 *
 * Ported from zzq2_glue.c of ZZQuake2RIX 0.10.3 (present_common,
 * t565_copy_burst, nc_burst_copy, the counters the launcher reads),
 * logic unchanged.  The release QG_DrawFrame (zzquake_platform.c) only
 * converts 8-bit palette indices, so its triple-buffer path is
 * reproduced here, slot for slot, with the same protocol (launcher
 * ZZQuake640 unchanged):
 *     SH_RENDER_SEQ new          -> a buffer is granted, else the frame is
 *                                   dropped (the engine never waits)
 *     SH_RENDER_FB               -> the ARM address of that buffer, checked
 *                                   like the release does
 *     SH_FB_USED                 <- the buffer written
 *     copy (RGB565 -> 32-bit 0x00RRGGBB or 16-bit big-endian), cache clean
 *     SH_FRAME++, SH_RENDER_DONE <- seq, SH_FRAME_READY <- seq
 * The release keeps its triple-buffer counters in static variables that
 * only its QG_DrawFrame updates; they are kept here and written into the
 * same slots after zzq_publish_stats() (interposed at link time,
 * --wrap=zzq_publish_stats), so that the launcher statistics and its
 * "CORE1 FIGE" watchdog (SH_RENDERS) see the frames.  Only the
 * triple-buffer mode of the launcher (default, 3 buffers obtained) is
 * supported.
 *
 * The 0.10 guard of Q2RIX is kept: a P96 buffer lying on the GPU's colour
 * buffers (window 0x06600000..) would be drawn by both sides.
 *
 * No Quake header here: this file sees the release side only.
 * ASCII only.
 */

#include "zzquake_config.h"
#include "zzqgl_diag.h"

typedef unsigned int  u32;
typedef unsigned char u8;

/* ---- release platform (ZZQuake640), unchanged ------------------- */
extern volatile u32 *shared;
extern u32  zzq_us_now(void);
extern void dcache_clean_range(u32 start, u32 len);
extern void zzq_ph_frame(u32 work_us);
extern void zzq_s2_frame(u32 work_us);
extern int  zzq_printf(const char *fmt, ...);
extern void Sys_Error(char *error, ...);
extern void __real_zzq_publish_stats(void);
/* ---- zzq2rix_gpu.c (vendored) ----------------------------------- */
extern void zz_rix_buffers(unsigned *lo, unsigned *hi);

#ifndef ZZQGL_VID_W                 /* 0.4: build_arm.sh ZZQGL_RES (320, 640, 800) */
#define ZZQGL_VID_W 640
#define ZZQGL_VID_H 480
#endif

/* 1 = burst path (default), 0 = word-by-word path kept for comparison (rix_present 0) */
int zzq2_present_burst = 1;
/* called every 16 lines of the conversion when set (zzq2rix_gpu.c, rix_overlap 1: the GPU
   still draws the next frame, its queue is pumped meanwhile) */
void (*zzq2_present_hook)(void);

static u32 t32_last_seq, t32_pass, t32_block, t32_rejects;
static u32 t32_renders, t32_heavy, t32_work_t0, t32_work_last, t32_work_max;
static u32 t32_fps_peak, t32_last_us;
static u32 t32_hist[ZZQ_HIST_BINS];
static int g_first_endframe, g_first_present;
static const unsigned short *g_last_pixels;   /* the last frame handed over, for the keepalive */
static unsigned g_last_pitch;
static u32 g_last_attempt_us;
/* 0.2: 1 while the early black frame of VID_Init is handed over, 2 during a keepalive: the
   Q1_FIRST_FRAME / PRESENTED gates and the timeline stamps are left to the first real frame */
int zzqgl_present_special;
/* 0.2: set by GL_EndRendering around the loading plaque and the modal dialogs (SCR_ModalMessage,
   "quit? y/n"): the frame that stays on screen with no frame after it.  Waited for (100 ms at
   most) instead of dropped when the launcher has not freed a buffer yet, as the release
   QG_DrawFrame does for scr_drawdialog. */
int zzqgl_present_must_show;
static u32 t32_show_waits, t32_show_timeouts;
void zzqgl_present_show_stats(unsigned *waits, unsigned *timeouts) { *waits = t32_show_waits; *timeouts = t32_show_timeouts; }
extern volatile u32 zzq2_world_count;         /* zzqgl_qgmain.c: R_DrawWorld calls */

u32 zzqgl_present_count(void) { return t32_renders; }
u32 zzqgl_present_dropped(void) { return t32_block; }

static void t32_overwrite(void)
{
    int h;
    shared[SH_WORK_US]       = t32_work_last;
    shared[SH_WORK_MAX]      = t32_work_max;
    shared[SH_FPS_PEAK_X100] = t32_fps_peak;
    shared[SH_GATE_PASS]     = t32_pass;
    shared[SH_GATE_BLOCK]    = t32_block;
    shared[SH_FB_REJECTS]    = t32_rejects;
    shared[SH_HEAVY_FRAMES]  = t32_heavy;
    shared[SH_RENDERS]       = t32_renders;
    for (h = 0; h < ZZQ_HIST_BINS; h++) shared[SH_HIST_BASE + h] = t32_hist[h];
}

/* every call of the release zzq_publish_stats(), the last one of zzq_end_wfe() included,
   is followed by the counters of this file */
void __wrap_zzq_publish_stats(void)
{
    __real_zzq_publish_stats();
    t32_overwrite();
}

/* RGB565 (little-endian halves, the GPU's buffer) -> the P96 buffer: 32-bit 0x00RRGGBB
   (5/6/5 bits replicated into 8) or 16-bit big-endian RGB565 (a byte swap). */
static u32 t565_row[ZZQGL_VID_W / 2] __attribute__((aligned(32)));   /* one source line, cached */
static u32 t565_lut_hi[256], t565_lut_lo[256];                        /* RGB565 byte -> 0x00RRGGBB part */
static int t565_lut_ok;

/* n32 blocks of 32 bytes from the GPU's non-cacheable buffer into cached memory: LDM of eight
   registers is one burst on the AXI port.  Integer registers only: no NEON in the Quake blob
   (the release's Core1 stack is not 8-byte aligned). */
static void nc_burst_copy(u32 *dst, const u32 *src, u32 n32)
{
#if defined(__arm__)
    __asm__ volatile(
        "1:\n\t"
        "ldmia %1!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "stmia %0!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "subs  %2, %2, #1\n\t"
        "bne   1b\n\t"
        : "+r"(dst), "+r"(src), "+r"(n32)
        :
        : "r3", "r4", "r5", "r6", "r8", "r9", "r10", "r12", "cc", "memory");
#else
    u32 i;
    for (i = 0; i < n32 * 8u; i++) dst[i] = src[i];
#endif
}

static void t565_lut_init(void)
{
    u32 i;
    for (i = 0; i < 256u; i++) {
        u32 r = i >> 3, gh = i & 7u, gl = i >> 5, b = i & 31u;
        t565_lut_hi[i] = (((r << 3) | (r >> 2)) << 16) | (((gh << 5) | (gh >> 1)) << 8);
        t565_lut_lo[i] = ((gl << 2) << 8) | ((b << 3) | (b >> 2));
    }
    t565_lut_ok = 1;
}

static void t565_copy_burst(u32 fb, u32 pitch, const unsigned short *src, u32 src_pitch, int bpp16)
{
    int x, y;
    if (!t565_lut_ok) t565_lut_init();
    for (y = 0; y < ZZQGL_VID_H; y++) {
        const u32 *row = (const u32 *)((const u8 *)src + (u32)y * src_pitch);
        const u32 *r2 = t565_row;
        if (zzq2_present_hook && (y & 15) == 0) zzq2_present_hook();
        nc_burst_copy(t565_row, row, (u32)ZZQGL_VID_W * 2u / 32u);
        if (bpp16) {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < ZZQGL_VID_W / 2; x++) {
                u32 v = r2[x];   /* two pixels: byte-swap each halfword (P96 16-bit is big-endian) */
                d[x] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
            }
        } else {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < ZZQGL_VID_W / 2; x++) {
                u32 v = r2[x];
                d[2 * x]     = t565_lut_hi[(v >> 8) & 0xFFu]  | t565_lut_lo[v & 0xFFu];
                d[2 * x + 1] = t565_lut_hi[v >> 24]           | t565_lut_lo[(v >> 16) & 0xFFu];
            }
        }
    }
}

static void t565_copy(u32 fb, u32 pitch, const unsigned short *src, u32 src_pitch, int bpp16)
{
    int x, y;
    if (zzq2_present_burst && ((u32)src & 31u) == 0u && (src_pitch & 31u) == 0u) {
        t565_copy_burst(fb, pitch, src, src_pitch, bpp16);
        return;
    }
    for (y = 0; y < ZZQGL_VID_H; y++) {
        const unsigned short *row = (const unsigned short *)((const u8 *)src + (u32)y * src_pitch);
        if (zzq2_present_hook && (y & 15) == 0) zzq2_present_hook();
        if (bpp16) {
            unsigned short *d = (unsigned short *)(fb + (u32)y * pitch);
            for (x = 0; x < ZZQGL_VID_W; x++) {
                u32 v = row[x];
                d[x] = (unsigned short)(((v >> 8) | (v << 8)) & 0xFFFFu);
            }
        } else {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < ZZQGL_VID_W; x += 2) {
                u32 v2 = *(const u32 *)(row + x);         /* two texels, one read of the NC buffer */
                u32 v0 = v2 & 0xFFFFu, v1 = v2 >> 16;
                u32 r0 = (v0 >> 11) & 31u, g0 = (v0 >> 5) & 63u, b0 = v0 & 31u;
                u32 r1 = (v1 >> 11) & 31u, g1 = (v1 >> 5) & 63u, b1 = v1 & 31u;
                d[x]     = (((r0 << 3) | (r0 >> 2)) << 16) | (((g0 << 2) | (g0 >> 4)) << 8) | ((b0 << 3) | (b0 >> 2));
                d[x + 1] = (((r1 << 3) | (r1 >> 2)) << 16) | (((g1 << 2) | (g1 >> 4)) << 8) | ((b1 << 3) | (b1 >> 2));
            }
        }
    }
}

static void gate(u32 code, const char *name)
{
    zzq_printf("%s\n", name);
    shared[SH_DIAG] = code;
    __asm__ volatile("dsb" ::: "memory");
}

/* zzq2rix_gpu.c (zz_rix_end_frame / pend_present): the GPU's RGB565 buffer after its frame
   fence, from the top line shown, pitch_bytes per line (640 x 481 buffers: 1280) */
void zzq2_present_rgb565(const unsigned short *pixels, unsigned pitch_bytes)
{
    u32 fb, pitch, seq, t0, now, wk;
    int bpp16;

    if (zzqgl_present_special == 1) {
        gate(ZZQGL_DIAG_EARLY_FRAME, "Q1_EARLY_FRAME");
    } else if (!g_first_endframe && !zzqgl_present_special) {
        g_first_endframe = 1;
        gate(ZZQGL_DIAG_FIRST_FRAME, "Q1_FIRST_FRAME");
    }
    if (!pixels) return;
    if (!shared[SH_TRIPLE])
        Sys_Error("ZZQuakeGL: the launcher did not obtain 3 buffers");
    t0 = zzq_us_now();
    g_last_pixels = pixels; g_last_pitch = pitch_bytes; g_last_attempt_us = t0;
    if (!t32_work_t0) t32_work_t0 = t0;

    seq = shared[SH_RENDER_SEQ];
    if (seq == t32_last_seq && zzqgl_present_must_show) {
        u32 w0 = zzq_us_now();          /* the 68k frees a buffer at its next VBlank (16.7 ms) */
        t32_show_waits++;
        while ((seq = shared[SH_RENDER_SEQ]) == t32_last_seq && (u32)(zzq_us_now() - w0) < 100000u)
            __asm__ volatile("dsb sy" ::: "memory");
        if (seq == t32_last_seq) t32_show_timeouts++;
    }
    if (seq == t32_last_seq) {          /* no buffer free: dropped */
        t32_block++;
        return;
    }
    t32_last_seq = seq;
    t32_pass++;
    fb = shared[SH_RENDER_FB];
    if (fb < 0x00200000u || fb > 0x08000000u) {
        shared[SH_FB_LAST_BAD] = fb;
        t32_rejects++;
        return;
    }
    shared[SH_FB_USED] = fb;
    bpp16 = (shared[SH_FB_BPP] == 2u);
    pitch = shared[SH_FB_PITCH];
    if (pitch == 0) pitch = (u32)ZZQGL_VID_W * (bpp16 ? 2u : 4u);
    {   /* P96 places the screen buffers where it wants in the board memory; one of them on the
           GPU's colour buffers would be drawn by both sides: checked before anything is written */
        unsigned lo, hi;
        zz_rix_buffers(&lo, &hi);
        if (fb < hi && fb + (u32)ZZQGL_VID_H * pitch > lo)
            Sys_Error("RIX: P96 buffer %08x over GPU buffers %08x-%08x", fb, lo, hi);
    }

    t565_copy(fb, pitch, pixels, pitch_bytes, bpp16);
    shared[SH_CONVERT_US] = zzq_us_now() - t0;
    dcache_clean_range(fb, (u32)ZZQGL_VID_H * pitch);
    shared[SH_FRAME]++;

    now = zzq_us_now();
    if (t32_last_us) {
        u32 d = now - t32_last_us;
        if (d > 0 && d < 1000000u) {
            u32 fps100 = 100000000u / d;
            if (fps100 > t32_fps_peak) t32_fps_peak = fps100;
        }
    }
    t32_last_us = now;

    wk = now - t32_work_t0;
    t32_work_last = wk;
    if (wk > t32_work_max) t32_work_max = wk;
    t32_renders++;
    if (wk > 16810u) t32_heavy++;
    zzq_ph_frame(wk);
    zzq_s2_frame(wk);
    {
        u32 bin = wk / 2000u;
        if (bin >= ZZQ_HIST_BINS) bin = ZZQ_HIST_BINS - 1u;
        t32_hist[bin]++;
    }
    __wrap_zzq_publish_stats();
    shared[SH_RENDER_DONE] = seq;
    __asm__ volatile("dsb sy" ::: "memory");
    shared[SH_FRAME_READY] = seq;
    __asm__ volatile("dsb sy" ::: "memory");
    t32_work_t0 = zzq_us_now();

    if (zzqgl_present_special) {
        ZZQGL_TL_MARK(ZZQGL_TL_EARLY);       /* the black frame accepted by the launcher (0.3) */
    } else {
        ZZQGL_TL_MARK(ZZQGL_TL_FIRST_FRAME);
        if (zzq2_world_count) ZZQGL_TL_MARK(ZZQGL_TL_WORLD);
        if (!g_first_present) {
            g_first_present = 1;
            shared[SH_DIAG] = ZZQGL_DIAG_PRESENTED;
            __asm__ volatile("dsb" ::: "memory");
        }
    }
}

/* Keepalive during long loads.  The launcher ends the run when SH_RENDERS has not moved for
   5 s ("CORE1 FIGE", + 3 s); a map load of GLQuake (strips of every alias model built when
   the mesh cache is empty, lightmaps built, textures uploaded) can take longer on Core1.
   GLQuake calls Draw_BeginDisc at every file it loads (glDrawBuffer(GL_FRONT), see
   zzqgl_gl.c): from there, the last frame handed over (the loading plaque) is handed over
   again, at most once a second, through the same protocol (one more frame for the launcher's
   counters).  Nothing is drawn: the GPU buffer of that frame is not the one the next frame
   draws into.
   0.2: the first run on the A4000 showed the watchdog firing during Host_Init itself (several
   seconds before any frame exists: GPU open, self-test, some 130 texture uploads, then the
   first map load up to its plaque).  VID_Init now hands over a black frame as soon as the GPU
   is open (gl_vid_zz.c early_frame), so that this keepalive has a frame to repeat from the
   first file GLQuake loads after Draw_Init. */
void zzqgl_present_keepalive(void)
{
    u32 now;
    if (!g_last_pixels) return;
    now = zzq_us_now();
    /* 0.3: the launcher publishes its first render buffer only once the engine is initialised
       (the black frame of VID_Init, 0.25 s after QG_Create, was refused on the A4000; the
       keepalive then waited a second between tries).  Until a first frame has been accepted,
       every call tries; afterwards at most once a second. */
    if (t32_pass && (u32)(now - g_last_attempt_us) < 1000000u) return;
    zzqgl_present_special = 2;
    zzq2_present_rgb565(g_last_pixels, g_last_pitch);
    zzqgl_present_special = 0;
    shared[SH_DIAG] = ZZQGL_DIAG_KEEPALIVE;   /* keepalive seen (diagnostic only) */
    __asm__ volatile("dsb" ::: "memory");
}
