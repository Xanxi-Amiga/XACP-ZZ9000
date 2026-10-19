/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2rix_gpu.c - ZZQuake2RIX: the RasterIX side of the adapter.
 *
 * This file sees librixgpu and ZZGL, and nothing of Quake II.  It owns
 * the GPU: the MMU attributes (zzq2rix_mmu.c), the librixgpu init
 * ladder, the chunk ring, the texture pool, the ZZGL context, the two
 * colour buffers of the G4B window and the frame cycle:
 *
 *     GLimp_BeginFrame  zz_rix_begin_frame   target buffer + fence value
 *     ref_gl draws      ZZGL -> librixgpu    chunks submitted as they fill
 *     GLimp_EndFrame    zz_rix_end_frame     frame fence, wait, present
 *
 * Presentation: the GPU's 640x481 RGB565 buffer is handed to
 * zzq2_present_rgb565() (zzq2_glue.c), which converts it into the P96
 * buffer the launcher granted, with the release handshake.  The
 * launcher (ZZQuake2_A0) is not changed.
 *
 * 0.10, frame size of the build (zzq2_res.h, ZZQ2_RES in build_arm.sh):
 *     640 x 480  zzq2rix.bin      640 x 481 buffers (1.3.2 lists, unchanged)
 *     320 x 240  zzq2rix_320.bin  the SAME 640 x 481 buffers and GPU
 *                                 registers; ref_gl's viewport is 320 x 240
 *                                 (lines 240..479), only those are shown
 *     800 x 600  zzq2rix_800.bin  800 x 601 buffers (librixgpu 1.4
 *                                 rix_set_geometry, after the ladder, which
 *                                 still runs at 640 x 481), FB0 0x06600000,
 *                                 FB1 0x066EAD00 (ends 0x067D5940, below the
 *                                 firmware staging 0x067F0000), Z-buffer
 *                                 800 x 601 in its 1 MiB.  Never run on the
 *                                 G4C before 0.10.
 *
 * Memory (ZZQUAKE2RIX_MEMORY_MAP.md v2, 14/10):
 *     0x3D800000  16 MiB  texture pool   inner WB / outer NC (uploads cleaned)
 *     0x3E800000  512 K   chunk ring     NC, 8 x 64 KiB
 *     0x3E880000  1 MiB   Z-buffer       NC
 *     0x3E980000  1 KiB   bind lists     NC (BINDLIST)
 *     window      colour buffers FB0/FB1 (NC), ladder scratch at init
 * The host build (ZZQ2RIX_HOST, test bench against the GPU model) puts
 * the same objects in the model's 16 MiB arena.
 *
 * Transport: DDR lists (FSC MEM->ST1), binds as short DDR lists
 * (BINDLIST, rule 11 kept) when ladder L15 passed, FIFO packets
 * otherwise.  Never a bind inside a long chunk (D2e).  No PL330, no WFI
 * (the release owns the interrupt controller): spin waits.
 *
 * Errors: a failed init or a lost frame fence ends the run through the
 * release Sys_Error with the librixgpu code (a lost fence means a hung
 * GPU: power cycle before the next run).
 *
 * ASCII only.
 */

#include <string.h>
#include "rixgpu.h"
#include "rixframe.h"
#include "rixtex.h"
#include "rixdesc.h"
#include "rix_diag.h"
#include "rix_window.h"
#include "zzgl.h"
#include "zzq2rix_mmu.h"
#include "zzq2_res.h"         /* ZZQ2_VID_W x ZZQ2_VID_H: the size of this blob (0.10) */

/* release / glue */
extern void Sys_Error(char *error, ...);
extern void zzq2_present_rgb565(const unsigned short *pixels, unsigned pitch_bytes);
extern unsigned zzq2_last_pak_end(void);
extern void zzq_s2_begin(int p);
extern void zzq_s2_end(int p);

/* ---- placement --------------------------------------------------------- */
#ifdef ZZQ2RIX_HOST
#define RIXQ2_POOL_BASE   0x30400000u
#define RIXQ2_POOL_PAGES  4096u            /* 8 MiB inside the model's arena */
#define RIXQ2_POOL_CACHE  0
#define RIXQ2_RING_BASE   0x30C00000u
#define RIXQ2_ZBUF        0x30C80000u
#define RIXQ2_BIND_BASE   0x30D80000u
#else
#define RIXQ2_POOL_BASE   0x3D800000u
#define RIXQ2_POOL_PAGES  8192u            /* 16 MiB */
#define RIXQ2_POOL_CACHE  1
#define RIXQ2_RING_BASE   0x3E800000u
#define RIXQ2_ZBUF        0x3E880000u
#define RIXQ2_BIND_BASE   0x3E980000u
#endif
/* 0.9.2: the same 512 KiB as 8 x 64 KiB (default) or 32 x 16 KiB (rix_ring 1, read at the first
   GLimp_SetMode: archived, takes effect at the next start) */
static u32 g_ring_slots = 8u, g_chunk_bytes = 65536u;
static int g_open;
void zz_rix_set_ring(int mode) { if (g_open) return; if (mode == 1) { g_ring_slots = 32u; g_chunk_bytes = 16384u; } else { g_ring_slots = 8u; g_chunk_bytes = 65536u; } }
#define RIXQ2_RING_SLOTS  g_ring_slots
#define RIXQ2_CHUNK_BYTES g_chunk_bytes
#define RIXQ2_RESERVE_LO  0x3D800000u      /* HEAP_LIMIT of the adapter */
#define RIXQ2_RESERVE_HI  0x3EC00000u

/* ---- state --------------------------------------------------------------- */
static volatile u32 g_rs[RS_COUNT];
static volatile u32 g_sd[SD_COUNT];
static volatile u32 g_caps_out, g_timer_hz;
static u32 g_probe_desc[RIX_TRI_DESC_WORDS];
/* 0.10: the GPU frame geometry of the build (the 320 build draws inside the 640 x 481 buffers) */
#if ZZQ2_VID_W > 640
#define RIXQ2_GEO_W       ((u32)ZZQ2_VID_W)
#define RIXQ2_GEO_H       ((u32)ZZQ2_VID_H)
#define RIXQ2_FB_STRIDE   ((RIXQ2_GEO_W * (RIXQ2_GEO_H + 1u) * 2u + 255u) & ~255u)
static const u32 g_color_bufs[2] = { RIX_FB0_ARM, RIX_FB0_ARM + RIXQ2_FB_STRIDE };
#else
#define RIXQ2_GEO_W       RIX_W
#define RIXQ2_GEO_H       RIX_H
#define RIXQ2_FB_STRIDE   RIX_FB_STRIDE
static const u32 g_color_bufs[2] = { RIX_FB0_ARM, RIX_FB1_ARM };
#endif
#define RIXQ2_ZBUF_BYTES  0x00100000u      /* Z-buffer room (bind lists above) */
/* first byte of the picture shown (ref_gl's top line): the window is at the bottom of the buffer in
   RasterIX coordinates (y up, line 0 = top), so it starts (h_alloc - 1 - height) lines down */
#define RIXQ2_SHOW_OFF    ((RIXQ2_GEO_H - (u32)ZZQ2_VID_H) * RIXQ2_GEO_W * 2u)
#define RIXQ2_PITCH       (RIXQ2_GEO_W * 2u)
/* 0.10: the span of the two colour buffers, for the glue's check of the P96 buffers (zzq2_glue.c) */
void zz_rix_buffers(unsigned *lo, unsigned *hi) { *lo = g_color_bufs[0]; *hi = g_color_bufs[1] + RIXQ2_FB_STRIDE; }
static zz_mmu_report g_mmu;
static int g_bind_mode, g_init_rc;   /* g_open: above (0.9.2) */
static u32 g_cur, g_fence_seq, g_frames, g_frames_dropped;
static u32 g_t_gpu_us, g_t_present_us, g_t_gpu_max_us;
/* 0.7: run totals for the performance report (zz_rix_perf_report, baseq2/rixstat.txt) */
static u32 g_begin_tick;
/* 0.8.2: draw batches by category (zz_rix_cat_begin/end, called by zzq2rix_qgl.c around each
   glBegin..glEnd; the category comes from the ref_gl function being run, zzq2_glue.c, and from
   the blend state of the lightmap pass) */
#define ZZ_NCAT 7
static const char *const g_cat_name[ZZ_NCAT] = {
    "  world      ", "  lightmaps  ", "  bmodels    ", "  models     ", "  water/glass", "  other 3D   ", "  2D         " };
typedef struct { u32 batches, tris, binds, wall_us, begin_us, end_us, bwait_us; } zz_catsum;
/* every field a u32: perf_add() adds two records word by word */
typedef struct {
    u32 frames, ref_us, swap_us, end_us, flush_us, bind_us, bind_wait_us, ringwait_us, upload_us;
    u32 tris_in, tris_out, culled, clipped, begins, verts, binds, binds_skipped, uploads, subuploads, chunks, words, state_words;
    u32 gpu_us, present_us, wall_us;
    u32 desc_us, emit_us, end_bwait_us;     /* 0.8.3: zzglEnd split (desc/emit with rix_prof 1 only) */
    u32 dq_frames, dq_peak, dq_held, dq_wait_us, dq_tex_us, dq_end_us, dq_refused;   /* 0.9: deferred submission (rix_defer 1) */
    u32 sync_frames, sync_gpu_us, sync_wall_us;
    u32 tex_versions, tex_version_us, tex_version_fail;   /* 0.9.2: lightmap versions (rix_texver 1) */
    u32 dq_starved, idle_between_us, idle_frames;         /* 0.9.2: GPU found idle at a submission; GPU idle from the frame fence to the next frame's first chunk */
    u32 overlap_frames;                                   /* 0.9.3: frames closed with rix_overlap 1 */           /* 0.9.1: rix_sync 1 (every chunk and bind waited at once: Core1 waits = GPU time) */
    zz_catsum cat[ZZ_NCAT];
} zz_perf;
static zz_perf g_pt;        /* the run (baseq2/rixstat.txt) */
static zz_perf g_seg;       /* 0.8.2: the timedemo under way (baseq2/rixtd.txt) */
static void perf_add(zz_perf *d, const zz_perf *a)
{
    u32 i, *dw = (u32 *)d;
    const u32 *aw = (const u32 *)a;
    for (i = 0; i < (u32)(sizeof(zz_perf) / sizeof(u32)); i++) dw[i] += aw[i];
}
/* current frame, per category, in timer ticks (converted once per frame) */
static struct { u32 batches, tris, binds, wall, begin, end, bwait_us; } g_cf[ZZ_NCAT];
static u32 c_t0, c_flush, c_end, c_tris, c_binds, c_bwait;
/* 0.8.2: timedemo segments */
extern volatile u32 zzq2_world_count;      /* zzq2_glue.c: calls of R_DrawWorld so far */
static u32 g_world_at_begin, g_last_end_tick, g_have_last_end;
static int g_td;                           /* cvar timedemo, given every frame by zzq2rix_video.c */
static int g_td_armed;                     /* a map was loaded while timedemo was set */
static int g_regseq, g_regseq_seen;        /* ref_gl's registration_sequence: one more per map load */
static char g_td_text[32768];
static u32 g_td_count, g_td_full;
static int g_td_new;
static u32 g_dq_peak_max;                  /* 0.9 */
static u32 g_fence_done_tick, g_have_fence_done;   /* 0.9.2 */
/* 0.9.3: rix_overlap 1 = frame N-1 is presented at the end of frame N, while the GPU still draws N; frame N
   is then closed without waiting for its fence and stays pending until the end of N+1.  N+1 draws into the
   buffer of N-1, presented by then.  The single Z-buffer is safe: the GPU runs everything in order, so N+1's
   depth clear comes after the last chunk of N.  No bind of N is in the GPU when N+1 begins (the end of N
   drains the queue: its last chunk is submitted only once the last bind has passed). */
extern void (*zzq2_present_hook)(void);
static u32 fence_pixel(u32 buf);
static int g_overlap_req, g_pend;
static u32 g_pend_buf, g_pend_fence, g_overlap_frames;
void zz_rix_set_overlap(int on) { g_overlap_req = on ? 1 : 0; }
static void pend_present(void)
{
    int rc;
    if (!g_pend) return;
    rc = rix_frame_wait(g_pend_buf, g_pend_fence, 2000000u, 0, 0);
    if (rc) {
        rix_frame_diag_last();
        Sys_Error("RIX E%04x pending fence %04x pix %04x fr %u", (unsigned)rc & 0xFFFFu, g_pend_fence, fence_pixel(g_pend_buf), g_frames);
    }
    zzq2_present_hook = zzgl_frame_open() ? zzgl_pump : 0;   /* keep the GPU fed while Core1 converts */
    zzq2_present_rgb565((const unsigned short *)rix_ptr(g_pend_buf + RIXQ2_SHOW_OFF), RIXQ2_PITCH);
    zzq2_present_hook = 0;
    g_pend = 0;
}
/* end of run: the GPU idle (the pending frame is not shown) */
static void pend_wait(void)
{
    if (!g_pend) return;
    (void)rix_frame_wait(g_pend_buf, g_pend_fence, 2000000u, 0, 0);
    g_pend = 0;
}
static int g_sync_req, g_sync_on;          /* 0.9.1: rix_sync */
void zz_rix_set_sync(int on) { g_sync_req = on ? 1 : 0; }
static u32 g_frame_fence, g_frame_buf;

/* a 256x256 RGB565 pattern for the ladder's texture stages (L11..L15) */
static void probe_texture_fill(u32 base)
{
    volatile u16 *p = (volatile u16 *)rix_ptr(base);
    u32 x, y;
    for (y = 0; y < 256u; y++)
        for (x = 0; x < 256u; x++)
            p[y * 256u + x] = (u16)(((x & 31u) << 11) | ((y & 63u) << 5) | ((x ^ y) & 31u));
}

static int probe_triangle(void)
{
    rix_vertex a, b, c;
    a.sx = 200.0f; a.sy = 151.0f; a.z = 0.5f; a.invw = 1.0f; a.u = 0.0f; a.v = 0.0f; a.r = a.g = a.b = a.a = 1.0f;
    b = a; b.sx = 440.0f; b.sy = 151.0f; b.u = 1.0f; b.v = 0.0f;
    c = a; c.sx = 320.0f; c.sy = 401.0f; c.u = 0.5f; c.v = 1.0f;
    return rix_tri_desc(&a, &b, &c, g_probe_desc);
}

static void why_set(char *why, int cap, const char *s)
{
    int i;
    if (!why || cap <= 0) return;
    for (i = 0; i < cap - 1 && s[i]; i++) why[i] = s[i];
    why[i] = 0;
}

/* ---- open ------------------------------------------------------------------ */
int zz_rix_open(int w, int h, char *why, int why_cap)
{
    rix_config cfg;
    rix_ring_cfg rc;
    zzgl_config gc;
    int rc_i;
    u32 caps;

    if (g_open) return 1;
    if (w != ZZQ2_VID_W || h != ZZQ2_VID_H) { why_set(why, why_cap, "not the size of this blob"); return 0; }
    /* 0.10: the buffers of the build stay in the window, below the firmware staging; the Z-buffer in its room */
    if (g_color_bufs[1] + RIXQ2_FB_STRIDE > RIX_STAGE_ARM || RIXQ2_GEO_W * (RIXQ2_GEO_H + 1u) * 2u > RIXQ2_ZBUF_BYTES)
        Sys_Error("RIX layout: FB1 end %08x, Z %u bytes", g_color_bufs[1] + RIXQ2_FB_STRIDE, RIXQ2_GEO_W * (RIXQ2_GEO_H + 1u) * 2u);

    /* 1. the reservation under .q2bss must be free of engine data: the
          heap limit is enforced by the adapter before any allocation
          (zzq2_glue.c); checked here once more against the real pak end */
    if (zzq2_last_pak_end() > RIXQ2_RESERVE_LO)
        Sys_Error("RIX pak end %08x above GPU reservation %08x", zzq2_last_pak_end(), RIXQ2_RESERVE_LO);

    /* 2. MMU attributes (checked, maintained, verified: zzq2rix_mmu.c) */
    rc_i = zzq2rix_mmu_setup(&g_mmu);
    if (rc_i)   /* the launcher shows 60 characters: the values first */
        Sys_Error("RIX MMU e%d ttbr0 %08x sctlr %08x i%03x d%08x ttbcr %x",
                  rc_i, g_mmu.ttbr0, g_mmu.sctlr, g_mmu.bad_index, g_mmu.bad_desc, g_mmu.ttbcr);

    /* 3. librixgpu init: ID, FIFO, ladder L0..L15 (textured stages with a
          pattern in the window's texture area), no PL330, no WFI */
    memset(&cfg, 0, sizeof cfg);
    cfg.flags = RIXF_SPIN | RIXF_NODMA | RIXF_NOSWAP;
    cfg.idle_tick_us = 100u;
    cfg.stats = g_rs; cfg.sd = g_sd; cfg.caps_out = &g_caps_out; cfg.timer_hz_out = &g_timer_hz;
    cfg.dma_prog = RIX_DL1_ARM;                     /* required by the config check; never used (NODMA) */
    probe_texture_fill(RIX_TEX_ARM);
    if (probe_triangle()) {
        cfg.probe_tri = g_probe_desc; cfg.probe_x = 320u; cfg.probe_y = (RIX_H_ALLOC - 1u) - 230u;
        cfg.tmu_env = RIX_TMU_ENV_REPLACE; cfg.tmu_env_color = 0u;
        cfg.tmu_cfg = RIX_TMU_CFG_RGB565(8u, 8u, 1);
        cfg.tex_pages = 64u; cfg.tex_base = RIX_TEX_ARM;
    }
    g_init_rc = rix_init(&cfg);
    caps = rix_caps();
    if (!(caps & RIXCAP_ID))
        Sys_Error("RIX: no RasterIX (magic %08x ver %08x)", g_sd[SD_ID_MAGIC], g_sd[SD_ID_VERSION]);
    if (!(caps & RIXCAP_FENCE_MEM) || !(caps & RIXCAP_TEX) || !(caps & RIXCAP_TRI_DDR))
        Sys_Error("RIX init E%04x ladder L%u failed caps %04x (power cycle?)",
                  (unsigned)g_init_rc & 0xFFFFu, g_sd[SD_LADDER_FAILED] ? g_sd[SD_LADDER_FAILED] - 1u : 99u, caps);

    /* 3b. 0.10: the frame geometry of the build, after the ladder (640 x 481) and before the ring (which
          zeroes the fence pixels of the buffers on their fence line); nothing at all for 640 and 320 */
    if (RIXQ2_GEO_W != RIX_W || RIXQ2_GEO_H != RIX_H) {
        rc_i = rix_set_geometry(RIXQ2_GEO_W, RIXQ2_GEO_H);
        if (rc_i) Sys_Error("RIX geometry %ux%u E%04x", RIXQ2_GEO_W, RIXQ2_GEO_H, (unsigned)rc_i & 0xFFFFu);
    }

    /* 4. the chunk ring and the bind transport */
    memset(&rc, 0, sizeof rc);
    rc.ring_base = RIXQ2_RING_BASE; rc.ring_slots = RIXQ2_RING_SLOTS; rc.chunk_bytes = RIXQ2_CHUNK_BYTES;
    rc.fence_x = RIX_CHUNK_FENCE_X; rc.color_bufs = g_color_bufs; rc.color_count = 2u; rc.ring_cacheable = 0u;
    rc.bind_base = RIXQ2_BIND_BASE; rc.bind_cacheable = 0u;
    rc_i = rix_ring_init(&rc);
    if (rc_i) Sys_Error("RIX ring E%04x", (unsigned)rc_i & 0xFFFFu);
    g_bind_mode = (caps & RIXCAP_TEX_DDR) ? RIX_BIND_LIST : RIX_BIND_FIFO;
    rc_i = rix_frame_set_bind_mode(g_bind_mode);
    if (rc_i) { g_bind_mode = RIX_BIND_FIFO; (void)rix_frame_set_bind_mode(RIX_BIND_FIFO); }

    /* 5. ZZGL: pool, Z-buffer, the size of the build */
    memset(&gc, 0, sizeof gc);
    gc.pool_base = RIXQ2_POOL_BASE; gc.pool_pages = RIXQ2_POOL_PAGES; gc.pool_cacheable = RIXQ2_POOL_CACHE;
    gc.zbuf = RIXQ2_ZBUF; gc.width = (u32)ZZQ2_VID_W; gc.height = (u32)ZZQ2_VID_H; gc.ztrick = 0;
    rc_i = zzgl_init(&gc);
    if (rc_i) Sys_Error("RIX ZZGL init E%04x", (unsigned)rc_i & 0xFFFFu);

    g_cur = 0u; g_fence_seq = 0u; g_frames = 0u;
    g_open = 1;
    return 1;
}

/* ---- frames -------------------------------------------------------------- */
static u32 next_fence(void)
{
    g_fence_seq++;
    if (g_fence_seq == 0u || g_fence_seq > 65534u) g_fence_seq = 1u;
    return g_fence_seq;
}

static u32 g_stray_frames;

/* fence diagnostics for a lost frame fence: what the polled buffer and the other one hold at the
   frame-fence pixel (line 480, pixel 0) */
static u32 fence_pixel(u32 buf) { return *(volatile u16 *)rix_ptr(buf + rix_geo_fence_off); }

void zz_rix_begin_frame(void)
{
    zzgl_target t;
    if (!g_open) return;
    if (zzgl_frame_open()) {
        /* a draw or a clear reached ZZGL between the previous EndFrame and this BeginFrame: that
           frame was opened on the previous target.  Close it there, with a fence of its own, and
           wait for it; otherwise its fence would land on the other buffer than the one polled
           (ZZQuake2RIX 0.1, frame 980 at the end of demo1: "fence lost", GPU alive). */
        int rc;
        g_frame_fence = next_fence();
        t.color = g_frame_buf; t.fence = g_frame_fence;
        zzgl_set_target(&t);
        rc = zzgl_swap();
        if (rc) Sys_Error("RIX stray frame %u: submit E%04x", g_frames, (unsigned)rc & 0xFFFFu);
        rc = rix_frame_wait(g_frame_buf, g_frame_fence, 2000000u, 0, 0);
        if (rc) Sys_Error("RIX E%04x stray fence %04x lost fr %u pix %04x", (unsigned)rc & 0xFFFFu, g_frame_fence, g_frames, fence_pixel(g_frame_buf));
        g_stray_frames++;
        /* 0.9.3: the stray drew on the pending frame's buffer, after it: its fence (just waited) is now the one
           that buffer shows; present the buffer now, before the next frame can target it */
        if (g_pend && g_pend_buf == g_frame_buf) g_pend_fence = g_frame_fence;
        pend_present();
#ifdef ZZQ2RIX_HOST
        { extern int printf(const char *, ...); const zzgl_stats *st = zzgl_get_stats();
          printf("STRAY frame at rix frame %u: begins %u tris_in %u tris_out %u binds %u uploads %u\n", g_frames, st->begins, st->tris_in, st->tris_out, st->binds, st->tex_uploads); }
#endif
        g_cur ^= 1u;
    }
    g_frame_buf = g_color_bufs[g_cur];
    g_frame_fence = next_fence();
    t.color = g_frame_buf; t.fence = g_frame_fence;
    zzgl_set_target(&t);
    memset(g_cf, 0, sizeof g_cf);
    g_world_at_begin = zzq2_world_count;
    /* 0.9.1: SYNC mode switched here, between frames (the queue of the deferred mode is empty) */
    if (g_sync_req != g_sync_on) { rix_frame_set_sync(g_sync_req); g_sync_on = g_sync_req; }
    g_begin_tick = rix_now();
}

/* ---- 0.8.2: categories ----------------------------------------------------------------------------
   One batch = glBegin .. glEnd, state flush and bind included (they happen in zzglBegin).  The ZZGL
   and frame counters restart when a frame opens, which can happen inside zzglBegin: a batch that
   starts with no frame open counts from zero. */
void zz_rix_cat_begin(void)
{
    if (zzgl_frame_open()) {
        const zzgl_stats *st = zzgl_get_stats();
        const rix_frame *fr = zzgl_frame();
        c_flush = st->flush_ticks; c_end = st->end_ticks; c_tris = st->tris_in; c_binds = st->binds;
        c_bwait = fr->bind_wait_us + fr->dq_wait_us;
    } else c_flush = c_end = c_tris = c_binds = c_bwait = 0u;
    c_t0 = rix_now();
}
void zz_rix_cat_end(int c)
{
    const zzgl_stats *st = zzgl_get_stats();
    const rix_frame *fr = zzgl_frame();
    u32 t = rix_now();
    if (c < 0 || c >= ZZ_NCAT) c = ZZ_NCAT - 1;
    g_cf[c].batches++;
    g_cf[c].wall += t - c_t0;
    g_cf[c].begin += st->flush_ticks - c_flush;
    g_cf[c].end += st->end_ticks - c_end;
    g_cf[c].tris += st->tris_in - c_tris;
    g_cf[c].binds += st->binds - c_binds;
    g_cf[c].bwait_us += fr->bind_wait_us + fr->dq_wait_us - c_bwait;
}
void zz_rix_set_timedemo(int on, int regseq) { g_td = on; g_regseq = regseq; }

static void perf_format(char *out, const zz_perf *p);
static void u2s(char *d, u32 v);
static void cat(char *d, const char *s);
/* the timedemo segment is over: its report goes to the end of g_td_text */
static void td_close(void)
{
    static char one[4096];
    char n[16];
    u32 w10 = g_seg.wall_us / 10000u;              /* hundredths of a second */
    one[0] = 0;
    g_td_count++;
    cat(one, "timedemo "); u2s(n, g_td_count); cat(one, n);
    cat(one, ": frames "); u2s(n, g_seg.frames); cat(one, n);
    cat(one, ", Core1 time "); u2s(n, w10 / 100u); cat(one, n); cat(one, ".");
    u2s(n, (w10 / 10u) % 10u); cat(one, n); cat(one, " s, ");
    if (w10) { u32 f10 = g_seg.frames * 1000u / w10; u2s(n, f10 / 10u); cat(one, n); cat(one, "."); u2s(n, f10 % 10u); cat(one, n); }
    else cat(one, "-");
    cat(one, " fps (the console line of the engine is the reference)\n");
    perf_format(one + strlen(one), &g_seg);
    cat(one, "\n");
    if (strlen(g_td_text) + strlen(one) < sizeof(g_td_text)) cat(g_td_text, one);
    else g_td_full++;
    memset(&g_seg, 0, sizeof g_seg);
    g_td_new = 1;
}
/* zzq2rix_video.c: 1 when a segment closed since the last call; the text of all of them */
int zz_rix_td_take(void) { int r = g_td_new; g_td_new = 0; return r; }
const char *zz_rix_td_text(void) { return g_td_text; }
u32 zz_rix_td_lost(void) { return g_td_full; }
/* quit or vid_restart during a timedemo: close it as it stands */
void zz_rix_td_flush(void) { if (g_seg.frames) td_close(); }

void zz_rix_end_frame(void)
{
    int rc, overlap;
    u32 t0, t1, polls = 0u, t_ref, pres_us = 0u;
    zz_perf rec;
    if (!g_open) return;
    if (!zzgl_frame_open()) {                      /* nothing was drawn: nothing to show */
        g_frames_dropped++;
        return;
    }
    t_ref = rix_now();
    overlap = g_overlap_req && !g_sync_on;
    if (overlap) {                                 /* 0.9.3: frame N-1 shown while the GPU works on this one */
        pend_present();
        pres_us = rix_ticks_to_us((u32)(rix_now() - t_ref));
    }
    t0 = rix_now();
    rc = zzgl_swap();
    if (rc) Sys_Error("RIX frame %u: submit E%04x", g_frames, (unsigned)rc & 0xFFFFu);
    memset(&rec, 0, sizeof rec);
    {   /* 0.7: this frame's numbers (into the run totals below, 0.8.2: and the timedemo's) */
        const zzgl_stats *st = zzgl_get_stats();
        const rix_frame *fr = zzgl_frame();
        u32 t_sw = rix_now(), c;
        rec.frames = 1u;
        rec.ref_us = rix_ticks_to_us((u32)(t_ref - g_begin_tick));
        rec.swap_us = rix_ticks_to_us((u32)(t_sw - t0));
        rec.end_us = rix_ticks_to_us(st->end_ticks);
        rec.bind_us = rix_ticks_to_us(st->bind_ticks);
        rec.flush_us = rix_ticks_to_us(st->flush_ticks);
        rec.upload_us = st->upload_us;
        rec.tris_in = st->tris_in; rec.tris_out = st->tris_out; rec.culled = st->culled; rec.clipped = st->clipped;
        rec.begins = st->begins; rec.verts = st->verts; rec.binds = st->binds; rec.binds_skipped = st->binds_skipped;
        rec.uploads = st->tex_uploads; rec.subuploads = st->tex_subuploads; rec.state_words = st->state_words;
        rec.chunks = fr->chunks; rec.words = fr->words; rec.ringwait_us = fr->ringwait_us; rec.bind_wait_us = fr->bind_wait_us;
        rec.desc_us = rix_ticks_to_us(st->desc_ticks); rec.emit_us = rix_ticks_to_us(st->emit_ticks); rec.end_bwait_us = st->end_bwait_us;
        rec.tex_versions = st->tex_versions; rec.tex_version_us = st->tex_version_us; rec.tex_version_fail = st->tex_version_fail;
        rec.dq_starved = fr->dq_starved;
        if (g_have_fence_done && fr->first_submit_tick) {
            u32 idle = rix_ticks_to_us((u32)(fr->first_submit_tick - g_fence_done_tick));
            if (idle < 1000000u) { rec.idle_between_us = idle; rec.idle_frames = 1u; }
        }
        if (g_sync_on) { rec.sync_frames = 1u; rec.sync_gpu_us = fr->ringwait_us + fr->bind_wait_us; }
        if (rix_frame_deferred()) {
            rec.dq_frames = 1u; rec.dq_peak = fr->dq_peak; rec.dq_held = fr->dq_held; rec.dq_wait_us = fr->dq_wait_us;
            rec.dq_tex_us = fr->dq_tex_us; rec.dq_end_us = fr->dq_end_us; rec.dq_refused = fr->dq_refused;
            if (fr->dq_peak > g_dq_peak_max) g_dq_peak_max = fr->dq_peak;
        }
        for (c = 0; c < ZZ_NCAT; c++) {
            rec.cat[c].batches = g_cf[c].batches; rec.cat[c].tris = g_cf[c].tris; rec.cat[c].binds = g_cf[c].binds;
            rec.cat[c].wall_us = rix_ticks_to_us(g_cf[c].wall); rec.cat[c].begin_us = rix_ticks_to_us(g_cf[c].begin);
            rec.cat[c].end_us = rix_ticks_to_us(g_cf[c].end); rec.cat[c].bwait_us = g_cf[c].bwait_us;
        }
        t0 = rix_now();                            /* the GPU wait below starts here */
    }
    if (overlap) {
        /* 0.9.3: this frame stays pending; no fence wait, no presentation here */
        u32 t2 = rix_now(), period;
        g_pend = 1; g_pend_buf = g_frame_buf; g_pend_fence = g_frame_fence;
        g_have_fence_done = 0u; g_overlap_frames++;
        rec.overlap_frames = 1u;
        rec.present_us = pres_us;
        g_t_present_us += pres_us;
        period = g_have_last_end ? rix_ticks_to_us((u32)(t2 - g_last_end_tick)) : 0u;
        g_last_end_tick = t2; g_have_last_end = 1u;
        rec.wall_us = (period < 1000000u) ? period : 0u;
        goto accounted;
    }
    /* the GPU wait is reported as the release profile step TEX (rasterisation) */
    zzq_s2_begin(2);
    rc = rix_frame_wait(g_frame_buf, g_frame_fence, 2000000u, 0, &polls);
    zzq_s2_end(2);
    t1 = rix_now();
    g_fence_done_tick = t1; g_have_fence_done = 1u;   /* 0.9.2: the GPU has nothing more from here */
    if (rc) {
        /* the frame fence never showed: pix = what the polled buffer holds at the fence pixel,
           oth = the other buffer, ch = chunks of the frame, cs = last chunk fence seq / pixel,
           alive 1 = the parser still takes NOPs (fence lost, not a hang), 2 = hung (power cycle) */
        rix_frame_diag_last();
        Sys_Error("RIX E%04x f%04x pix %04x oth %04x ch %u cs %04x/%04x al %u fr %u",
                  (unsigned)rc & 0xFFFFu, g_frame_fence, fence_pixel(g_frame_buf), fence_pixel(g_color_bufs[g_cur ^ 1u]),
                  g_sd[SD_FD_CHUNKS], g_sd[SD_FD_LAST_CSEQ], g_sd[SD_FD_CSEQ_PIX], g_sd[SD_FD_ALIVE], g_frames);
    }
    {
        u32 us = rix_ticks_to_us((u32)(t1 - t0));
        g_t_gpu_us += us;
        if (us > g_t_gpu_max_us) g_t_gpu_max_us = us;
        rec.gpu_us = us;
        if (g_sync_on) rec.sync_gpu_us += us;
    }
    pend_present();                                /* 0.9.3: rix_overlap just switched off: the pending frame first */
    zzq2_present_rgb565((const unsigned short *)rix_ptr(g_frame_buf + RIXQ2_SHOW_OFF), RIXQ2_PITCH);
    {
        u32 t2 = rix_now(), period;
        rec.present_us = rix_ticks_to_us((u32)(t2 - t1));
        g_t_present_us += rec.present_us;
        /* 0.8.2: frame period (end to end), long ones (map loads) left out */
        period = g_have_last_end ? rix_ticks_to_us((u32)(t2 - g_last_end_tick)) : 0u;
        g_last_end_tick = t2; g_have_last_end = 1u;
        rec.wall_us = (period < 1000000u) ? period : 0u;
        if (g_sync_on) rec.sync_wall_us = rec.wall_us;
    }
accounted:
    perf_add(&g_pt, &rec);
    /* 0.8.2: a timedemo segment = the frames that draw the world after a map load (ref_gl's
       registration_sequence moved) that happened while timedemo was set, up to the first frame
       without the world (demo over: disconnect) or the next map load.  "timedemo 1" typed over
       the attract demo starts nothing; the map load of "d1" does. */
    if (g_regseq != g_regseq_seen) {
        g_regseq_seen = g_regseq;
        if (g_seg.frames) td_close();
        g_td_armed = g_td;
    }
    if (!g_td) {
        g_td_armed = 0;
        if (g_seg.frames) td_close();
    } else if (zzq2_world_count != g_world_at_begin) {
        if (g_td_armed) {
            if (!g_seg.frames) rec.wall_us = 0u;   /* the period before the first frame is not the demo's */
            perf_add(&g_seg, &rec);
        }
    } else if (g_seg.frames) {
        td_close();
        g_td_armed = 0;
    }
    g_frames++;
    g_cur ^= 1u;
}

/* ---- reports --------------------------------------------------------------- */
static void u2s(char *d, u32 v)              /* decimal, no stdio on this side */
{
    char t[12]; int n = 0;
    if (v == 0u) { *d++ = '0'; *d = 0; return; }
    while (v) { t[n++] = (char)('0' + v % 10u); v /= 10u; }
    while (n) *d++ = t[--n];
    *d = 0;
}
static void x2s(char *d, u32 v, int digits)
{
    static const char hx[] = "0123456789abcdef";
    int i;
    for (i = digits - 1; i >= 0; i--) *d++ = hx[(v >> (4 * i)) & 15u];
    *d = 0;
}
static void cat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }

/* console lines after GLimp_SetMode: the ladder, the transport, the memory */
void zz_rix_console_report(void (*out)(const char *line))
{
    char l[128], n[16];
    u32 i, stages = g_sd[SD_LADDER_STAGES], failed = g_sd[SD_LADDER_FAILED];
    l[0] = 0; cat(l, "RIX: ladder "); u2s(n, stages); cat(l, n); cat(l, " stages, ");
    if (failed) { cat(l, "first failed L"); u2s(n, failed - 1u); cat(l, n); } else cat(l, "all passed");
    cat(l, ", caps "); x2s(n, rix_caps(), 4); cat(l, n);
    cat(l, ", init E"); x2s(n, (u32)g_init_rc & 0xFFFFu, 4); cat(l, n);
    out(l);
    l[0] = 0; cat(l, "RIX: binds "); cat(l, g_bind_mode == RIX_BIND_LIST ? "short DDR lists (BINDLIST)" : "FIFO packets");
    cat(l, ", chunks "); u2s(n, RIXQ2_RING_SLOTS); cat(l, n); cat(l, "x"); u2s(n, RIXQ2_CHUNK_BYTES / 1024u); cat(l, n); cat(l, "K, pool ");
    u2s(n, RIXQ2_POOL_PAGES * 2u / 1024u); cat(l, n); cat(l, " MiB at "); x2s(n, RIXQ2_POOL_BASE, 8); cat(l, n);
    out(l);
    l[0] = 0; cat(l, "RIX: frame "); u2s(n, (u32)ZZQ2_VID_W); cat(l, n); cat(l, "x"); u2s(n, (u32)ZZQ2_VID_H); cat(l, n);
    cat(l, " in buffers "); u2s(n, rix_geo_w); cat(l, n); cat(l, "x"); u2s(n, rix_geo_h_alloc); cat(l, n);
    cat(l, " at "); x2s(n, g_color_bufs[0], 8); cat(l, n); cat(l, " "); x2s(n, g_color_bufs[1], 8); cat(l, n);
    out(l);
    l[0] = 0; cat(l, "RIX: MMU table "); x2s(n, g_mmu.table, 8); cat(l, n);
    cat(l, " TTBR0 "); x2s(n, g_mmu.ttbr0, 8); cat(l, n);
    cat(l, " L1 "); u2s(n, g_mmu.l1_us); cat(l, n); cat(l, " us, L2 ");
    if (g_mmu.l2_done) { u2s(n, g_mmu.l2_us); cat(l, n); cat(l, " us"); } else cat(l, "off");
    out(l);
    for (i = 0; i < 4u; i++) {
        /* ladder stages 4 per line: Lk status/checks (status 0 = ok) */
        u32 k;
        l[0] = 0;
        for (k = i * 4u; k < i * 4u + 4u && k < SD_LADDER_MAX; k++) {
            u32 st = g_sd[SD_LADDER + k * SD_LADDER_SLOTS], ck = g_sd[SD_LADDER + k * SD_LADDER_SLOTS + 2u];
            cat(l, "L"); u2s(n, k); cat(l, n); cat(l, ":");
            if (st == 0xEEEEEEEEu) cat(l, "-");
            else { x2s(n, st & 0xFFFFu, 4); cat(l, n); cat(l, "/"); x2s(n, ck & 0xFFFFu, 2); cat(l, n); }
            cat(l, " ");
        }
        out(l);
    }
}

/* ---- performance report (0.7) ---------------------------------------------------------------------
   Per frame averages over the run, written to baseq2/rixstat.txt at R_Shutdown (zzq2rix_video.c).
   ref   = GLimp_BeginFrame -> GLimp_EndFrame: everything ref_gl does for a frame (BSP walk, models,
           2D) including ZZGL; zzglBegin = the state flush of each batch (registers, binds); binds =
           its part in rix_frame_texture; zzglEnd = vertex batches -> descriptors in the chunk ring
           (ring slot waits included); swap = closing the frame; gpu = waiting for the frame fence;
           present = RGB565 -> P96.  ref - zzglBegin - zzglEnd - uploads = ref_gl's own CPU time
           (BSP walk, lighting, model lerp) plus the cheap qgl calls (glVertex: the transform). */
static void line_kv(char *out, const char *k, u32 v, const char *unit)
{
    char n[16];
    cat(out, k); u2s(n, v); cat(out, n); cat(out, unit);
}
static void cat_line(char *out, const char *name, const zz_catsum *c, u32 f)
{
    char n[16];
    cat(out, name);
    cat(out, "  batch "); u2s(n, c->batches / f); cat(out, n);
    cat(out, "  tri "); u2s(n, c->tris / f); cat(out, n);
    cat(out, "  bind "); u2s(n, (c->binds * 10u) / f / 10u); cat(out, n); cat(out, "."); u2s(n, ((c->binds * 10u) / f) % 10u); cat(out, n);
    cat(out, "  | us "); u2s(n, c->wall_us / f); cat(out, n);
    cat(out, " = begin "); u2s(n, c->begin_us / f); cat(out, n);
    cat(out, " (bind wait "); u2s(n, c->bwait_us / f); cat(out, n); cat(out, ")");
    cat(out, " + end "); u2s(n, c->end_us / f); cat(out, n);
    cat(out, " + vertices "); u2s(n, (c->wall_us > c->begin_us + c->end_us) ? (c->wall_us - c->begin_us - c->end_us) / f : 0u); cat(out, n);
    cat(out, "\n");
}
static void perf_format(char *out, const zz_perf *p)
{
    u32 f = p->frames ? p->frames : 1u, c;
    zz_catsum all;
    out[0] = 0;
    cat(out, "frames "); { char n[16]; u2s(n, p->frames); cat(out, n); } cat(out, "\n");
    cat(out, "per frame, microseconds:\n");
    line_kv(out, "  ref (BeginFrame->EndFrame) ", p->ref_us / f, "\n");
    line_kv(out, "    zzglBegin (state+binds)  ", p->flush_us / f, "\n");
    line_kv(out, "      of which binds         ", p->bind_us / f, "\n");
    line_kv(out, "      bind fence waits       ", p->bind_wait_us / f, "\n");
    line_kv(out, "    zzglEnd (triangles)      ", p->end_us / f, "\n");
    line_kv(out, "      ring slot waits        ", p->ringwait_us / f, "\n");
    line_kv(out, "    texture uploads          ", p->upload_us / f, "\n");
    line_kv(out, "    ref_gl rest (engine)     ", (p->ref_us > p->flush_us + p->end_us + p->upload_us) ? (p->ref_us - p->flush_us - p->end_us - p->upload_us) / f : 0u, "\n");
    line_kv(out, "  swap (close the frame)     ", p->swap_us / f, "\n");
    line_kv(out, "  gpu (fence wait)           ", p->gpu_us / f, "\n");
    line_kv(out, "  present (RGB565 -> P96)    ", p->present_us / f, "\n");
    cat(out, "per frame, counts:\n");
    line_kv(out, "  glBegin batches            ", p->begins / f, "\n");
    line_kv(out, "  vertices                   ", p->verts / f, "\n");
    line_kv(out, "  triangles in               ", p->tris_in / f, "\n");
    line_kv(out, "  culled                     ", p->culled / f, "\n");
    line_kv(out, "  clipped                    ", p->clipped / f, "\n");
    line_kv(out, "  descriptors out            ", p->tris_out / f, "\n");
    line_kv(out, "  binds sent                 ", p->binds / f, "\n");
    line_kv(out, "  binds skipped              ", p->binds_skipped / f, "\n");
    line_kv(out, "  chunks                     ", p->chunks / f, "\n");
    line_kv(out, "  list words                 ", p->words / f, "\n");
    line_kv(out, "  state words                ", p->state_words / f, "\n");
    line_kv(out, "  uploads x1000              ", (p->uploads * 1000u) / f, "\n");
    line_kv(out, "  sub-uploads x1000          ", (p->subuploads * 1000u) / f, "\n");
    if (p->tris_in >= f) line_kv(out, "zzglEnd ns per triangle in   ", (p->end_us / f) * 1000u / (p->tris_in / f), "\n");
    if (p->binds) line_kv(out, "bind us each                 ", p->bind_us / p->binds, "\n");
    /* 0.8.3: inside zzglEnd, and how full the chunks are */
    cat(out, "zzglEnd split, per frame, microseconds:\n");
    line_kv(out, "  bind waits inside zzglEnd  ", p->end_bwait_us / f, " (a full chunk closed behind a pending bind)\n");
    if (p->desc_us || p->emit_us) {
        u32 de = p->desc_us + p->emit_us;
        line_kv(out, "  descriptor computation     ", p->desc_us / f, " (rix_tri_desc)\n");
        line_kv(out, "  descriptor writes          ", (p->emit_us > p->end_bwait_us ? p->emit_us - p->end_bwait_us : 0u) / f, " (rix_frame_tri, 40 words each into the chunk)\n");
        line_kv(out, "  the rest                   ", (p->end_us > de ? p->end_us - de : 0u) / f, " (copy, clamps, near clip, project, cull, screen clip)\n");
    } else cat(out, "  (rix_prof 0: set rix_prof 1 for descriptor computation / writes / rest)\n");
    if (p->chunks) {
        line_kv(out, "  chunk fill, % of a slot    ", (p->words / p->chunks) * 400u / RIXQ2_CHUNK_BYTES, "\n");
        line_kv(out, "  ring slots x KiB           ", RIXQ2_RING_SLOTS * 1000u + RIXQ2_CHUNK_BYTES / 1024u, " (slots * 1000 + KiB per slot)\n");
        line_kv(out, "  chunks closed full         ", p->chunks > p->binds + f ? (p->chunks - p->binds - f) / f : 0u, " per frame (the others end at a bind or at the frame's end)\n");
    }
    /* 0.9.1: GPU time measured in SYNC mode */
    if (p->sync_frames) {
        u32 g = p->sync_frames;
        cat(out, "GPU time (rix_sync 1: Core1 waits for every chunk and bind, nothing overlaps), per frame:\n");
        { char n[16]; cat(out, "  frames in this mode         "); u2s(n, g); cat(out, n); cat(out, " of "); u2s(n, p->frames); cat(out, n); cat(out, "\n"); }
        line_kv(out, "  GPU busy, us               ", p->sync_gpu_us / g, " (chunk + bind fence waits + final wait; polling adds up to ~50 us per wait)\n");
        line_kv(out, "  Core1 alone, us            ", (p->sync_wall_us > p->sync_gpu_us) ? (p->sync_wall_us - p->sync_gpu_us) / g : 0u, " (frame period minus the waits above)\n");
    }
    /* 0.9.2: GPU idle, texture versions */
    cat(out, "GPU idle and texture updates, per frame:\n");
    if (p->idle_frames) line_kv(out, "  GPU idle between frames, us", p->idle_between_us / p->idle_frames, " (frame fence passed -> first chunk of the next frame: present + start of frame)\n");
    if (p->overlap_frames) {
        char n[16];
        cat(out, "  presentation overlapped     "); u2s(n, p->overlap_frames); cat(out, n); cat(out, " frames (rix_overlap 1: frame N-1 shown during N; idle between frames not measured)\n");
    }
    line_kv(out, "  draws sent to an idle GPU  ", (p->dq_starved * 10u) / f / 10u, " (deferred mode: the GPU had finished everything before)\n");
    line_kv(out, "  texture versions x1000     ", (p->tex_versions * 1000u) / f, " (rix_texver 1: lightmap updates without a drain)\n");
    if (p->tex_versions) line_kv(out, "  us per version             ", p->tex_version_us / p->tex_versions, " (allocation + 32 KiB copy + clean)\n");
    line_kv(out, "  versions refused           ", p->tex_version_fail, " (pool or retired list full: drained instead)\n");
    /* 0.9: deferred submission */
    if (p->dq_frames) {
        u32 g = p->dq_frames;
        cat(out, "deferred submission (rix_defer 1), per frame:\n");
        { char n[16]; cat(out, "  frames in this mode         "); u2s(n, g); cat(out, n); cat(out, " of "); u2s(n, p->frames); cat(out, n); cat(out, "\n"); }
        line_kv(out, "  queue peak, events (avg)   ", p->dq_peak / g, "\n");
        line_kv(out, "  queue peak, events (max)   ", g_dq_peak_max, " (whole run)\n");
        line_kv(out, "  chunks held behind a bind  ", p->dq_held / g, "\n");
        line_kv(out, "  waits for the queue, us    ", p->dq_wait_us / g, " (ring slot, full queue, texture drains, end of frame)\n");
        line_kv(out, "    texture drains, us       ", p->dq_tex_us / g, "\n");
        line_kv(out, "    end of frame, us         ", p->dq_end_us / g, "\n");
        line_kv(out, "  rule-11 guard refusals     ", p->dq_refused, " (must be 0)\n");
    }
    /* 0.8.2 */
    cat(out, "per frame by category (batch = glBegin..glEnd; begin = state + binds, end = triangles,\n"
             "vertices = the glVertex/glColor/glTexCoord calls and ref_gl code between them; bind wait = every\n"
             "wait of Core1 for the GPU inside the batch, deferred queue included):\n");
    memset(&all, 0, sizeof all);
    for (c = 0; c < ZZ_NCAT; c++) {
        const zz_catsum *k = &p->cat[c];
        cat_line(out, g_cat_name[c], k, f);
        all.batches += k->batches; all.tris += k->tris; all.binds += k->binds; all.wall_us += k->wall_us;
        all.begin_us += k->begin_us; all.end_us += k->end_us; all.bwait_us += k->bwait_us;
    }
    cat_line(out, "  all batches", &all, f);
}
void zz_rix_perf_report(char *out, int cap)
{
    out[0] = 0;
    if (cap < 4096) return;
    perf_format(out, &g_pt);
    cat(out, "stray frames "); { char n[16]; u2s(n, g_stray_frames); cat(out, n); } cat(out, "\n");
}

/* ---- 0.8.3: start-up micro-benchmark --------------------------------------------------------------
   Run once after the texel self-test (GPU idle: that frame's fence was waited).  Per descriptor:
   rix_tri_desc on a typical textured triangle; the 40 word stores rix_dl_tri makes into the chunk
   ring (non-cacheable); the same stores into cacheable memory; and the 40 words copied into the ring
   by 8-register bursts (STM).  Writes go to the last ring slot, whose content no frame uses any
   more (chunk_open rebuilds a slot from its start). */
static u32 mb_cached[40u * 64u];
static void mb_burst40(volatile u32 *dst, const u32 *src)
{
#if defined(__arm__)
    __asm__ volatile(
        "ldmia %1!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "stmia %0!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "ldmia %1!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "stmia %0!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "ldmia %1!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "stmia %0!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "ldmia %1!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "stmia %0!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "ldmia %1!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "stmia %0!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        : "+r"(dst), "+r"(src)
        :
        : "r3", "r4", "r5", "r6", "r8", "r9", "r10", "r12", "memory");
#else
    u32 i;
    for (i = 0; i < 40u; i++) dst[i] = src[i];
#endif
}
void zz_rix_microbench(char *out, int cap)
{
    enum { N = 1000 };
    rix_vertex a, b, c;
    u32 d[40], i, k, t0, ns_desc, ns_nc, ns_cached, ns_burst, ok = 0u;
    volatile u32 *nc;
    char n[16];
    out[0] = 0;
    if (!g_open || cap < 512) return;
    nc = (volatile u32 *)rix_ptr(RIXQ2_RING_BASE + (RIXQ2_RING_SLOTS - 1u) * RIXQ2_CHUNK_BYTES);
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); memset(&c, 0, sizeof c);
    a.sx = 100.25f; a.sy = 100.5f;  a.z = 0.50f; a.invw = 0.50f; a.u = 0.0f; a.v = 0.0f;
    b.sx = 160.75f; b.sy = 112.25f; b.z = 0.52f; b.invw = 0.48f; b.u = 1.0f; b.v = 0.0f;
    c.sx = 118.5f;  c.sy = 171.0f;  c.z = 0.55f; c.invw = 0.45f; c.u = 0.0f; c.v = 1.0f;
    a.r = b.r = c.r = 1.0f; a.g = b.g = c.g = 0.8f; a.b = b.b = c.b = 0.6f; a.a = b.a = c.a = 1.0f;
    t0 = rix_now();
    for (i = 0; i < (u32)N; i++) { a.sx += 0.001f; ok += (u32)rix_tri_desc(&a, &b, &c, &d[1]); }
    ns_desc = rix_ticks_to_us(rix_now() - t0);              /* us for N = ns per descriptor */
    d[0] = 0x12345678u;
    t0 = rix_now();
    for (i = 0; i < (u32)N; i++) { volatile u32 *p = nc + (i % 400u) * 40u; for (k = 0; k < 40u; k++) p[k] = d[k]; }
    dsb();
    ns_nc = rix_ticks_to_us(rix_now() - t0);
    t0 = rix_now();
    for (i = 0; i < (u32)N; i++) { volatile u32 *p = mb_cached + (i % 64u) * 40u; for (k = 0; k < 40u; k++) p[k] = d[k]; }
    dsb();
    ns_cached = rix_ticks_to_us(rix_now() - t0);
    t0 = rix_now();
    for (i = 0; i < (u32)N; i++) mb_burst40(nc + (i % 400u) * 40u, d);
    dsb();
    ns_burst = rix_ticks_to_us(rix_now() - t0);
    cat(out, "RIX micro-benchmark, ns per descriptor (1000 each):\n");
    cat(out, "  rix_tri_desc (60 px textured triangle)   "); u2s(n, ns_desc); cat(out, n); cat(out, ok == (u32)N ? "\n" : " (some refused)\n");
    cat(out, "  40 word stores to the ring (rix_dl_tri)  "); u2s(n, ns_nc); cat(out, n); cat(out, "\n");
    cat(out, "  40 word stores to cached memory          "); u2s(n, ns_cached); cat(out, n); cat(out, "\n");
    cat(out, "  40 words to the ring by 8-word bursts    "); u2s(n, ns_burst); cat(out, n); cat(out, "\n");
}

/* ---- texel self-test (0.6) ---------------------------------------------------------------------
   Draws, in 2D at 1:1 (the HUD's case), textures whose every texel encodes its own (u, v), reads
   the GPU's pixels back and counts, per case, the pixels that show the expected texel and the
   offsets of the others.  Answers what photos of the screen cannot: which texel the TMU really
   fetches for a given pixel (column order in the 32-bit words, half-texel convention, position
   dependence).  Runs once in GLimp_SetMode (before ref_gl uploads anything), in a frame that is
   never presented; the report goes to the console and to baseq2/rixtest.txt (zzq2rix_video.c).
   Encoding (RGB565, exact through the conversion): R5 = u & 31, B5 = v & 31,
   G6 = 0x20 | (v >> 5) << 2 | (u >> 5)  (0x20 = marker). */
#define TT_TEX_C  (ZZGL_MAX_TEXTURES - 3u)   /* 0.7: 32 x 32 RGBA5551, alpha 0 on a checkerboard */
#define TT_TEX_A  (ZZGL_MAX_TEXTURES - 2u)   /* 32 x 32 */
#define TT_TEX_B  (ZZGL_MAX_TEXTURES - 1u)   /* 128 x 128, the size of conchars */
static unsigned char tt_img[128u * 128u * 4u];
static unsigned char tt_rd[128u * 3u];

static void tt_make(u32 size)
{
    u32 u, v;
    for (v = 0; v < size; v++)
        for (u = 0; u < size; u++) {
            unsigned char *p = &tt_img[(v * size + u) * 4u];
            u32 g6 = 0x20u | ((v >> 5) << 2) | (u >> 5);
            p[0] = (unsigned char)((u & 31u) << 3); p[1] = (unsigned char)(g6 << 2);
            p[2] = (unsigned char)((v & 31u) << 3); p[3] = 255u;
        }
}

/* 0.7: RGBA5551 + alpha test (ref_gl's 2D: GL_GREATER 0.666): texel (u, v) opaque when (u ^ v) & 4 is 0,
   transparent (alpha 0, colour black) otherwise; R5 = u, B5 = v, G5 = 0x14 marker */
static void tt_make_alpha(void)
{
    u32 u, v;
    for (v = 0; v < 32u; v++)
        for (u = 0; u < 32u; u++) {
            unsigned char *p = &tt_img[(v * 32u + u) * 4u];
            int opaque = (((u ^ v) & 4u) == 0u);
            p[0] = opaque ? (unsigned char)(u << 3) : 0u; p[1] = opaque ? (unsigned char)(0x14u << 3) : 0u;
            p[2] = opaque ? (unsigned char)(v << 3) : 0u; p[3] = opaque ? 255u : 0u;
        }
}
#define TT_AX 440u
#define TT_AY 48u
static int tt_check_alpha(char *out)
{
    u32 i, j, ok_op = 0u, ok_tr = 0u, bad_op = 0u, bad_tr = 0u, n_op = 0u, n_tr = 0u;
    char l[160], t[16];
    for (j = 0; j < 32u; j++) {
        s32 gy = 479 - (s32)(TT_AY + j);
        zzglReadPixels((s32)TT_AX, gy, 32, 1, ZGL_RGB, ZGL_UNSIGNED_BYTE, tt_rd);
        for (i = 0; i < 32u; i++) {
            u32 r5 = tt_rd[i * 3u] >> 3, g6 = tt_rd[i * 3u + 1u] >> 2, b5 = tt_rd[i * 3u + 2u] >> 3;
            if (((i ^ j) & 4u) == 0u) { n_op++; if (r5 == i && b5 == j && (g6 >> 1) == 0x14u) ok_op++; else bad_op++; }
            else { n_tr++; if (r5 == 31u && g6 == 63u && b5 == 31u) ok_tr++; else bad_tr++; }
        }
    }
    l[0] = 0; cat(l, "t32 RGBA5551 alpha-test @440,48 opaque ok "); u2s(t, ok_op); cat(l, t); cat(l, "/"); u2s(t, n_op); cat(l, t);
    cat(l, ", transparent kept "); u2s(t, ok_tr); cat(l, t); cat(l, "/"); u2s(t, n_tr); cat(l, t);
    cat(out, l); cat(out, "\n");
    return (bad_op == 0u && bad_tr == 0u);
}
static int g_alpha_ok = -1;   /* 0.7: result of the RGBA5551 case (-1: not run) */
int zz_rix_alpha_ok(void) { return g_alpha_ok; }

static void s2s(char *d, s32 v) { if (v < 0) { *d++ = '-'; v = -v; } else *d++ = '+'; u2s(d, (u32)v); }

typedef struct { const char *name; u32 tex, size, x, y, linear; float bias; } tt_case;

/* screen positions in Quake II's 2D coordinates (glOrtho 0..640, 480..0: y down) */
static const tt_case tt_cases[] = {
    { "t32 nearest      @8,8    ", TT_TEX_A,  32u,   8u,   8u, 0u, 0.0f  },
    { "t32 nearest      @300,8  ", TT_TEX_A,  32u, 300u,   8u, 0u, 0.0f  },
    { "t32 nearest+0.25 @8,48   ", TT_TEX_A,  32u,   8u,  48u, 0u, 0.25f },
    { "t32 nearest+0.25 @300,48 ", TT_TEX_A,  32u, 300u,  48u, 0u, 0.25f },
    { "t32 linear       @440,8  ", TT_TEX_A,  32u, 440u,   8u, 1u, 0.0f  },
    { "",                           TT_TEX_C,  32u, TT_AX, TT_AY, 0u, 0.0f },   /* 0.7: RGBA5551 alpha test, checked by tt_check_alpha */
    { "t128 nearest     @8,96   ", TT_TEX_B, 128u,   8u,  96u, 0u, 0.0f  },
    { "t128 nearest     @300,96 ", TT_TEX_B, 128u, 300u,  96u, 0u, 0.0f  },
    { "t128 nearest+0.25 @8,240 ", TT_TEX_B, 128u,   8u, 240u, 0u, 0.25f },
    { "t128 nearest+0.25 @300,240", TT_TEX_B, 128u, 300u, 240u, 0u, 0.25f },
};
#define TT_NCASES (sizeof(tt_cases) / sizeof(tt_cases[0]))

static void tt_draw(const tt_case *c)
{
    float x0 = (float)c->x, y0 = (float)c->y, x1 = x0 + (float)c->size, y1 = y0 + (float)c->size;
    if (c->tex == TT_TEX_C) { zzglEnable(ZGL_ALPHA_TEST); zzglAlphaFunc(ZGL_GREATER, 0.666f); }
    else zzglDisable(ZGL_ALPHA_TEST);
    zzglBindTexture(ZGL_TEXTURE_2D, c->tex);
    zzglTexParameterf(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, c->linear ? (float)ZGL_LINEAR : (float)ZGL_NEAREST);
    zzgl_set_texel_bias(c->bias);
    zzglBegin(ZGL_QUADS);                            /* as ref_gl's Draw_Pic */
    zzglTexCoord2f(0.0f, 0.0f); zzglVertex2f(x0, y0);
    zzglTexCoord2f(1.0f, 0.0f); zzglVertex2f(x1, y0);
    zzglTexCoord2f(1.0f, 1.0f); zzglVertex2f(x1, y1);
    zzglTexCoord2f(0.0f, 1.0f); zzglVertex2f(x0, y1);
    zzglEnd();
}

/* one case: ok count, the (du, dv) offsets in [-2, 2] and the rest; pairswap = pixels whose texel is
   the other one of its pair of columns (du = +1 on even columns, -1 on odd ones) */
static void tt_check(const tt_case *c, char *out, int cap, int *all_ok)
{
    u32 hist[5][5], ok = 0u, other = 0u, nomark = 0u, pairswap = 0u, n = c->size * c->size, i, j;
    s32 fdu = 0, fdv = 0; u32 fi = 0u, fj = 0u; int first = 1;
    char l[160], t[16];
    for (j = 0; j < 5u; j++) for (i = 0; i < 5u; i++) hist[j][i] = 0u;
    for (j = 0; j < c->size; j++) {
        s32 gy = 479 - (s32)(c->y + j);                   /* OpenGL window row of screen row y + j */
        zzglReadPixels((s32)c->x, gy, (s32)c->size, 1, ZGL_RGB, ZGL_UNSIGNED_BYTE, tt_rd);
        for (i = 0; i < c->size; i++) {
            u32 r5 = tt_rd[i * 3u] >> 3, g6 = tt_rd[i * 3u + 1u] >> 2, b5 = tt_rd[i * 3u + 2u] >> 3;
            s32 du, dv;
            if ((g6 & 0x30u) != 0x20u) { nomark++; if (first) { first = 0; fi = i; fj = j; fdu = 99; fdv = (s32)g6; } continue; }
            du = (s32)(r5 | ((g6 & 3u) << 5)) - (s32)i;
            dv = (s32)(b5 | (((g6 >> 2) & 3u) << 5)) - (s32)j;
            if (du == 0 && dv == 0) { ok++; continue; }
            if (first) { first = 0; fi = i; fj = j; fdu = du; fdv = dv; }
            if (dv == 0 && du == (((i & 1u) == 0u) ? 1 : -1)) pairswap++;
            if (du >= -2 && du <= 2 && dv >= -2 && dv <= 2) hist[dv + 2][du + 2]++; else other++;
        }
    }
    if (ok != n && !c->linear) *all_ok = 0;   /* linear: information only (the encoding wraps 31 -> 0 at column 0) */
    l[0] = 0; cat(l, c->name); cat(l, " ok "); u2s(t, ok); cat(l, t); cat(l, "/"); u2s(t, n); cat(l, t);
    if (ok != n) {
        u32 shown = 0u, k;
        for (k = 0; k < 25u && shown < 4u; k++) {
            u32 best = 0u, bj = 0u, bi = 0u;
            for (j = 0; j < 5u; j++) for (i = 0; i < 5u; i++) if (hist[j][i] > best) { best = hist[j][i]; bj = j; bi = i; }
            if (!best) break;
            cat(l, " d"); s2s(t, (s32)bi - 2); cat(l, t); cat(l, ","); s2s(t, (s32)bj - 2); cat(l, t); cat(l, ":"); u2s(t, best); cat(l, t);
            hist[bj][bi] = 0u; shown++;
        }
        if (other) { cat(l, " far:"); u2s(t, other); cat(l, t); }
        if (nomark) { cat(l, " nomark:"); u2s(t, nomark); cat(l, t); }
        if (pairswap) { cat(l, " pairswap:"); u2s(t, pairswap); cat(l, t); }
        cat(l, " first@"); u2s(t, fi); cat(l, t); cat(l, ","); u2s(t, fj); cat(l, t);
        cat(l, "="); if (fdu == 99) { cat(l, "g"); x2s(t, (u32)fdv, 2); cat(l, t); } else { s2s(t, fdu); cat(l, t); cat(l, ","); s2s(t, fdv); cat(l, t); }
    }
    l[sizeof(l) - 1] = 0;
    if ((int)(sizeof(l)) > cap) l[cap - 1] = 0;
    cat(out, l); cat(out, "\n");
}

/* returns 1 when every case shows the expected texel everywhere; report: lines of at most ~150 chars */
int zz_rix_textest(char *report, int cap)
{
    zzgl_target t;
    u32 k;
    int rc, all_ok = 1;
    report[0] = 0;
    if (!g_open || cap < 2048) return 0;
    g_frame_buf = g_color_bufs[g_cur];
    g_frame_fence = next_fence();
    t.color = g_frame_buf; t.fence = g_frame_fence;
    zzgl_set_target(&t);
    zzglViewport(0, 0, 640, 480);
    zzglMatrixMode(ZGL_PROJECTION); zzglLoadIdentity(); zzglOrtho(0.0, 640.0, 480.0, 0.0, -99999.0, 99999.0);
    zzglMatrixMode(ZGL_MODELVIEW); zzglLoadIdentity();
    zzglDisable(ZGL_DEPTH_TEST); zzglDisable(ZGL_CULL_FACE); zzglDisable(ZGL_BLEND); zzglDisable(ZGL_ALPHA_TEST);
    zzglClearColor(1.0f, 1.0f, 1.0f, 1.0f); zzglClear(ZGL_COLOR_BUFFER_BIT);   /* white: the alpha case's background */
    zzglEnable(ZGL_TEXTURE_2D);
    zzglTexEnvf(ZGL_TEXTURE_ENV, ZGL_TEXTURE_ENV_MODE, (float)ZGL_REPLACE);
    zzglColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    tt_make(32u);  zzglBindTexture(ZGL_TEXTURE_2D, TT_TEX_A); zzglTexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA, 32, 32, 0, ZGL_RGBA, ZGL_UNSIGNED_BYTE, tt_img);
    tt_make(128u); zzglBindTexture(ZGL_TEXTURE_2D, TT_TEX_B); zzglTexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA, 128, 128, 0, ZGL_RGBA, ZGL_UNSIGNED_BYTE, tt_img);
    tt_make_alpha(); zzgl_set_alpha_textures(1);
    zzglBindTexture(ZGL_TEXTURE_2D, TT_TEX_C); zzglTexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA, 32, 32, 0, ZGL_RGBA, ZGL_UNSIGNED_BYTE, tt_img);
    zzgl_set_alpha_textures(0);                        /* the caller sets the run's choice afterwards */
    for (k = 0; k < TT_NCASES; k++) tt_draw(&tt_cases[k]);
    zzgl_set_texel_bias(0.0f);
    rc = zzgl_swap();
    if (!rc) rc = rix_frame_wait(g_frame_buf, g_frame_fence, 2000000u, 0, 0);
    if (rc) {
        cat(report, "RIX texel test: frame E"); { char n[8]; x2s(n, (u32)rc & 0xFFFFu, 4); cat(report, n); } cat(report, "\n");
        all_ok = 0;
    } else {
        cat(report, "RIX texel test (2D 1:1, RGB565, REPLACE): case ok/pixels, then the commonest (du,dv):count\n");
        for (k = 0; k < TT_NCASES; k++) if (tt_cases[k].tex != TT_TEX_C) tt_check(&tt_cases[k], report, cap, &all_ok);
        g_alpha_ok = tt_check_alpha(report);
        cat(report, all_ok ? "RIX texel test: ALL OK (nearest cases; the linear one is for information)\n" : "RIX texel test: MISMATCHES\n");
        cat(report, g_alpha_ok > 0 ? "RIX alpha test: RGBA5551 + alpha test OK\n" : "RIX alpha test: RGBA5551 FAILED (alpha-keyed textures stay RGB565)\n");
    }
    {   u32 del[3]; del[0] = TT_TEX_A; del[1] = TT_TEX_B; del[2] = TT_TEX_C; zzglDeleteTextures(3, del); }
    zzglDisable(ZGL_ALPHA_TEST);
    zzglTexEnvf(ZGL_TEXTURE_ENV, ZGL_TEXTURE_ENV_MODE, (float)ZGL_MODULATE);
    zzglDisable(ZGL_TEXTURE_2D);
    g_cur ^= 1u;                                       /* this buffer is never presented */
    return all_ok;
}

/* end of run: numbers for the "last printf" line of the glue */
void zz_rix_stats(u32 *frames, u32 *gpu_us_avg, u32 *gpu_us_max, u32 *present_us_avg, u32 *binds_last, u32 *pool_pages_free)
{
    u32 n = g_frames ? g_frames : 1u;
    const zzgl_stats *st = zzgl_get_stats();
    *frames = g_frames + g_stray_frames * 100000u;   /* stray frames (draws outside Begin/End) in the hundred-thousands */
    *gpu_us_avg = g_t_gpu_us / n;
    *gpu_us_max = g_t_gpu_max_us;
    *present_us_avg = g_t_present_us / n;
    *binds_last = st ? st->binds : 0u;
    *pool_pages_free = g_open ? rix_texpool_free_pages() : 0u;
}

/* zz_alpha (GLimp_SetMode): alpha-keyed textures as RGBA5551 from now on (vid_restart re-uploads) */
void zz_rix_set_alpha_textures(int on) { zzgl_set_alpha_textures(on); }

/* end of run: the GPU idle, the attributes of the release back */
void zz_rix_close(void)
{
    if (!g_open) return;
    pend_wait();                                   /* 0.9.3 */
    rix_shutdown();
    (void)zzq2rix_mmu_restore();
    g_open = 0;
}
