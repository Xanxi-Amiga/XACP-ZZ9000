/*
 * zzvr_rix.c - RasterIX hardware rendering for ZZVirtuaRacingRIX 1.0.
 *
 * VirtuaRacingDeluxeEnhanced reconstructs the game's polygon lists.
 * The RasterIX GPU draws the race at 640 x 480 with interpolated camera,
 * cars and track between the original 32X game updates. The active 1.0
 * path bypasses the SH-2 race drawing commands when acceleration is working,
 * and preserves the game HUD and menu presentation.
 *
 * Features include improved polygon edges, blended shadows and smoke,
 * extended viewing distance with haze, a moving sky and wheel effects.
 * If the GPU path becomes unavailable, the emulated 32X rendering takes over.
 *
 * GPU driver: Core1/librixgpu (0BSD). Shared protocol: zzvr_shared.h.
 * See docs/history/ for the earlier overlay-based implementation.
 */
#include <stdint.h>
#include <string.h>

#include "pico/pico_int.h"
#include "zzvr_shared.h"
#include "vrde/hle.h"
#include "vrde/hud.h"
#include "vrde/expand.h"
#include "vrde/sky.h"
#include "librixgpu/rixdesc.h"
#include "librixgpu/rixclip.h"
#include "zzvr_mem.h"
#if defined(__arm__)
#include "librixgpu/rixgpu.h"
#include "librixgpu/rixframe.h"
#endif

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

#define SW 320
#define SH 224
#define GPU_PITCH_PX 640u
#define SENT565  0x0821u                     /* the clear colour: "the GPU drew nothing here" */
#define FENCE_TIMEOUT_US 200000u

#if defined(__arm__)
static inline u32 cyc(void){ u32 v; __asm__ volatile("mrc p15,0,%0,c9,c13,0":"=r"(v)); return v; }
#define CYC_PER_US 666u
#else
#include <time.h>
#include <stdlib.h>
static inline u32 cyc(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (u32)(t.tv_sec*1000000u + t.tv_nsec/1000u); }
#define CYC_PER_US 1u
#endif

static volatile u32 *g_sh;
static int g_on;                 /* RasterIX ready and in use */
static int g_rixonly;
/* A6c: 1 = the SH-2s draw the race, the 32X frame buffer says where the Mega Drive
   layer is in front (a composed pixel that is not the frame buffer's own colour);
   0 = A6, the frame buffer holds nothing but the HUD's bitmaps: the only pixels
   kept from the emulator's picture are the HUD's and those the GPU left empty
   (the sky, where the Mega Drive layer showed through index 0 before).  Every
   colour the game's polygons use has the 32X priority bit (checked on the PC
   bench): in the original no Mega Drive pixel is ever in front of a polygon. */
static int g_fbref = 1;
void zzvr_rix_set_fbref(int fbref) { g_fbref = fbref; }

/* A7a: 60 images a second.  With A6 (g_fbref 0) on a 640 x 480 x 16 screen, an image
   is drawn at the end of every frame of a race from hle_interpolated(): the GPU
   buffer is first filled by Core1 with the emulator's picture doubled (what the GPU
   leaves undrawn shows it: the sky), the GPU draws over it without a clear, then
   (next frame, fence) Core1 writes the HUD's pixels over the GPU's, and the PL330
   copies the 3D lines into the P96 buffer, swapping the bytes of each pixel for the
   big-endian screen.  Core1 no longer reads the GPU's picture (A6c: 4.2 ms a frame). */
static int g_direct_ok;              /* init: scale 2, 16-bit screen, DMAC present, flag noa7 off */
static const u16 *g_fb3a;            /* FB3A and its line offset, from zzvr_rix_composite */
static u32 g_loffs;
static int g_pend_direct, g_cur_direct;
#define HUD_MAX 65536u
static u32 g_hud_pos[HUD_MAX];       /* (y << 16) | x of each HUD pixel of the pending image */
static u16 g_hud_col[HUD_MAX];
static u32 g_hud_n;
static int g_core1_copy;              /* A8c: no PL330: Core1 copies the 3D lines */
static int g_dup_ok;                  /* A8h: the PL330 doubles the prefill's rows */
static void dup_failed(void);
static int g_sky_mode;               /* A8b: 0 sky moved and blended, 1 off (the emulator's), 2 moved only */
void zzvr_rix_set_sky(int mode) { g_sky_mode = mode; }
/* A8e: the launcher writes the screen's format (ZZVR_SH_OUT_FMT) after START, once its
   screen is open: zzvr_rix_init saw the slot as the previous Core1 program left it (A8c and
   A8d on the A4000, after Half-Life: A7 off). Read again at every frame (composite). */
static int g_fmt_ok;
int zzvr_rix_direct(void) { return g_on && g_direct_ok && g_fmt_ok && !g_fbref; }
/* 1.0: RasterIX initialised and drawing (A6 needs it: without it the SH-2s draw the race) */
int zzvr_rix_ok(void) { return g_on; }
static int g_frame;

/* the picture being drawn by the GPU (sent at a frame's end, read at the next) */
static int g_pending;
static u32 g_pend_buf, g_pend_fence;
static int g_pend_top, g_pend_bottom;
/* the overlay: the picture on screen */
static int g_ov_valid, g_ov_first, g_ov_top, g_ov_bottom;
static int g_invalidate_next;
static u16 g_ov[SH * SW];        /* the GPU's colour where g_fl == 1 */
static u8  g_fl[SH * SW];        /* 0 leave FB3A, 1 the GPU's pixel, 2 the GPU drew nothing (rixonly: black) */
static u8  g_idx[SH * SW];       /* the 32X frame buffer's palette index under each pixel */
static u8  g_keep[SH * SW];      /* 1 = HUD pixel (or a line not from the 3D): keep FB3A */
static int g_cur;
static u32 g_fence_seq;
static int g_cal;                /* colour mapping 0..2 */

/* A3b: two output scales.  1 (flag r320): the A3a path, the picture read back and
   laid over FB3A.  2 (the default of A3b): the 3D drawn at 640 x 448 (lines 32..415
   of the GPU's 640 x 481 colour buffers); no read back: zzvr_rix_publish2x() reads
   the GPU picture through L1 while it writes the 640 x 480 P96 buffer (FB3A
   doubled, the GPU's 3D where it belongs).  A3b3: the colour buffers are FB0/FB1 of
   the GPU window in both scales (A3b1/A3b2 had them in the Core1 arena: reset loop
   in a race on the A4000); at scale 2 the window is switched to 4 KiB pages after
   the init (zzvr_mmu.c mmu_rix_window_cached: lines cached inner WB / outer NC,
   fence lines non-cacheable). */
static u32 g_bufs[2] = { 0x06600000u, 0x06696500u };          /* RIX_FB0_ARM, RIX_FB1_ARM */
static int g_scale = 1;
static float g_yb = 480.0f;          /* memory line = g_yb - y (frame layout height - 1) */
static float g_xmax = 319.46875f;
static u32 g_cur_buf;                /* scale 2: the buffer of the picture on screen */
#define ZZVR_RIX_RING_BASE 0x3E800000u                         /* zzvr_mem.h ZZVR_ARENA_RIX_RING */

static void build_argb_tables(void);
#if defined(__arm__)
extern void mmu_rix_window_cached(void);
#endif
static void stat_add(int sum, int max, u32 v)
{
    g_sh[sum] += v;
    if (v > g_sh[max]) g_sh[max] = v;
}

/* ---- colours ----------------------------------------------------------- */
/* the GPU's 8-bit channel from a float vertex colour is not documented: three
   mappings of a 5- or 6-bit value v are tried at init (calibrate) */
static float chan(u32 v, u32 bits, int mapping)
{
    switch (mapping) {
    case 0:  return bits == 5 ? (float)(8u * v + 4u) / 255.0f : (float)(4u * v + 2u) / 255.0f;
    case 1:  return ((float)v + 0.5f) / (float)((1u << bits) - 1u);
    default: return ((float)v + 0.5f) / (float)(1u << bits);
    }
}

/* the 32X's BGR555 colour as PicoDrive's pal_native has it (RGB565, priority in
   the green LSB: draw.c PXCONV), from the palette as it is now */
static u16 pal565(int index)
{
    u32 t = Pico32xMem->pal[index & 0xFF];
    if (Pico32x.vdp_regs[0] & P32XV_PRI) t ^= 0x8000u;
    return (u16)(((t & 0x001Fu) << 11) | ((t & 0x03E0u) << 1) | ((t & 0xFC00u) >> 10));
}

static void set_colour(rix_vertex *v, u16 c, float alpha)
{
    v->r = chan((u32)(c >> 11), 5u, g_cal);
    v->g = chan((u32)((c >> 5) & 63u), 6u, g_cal);
    v->b = chan((u32)(c & 31u), 5u, g_cal);
    v->a = alpha;
}

/* ---- the GPU (hardware, or the host model) ----------------------------- */
#if defined(__arm__)
static volatile u32 g_rs[RS_COUNT], g_sd[SD_COUNT], g_caps_out, g_timer_hz;
static rix_frame g_fr;

#define FRAG_OPAQUE  rix_fragment(RIX_TF_ALWAYS, 0, RIX_TF_ALWAYS, 0u, RIX_BF_ONE, RIX_BF_ZERO)
#define FRAG_BLEND   rix_fragment(RIX_TF_ALWAYS, 0, RIX_TF_ALWAYS, 0u, RIX_BF_SRC_ALPHA, RIX_BF_ONE_MINUS_SRC_ALPHA)

static int gpu_begin_c(u32 buf, int clear)
{
    rix_frame_desc d;
    memset(&d, 0, sizeof d);
    d.color = buf; d.zbuf = 0u; d.features = 0u; d.fragment = FRAG_OPAQUE;
    d.clear_rgba = rix_fence_color(SENT565); d.clear_color = clear ? 1 : 0;
    return rix_frame_begin(&g_fr, &d);
}
static int gpu_begin(u32 buf) { return gpu_begin_c(buf, 1); }
static int gpu_blend(int on)
{
    int rc = rix_frame_features(&g_fr, on ? RIX_FEAT_BLENDING : 0u);
    if (!rc) rc = rix_frame_fragment(&g_fr, on ? FRAG_BLEND : FRAG_OPAQUE);
    return rc;
}
static int gpu_tri(const u32 *desc) { return rix_frame_tri(&g_fr, desc); }
static int gpu_end(u32 fence) { return rix_frame_end(&g_fr, fence); }
static int gpu_wait(u32 buf, u32 fence) { u32 polls = 0; return rix_frame_wait(buf, fence, FENCE_TIMEOUT_US, 0, &polls); }
static const volatile u16 *gpu_pixels(u32 buf) { return (const volatile u16 *)(uintptr_t)buf; }

/* PL310 L2: clean + invalidate by physical address, one line at a time */
#define PL310_SYNC      0xF8F02730u
#define PL310_CLINV_PA  0xF8F027F0u
#define PL310_CTRL      0xF8F02100u
static void l2_clean_inval(u32 a, u32 len)
{
    volatile u32 *clinv = (volatile u32 *)PL310_CLINV_PA, *sync = (volatile u32 *)PL310_SYNC;
    u32 p;
    for (p = a & ~31u; p < a + len; p += 32u) {
        __asm__ volatile("mcr p15,0,%0,c7,c14,1"::"r"(p):"memory");   /* L1 clean + invalidate by MVA */
    }
    __asm__ volatile("dsb":::"memory");
    if (!(*(volatile u32 *)PL310_CTRL & 1u)) return;
    for (p = a & ~31u; p < a + len; p += 32u) { *clinv = p; while (*clinv & 1u) { } }
    *sync = 0u; while (*sync & 1u) { }
    __asm__ volatile("dsb":::"memory");
}
#else
/* host model: the 39-word descriptor's edge and colour planes (rixdesc.c) */
static u16 g_host_fb[2][GPU_PITCH_PX * 481u];
static int g_host_blend, g_host_buf;
static int gpu_begin_c(u32 buf, int clear)
{
    u32 i; g_host_buf = buf == g_bufs[1];
    if (clear) for (i = 0; i < GPU_PITCH_PX * 481u; i++) g_host_fb[g_host_buf][i] = SENT565;
    g_host_blend = 0; return 0;
}
static int gpu_begin(u32 buf) { return gpu_begin_c(buf, 1); }
static int gpu_blend(int on) { g_host_blend = on; return 0; }
static float fword(u32 w) { union { u32 u; float f; } v; v.u = w; return v.f; }
static u32 to8(float c) { int v = (int)(c * 255.0f); return v < 0 ? 0u : v > 255 ? 255u : (u32)v; }
static int gpu_tri(const u32 *d)
{
    int bsx = (int)(d[1] & 0xFFFF), bsy = (int)(d[1] >> 16), bex = (int)(d[2] & 0xFFFF), bey = (int)(d[2] >> 16), x, y, k;
    for (y = bsy; y < bey; y++) for (x = bsx; x < bex; x++) {
        int64_t w[3]; float c[4]; int dx = x - bsx, dy = y - bsy, line = (int)g_yb - y;
        for (k = 0; k < 3; k++) w[k] = (int64_t)(int32_t)d[3 + k] + (int64_t)dx * (int32_t)d[6 + k] + (int64_t)dy * (int32_t)d[9 + k];
        if (w[0] < 0 || w[1] < 0 || w[2] < 0) continue;
        if (line < 0 || line > 480 || x < 0 || x >= 640) continue;
        for (k = 0; k < 4; k++) c[k] = fword(d[12 + k]) + (float)dx * fword(d[16 + k]) + (float)dy * fword(d[20 + k]);
        {
            u32 r = to8(c[0]), g = to8(c[1]), b = to8(c[2]);
            u16 *p = &g_host_fb[g_host_buf][(u32)line * GPU_PITCH_PX + (u32)x];
            if (g_host_blend) {
                u32 a = to8(c[3]), dr = ((*p >> 11) << 3), dg = (((*p >> 5) & 63u) << 2), db = ((*p & 31u) << 3);
                r = (r * a + dr * (255u - a)) / 255u; g = (g * a + dg * (255u - a)) / 255u; b = (b * a + db * (255u - a)) / 255u;
            }
            *p = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    return 0;
}
static int gpu_end(u32 fence) { (void)fence; return 0; }
static int gpu_wait(u32 buf, u32 fence) { (void)buf; (void)fence; return 0; }
static const volatile u16 *gpu_pixels(u32 buf) { return g_host_fb[buf == g_bufs[1]]; }
#endif

/* scale 2: drop Core1's L1 lines of a GPU picture before reading it (the buffers
   are inner write-back, outer non-cacheable; Core1 never writes them) */
static void buf_inval(u32 buf, u32 first_line, u32 lines)
{
#if defined(__arm__)
    u32 a, e;
    if (g_scale != 2) return;
    a = buf + first_line * GPU_PITCH_PX * 2u;
    e = a + lines * GPU_PITCH_PX * 2u;
    for (a &= ~31u; a < e; a += 32u) __asm__ volatile("mcr p15,0,%0,c7,c6,1"::"r"(a):"memory");   /* DCIMVAC */
    __asm__ volatile("dsb":::"memory");
#else
    (void)buf; (void)first_line; (void)lines;
#endif
}

/* A7a: Core1's lines of a GPU buffer written back and dropped (DCCIMVAC): before the
   GPU draws over the prefill, and after the HUD's pixels */
static void buf_clean_inval(u32 buf, u32 first_line, u32 lines)
{
#if defined(__arm__)
    u32 a, e;
    a = buf + first_line * GPU_PITCH_PX * 2u;
    e = a + lines * GPU_PITCH_PX * 2u;
    for (a &= ~31u; a < e; a += 32u) __asm__ volatile("mcr p15,0,%0,c7,c14,1"::"r"(a):"memory");
    __asm__ volatile("dsb":::"memory");
#else
    (void)buf; (void)first_line; (void)lines;
#endif
}
/* A8h: the first row of each line y0..y1 (rows 2y) written back and dropped */
static void buf_clean_inval_rows2(u32 buf, u32 y0, u32 y1)
{
#if defined(__arm__)
    u32 y, a, e;
    for (y = y0; y <= y1; y++) {
        a = buf + 2u * y * GPU_PITCH_PX * 2u;
        e = a + GPU_PITCH_PX * 2u;
        for (a &= ~31u; a < e; a += 32u) __asm__ volatile("mcr p15,0,%0,c7,c14,1"::"r"(a):"memory");
    }
    __asm__ volatile("dsb":::"memory");
#else
    (void)buf; (void)y0; (void)y1;
#endif
}
static inline void line_clean_inval(const void *p)
{
#if defined(__arm__)
    __asm__ volatile("mcr p15,0,%0,c7,c14,1"::"r"((u32)(uintptr_t)p & ~31u):"memory");
#else
    (void)p;
#endif
}
static u32 *buf_words(u32 buf) { return (u32 *)(uintptr_t)gpu_pixels(buf); }

/* A7a: the PL330 copy of the 3D lines (host: done at once) */
static int copy_start(u32 src_buf, u32 src_off, void *dst, u32 bytes, int swap)
{
#if defined(__arm__)
    return rix_copy_start(src_buf + src_off, (u32)(uintptr_t)dst, bytes, swap);
#else
    const u16 *a = (const u16 *)((const u8 *)gpu_pixels(src_buf) + src_off);
    u16 *b = (u16 *)dst;
    u32 i;
    for (i = 0; i < bytes / 2u; i++) b[i] = swap ? (u16)((a[i] >> 8) | (a[i] << 8)) : a[i];
    return 0;
#endif
}
/* A8h: the PL330 doubles the prefill's rows (row 2k onto 2k+1) */
static int dup_start(u32 buf, u32 row, u32 pairs)
{
#if defined(__arm__)
    return rix_dup_rows_start(buf + row * GPU_PITCH_PX * 2u, GPU_PITCH_PX * 2u, pairs);
#else
    u8 *a = (u8 *)gpu_pixels(buf) + row * GPU_PITCH_PX * 2u;
    u32 i;
    for (i = 0; i < pairs; i++) memcpy(a + (2u * i + 1u) * GPU_PITCH_PX * 2u, a + 2u * i * GPU_PITCH_PX * 2u, GPU_PITCH_PX * 2u);
    return 0;
#endif
}
static int copy_wait(void)
{
#if defined(__arm__)
    return rix_copy_wait(20000u);
#else
    return 0;
#endif
}

static u32 next_fence(void)
{
    g_fence_seq++;
    if (g_fence_seq == 0u || g_fence_seq > 65534u) g_fence_seq = 1u;
    return g_fence_seq;
}

extern void zzvr_a6_off(void);
static void gpu_error(int rc)
{
    zzvr_a6_off();                       /* A8g: the SH-2s draw the race again (A6 off) */
    g_sh[ZZVR_SH_RIX_ERR] = (u32)rc;
    g_sh[ZZVR_SH_RIX_ERR_COUNT]++;
    g_on = 0;                            /* the SH-2s' picture alone from now on */
    g_ov_valid = 0;
    g_pending = 0;
    g_sh[ZZVR_SH_RIX_MAGIC] = 0x4641494Cu;   /* 'FAIL' */
}

/* a polygon (n corners in console coordinates) clipped and sent as a fan */
static int send_polygon(const vrde_xy *p, int n, u16 colour, float alpha, float y0, float y1, u32 *tris)
{
    rix_vertex in[8], out[RIX_CLIP_MAX];
    u32 desc[RIX_TRI_DESC_WORDS], m, i;
    int k, rc;
    rix_vertex base;
    memset(&base, 0, sizeof base);
    base.invw = 1.0f;
    set_colour(&base, colour, alpha);                  /* A8b: once per polygon */
    for (k = 0; k < n; k++) {
        in[k] = base;
        in[k].sx = (float)g_scale * p[k].x - 0.5f;
        in[k].sy = g_yb + 0.5f - (float)g_scale * p[k].y;
        in[k].a = alpha * p[k].a;                      /* A8a: the haze in each vertex's alpha */
    }
    m = rix_clip_screen(in, (u32)n, 0.0f, y0, g_xmax, y1, out);
    for (i = 1; i + 1u < m; i++) {
        if (!rix_tri_desc_flat(&out[0], &out[i], &out[i + 1u], desc)) continue;   /* A8g: flat, no texture or depth */
        rc = gpu_tri(desc);
        if (rc) return rc;
        (*tris)++;
    }
    return 0;
}

/* ---- colour calibration -------------------------------------------------- */
static void calibrate(void)
{
    int mapping, i, best = -1;
    u32 errs[3], fence, worst = 0;
    const volatile u16 *px;
    int rc = gpu_begin(g_bufs[0]);
    u32 tris = 0;
    for (mapping = 0; mapping < 3 && !rc; mapping++) {
        g_cal = mapping;
        for (i = 0; i < 64 && !rc; i++) {
            vrde_xy q[4] = { { 0, 0, 1.0f }, { 0, 0, 1.0f }, { 0, 0, 1.0f }, { 0, 0, 1.0f } };
            u16 c = (u16)(((u32)(i & 31) << 11) | ((u32)i << 5) | (u32)(31 - (i & 31)));
            float x0 = 4.0f * (float)i, line0 = 150.0f + 8.0f * (float)mapping;   /* console coordinates */
            q[0].x = x0;        q[0].y = line0;
            q[1].x = x0 + 4.0f; q[1].y = line0;
            q[2].x = x0 + 4.0f; q[2].y = line0 + 4.0f;
            q[3].x = x0;        q[3].y = line0 + 4.0f;
            rc = send_polygon(q, 4, c, 1.0f, 1.0f, g_yb + 0.46875f, &tris);
        }
    }
    fence = next_fence();
    if (!rc) rc = gpu_end(fence);
    if (!rc) rc = gpu_wait(g_bufs[0], fence);
    if (rc) { gpu_error(rc); return; }
    buf_inval(g_bufs[0], 0u, 448u);
    px = gpu_pixels(g_bufs[0]);
    for (mapping = 0; mapping < 3; mapping++) {
        errs[mapping] = 0;
        for (i = 0; i < 64; i++) {
            u16 c = (u16)(((u32)(i & 31) << 11) | ((u32)i << 5) | (u32)(31 - (i & 31)));
            /* console pixels (4i+1..4i+2, L+1..L+2) and (4i, L+1): the memory pixels of
               their top-left sub-pixel at this scale */
            int line = g_scale * (151 + 8 * mapping), x;
            for (x = 4 * i + 1; x <= 4 * i + 2; x++) {
                if (px[(u32)line * GPU_PITCH_PX + (u32)(g_scale * x)] != c) errs[mapping]++;
                if (px[(u32)(line + g_scale) * GPU_PITCH_PX + (u32)(g_scale * x)] != c) errs[mapping]++;
            }
            if (px[(u32)line * GPU_PITCH_PX + (u32)(g_scale * 4 * i)] != c) errs[mapping]++;
        }
        if (errs[mapping] > worst) worst = errs[mapping];
        g_sh[ZZVR_SH_RIX_CAL_ERR0 + mapping] = errs[mapping];
        if (best < 0 && errs[mapping] == 0) best = mapping;
    }
    if (best < 0) {            /* none exact: the closest */
        best = 0;
        for (mapping = 1; mapping < 3; mapping++) if (errs[mapping] < errs[best]) best = mapping;
        g_sh[ZZVR_SH_RIX_CAL] = (worst << 8) | 0u;
    } else {
        g_sh[ZZVR_SH_RIX_CAL] = (worst << 8) | (u32)(best + 1);
    }
    g_cal = best;
    g_cur = 1;
}

/* ---- init ------------------------------------------------------------------ */
void zzvr_rix_init(volatile u32 *sh, int hle_on)
{
    int i;
    u32 ctrl;
    g_sh = sh;
    for (i = ZZVR_SH_RIX_FIRST; i <= ZZVR_SH_RIX_LAST; i++) sh[i] = 0;
    for (i = 0; i < ZZVR_SH_RIXSD_COUNT; i++) sh[ZZVR_SH_RIXSD_FIRST + i] = 0;
    for (i = ZZVR_SH_CHKLOG_N; i < ZZVR_SH_CHKLOG_FIRST + 3 * ZZVR_SH_CHKLOG_MAX; i++) sh[i] = 0;
    sh[ZZVR_SH_RIX_MAPS_US_MAX] = 0;
    sh[ZZVR_SH_RIX_LOWEST] = 10000u;
    ctrl = sh[ZZVR_SH_HLE_CTRL];
    g_rixonly = (ctrl & 8u) ? 1 : 0;
    g_scale = (sh[ZZVR_SH_OUT_SCALE] == 2u) ? 2 : 1;      /* A3b: written by the launcher before START */
    if (g_scale == 2) {
        g_bufs[0] = 0x06600000u; g_bufs[1] = 0x06696500u;   /* A3b3: the window, 640 x 481 as OpenLara */
        g_yb = 480.0f; g_xmax = 639.46875f;
    } else {
        g_bufs[0] = 0x06600000u; g_bufs[1] = 0x06696500u;
        g_yb = 480.0f; g_xmax = 319.46875f;
    }
    g_cur_buf = 0u;
    for (i = ZZVR_SH_A7_STATE; i <= ZZVR_SH_A7_HELD; i++) if (i != ZZVR_SH_A7_FPI2) sh[i] = 0;
    sh[ZZVR_SH_A8_POLY_SUM] = 0; sh[ZZVR_SH_A8_POLY_MAX] = 0; sh[ZZVR_SH_A8_HAZED_SUM] = 0; sh[ZZVR_SH_A8_GOV] = 0;
    g_direct_ok = 0; g_pend_direct = 0; g_cur_direct = 0; g_hud_n = 0; g_fb3a = 0;
    build_argb_tables();
    g_on = 0; g_pending = 0; g_ov_valid = 0; g_invalidate_next = 0; g_frame = 0; g_cur = 0;
    sh[ZZVR_SH_RIX_MAGIC] = 0x4E4F5258u;   /* 'NORX' */
    if (!hle_on || (ctrl & 4u)) return;
    rix_desc_delta = 1;                    /* planes from the differences: a flat colour stays exact */
#if defined(__arm__)
    {
        rix_config cfg;
        rix_ring_cfg rc;
        volatile u32 *gt = (volatile u32 *)0xF8F00200u;   /* global timer, CPU clock / 2 */
        u32 t0 = gt[0], caps;
        int r, k;
        l2_clean_inval(0x06600000u, 0x00200000u);
        l2_clean_inval(ZZVR_RIX_RING_BASE, 0x00100000u);
        sh[ZZVR_SH_RIX_L2_US] = (gt[0] - t0) / 333u;
        memset(&cfg, 0, sizeof cfg);
        cfg.flags = RIXF_SPIN | RIXF_NODMA | RIXF_NOSWAP;
        cfg.idle_tick_us = 100u;
        cfg.stats = g_rs; cfg.sd = g_sd; cfg.caps_out = &g_caps_out; cfg.timer_hz_out = &g_timer_hz;
        cfg.dma_prog = RIX_DL1_ARM;            /* required by the config check; never used (NODMA) */
        r = rix_init(&cfg);
        caps = rix_caps();
        sh[ZZVR_SH_RIX_INIT_RC] = (u32)r;
        sh[ZZVR_SH_RIX_CAPS] = caps;
        sh[ZZVR_SH_RIX_TIMER_HZ] = g_timer_hz;
        for (k = 0; k < ZZVR_SH_RIXSD_COUNT && k < SD_COUNT; k++) sh[ZZVR_SH_RIXSD_FIRST + k] = g_sd[k];
        if ((!(caps & RIXCAP_ID) || !(caps & RIXCAP_FENCE_MEM)) && (caps & RIXCAP_ID) && rix_alive() == 0) {
            /* A3a2: the GPU still takes commands: one more init, as a second launch would */
            r = rix_init(&cfg);
            caps = rix_caps();
            sh[ZZVR_SH_RIX_RETRY_RC] = 0x10000u | ((u32)r & 0xFFFFu);
            sh[ZZVR_SH_RIX_RETRY_CAPS] = caps;
        }
        if (!(caps & RIXCAP_ID) || !(caps & RIXCAP_FENCE_MEM)) { sh[ZZVR_SH_RIX_MAGIC] = 0x4641494Cu; return; }
        memset(&rc, 0, sizeof rc);
        /* A3a3: the ring in the Core1 arena, as ZZQuake2RIX / ZZOpenLaraRIX (8 x 64 KiB at
           0x3E800000, section mapped Normal NC by zzvr_mmu.c).  A3a had librixgpu's window
           ring (0x067E3000, 7 x 16 KiB): its slots 3..6 reach 0x067F0000, the firmware's
           staging area, and the first race picture (many chunks) hung the GPU. */
        rc.ring_base = ZZVR_RIX_RING_BASE; rc.ring_slots = 8u; rc.chunk_bytes = 65536u;
        rc.fence_x = RIX_CHUNK_FENCE_X; rc.color_bufs = g_bufs; rc.color_count = 2u; rc.ring_cacheable = 0u;
        rc.bind_base = 0u; rc.bind_cacheable = 0u;
        r = rix_ring_init(&rc);
        if (r) { sh[ZZVR_SH_RIX_INIT_RC] = (u32)r; sh[ZZVR_SH_RIX_MAGIC] = 0x4641494Cu; return; }
    }
#endif
    g_on = 1;
    sh[ZZVR_SH_RIX_MAGIC] = 0x52495833u;   /* 'RIX3' */
    calibrate();
#if defined(__arm__)
    /* A3b3: from now on Core1 reads the colour buffers' lines through L1 (zzvr_mmu.c) */
    if (g_on && g_scale == 2) mmu_rix_window_cached();
#endif
    /* A7a: an image every frame, copied by the PL330 (A6 must be on: zzvr_rix_direct).
       A8c: copied by Core1 when the PL330 is not clocked or held in reset (seen once on the
       A4000 after A8b had used it: the A6c way then drew 20 images a second) */
    g_core1_copy = 0;
#if defined(__arm__)
    if (!rix_dma_present()) g_core1_copy = 1;
#elif defined(ZZVR_HOST_CORE1_COPY)
    g_core1_copy = 1;                      /* bench: the path without the PL330 */
#endif
    g_fmt_ok = 0;                          /* zzvr_rix_composite: once the launcher has written it */
    g_dup_ok = !g_core1_copy && !(ctrl & 128u);   /* A8h (flag nodup: Core1 writes both rows) */
    if (g_on && g_scale == 2 && !g_rixonly && !(ctrl & 64u))
        { g_direct_ok = 1; sh[ZZVR_SH_A7_STATE] = 0x41370001u | (g_core1_copy ? 4u : 0u) | (g_dup_ok ? 16u : 0u); }
    else sh[ZZVR_SH_A7_STATE] = (ctrl & 64u) ? 0x41370000u : 0x4137FF00u;
}

/* A8a: the far view's haze (VRDE's far_view 1): from haze_start on, the 3D fades into
   what lies behind it (the sky the prefill put there), all gone at haze_end; linear in
   the depth, per vertex (VRDE per pixel) */
static int g_haze_on;
static float g_haze_start = 5000.0f, g_haze_end = 7680.0f;
void zzvr_rix_set_haze(int on, float start, float end)
{
    g_haze_on = on; g_haze_start = start; g_haze_end = end > start + 1.0f ? end : start + 1.0f;
}
static u32 g_hazed;

/* the list's polygons, in its order (painter's algorithm), as fans of triangles */
static int draw_list(const hle_frame_t *f, float y0, float y1, u32 *tris)
{
    int i, rc = 0, blend = 0;
    float inv = 1.0f / (g_haze_end - g_haze_start);
    g_hazed = 0;
    for (i = 0; i < f->count && !rc; i++) {
        const hle_polygon_t *p = &f->polygons[i];
        vrde_xy corners[4], fan[8];
        int n = p->vertex_count, k, dithered, faded = 0, gone = 1, want;
        if (p->drawn_with == HLE_SH2_VERTICES_ONLY) continue;   /* faces away with the exact vertices */
        if (n < 3 || n > 4) continue;
        for (k = 0; k < n; k++) {
            float a = 1.0f;
            corners[k].x = 160.0f + p->fx[k]; corners[k].y = 112.0f - p->fy[k] + 1.0f;
            if (g_haze_on && p->depth[k] > g_haze_start) {
                a = 1.0f - (p->depth[k] - g_haze_start) * inv;
                if (a < 0.0f) a = 0.0f;
                faded = 1;
            }
            if (a > 0.004f) gone = 0;
            corners[k].a = a;
        }
        if (gone) continue;                                     /* all in the haze */
        g_hazed += (u32)faded;
        n = vrde_expand_polygon(corners, n, VRDE_EXPAND_CONSOLE, fan);
        dithered = (p->colour & 2) != 0;
        want = dithered || faded;
        if (want != blend) { rc = gpu_blend(want); blend = want; if (rc) break; }
        rc = send_polygon(fan, n, pal565(p->colour >> 8), dithered ? 0.5f : 1.0f, y0, y1, tris);
    }
    return rc;
}

/* ---- a new race picture on screen (end of frame) ----------------------------- */
static void finish_pending(int keep_it);
static void finish_pending2x(void);

void zzvr_rix_picture(const hle_frame_t *f, int buffer)
{
    u32 c0, fence, tris = 0;
    int i, y, rc, blend = 0, top, bottom;
    float y0, y1;
    if (!g_on) { if (g_sh) g_sh[ZZVR_SH_RIX_SKIPPED]++; return; }
    if (g_pending) finish_pending(0);     /* not composited: only if a frame skipped the composite */
    c0 = cyc();
    top = f->view_top < 0 ? 0 : f->view_top;
    bottom = f->view_bottom > SH - 1 ? SH - 1 : f->view_bottom;
    /* the HUD and the palette index of every pixel, from the frame buffer now on screen */
    {
        const u16 *dram = Pico32xMem->dram[buffer & 1];
        for (y = top; y <= bottom; y++) {
            u32 at = (u32)dram[y] * 2u, x;
            u8 *ix = &g_idx[y * SW];
            hud_keep_row(buffer & 1, y, &g_keep[y * SW]);
            if (!g_fbref) continue;                         /* A6c: the index map is not used */
            if (at + SW <= 0x20000u) {                     /* A3b: a word = two pixels */
                const u16 *w = &dram[at >> 1];
                for (x = 0; x < SW; x += 2) { u16 pair = *w++; ix[x] = (u8)(pair >> 8); ix[x + 1] = (u8)pair; }
            } else {
                for (x = 0; x < SW; x++) {
                    u32 b = (at + x) & 0x1FFFFu;
                    u16 pair = dram[b >> 1];
                    ix[x] = (u8)((b & 1u) ? (pair & 0xFFu) : (pair >> 8));
                }
            }
        }
    }
    stat_add(ZZVR_SH_RIX_MAPS_US_SUM, ZZVR_SH_RIX_MAPS_US_MAX, (cyc() - c0) / CYC_PER_US);
    /* the 3D */
    y0 = g_yb - (float)(g_scale * bottom + g_scale - 1);
    y1 = g_yb - (float)(g_scale * top) + 0.46875f;
    g_cur ^= 1;
    rc = gpu_begin(g_bufs[g_cur]);
    if (!rc) rc = draw_list(f, y0, y1, &tris);
    (void)i; (void)blend;
    fence = next_fence();
    if (!rc) rc = gpu_end(fence);
    if (rc) { gpu_error(rc); return; }
    g_pending = 1; g_pend_buf = g_bufs[g_cur]; g_pend_fence = fence;
    g_pend_top = top; g_pend_bottom = bottom;
    g_sh[ZZVR_SH_RIX_PICTURES]++;
    stat_add(ZZVR_SH_RIX_TRIS_SUM, ZZVR_SH_RIX_TRIS_MAX, tris);
    stat_add(ZZVR_SH_RIX_BUILD_US_SUM, ZZVR_SH_RIX_BUILD_US_MAX, (cyc() - c0) / CYC_PER_US);
}

/* ---- A7a: an image at the end of every frame of a race ------------------------ */
static void finish_pending2x(void);
void zzvr_rix_frame_direct(const hle_frame_t *f, int buffer)
{
    u32 c0, c1, fence, tris = 0, G, *gw, nh = 0;
    int y, top, bottom, rc;
    float y0, y1;
    if (!g_on) { if (g_sh) g_sh[ZZVR_SH_RIX_SKIPPED]++; return; }
    if (g_pending) finish_pending2x();    /* only if a frame skipped the composite */
    if (!g_fb3a) return;
#if !defined(__arm__) && defined(ZZVR_HOST_RIXFAIL)
    { extern int host_rixfail_frame; if (host_rixfail_frame && g_frame >= host_rixfail_frame) { gpu_error(-99); return; } }   /* bench: RasterIX failing */
#endif
    c0 = cyc();
    top = f->view_top < 0 ? 0 : f->view_top;
    bottom = f->view_bottom > SH - 1 ? SH - 1 : f->view_bottom;
    if (bottom < top) return;
    g_cur ^= 1;
    G = g_bufs[g_cur];
    gw = buf_words(G);
    /* 1: the HUD's pixels (written over the GPU's picture once it is drawn); A8b: and
       whether the sky as we draw it (vrde/sky.c) is what the console shows, every 4th line */
    for (y = top; y <= bottom; y++) {
        const u16 *src = g_fb3a + ((u32)y + g_loffs) * SW;
        u8 *keep = &g_keep[y * SW];
        int x, k;
        hud_keep_row(buffer & 1, y, keep);
        for (x = 0; x < SW; x += 4) {
            if (!*(const u32 *)(const void *)&keep[x]) continue;
            for (k = x; k < x + 4; k++)
                if (keep[k] && nh < HUD_MAX) { g_hud_pos[nh] = ((u32)y << 16) | (u32)k; g_hud_col[nh] = src[k]; nh++; }
        }
    }
    g_hud_n = nh;
    {
        int sky = 0, sky_half = 0;
        if (g_sky_mode != 1) {
            int ok;
            sky_half = sky_scroll_half(f->scene, f->camera_t, &ok);
            sky = ok;
            for (y = top; sky && y <= bottom; y += 4)
                if (!sky_line_matches(y, g_fb3a + ((u32)y + g_loffs) * SW, &g_keep[y * SW])) sky = 0;
            if (g_sh) { if (sky) g_sh[ZZVR_SH_A8_SKY_FRAMES]++; else g_sh[ZZVR_SH_A8_SKY_OFF]++; }
        }
        /* 2: the background the GPU draws over: the sky moved with the camera and blended
           (A8b), else the emulator's picture doubled (little-endian, as the GPU writes).
           A8h: Core1 writes each line's first row only, a quarter of the picture at a
           time, and the PL330 copies that quarter's rows onto the rows below them while
           Core1 writes the next (A8g: Core1 wrote both rows) */
        int dup = g_dup_ok, seg = (bottom - top + 4) / 4, s0, done = top - 1;
        if (seg < 1) seg = 1;
        for (s0 = top; s0 <= bottom; s0 += seg) {
            int s1 = s0 + seg - 1 > bottom ? bottom : s0 + seg - 1;
            for (y = s0; y <= s1; y++) {
                u32 *d0 = gw + (u32)(2 * y) * (GPU_PITCH_PX / 2u), *d1 = d0 + GPU_PITCH_PX / 2u;
                int x;
                if (sky && sky_line_rows(y, sky_half, d0, dup ? (u32 *)0 : d1)) {
                    /* A8g: straight into the rows */
                } else {
                    const u16 *src = g_fb3a + ((u32)y + g_loffs) * SW;
                    for (x = 0; x < SW; x += 4) {
                        u32 p0 = src[x], p1 = src[x + 1], p2 = src[x + 2], p3 = src[x + 3];
                        d0[x] = p0 | (p0 << 16); d0[x + 1] = p1 | (p1 << 16); d0[x + 2] = p2 | (p2 << 16); d0[x + 3] = p3 | (p3 << 16);
                    }
                    if (!dup) for (x = 0; x < SW; x += 4) {
                        u32 p0 = src[x], p1 = src[x + 1], p2 = src[x + 2], p3 = src[x + 3];
                        d1[x] = p0 | (p0 << 16); d1[x + 1] = p1 | (p1 << 16); d1[x + 2] = p2 | (p2 << 16); d1[x + 3] = p3 | (p3 << 16);
                    }
                }
            }
            if (dup) {
                buf_clean_inval_rows2(G, (u32)s0, (u32)s1);   /* the first rows in DDR before the PL330 reads them */
                if (dup_start(G, (u32)(2 * s0), (u32)(s1 - s0 + 1)) == 0) done = s1;
                else { dup = 0; dup_failed(); }
            }
            if (!dup && done < s1) {
                /* no PL330 (or it failed): Core1 doubles what it has not */
                for (y = done + 1; y <= s1; y++) {
                    u32 *d0 = gw + (u32)(2 * y) * (GPU_PITCH_PX / 2u);
                    memcpy(d0 + GPU_PITCH_PX / 2u, d0, GPU_PITCH_PX * 2u);
                }
                done = s1;
            }
        }
        if (dup && copy_wait() != 0) {
            /* the PL330 did not finish: Core1 doubles every row */
            dup_failed();
            for (y = top; y <= bottom; y++) {
                u32 *d0 = gw + (u32)(2 * y) * (GPU_PITCH_PX / 2u);
                memcpy(d0 + GPU_PITCH_PX / 2u, d0, GPU_PITCH_PX * 2u);
            }
            dup = 0;
        }
        if (!dup) buf_clean_inval(G, (u32)(2 * top), (u32)(2 * (bottom - top + 1)));
    }
    c1 = cyc();
    stat_add(ZZVR_SH_A7_PREFILL_US_SUM, ZZVR_SH_A7_PREFILL_US_MAX, (c1 - c0) / CYC_PER_US);
    y0 = g_yb - (float)(g_scale * bottom + g_scale - 1);
    y1 = g_yb - (float)(g_scale * top) + 0.46875f;
    rc = gpu_begin_c(G, 0);                /* no clear: the prefill is the background */
    if (!rc) rc = draw_list(f, y0, y1, &tris);
    fence = next_fence();
    if (!rc) rc = gpu_end(fence);
    if (rc) { gpu_error(rc); return; }
    g_pending = 1; g_pend_buf = G; g_pend_fence = fence; g_pend_direct = 1;
    g_pend_top = top; g_pend_bottom = bottom;
    g_sh[ZZVR_SH_RIX_PICTURES]++;
    g_sh[ZZVR_SH_A7_PICTURES]++;
    g_sh[ZZVR_SH_A7_HUD_SUM] += nh;
    g_sh[ZZVR_SH_A8_POLY_SUM] += (u32)f->count;
    if ((u32)f->count > g_sh[ZZVR_SH_A8_POLY_MAX]) g_sh[ZZVR_SH_A8_POLY_MAX] = (u32)f->count;
    g_sh[ZZVR_SH_A8_HAZED_SUM] += g_hazed;
    stat_add(ZZVR_SH_RIX_TRIS_SUM, ZZVR_SH_RIX_TRIS_MAX, tris);
    stat_add(ZZVR_SH_A7_SUBMIT_US_SUM, ZZVR_SH_A7_SUBMIT_US_MAX, (cyc() - c1) / CYC_PER_US);
    stat_add(ZZVR_SH_RIX_BUILD_US_SUM, ZZVR_SH_RIX_BUILD_US_MAX, (cyc() - c0) / CYC_PER_US);
}

/* A8h: the PL330 failed to double the rows: Core1 does it from now on */
static void dup_failed(void)
{
    g_dup_ok = 0;
    if (g_sh) g_sh[ZZVR_SH_A7_STATE] = (g_sh[ZZVR_SH_A7_STATE] & ~16u) | 32u;
}

/* A7b: hle_interpolated() returned the same list (i40 / i30, or the game paused):
   the image on screen stays, copied again by the PL330 */
void zzvr_rix_hold(void)
{
    if (g_sh) g_sh[ZZVR_SH_A7_HELD]++;
}

/* A8a: an A7 image is on screen (the governor may keep it) */
int zzvr_rix_has_image(void) { return g_ov_valid && g_cur_direct; }

/* A7b: publish2x_direct cleaned the lines Core1 wrote: publish_p96 skips its clean */
static int g_cleaned;
int zzvr_rix_take_cleaned(void) { int c = g_cleaned; g_cleaned = 0; return c; }

/* the picture now on screen is not a race picture: no overlay from the next frame */
void zzvr_rix_no_picture(void)
{
    if (g_on) g_invalidate_next = 1;
}

/* after a savestate: forget the picture */
void zzvr_rix_reset(void)
{
    if (g_pending) finish_pending(0);
    g_ov_valid = 0; g_invalidate_next = 0;
}

/* A3a4: one line of the GPU picture (640 bytes, 32-byte aligned) out of the
   non-cacheable window with 8-word LDM bursts: A3a3 read it word by word, 4.7 ms
   per picture on the A4000 (about 150 ns per uncached read) */
static u32 g_row[SW / 2] __attribute__((aligned(32)));
static void nc_row_copy(u32 *dst, const volatile u32 *src)
{
#if defined(__arm__)
    u32 n = (SW * 2u) / 32u;
    __asm__ volatile(
        "1:\n\t"
        "ldmia %1!, {r2-r9}\n\t"
        "stmia %0!, {r2-r9}\n\t"
        "subs %2, %2, #1\n\t"
        "bne 1b\n\t"
        : "+r"(dst), "+r"(src), "+r"(n)
        :
        : "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "memory", "cc");
#else
    u32 i;
    for (i = 0; i < SW / 2u; i++) dst[i] = src[i];
#endif
}

/* wait for the pending picture and read it back into the overlay */
static void finish_pending(int keep_it)
{
    u32 c0 = cyc(), c1;
    int rc, y;
    rc = gpu_wait(g_pend_buf, g_pend_fence);
    c1 = cyc();
    g_pending = 0;
    if (rc) { gpu_error(rc); return; }
    stat_add(ZZVR_SH_RIX_WAIT_US_SUM, ZZVR_SH_RIX_WAIT_US_MAX, (c1 - c0) / CYC_PER_US);
    if (!keep_it) return;
    {
        const volatile u16 *gpu = gpu_pixels(g_pend_buf);
        u8 empty = g_rixonly ? 2u : 0u;
        for (y = g_pend_top; y <= g_pend_bottom; y++) {
            const u32 *src = g_row;
            u16 *ov = &g_ov[y * SW];
            nc_row_copy(g_row, (const volatile u32 *)(gpu + (u32)y * GPU_PITCH_PX));
            u8 *fl = &g_fl[y * SW];
            const u8 *keep = &g_keep[y * SW];
            int x;
            for (x = 0; x < SW; x += 2) {
                u32 two = src[x >> 1];
                u16 a = (u16)(two & 0xFFFFu), b = (u16)(two >> 16);
                ov[x] = a; ov[x + 1] = b;
                fl[x]     = keep[x]     ? 0u : (a == SENT565 ? empty : 1u);
                fl[x + 1] = keep[x + 1] ? 0u : (b == SENT565 ? empty : 1u);
            }
        }
    }
    g_ov_top = g_pend_top; g_ov_bottom = g_pend_bottom;
    g_ov_valid = 1; g_ov_first = 1;
    stat_add(ZZVR_SH_RIX_READ_US_SUM, ZZVR_SH_RIX_READ_US_MAX, (cyc() - c1) / CYC_PER_US);
}

/* A3b (scale 2): the picture is ready: drop the stale L1 lines of its rows, it is
   the one on screen from this frame on */
static void finish_pending2x(void)
{
    u32 c0 = cyc(), c1;
    int rc = gpu_wait(g_pend_buf, g_pend_fence);
    c1 = cyc();
    g_pending = 0;
    if (rc) { gpu_error(rc); return; }
    stat_add(ZZVR_SH_RIX_WAIT_US_SUM, ZZVR_SH_RIX_WAIT_US_MAX, (c1 - c0) / CYC_PER_US);
    g_cur_direct = g_pend_direct;
    if (g_pend_direct) {
        /* A7a: the HUD's pixels over the GPU's (their lines are fetched from DDR: the
           prefill left none in L1), written back for the PL330 */
        u32 *gw = buf_words(g_pend_buf), i, last0 = 1u, last1 = 1u;
        for (i = 0; i < g_hud_n; i++) {
            u32 y = g_hud_pos[i] >> 16, x = g_hud_pos[i] & 0xFFFFu, c = g_hud_col[i];
            u32 *d0 = gw + 2u * y * (GPU_PITCH_PX / 2u) + x, *d1 = d0 + GPU_PITCH_PX / 2u;
            u32 l0 = (u32)(uintptr_t)d0 & ~31u, l1 = (u32)(uintptr_t)d1 & ~31u;
            /* A7b: a cache line written back once it is done (the list goes along the lines) */
            if (l0 != last0 && last0 != 1u) { line_clean_inval((const void *)(uintptr_t)last0); line_clean_inval((const void *)(uintptr_t)last1); }
            *d0 = c | (c << 16); *d1 = c | (c << 16);
            last0 = l0; last1 = l1;
        }
        if (last0 != 1u) { line_clean_inval((const void *)(uintptr_t)last0); line_clean_inval((const void *)(uintptr_t)last1); }
#if defined(__arm__)
        __asm__ volatile("dsb":::"memory");
#endif
        g_pend_direct = 0;
    } else {
        buf_inval(g_pend_buf, (u32)(2 * g_pend_top), (u32)(2 * (g_pend_bottom - g_pend_top + 1)));
    }
    g_cur_buf = g_pend_buf;
    g_ov_top = g_pend_top; g_ov_bottom = g_pend_bottom;
    g_ov_valid = 1; g_ov_first = 1;
    stat_add(ZZVR_SH_RIX_READ_US_SUM, ZZVR_SH_RIX_READ_US_MAX, (cyc() - c1) / CYC_PER_US);
}

/* RGB565 -> P96 ARGB8888 in two 1 KiB tables (high byte: red and the top of green,
   low byte: the rest of green and blue), the same values as the 64 K table of the
   320 path: r8 = r5 << 3 | r5 >> 2, g8 = g6 << 2 | g6 >> 4, b8 = b5 << 3 | b5 >> 2 */
static u32 g_thi[256], g_tlo[256];
static void build_argb_tables(void)
{
    u32 v;
    for (v = 0; v < 256u; v++) {
        u32 r5 = v >> 3, gh = v & 7u, gl = v >> 5, b5 = v & 31u;
        g_thi[v] = 0xFF000000u | (((r5 << 3) | (r5 >> 2)) << 16) | (((gh << 5) | (gh >> 1)) << 8);
        g_tlo[v] = ((gl << 2) << 8) | ((b5 << 3) | (b5 >> 2));
    }
}
#define ARGB(p) (g_thi[(p) >> 8] | g_tlo[(p) & 0xFFu])

int zzvr_rix_scale(void) { return g_scale; }

/* A3b: the 640 x 480 P96 buffer from FB3A (each pixel doubled) and, in the 3D lines
   of the race picture on screen, the GPU's 640 x 448 picture where it belongs (as
   the A3a overlay: not a HUD pixel, the 32X layer in front, the GPU drew there).
   A3b2: in 16 bits (ZZVR_SH_OUT_FMT 1 or 2) it is copies only: FB3A, the GPU and the
   screen are all RGB565; a doubled pixel is one 32-bit word, two GPU pixels too.
   A3b1 measured 7.2 ms a frame for the 32-bit version (1.2 MB written). */
#if defined(__arm__)
static inline u32 swap16x2(u32 v) { u32 r; __asm__("rev16 %0, %1" : "=r"(r) : "r"(v)); return r; }
#else
static inline u32 swap16x2(u32 v) { return ((v >> 8) & 0x00FF00FFu) | ((v << 8) & 0xFF00FF00u); }
#endif

static void publish2x_16(const u16 *src, u32 *dst, u32 stride, u32 loffs, u32 lines, int be,
                         int gpu_ok, int first, u32 *cmp, u32 *same, u32 *empty)
{
    const u16 *pal = Pico32xMem->pal_native;
    const u32 *gpu = (const u32 *)gpu_pixels(g_cur_buf);
    u32 r;
    for (r = 0; r < 240u; r++) {
        u32 *d0 = dst + 2u * r * stride, *d1 = d0 + stride, x;
        const u16 *s;
        int y;
        if (r < loffs || r >= loffs + lines) {
            for (x = 0; x < SW; x++) { d0[x] = 0u; d1[x] = 0u; }
            continue;
        }
        s = src + r * SW;
        y = (int)(r - loffs);
        if (gpu_ok && y >= g_ov_top && y <= g_ov_bottom) {
            /* A3b4: two passes.  1: the GPU's two lines copied as they are (a word = two
               sub-pixels; empty sub-pixels take the SH-2s' pixel), prefetched a line
               ahead every 8 words.  2: the console pixels that must show FB3A (HUD, the
               Mega Drive layer in front) written over, from the 1x arrays only.  A3b3
               decided per pixel with the GPU words in the loop: 5.2 ms a frame. */
            const u8 *keep = &g_keep[y * SW], *ix = &g_idx[y * SW];
            const u32 *g0 = gpu + (u32)(2 * y) * (GPU_PITCH_PX / 2u), *g1 = g0 + GPU_PITCH_PX / 2u;
            const u32 S2 = SENT565 | (SENT565 << 16);
            for (x = 0; x < SW; x++) {
                u32 q0, q1, t0, t1;
                if ((x & 7u) == 0u) { __builtin_prefetch(g0 + x + 64u); __builtin_prefetch(g1 + x + 64u); }
                q0 = g0[x]; q1 = g1[x];
                t0 = q0 ^ S2; t1 = q1 ^ S2;
                if ((((t0 - 0x00010001u) & ~t0) | ((t1 - 0x00010001u) & ~t1)) & 0x80008000u) {
                    u32 e = g_rixonly ? 0u : s[x];
                    if (!(t0 & 0xFFFFu)) q0 = (q0 & 0xFFFF0000u) | e;
                    if (!(t0 >> 16))     q0 = (q0 & 0x0000FFFFu) | (e << 16);
                    if (!(t1 & 0xFFFFu)) q1 = (q1 & 0xFFFF0000u) | e;
                    if (!(t1 >> 16))     q1 = (q1 & 0x0000FFFFu) | (e << 16);
                }
                d0[x] = be ? swap16x2(q0) : q0;
                d1[x] = be ? swap16x2(q1) : q1;
            }
            for (x = 0; x < SW; x++) {
                u32 p = s[x];
                if (keep[x] || (g_fbref && p != pal[ix[x]])) {
                    u32 a = p | (p << 16);
                    if (be) a = swap16x2(a);
                    d0[x] = a; d1[x] = a;
                } else if (first) {
                    u32 o0 = g0[x], o1 = g1[x];
                    u32 n = ((o0 & 0xFFFFu) != SENT565) + ((o0 >> 16) != SENT565) + ((o1 & 0xFFFFu) != SENT565) + ((o1 >> 16) != SENT565);
                    *cmp += n; *empty += 4u - n;
                    *same += ((o0 & 0xFFFFu) == p) + ((o0 >> 16) == p) + ((o1 & 0xFFFFu) == p) + ((o1 >> 16) == p);
                }
            }
        } else if (be) {
            for (x = 0; x < SW; x++) { u32 p = s[x], a = swap16x2(p | (p << 16)); d0[x] = a; d1[x] = a; }
        } else {
            for (x = 0; x < SW; x++) { u32 p = s[x], a = p | (p << 16); d0[x] = a; d1[x] = a; }
        }
    }
}

static void publish2x_32(const u16 *src, u32 *dst, u32 stride, u32 loffs, u32 lines,
                         int gpu_ok, int first, u32 *cmp, u32 *same, u32 *empty)
{
    const u32 black = 0xFF000000u;
    const u16 *pal = Pico32xMem->pal_native;
    const u16 *gpu = (const u16 *)gpu_pixels(g_cur_buf);
    u32 r;
    for (r = 0; r < 240u; r++) {
        u32 *d0 = dst + 2u * r * stride, *d1 = d0 + stride, x;
        const u16 *s;
        int y;
        if (r < loffs || r >= loffs + lines) {
            for (x = 0; x < 640u; x++) { d0[x] = black; d1[x] = black; }
            continue;
        }
        s = src + r * SW;
        y = (int)(r - loffs);
        if (gpu_ok && y >= g_ov_top && y <= g_ov_bottom) {
            const u8 *keep = &g_keep[y * SW], *ix = &g_idx[y * SW];
            const u16 *g0 = gpu + (u32)(2 * y) * GPU_PITCH_PX, *g1 = g0 + GPU_PITCH_PX;
            for (x = 0; x < SW; x++) {
                u16 p = s[x];
                u32 a = ARGB(p);
                if ((x & 15u) == 0u) { __builtin_prefetch(g0 + 2u * x + 64u); __builtin_prefetch(g1 + 2u * x + 64u); }
                if (!keep[x] && (!g_fbref || p == pal[ix[x]])) {
                    u16 q0 = g0[2u * x], q1 = g0[2u * x + 1u], q2 = g1[2u * x], q3 = g1[2u * x + 1u];
                    u32 e = g_rixonly ? black : a;
                    d0[2u * x]      = q0 == SENT565 ? e : ARGB(q0);
                    d0[2u * x + 1u] = q1 == SENT565 ? e : ARGB(q1);
                    d1[2u * x]      = q2 == SENT565 ? e : ARGB(q2);
                    d1[2u * x + 1u] = q3 == SENT565 ? e : ARGB(q3);
                    if (first) {
                        *cmp += (q0 != SENT565) + (q1 != SENT565) + (q2 != SENT565) + (q3 != SENT565);
                        *same += (q0 == p) + (q1 == p) + (q2 == p) + (q3 == p);
                        *empty += (q0 == SENT565) + (q1 == SENT565) + (q2 == SENT565) + (q3 == SENT565);
                    }
                } else {
                    d0[2u * x] = a; d0[2u * x + 1u] = a; d1[2u * x] = a; d1[2u * x + 1u] = a;
                }
            }
        } else {
            for (x = 0; x < SW; x++) {
                u32 a = ARGB(s[x]);
                d0[2u * x] = a; d0[2u * x + 1u] = a; d1[2u * x] = a; d1[2u * x + 1u] = a;
            }
        }
    }
}

/* A7a: the rows of the 3D written by the PL330 from the GPU buffer, the others by
   Core1 as before.  If the PL330 fails, Core1 copies those rows itself (byte-swapped)
   and A7's copy stays off for the rest of the run. */
/* the P96 buffers Core1 wrote through its caches (outer write-back: L2) since their
   last PL330 copy: their 3D lines are cleaned and invalidated in L2 once, so that no
   dirty line of an earlier picture is evicted over the PL330's pixels */
static u32 g_xbase[4]; static u8 g_xdirty[4]; static int g_nx;
static int xslot(const void *dst)
{
    u32 b = (u32)(uintptr_t)dst; int k;
    for (k = 0; k < g_nx; k++) if (g_xbase[k] == b) return k;
    if (g_nx < 4) { g_xbase[g_nx] = b; g_xdirty[g_nx] = 1; return g_nx++; }
    return -1;
}
/* A8c: the 3D lines copied by Core1 (no PL330): from the GPU buffer (no line of it in
   L1: the prefill and the HUD's lines were cleaned and invalidated) to the P96 buffer,
   which publish_p96 then cleans */
static void core1_copy_rows(u32 *dst, u32 stride, u32 loffs, u32 first, u32 last, int be)
{
    const u32 *g = buf_words(g_cur_buf);
    u32 r, x;
    for (r = first; r <= last && r < loffs + 224u; r++) {
        const u32 *g0 = g + 2u * (r - loffs) * (GPU_PITCH_PX / 2u), *g1 = g0 + GPU_PITCH_PX / 2u;
        u32 *d0 = dst + 2u * r * stride, *d1 = d0 + stride;
        if (be) {
            for (x = 0; x < SW; x += 4) {
                u32 a0 = g0[x], a1 = g0[x + 1], a2 = g0[x + 2], a3 = g0[x + 3];
                d0[x] = swap16x2(a0); d0[x + 1] = swap16x2(a1); d0[x + 2] = swap16x2(a2); d0[x + 3] = swap16x2(a3);
            }
            for (x = 0; x < SW; x += 4) {
                u32 a0 = g1[x], a1 = g1[x + 1], a2 = g1[x + 2], a3 = g1[x + 3];
                d1[x] = swap16x2(a0); d1[x + 1] = swap16x2(a1); d1[x + 2] = swap16x2(a2); d1[x + 3] = swap16x2(a3);
            }
        } else {
            memcpy(d0, g0, SW * 4u);
            memcpy(d1, g1, SW * 4u);
        }
    }
}

static void publish2x_direct(const u16 *src, u32 *dst, u32 pitch_b, u32 loffs, int be)
{
    u32 stride = pitch_b >> 2, r, x, c0;
    u32 first = loffs + (u32)g_ov_top, last = loffs + (u32)g_ov_bottom;
    u32 off = 2u * (u32)g_ov_top * GPU_PITCH_PX * 2u, bytes = 2u * (u32)(g_ov_bottom - g_ov_top + 1) * GPU_PITCH_PX * 2u;
    int rc, k = xslot(dst);
#if defined(__arm__)
    if (k < 0 || g_xdirty[k]) l2_clean_inval((u32)(uintptr_t)(dst + 2u * first * stride), bytes);
#endif
    if (k >= 0) g_xdirty[k] = 0;
    if (g_core1_copy) {
        /* A8c: no PL330: Core1 copies, publish_p96 cleans the whole buffer */
        c0 = cyc();
        buf_inval(g_cur_buf, 2u * (u32)g_ov_top, 2u * (u32)(g_ov_bottom - g_ov_top + 1));   /* no line the prefetcher may have read */
        core1_copy_rows(dst, stride, loffs, first, last, be);
        for (r = 0; r < 240u; r++) {
            u32 *d0 = dst + 2u * r * stride, *d1 = d0 + stride;
            const u16 *s;
            if (r >= first && r <= last && r < loffs + 224u) continue;
            if (r < loffs || r >= loffs + 224u) { for (x = 0; x < SW; x++) { d0[x] = 0u; d1[x] = 0u; } }
            else {
                s = src + r * SW;
                if (be) { for (x = 0; x < SW; x++) { u32 p = s[x], a = swap16x2(p | (p << 16)); d0[x] = a; d1[x] = a; } }
                else    { for (x = 0; x < SW; x++) { u32 p = s[x], a = p | (p << 16); d0[x] = a; d1[x] = a; } }
            }
        }
        if (k >= 0) g_xdirty[k] = 1;          /* written through the caches */
        g_cleaned = 0;
        stat_add(ZZVR_SH_A7_DMA_US_SUM, ZZVR_SH_A7_DMA_US_MAX, (cyc() - c0) / CYC_PER_US);
        return;
    }
    rc = copy_start(g_cur_buf, off, dst + 2u * first * stride, bytes, be);
    if (rc) g_sh[ZZVR_SH_A7_DMA_ERR] = (u32)rc;
    for (r = 0; r < 240u; r++) {
        u32 *d0 = dst + 2u * r * stride, *d1 = d0 + stride;
        const u16 *s;
        if (r >= first && r <= last && r < loffs + 224u) continue;
        if (r < loffs || r >= loffs + 224u) { for (x = 0; x < SW; x++) { d0[x] = 0u; d1[x] = 0u; } }
        else {
            s = src + r * SW;
            if (be) { for (x = 0; x < SW; x++) { u32 p = s[x], a = swap16x2(p | (p << 16)); d0[x] = a; d1[x] = a; } }
            else    { for (x = 0; x < SW; x++) { u32 p = s[x], a = p | (p << 16); d0[x] = a; d1[x] = a; } }
        }
#if defined(__arm__)
        {   /* A7b: only the lines Core1 wrote are cleaned (publish_p96 cleaned all 480: 0.4 ms) */
            u32 a = (u32)(uintptr_t)d0, e = a + 2u * pitch_b;
            for (; a < e; a += 32u) __asm__ volatile("mcr p15,0,%0,c7,c10,1"::"r"(a):"memory");
        }
#endif
    }
#if defined(__arm__)
    __asm__ volatile("dsb":::"memory");
#endif
    g_cleaned = 1;
    c0 = cyc();
    if (!rc) rc = copy_wait();
    stat_add(ZZVR_SH_A7_DMA_US_SUM, ZZVR_SH_A7_DMA_US_MAX, (cyc() - c0) / CYC_PER_US);
    if (rc) {
        const u32 *g = buf_words(g_cur_buf);
        g_sh[ZZVR_SH_A7_DMA_ERR] = (u32)rc;
        g_sh[ZZVR_SH_A7_STATE] |= 2u;
        g_direct_ok = 0;                      /* the next images go the A6c way */
        g_cleaned = 0;                        /* publish_p96 cleans everything */
        (void)g;
        core1_copy_rows(dst, stride, loffs, first, last, be);
    }
}

void zzvr_rix_publish2x(const u16 *src, void *dst, u32 pitch_b, u32 loffs, u32 lines)
{
    int gpu_ok = g_on && g_ov_valid, first = gpu_ok && g_ov_first;
    u32 c0 = cyc(), compared = 0, same = 0, empty = 0, fmt = g_sh ? g_sh[ZZVR_SH_OUT_FMT] : 0u;
    if (gpu_ok && g_cur_direct && (fmt == 1u || fmt == 2u) && pitch_b == GPU_PITCH_PX * 2u && lines == 224u) {
        publish2x_direct(src, (u32 *)dst, pitch_b, loffs, fmt == 2u);
        g_ov_first = 0;
        if (g_sh[ZZVR_SH_A7_STATE] & 2u) { int k = xslot(dst); if (k >= 0) g_xdirty[k] = 1; }
        g_sh[ZZVR_SH_RIX_COMP_FRAMES]++;
        stat_add(ZZVR_SH_RIX_COMP_US_SUM, ZZVR_SH_RIX_COMP_US_MAX, (cyc() - c0) / CYC_PER_US);
        return;
    }
    if (first && (fmt == 1u || fmt == 2u) && (g_sh[ZZVR_SH_RIX_PICTURES] & 3u)) { first = 0; g_ov_first = 0; }   /* A3b4: 16-bit stats on 1 picture in 4 */
    { int k = xslot(dst); if (k >= 0) g_xdirty[k] = 1; }     /* A7a: Core1 wrote it through L2 */
    if (fmt == 1u || fmt == 2u)
        publish2x_16(src, (u32 *)dst, pitch_b >> 2, loffs, lines, fmt == 2u, gpu_ok, first, &compared, &same, &empty);
    else
        publish2x_32(src, (u32 *)dst, pitch_b >> 2, loffs, lines, gpu_ok, first, &compared, &same, &empty);
    if (!gpu_ok) return;
    if (first) {
        g_ov_first = 0;
        if (!g_fbref) { compared = same = 0; }          /* A6c: the SH-2s drew nothing to compare with */
        g_sh[ZZVR_SH_RIX_PIX] += compared;
        g_sh[ZZVR_SH_RIX_PIX_SAME] += same;
        g_sh[ZZVR_SH_RIX_NOPIX] += empty;
        if (compared) {
            u32 permyriad = (u32)(((unsigned long long)same * 10000u) / compared);
            if (permyriad < g_sh[ZZVR_SH_RIX_LOWEST]) { g_sh[ZZVR_SH_RIX_LOWEST] = permyriad; g_sh[ZZVR_SH_RIX_LOWEST_FRAME] = (u32)g_frame; }
        }
    }
    g_sh[ZZVR_SH_RIX_COMP_FRAMES]++;
    stat_add(ZZVR_SH_RIX_COMP_US_SUM, ZZVR_SH_RIX_COMP_US_MAX, (cyc() - c0) / CYC_PER_US);
}

/* ---- every frame, after PicoFrame and before the conversion to P96 ------------- */
void zzvr_rix_composite(u16 *fb3a, u32 loffs)
{
    u32 c0, compared = 0, same = 0, empty = 0;
    int y;
    const u16 *pal;
    g_frame++;
    g_fb3a = fb3a; g_loffs = loffs;      /* A7a: the prefill reads it at the end of the frame */
    if (!g_on) return;
    if (g_sh) {                          /* A8e: the screen's format, as the launcher wrote it */
        u32 fmt = g_sh[ZZVR_SH_OUT_FMT];
        int ok = (fmt == 1u || fmt == 2u);
        if (ok != g_fmt_ok && g_direct_ok) {
            g_fmt_ok = ok;
            if (ok) g_sh[ZZVR_SH_A7_STATE] &= ~8u; else g_sh[ZZVR_SH_A7_STATE] |= 8u;
        }
        if (!g_direct_ok) g_fmt_ok = ok;
    }
    if (g_invalidate_next) { g_ov_valid = 0; g_invalidate_next = 0; }
    if (g_scale == 2) {                 /* A3b: the merge is done by zzvr_rix_publish2x() */
        if (g_pending) finish_pending2x();
        return;
    }
    if (g_pending) finish_pending(1);
    if (!g_ov_valid || !g_on) return;
    c0 = cyc();
    pal = Pico32xMem->pal_native;
    for (y = g_ov_top; y <= g_ov_bottom; y++) {
        u16 *row = fb3a + ((u32)y + loffs) * SW;
        const u16 *ov = &g_ov[y * SW];
        const u8 *fl = &g_fl[y * SW], *ix = &g_idx[y * SW];
        int x;
        for (x = 0; x < SW; x++) {
            u8 f = fl[x];
            if (!f) continue;
            if (g_fbref && row[x] != pal[ix[x]]) continue;   /* the Mega Drive layer is in front here */
            if (f == 1u) {
                compared++;
                same += ov[x] == row[x];
                row[x] = ov[x];
            } else {
                empty++;
                row[x] = 0u;
            }
        }
    }
    if (g_ov_first) {
        u32 permyriad;
        g_ov_first = 0;
        if (!g_fbref) { compared = same = 0; }          /* A6c */
        g_sh[ZZVR_SH_RIX_PIX] += compared;
        g_sh[ZZVR_SH_RIX_PIX_SAME] += same;
        g_sh[ZZVR_SH_RIX_NOPIX] += empty;
        if (compared) {
            permyriad = (u32)(((unsigned long long)same * 10000u) / compared);
            if (permyriad < g_sh[ZZVR_SH_RIX_LOWEST]) { g_sh[ZZVR_SH_RIX_LOWEST] = permyriad; g_sh[ZZVR_SH_RIX_LOWEST_FRAME] = (u32)g_frame; }
        }
    }
    g_sh[ZZVR_SH_RIX_COMP_FRAMES]++;
    stat_add(ZZVR_SH_RIX_COMP_US_SUM, ZZVR_SH_RIX_COMP_US_MAX, (cyc() - c0) / CYC_PER_US);
}

#if !defined(__arm__)
/* host bench: the overlay as it is */
const u16 *zzvr_rix_host_overlay(const u8 **flags) { *flags = g_fl; return g_ov; }
#endif
