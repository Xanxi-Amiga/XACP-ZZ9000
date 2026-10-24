/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
/* Presentation extracted from supplied ZZQuake2RIX 0.9.1, pixel copy and ownership unchanged. */
#include <stdio.h>
#include "zzquake_config.h"
typedef unsigned int u32;
typedef unsigned char u8;
extern volatile u32 *shared;
extern void zzq_publish_stats(void);
extern u32 zzq_us_now(void);
extern void Sys_Error(char*,...);
static int g_first_endframe,g_first_present,g_tm_shown;
static unsigned g_tm_show;
#define ZZQ2_DIAG_FIRST_FRAME 0x4f4c0300u
#define ZZQ2_DIAG_PRESENTED 0x4f4c0301u
static void q2_gate(u32 n,const char*s){(void)s;shared[SH_DIAG]=n;}
static void q2_diag(u32 n){shared[SH_DIAG]=n;}
extern void dcache_clean_range(u32 start, u32 len);
extern void zzq_ph_frame(u32 work_us);
extern void zzq_s2_frame(u32 work_us);

static u32 t32_last_seq, t32_pass, t32_block, t32_rejects;
static u32 t32_renders, t32_heavy, t32_work_t0, t32_work_last, t32_work_max;
static u32 t32_fps_peak, t32_last_us;
static u32 t32_hist[ZZQ_HIST_BINS];
#ifdef OL_PRESENT_800
static unsigned short t32_xmap[800], t32_ymap[600];
#else
static unsigned short t32_xmap[640], t32_ymap[480];
#endif
/* ZZOpenLara A2.4, software blobs only (-DOL_SOFT_PRESENT): the picture is copied at the size of the
   screen the launcher opened (SH_FB_WIDTH x SH_FB_HEIGHT: 640x480, or a real 320x240 with "320"),
   read at every frame. The RasterIX blob is built without it: the same 640 x 480 constants as before. */
#ifdef OL_SOFT_PRESENT
static u32 scr_w = 640u, scr_h = 480u;
#define SCR_W scr_w
#define SCR_H scr_h
#elif defined(OL_PRESENT_800)
/* ZZOpenLara A2.15 (-DOL_PRESENT_800, zzopenlara_rix800.bin): the 800 x 600 screen of the release
   launcher's "800" option, copied 1:1 from the 800 x 601 GPU buffer */
#define SCR_W 800u
#define SCR_H 600u
#else
#define SCR_W 640u
#define SCR_H 480u
#endif
static int t32_mw, t32_mh;
#ifdef OL_SOFT_PRESENT
static int t32_sw;
#endif

/* The release zzq_publish_stats() writes its own triple-buffer
   counters, which stay at zero here. It is interposed (build_arm.sh,
   --wrap=zzq_publish_stats): every call, including the last one made
   by the release exit path zzq_end_wfe(), is followed by the counters
   of zzq2_present32(). */
static void t32_overwrite(void);
extern void __real_zzq_publish_stats(void);
void __wrap_zzq_publish_stats(void)
{
    __real_zzq_publish_stats();
    t32_overwrite();
}

static void t32_publish(void)
{
    zzq_publish_stats();
}

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

/* RGB565 (little-endian halves, the GPU's buffer) -> the P96 buffer:
   32-bit 0x00RRGGBB (5/6/5 bits replicated into 8) or 16-bit big-endian
   RGB565 (a byte swap).  src_pitch in bytes (640x481 buffers: 1280). */
/* 0.3: zz_present (zzq2rix_video.c, archived): 1 = burst path below (default), 0 = the 0.2
   word-by-word path, kept for comparison in the same run */
int zzq2_present_burst = 1;
#ifdef OL_PRESENT_800
#define SCR_W_MAX 800
#else
#define SCR_W_MAX 640
#endif
static u32 t565_row[SCR_W_MAX / 2] __attribute__((aligned(32)));   /* one source line, cached */
static u32 t565_lut_hi[256], t565_lut_lo[256];             /* RGB565 byte -> 0x00RRGGBB part */
static int t565_lut_ok;

/* n32 blocks of 32 bytes from the GPU's non-cacheable buffer into cached memory: LDM of eight
   registers is one burst on the AXI port, where the 0.2 loop issued one uncached word read per
   two pixels (153600 per frame, measured 25.7 ms of "show" on the A4000, 14/10).  Integer
   registers only: no NEON in the Quake blob (the release's Core1 stack is not 8-byte aligned). */
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

/* RGB565 -> 0x00RRGGBB with the same bit replication as the 0.2 loop, split by source byte:
   high byte = R5 G5..G3, low byte = G2..G0 B5; the fields never overlap, so OR of two lookups */
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

/* 0.9.3 (zzq2_glue.c of ZZQuake2RIX): called every 16 lines of the conversion when set
   (zzq2rix_gpu.c, rix_overlap 1: the GPU is still drawing the next frame, its queue is pumped meanwhile) */
void (*zzq2_present_hook)(void);
static void t565_copy_burst(u32 fb, u32 pitch, const unsigned short *src, u32 src_pitch, int bpp16)
{
    int x, y;
    if (!t565_lut_ok) t565_lut_init();
    for (y = 0; y < (int)SCR_H; y++) {
        if (zzq2_present_hook && (y & 15) == 0) zzq2_present_hook();
        const u32 *row = (const u32 *)((const u8 *)src + (u32)y * src_pitch);
        const u32 *r2 = t565_row;
        nc_burst_copy(t565_row, row, SCR_W * 2u / 32u);
        if (bpp16) {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < (int)(SCR_W / 2u); x++) {
                u32 v = r2[x];   /* two pixels: byte-swap each halfword (P96 16-bit is big-endian) */
                d[x] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
            }
        } else {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < (int)(SCR_W / 2u); x++) {
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
    for (y = 0; y < (int)SCR_H; y++) {
        const unsigned short *row = (const unsigned short *)((const u8 *)src + (u32)y * src_pitch);
        if (zzq2_present_hook && (y & 15) == 0) zzq2_present_hook();
        if (bpp16) {
            unsigned short *d = (unsigned short *)(fb + (u32)y * pitch);
            for (x = 0; x < (int)SCR_W; x++) {
                u32 v = row[x];
                d[x] = (unsigned short)(((v >> 8) | (v << 8)) & 0xFFFFu);
            }
        } else {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < (int)SCR_W; x += 2) {
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

static void t32_copy(u32 fb, u32 pitch, const u32 *src, int w, int h, int bpp16)
{
    int x, y;
#ifdef OL_SOFT_PRESENT
    if (w != t32_mw || h != t32_mh || (int)SCR_W != t32_sw) {
        t32_sw = (int)SCR_W;
#else
    if (w != t32_mw || h != t32_mh) {
#endif
        for (x = 0; x < (int)SCR_W; x++) t32_xmap[x] = (unsigned short)((x * w) / (int)SCR_W);
        for (y = 0; y < (int)SCR_H; y++) t32_ymap[y] = (unsigned short)((y * h) / (int)SCR_H);
        t32_mw = w; t32_mh = h;
    }
    for (y = 0; y < (int)SCR_H; y++) {
        const u32 *row = src + (u32)t32_ymap[y] * (u32)w;
        if (bpp16) {
            unsigned short *d = (unsigned short *)(fb + (u32)y * pitch);
            for (x = 0; x < (int)SCR_W; x++) {
                u32 p = (w == (int)SCR_W) ? row[x] : row[t32_xmap[x]];
                u32 v = ((p >> 8) & 0xF800u) | ((p >> 5) & 0x07E0u) | ((p >> 3) & 0x001Fu);
                d[x] = (unsigned short)(((v >> 8) | (v << 8)) & 0xFFFFu);
            }
        } else if (w == (int)SCR_W) {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < (int)SCR_W; x += 4) {
                d[x + 0] = row[x + 0] & 0x00FFFFFFu;
                d[x + 1] = row[x + 1] & 0x00FFFFFFu;
                d[x + 2] = row[x + 2] & 0x00FFFFFFu;
                d[x + 3] = row[x + 3] & 0x00FFFFFFu;
            }
        } else {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < (int)SCR_W; x++) d[x] = row[t32_xmap[x]] & 0x00FFFFFFu;
        }
    }
}

/* src565 = 0: pixels is w x h of 0x00RRGGBB (TinyGL); src565 = 1: pixels
   is the GPU's 640-wide RGB565 buffer, pitch565 bytes per line */
static void present_common(const void *pixels, int w, int h, int src565, u32 pitch565)
{
    u32 fb, pitch, seq, t0, now, wk;
    int bpp16;

    if (!g_first_endframe) {
        g_first_endframe = 1;
        q2_gate(ZZQ2_DIAG_FIRST_FRAME, "Q2_FIRST_FRAME");
    }
#ifdef OL_SOFT_PRESENT
    scr_w = shared[SH_FB_WIDTH];
    scr_h = shared[SH_FB_HEIGHT];
    if (scr_w != 320u || scr_h != 240u) { scr_w = 640u; scr_h = 480u; }
#endif
#ifdef OL_PRESENT_800
    if (!pixels || w <= 0 || h <= 0 || w > 800 || h > 600) return;
#else
    if (!pixels || w <= 0 || h <= 0 || w > 640 || h > 480) return;
#endif
    if (!shared[SH_TRIPLE])
        Sys_Error("ZZQ2GL: the launcher did not obtain 3 buffers");
    t0 = zzq_us_now();
    if (!t32_work_t0) t32_work_t0 = t0;

    seq = shared[SH_RENDER_SEQ];
    if (seq == t32_last_seq) {          /* no buffer free: dropped */
        t32_block++;
        g_tm_show += zzq_us_now() - t0;
        return;
    }
    t32_last_seq = seq;
    t32_pass++;
    fb = shared[SH_RENDER_FB];
    if (fb < 0x00200000u || fb > 0x08000000u) {
        shared[SH_FB_LAST_BAD] = fb;
        t32_rejects++;
        g_tm_show += zzq_us_now() - t0;
        return;
    }
    shared[SH_FB_USED] = fb;
    bpp16 = (shared[SH_FB_BPP] == 2u);
    pitch = shared[SH_FB_PITCH];
    if (pitch == 0) pitch = SCR_W * (bpp16 ? 2u : 4u);

    if (src565) t565_copy(fb, pitch, (const unsigned short *)pixels, pitch565, bpp16);
    else t32_copy(fb, pitch, (const u32 *)pixels, w, h, bpp16);
    shared[SH_CONVERT_US] = zzq_us_now() - t0;
    dcache_clean_range(fb, SCR_H * pitch);
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
    t32_publish();
    shared[SH_RENDER_DONE] = seq;
    __asm__ volatile("dsb sy" ::: "memory");
    shared[SH_FRAME_READY] = seq;
    __asm__ volatile("dsb sy" ::: "memory");
    t32_work_t0 = zzq_us_now();

    if (!g_first_present) {
        g_first_present = 1;
        q2_diag(ZZQ2_DIAG_PRESENTED);
    }
    g_tm_shown = 1;
    g_tm_show += zzq_us_now() - t0;
}

void zzq2_present32(const unsigned *pixels, int w, int h)
{
    present_common(pixels, w, h, 0, 0u);
}

/* ZZQuake2RIX (zzq2rix_gpu.c): the GPU's 640x481 RGB565 buffer, after its frame fence */
void zzq2_present_rgb565(const unsigned short *pixels, unsigned pitch_bytes)
{
#ifdef OL_PRESENT_800
    present_common(pixels, 800, 600, 1, pitch_bytes);
#else
    present_common(pixels, 640, 480, 1, pitch_bytes);
#endif
}
