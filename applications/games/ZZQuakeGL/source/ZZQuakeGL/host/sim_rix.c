/* host_tests/sim_rix.c - ZZCube RasterIX B1 - behavioural simulator
 *
 * Runs the UNMODIFIED Core1 library (rixgpu.c) and application
 * (zzgear_app.c) on a PC against a model of: the AXI-Stream FIFO, the
 * FrameStreamingCore (ST0->ST1 and MEM->ST1), a RasterIX that executes
 * RENDER_CONFIG / FRAMEBUFFER memset (scissor, resolution, Y flip exactly
 * as FramebufferWriterClear.v) / TRIANGLE_STREAM (edge-function fill),
 * the PL330 (program parser + endian swap), the private timer + GIC
 * (WFI wake), the global timer, the VDMA start address register, Core0's
 * vblank ISR and a 68k presenter with either coherent (OP_MEMCPY) or
 * vblank-granular view of the mailbox.
 *
 * Safety invariants checked while the model runs:
 *   - the GPU never writes into the buffer the scanout is reading, nor
 *     into the buffer it was reading during the previous frame;
 *   - the DMA never swaps a buffer the GPU still writes;
 *   - every published frame has its fence value in line 480.
 *
 * Build: cc -O2 -DRIX_HOST_SIM -I../ARM_RasterIX sim_rix.c ../ARM_RasterIX/rixgpu.c \
 *        ../ARM_RasterIX/zzgear_app.c ../ARM_RasterIX/gear_mesh.c ../ARM_RasterIX/gear_xform.c ../ARM_RasterIX/cube_geom.c -lm
 * SPDX-License-Identifier: 0BSD
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rix_shared.h"
#include "rix_hw.h"
#include "rixtex.h"

/* ------------------------------------------------------------------ */
/* virtual memory                                                      */
/* ------------------------------------------------------------------ */
static unsigned char window[0x200000];
static unsigned char mbox[0x10000];
static unsigned char arena[0x01000000];          /* Core1 arena 0x30000000.. (16 MiB modelled): placement probe, ROOM texture pool */
#define ARENA_BASE 0x30000000u
void *sim_ptr(u32 a)
{
    if (a >= RIX_WIN_BASE && a < RIX_WIN_END) return window + (a - RIX_WIN_BASE);
    if (a >= RIX_SHARED_ARM && a < RIX_SHARED_ARM + sizeof(mbox)) return mbox + (a - RIX_SHARED_ARM);
    if (a >= ARENA_BASE && a < ARENA_BASE + sizeof(arena)) return arena + (a - ARENA_BASE);
    fprintf(stderr, "sim_ptr: bad address %08x\n", a);
    exit(2);
}
static u32 rd32(u32 a) { u32 v; memcpy(&v, sim_ptr(a), 4); return v; }

/* ZZQuake2RIX 0.6: optional TMU0 model (sim_model_tmu = 1): RGB565 textures read through the page
   table of the last OP_TEXTURE, REPEAT, nearest = floor(frac(s/q) * size), linear = the RasterIX
   sampler (half-texel offset, 4 texels); the texel replaces the fragment (REPLACE; MODULATE is not
   modelled).  Attributes are evaluated at the pixel's integer position, as the descriptor defines
   them (bbox start + dx, dy). */
int sim_model_tmu = 0;
static u32 tmu_pages[1024], tmu_npages;
static float fl_of(u32 b) { float f; memcpy(&f, &b, 4); return f; }
static u32 tmu_fmt;      /* texel format of the sample in progress: 2 = RGB565, 1 = RGBA5551 */
static u32 tmu_alpha;    /* alpha of the last texel read, 0..255 */
static u16 tmu_texel(u32 log2w, u32 log2h, s32 iu, s32 iv)
{
    u32 w = 1u << log2w, h = 1u << log2h, n = ((u32)iv & (h - 1u)) * w + ((u32)iu & (w - 1u)), off = n * 2u, pg = off / 2048u;
    u16 v;
    if (pg >= tmu_npages) return 0xF81Eu;
    memcpy(&v, sim_ptr(tmu_pages[pg] + (off & 2047u)), 2);
    tmu_alpha = 255u;
    if (tmu_fmt == 1u) {        /* RGBA5551 -> RGB565 + alpha */
        u32 r5 = v >> 11, g5 = (v >> 6) & 31u, b5 = (v >> 1) & 31u;
        tmu_alpha = (v & 1u) ? 255u : 0u;
        v = (u16)((r5 << 11) | (((g5 << 1) | (g5 >> 4)) << 5) | b5);
    }
    return v;
}
static u16 tmu_sample(const u32 *d, u32 cfg, s32 dx, s32 dy)
{
    float s = fl_of(d[30]) + (float)dx * fl_of(d[33]) + (float)dy * fl_of(d[36]);
    float t = fl_of(d[31]) + (float)dx * fl_of(d[34]) + (float)dy * fl_of(d[37]);
    float q = fl_of(d[32]) + (float)dx * fl_of(d[35]) + (float)dy * fl_of(d[38]);
    u32 lw = cfg & 15u, lh = (cfg >> 4) & 15u;
    float u, v, fu, fv;
    tmu_fmt = (cfg >> 12) & 15u;
    s32 iu, iv;
    if (q == 0.0f) return 0xF81Eu;
    u = s / q; v = t / q;
    u = (u - (float)(s32)u) * (float)(1u << lw); if (u < 0.0f) u += (float)(1u << lw);
    v = (v - (float)(s32)v) * (float)(1u << lh); if (v < 0.0f) v += (float)(1u << lh);
    if (!((cfg >> 10) & 1u)) { iu = (s32)u; iv = (s32)v; return tmu_texel(lw, lh, iu, iv); }
    u -= 0.5f; v -= 0.5f;
    iu = (s32)(u < 0.0f ? u - 1.0f : u); iv = (s32)(v < 0.0f ? v - 1.0f : v);
    fu = u - (float)iu; fv = v - (float)iv;
    {
        u16 t00 = tmu_texel(lw, lh, iu, iv), t01 = tmu_texel(lw, lh, iu + 1, iv), t10 = tmu_texel(lw, lh, iu, iv + 1), t11 = tmu_texel(lw, lh, iu + 1, iv + 1);
        u32 sh[3] = { 11u, 5u, 0u }, mk[3] = { 31u, 63u, 31u }, c, out = 0u;
        for (c = 0; c < 3u; c++) {
            float a = (float)((t00 >> sh[c]) & mk[c]) * (1.0f - fu) + (float)((t01 >> sh[c]) & mk[c]) * fu;
            float b = (float)((t10 >> sh[c]) & mk[c]) * (1.0f - fu) + (float)((t11 >> sh[c]) & mk[c]) * fu;
            out |= ((u32)(a * (1.0f - fv) + b * fv + 0.5f) & mk[c]) << sh[c];
        }
        return (u16)out;
    }
}
static void wr16(u32 a, u16 v) { memcpy(sim_ptr(a), &v, 2); }

/* ------------------------------------------------------------------ */
/* virtual time                                                        */
/* ------------------------------------------------------------------ */
static double now_us = 0.0;
static const double TICKS_PER_US = 333.333343;
static u32 opt_coherent = 1;
static int opt_presenter_dead = 0;
static double opt_poll_period_us = 2000.0;       /* 68k sees the mailbox at once (OP_MEMCPY) or at vblank */
static double opt_gpu_ns_per_px = 10.0;
static double opt_dma_mb_s = 450.0;
static int violations = 0;
static int verbose = 0;

/* ------------------------------------------------------------------ */
/* scanout + 68k presenter model                                       */
/* ------------------------------------------------------------------ */
static u32 vdma_sa = 0x00E00000u + 0x200000u;   /* P96 screen */
static u32 vdma_scanning = 0, vdma_scanning_prev = 0;
static double next_vblank_us = 16667.0, next_68k_poll_us = 2000.0, last_cache_sync_us = 0.0;
static u32 pan_pending = 0, pan_target = 0, last_ready_seen = 0, presenter_pans = 0;
static u32 mbox_snapshot_ready = 0;             /* what a vblank-granular 68k sees */
static int stop_after_pans = 0;

static void presenter_poll(void)
{
    u32 ready;
    if (opt_presenter_dead) return;
    ready = opt_coherent ? rd32(RIX_SHARED_ARM + SH_READY * 4) : mbox_snapshot_ready;
    if (ready != last_ready_seen && ready != 0) {
        u32 idx = ready & 3u;
        static const u32 bufs[3] = { RIX_FB0_ARM, RIX_FB1_ARM, RIX_FB2_ARM };
        last_ready_seen = ready;
        if (idx < 3) { pan_pending = 1; pan_target = bufs[idx]; presenter_pans++; }
        if (stop_after_pans && (int)presenter_pans >= stop_after_pans) {
            u32 one = 1; memcpy(sim_ptr(RIX_SHARED_ARM + SH_STOP * 4), &one, 4);
        }
    }
}

/* GPU + DMA deferred completion.  GPU lists are queued in submission order
 * (the hardware serializes them); the pixels of a list are applied at parse
 * time, except the writes to line 480 (fence rows) of the color buffers:
 * those are logged per list and applied when that list completes, so a fence
 * (or a chunk fence) becomes visible exactly when its list is done and in
 * list order.  Needed for the chunk ring: the app reuses a slot as soon as
 * it sees the fence of the chunk that used it. */
typedef struct { double done_us; u32 target; int valid; } pending_job;
static pending_job dma_job;
#define GPU_Q_MAX   64
#define GPU_DEF_MAX 4096
#define GPU_LIST_MAX 4096
typedef struct { double done_us; u32 target; u32 ndef; u32 def_addr[GPU_DEF_MAX]; u16 def_val[GPU_DEF_MAX]; u8 def_strb[GPU_DEF_MAX];
                 u32 list_addr, list_words, list_copy[GPU_LIST_MAX]; int has_tex; } gpu_qjob;   /* list_addr != 0: fetched from DDR (ring hazard check); has_tex: contains an OP_TEXTURE (FSC deadlock check) */
static u32 ring_hazards = 0;
static gpu_qjob gpu_q[GPU_Q_MAX]; static u32 gpu_q_head = 0, gpu_q_n = 0;   /* ring: head = oldest */
static gpu_qjob *gpu_cur = NULL;      /* list being parsed (deferred writes go here) */
static u32 gpu_q_overflow = 0;
static int gpu_any_pending(void) { return gpu_q_n != 0; }
static int gpu_writing(u32 buf) { u32 k; for (k = 0; k < gpu_q_n; k++) if (gpu_q[(gpu_q_head + k) % GPU_Q_MAX].target == buf) return 1; return 0; }
static void gpu_q_complete_oldest(void)
{
    gpu_qjob *j = &gpu_q[gpu_q_head]; u32 k;
    if (j->list_addr) {
        /* the hardware FSC reads the list from DDR while the parser consumes it:
           a list modified before its completion is a ring-slot reuse hazard */
        for (k = 0; k < j->list_words; k++) {
            u32 v; memcpy(&v, sim_ptr(j->list_addr + k * 4), 4);
            if (v != j->list_copy[k]) {
                if (ring_hazards < 5) fprintf(stderr, "VIOLATION t=%.0fus DDR list %08x word %u rewritten before the GPU completed it\n", now_us, j->list_addr, k);
                ring_hazards++; violations++; break;
            }
        }
    }
    for (k = 0; k < j->ndef; k++) {
        u8 *p = (u8 *)sim_ptr(j->def_addr[k]);
        if (j->def_strb[k] & 1) p[0] = (u8)(j->def_val[k] & 0xFFu);
        if (j->def_strb[k] & 2) p[1] = (u8)(j->def_val[k] >> 8);
    }
    gpu_q_head = (gpu_q_head + 1) % GPU_Q_MAX; gpu_q_n--;
}

static void advance_to(double t)
{
    /* process events in order up to t */
    for (;;) {
        double next = t;
        int ev = 0;
        if (next_vblank_us < next) { next = next_vblank_us; ev = 1; }
        if (next_68k_poll_us < next) { next = next_68k_poll_us; ev = 2; }
        if (gpu_q_n && gpu_q[gpu_q_head].done_us < next) { next = gpu_q[gpu_q_head].done_us; ev = 3; }
        if (dma_job.valid && dma_job.done_us < next) { next = dma_job.done_us; ev = 4; }
        now_us = next;
        if (ev == 0) break;
        if (ev == 1) {
            /* Core0 ISR: program the VDMA with the current pan offset; the
               new frame starts reading it now; the previous frame read vdma_scanning */
            if (pan_pending) { vdma_sa = pan_target; pan_pending = 0; }
            vdma_scanning_prev = vdma_scanning;
            vdma_scanning = vdma_sa;
            /* Core0 L1+L2 flush: a vblank-granular 68k now sees the mailbox */
            mbox_snapshot_ready = rd32(RIX_SHARED_ARM + SH_READY * 4);
            last_cache_sync_us = now_us;
            next_vblank_us += 16667.0;
        } else if (ev == 2) {
            presenter_poll();
            next_68k_poll_us += opt_poll_period_us;
        } else if (ev == 3) {
            gpu_q_complete_oldest();
        } else if (ev == 4) {
            dma_job.valid = 0;
        }
    }
}

/* every access costs a little virtual time (AXI round trip) so spin loops progress */
u32 sim_now_ticks(void) { advance_to(now_us + 0.08); return (u32)(now_us * TICKS_PER_US); }
void sim_barrier(void) { advance_to(now_us + 0.01); }

/* ------------------------------------------------------------------ */
/* RasterIX model                                                      */
/* ------------------------------------------------------------------ */
static u32 regs[32];
static int opt_dirty_regs = 0;
/* hang models (what the B1/B1a hardware runs looked like):
   1 = the GPU stops consuming commands from the start (hung by an earlier run)
   2 = the CommandParser never returns after a memset executed with the scissor enabled */
static int opt_hang_mode = 0;
static int opt_read_credit = 1;   /* 1 = G4C (XACPCommandReadCredit on the list path); 0 = G4B model (any OP_TEXTURE inside a DDR list hangs, opt_hang_mode 4) */
/* G4C FSC model (D2e, 14/10): the FSC serves both the list fetches and the texture page fetches.
   When the parser reaches an OP_TEXTURE inside a DDR list, the pages can only be fetched if the FSC
   has finished fetching that list, i.e. if the words still to deliver after the page table fit in
   its buffers.  Ladder L15 completes on the hardware with 77 words after its table (144-word list, burst padding included);
   ROOM A's first chunk (thousands of words after the first table) deadlocked.  The model refuses
   anything beyond the proven value. */
static int opt_fsc_prefetch_words = 77;
static u32 fsc_list_off = 0;      /* offset of the DDR list being parsed inside the parser stream buffer */
static int gpu_hung = 0;
static int fsc_src_mem = 0;     /* words currently parsed came from a MEM->ST1 (DDR) transfer */
static u32 fsc_mem_addr = 0;    /* ... and from this DDR address */
static u32 fsc_buf[160000], fsc_count = 0;   /* the packet being fetched (defined here for the hazard copy) */
static double gpu_busy_until = 0.0;
static u32 gpu_pixels_total = 0, gpu_cmds = 0, gpu_lists = 0;

static u32 gpu_masked_px = 0;   /* pixels dropped by the color mask (REG 3) */

/* RTL: FramebufferWriterStrobeGen: m_frag_tstrb = tstrb ? confMask : 0, with
 * confMask = ColorBufferReduceMask({R,G,B,A})[0 +: 2] = {G, B} (bits 17, 16
 * of OP_RENDER_CONFIG_FRAGMENT_PIPELINE).  A register bank at its power-on
 * value (0) therefore strobes nothing: neither pixels nor memsets reach DDR.
 * This is the gating the B1 hardware SELFTEST ran into (E120). */
static void gpu_write_px(u32 index, u16 v)
{
    u32 addr = regs[16] + index * 2u;
    u32 buf = regs[16];
    u32 strb0 = (regs[3] >> 16) & 1u, strb1 = (regs[3] >> 17) & 1u;
    /* ZZQuakeGL 0.5: the 1024 build draws into colour buffers in the modelled arena */
    if ((addr >= RIX_WIN_END || addr < RIX_WIN_BASE) && !(addr >= ARENA_BASE && addr + 2u <= ARENA_BASE + sizeof(arena))) { violations++; return; }
    if (!(strb0 | strb1)) { gpu_masked_px++; return; }
    if (buf == vdma_scanning || buf == vdma_scanning_prev) {
        if (violations < 5) fprintf(stderr, "VIOLATION t=%.0fus GPU writes buffer %08x scanned by VDMA (cur %08x prev %08x)\n", now_us, buf, vdma_scanning, vdma_scanning_prev);
        violations++;
    }
    /* ZZQuake2RIX 0.10: the fence line is the last memory line of the geometry in REG 9 (640 x 481: line
       480 of FB0..FB2, as before; 800 x 601: line 600 of the two 800-wide buffers) */
    if ((regs[9] & 0x7FFu) && index >= (regs[9] & 0x7FFu) * (((regs[9] >> 16) & 0x7FFu) - 1u) && gpu_cur
        && ((buf == RIX_FB0_ARM || buf == RIX_FB1_ARM || buf == RIX_FB2_ARM) || (regs[9] & 0x7FFu) != 640u)) {
        /* fence row: visible at list completion */
        if (gpu_cur->ndef < GPU_DEF_MAX) { gpu_cur->def_addr[gpu_cur->ndef] = addr; gpu_cur->def_val[gpu_cur->ndef] = v; gpu_cur->def_strb[gpu_cur->ndef] = (u8)(strb0 | (strb1 << 1)); gpu_cur->ndef++; return; }
        gpu_q_overflow++;   /* falls through: applied immediately (model limit, reported) */
    }
    if (strb0 && strb1) wr16(addr, v);
    else {
        u8 *p = (u8 *)sim_ptr(addr);
        if (strb0) p[0] = (u8)(v & 0xFFu);
        if (strb1) p[1] = (u8)(v >> 8);
    }
}

static void gpu_exec_list(const u32 *w, u32 n)
{
    u32 i = 0;
    double cost = 0.0;
    u32 xres = regs[9] & 0x7FF, yres = (regs[9] >> 16) & 0x7FF;
    gpu_lists++;
    if (gpu_q_n == GPU_Q_MAX) { advance_to(gpu_q[gpu_q_head].done_us + 0.001); }   /* model queue full: wait for the oldest */
    gpu_cur = &gpu_q[(gpu_q_head + gpu_q_n) % GPU_Q_MAX]; gpu_cur->ndef = 0; gpu_cur->target = regs[16];
    gpu_cur->list_addr = 0; gpu_cur->list_words = 0; gpu_cur->has_tex = 0;
    if (fsc_src_mem && fsc_mem_addr && fsc_count <= GPU_LIST_MAX) { gpu_cur->list_addr = fsc_mem_addr; gpu_cur->list_words = fsc_count; memcpy(gpu_cur->list_copy, fsc_buf, fsc_count * 4); }
    while (i < n) {
        u32 op = w[i] >> 28, imm = w[i] & 0x0FFFFFFF;
        gpu_cmds++;
        if (op == 0) { i++; cost += 0.03; }
        else if (op == 1) { regs[imm & 31] = w[i + 1]; i += 2; cost += 0.03;
            xres = regs[9] & 0x7FF; yres = (regs[9] >> 16) & 0x7FF; }
        else if (op == 2) {
            u32 ctrl = imm & 0xFF;
            if ((ctrl & 0x12) == 0x12) {
                u32 sc = regs[0] & RIX_FEAT_SCISSOR;
                if (opt_hang_mode == 2 && sc) gpu_hung = 1;   /* parser never comes back after this one */
                if (opt_hang_mode == 3 && !((regs[3] >> 16) & 3u)) gpu_hung = 1;   /* run 1: fully masked memset */
                u32 x0 = sc ? (regs[6] & 0x7FF) : 0, y0 = sc ? ((regs[6] >> 16) & 0x7FF) : 0;
                u32 x1 = sc ? (regs[7] & 0x7FF) : xres, y1 = sc ? ((regs[7] >> 16) & 0x7FF) : yres;
                u32 r = (regs[1] >> 24) & 0xFF, g = (regs[1] >> 16) & 0xFF, b = (regs[1] >> 8) & 0xFF;
                u16 px = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                u32 x, y;
                gpu_cur->target = regs[16];
                for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) {
                    u32 index = ((yres - 1u) - y) * xres + x;
                    gpu_write_px(index, px);
                }
                cost += (double)((x1 - x0) * (y1 - y0)) * opt_gpu_ns_per_px / 1000.0;
                gpu_pixels_total += (x1 - x0) * (y1 - y0);
            } else if ((ctrl & 0x22) == 0x22) {
                /* depth memset: not modelled as data (no depth in the model), costed like a colour memset */
                u32 sc = regs[0] & RIX_FEAT_SCISSOR;
                u32 x0 = sc ? (regs[6] & 0x7FF) : 0, y0 = sc ? ((regs[6] >> 16) & 0x7FF) : 0;
                u32 x1 = sc ? (regs[7] & 0x7FF) : xres, y1 = sc ? ((regs[7] >> 16) & 0x7FF) : yres;
                {
                    u32 z = regs[17], zend = z + xres * yres * 2u;
                    int in_win = (z >= RIX_WIN_BASE && zend <= RIX_WIN_END), in_arena = (z >= ARENA_BASE && zend <= ARENA_BASE + sizeof(arena));
                    if (!in_win && !in_arena) { fprintf(stderr, "depth memset with the depth buffer outside the window and the modelled arena: %08x\n", z); violations++; }
                }
                cost += (double)((x1 - x0) * (y1 - y0)) * opt_gpu_ns_per_px / 1000.0;
            }
            i++;
        }
        else if (op == 3) {
            const u32 *d = &w[i + 1];
            u32 bsx = d[1] & 0xFFFF, bsy = d[1] >> 16, bex = d[2] & 0xFFFF, bey = d[2] >> 16;
            s32 wi0 = (s32)d[3], wi1 = (s32)d[4], wi2 = (s32)d[5];
            s32 wx0 = (s32)d[6], wx1 = (s32)d[7], wx2 = (s32)d[8];
            s32 wy0 = (s32)d[9], wy1 = (s32)d[10], wy2 = (s32)d[11];
            u32 x, y, filled = 0, h = 2166136261u, k;
            u32 sc = regs[0] & RIX_FEAT_SCISSOR;
            u16 tri_tag;
            /* tag = hash of the triangle's geometry words (never 0 = clear): the same
               triangle gets the same tag in every run, two different ones almost never */
            for (k = 1; k < 12; k++) { h ^= d[k]; h *= 16777619u; }
            tri_tag = (u16)(0x8000u | (h & 0x7FFFu));
            if (tri_tag == 0xF81Fu) tri_tag = 0xF81Eu;      /* never the ROOM clear colour (holes check) */
            gpu_cur->target = regs[16];
            for (y = bsy; y < bey; y++) {
                s32 e0 = wi0 + (s32)(y - bsy) * wy0, e1 = wi1 + (s32)(y - bsy) * wy1, e2 = wi2 + (s32)(y - bsy) * wy2;
                for (x = bsx; x < bex; x++) {
                    if (e0 >= 0 && e1 >= 0 && e2 >= 0 && x < xres && y < yres) {
                        int inside = 1;
                        if (sc) {
                            if (x < (regs[6] & 0x7FF) || x >= (regs[7] & 0x7FF) || y < ((regs[6] >> 16) & 0x7FF) || y >= ((regs[7] >> 16) & 0x7FF)) inside = 0;
                        }
                        if (inside) {
                            u16 px = tri_tag;
                            int keep = 1;
                            if (sim_model_tmu && (regs[0] & RIX_FEAT_TMU0)) {
                                px = tmu_sample(d, regs[12], (s32)(x - bsx), (s32)(y - bsy));
                                if (regs[0] & RIX_FEAT_ALPHA_TEST) {   /* fragment alpha = texel alpha (REPLACE) */
                                    u32 fn = (regs[3] >> 3) & 7u, ref = (regs[3] >> 6) & 0xFFu, a = tmu_alpha;
                                    keep = fn == 0u ? 1 : fn == 1u ? 0 : fn == 2u ? a < ref : fn == 3u ? a == ref : fn == 4u ? a <= ref :
                                           fn == 5u ? a > ref : fn == 6u ? a != ref : a >= ref;
                                }
                            }
                            if (keep) { gpu_write_px(((yres - 1u) - y) * xres + x, px); filled++; }
                        }
                    }
                    e0 += wx0; e1 += wx1; e2 += wx2;
                }
            }
            cost += (double)filled * opt_gpu_ns_per_px / 1000.0 + (double)(bey - bsy) * 0.02;
            gpu_pixels_total += filled;
            i += 1 + (imm & 0x3FF) / 4;
        }
        else if (op == 5) {
            u32 pages = imm & 0x7FFFF;
            gpu_cur->has_tex = 1;
            if (opt_hang_mode == 4 && fsc_src_mem) gpu_hung = 1;   /* G4B: page table in a DDR list: FSC/TMU AXI deadlock model */
            if (opt_read_credit && fsc_src_mem) {
                u32 pos = (i >= fsc_list_off) ? i - fsc_list_off : 0u;
                u32 after = (fsc_count > pos + 1u + pages) ? fsc_count - pos - 1u - pages : 0u;
                if (after > (u32)opt_fsc_prefetch_words) {
                    if (violations < 5) fprintf(stderr, "VIOLATION t=%.0fus OP_TEXTURE inside a DDR list with %u words still to fetch after its page table (> %d, ladder L15's proven value): FSC deadlock (D2e)\n", now_us, after, opt_fsc_prefetch_words);
                    violations++; gpu_hung = 1;
                }
            }
            if (((imm >> 19) & 3u) == 0u) { u32 k; tmu_npages = pages < 1024u ? pages : 1024u; for (k = 0; k < tmu_npages; k++) tmu_pages[k] = w[i + 1u + k]; }
            cost += 1.0 + 3.6 * (double)pages;   /* G4B: ~16 us for 4 pages, ~233 us for 64 (C1b/D1 probes) */
            i += 1 + pages;
        }
        else if (op == 4) { i += 1 + (imm & 0x1FF) / 4; }
        else { fprintf(stderr, "GPU: unknown op %u\n", op); violations++; i++; }
    }
    /* completion time: serialized behind any running list */
    {
        double start = (gpu_busy_until > now_us) ? gpu_busy_until : now_us;
        gpu_busy_until = start + cost + 20.0;
        gpu_cur->done_us = gpu_busy_until;
        gpu_q_n++;
        gpu_cur = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* FSC + FIFO model                                                    */
/* ------------------------------------------------------------------ */
static u32 fifo_words[4096]; static u32 fifo_n = 0;
static u32 fifo_mmio_words = 0;   /* D2f: words written to TDFD by Core1 MMIO during the case (FIFO density) */
static u32 fifo_isr = 0;
static u32 fsc_state = 0, fsc_cmd = 0, fsc_n = 0;
static int fsc_resync_words = 0;   /* words swallowed by a pre-existing stream (test knob) */

static void gpu_run_words(const u32 *w, u32 n)
{
    gpu_exec_list(w, n);
}

/* CommandParser view: one continuous ST1 stream.  Packet (FSC command)
 * boundaries are invisible to it (CommandParser.v never uses tlast), so a
 * command split over two ST0 packets is parsed exactly like an unsplit one.
 * Complete commands are executed as soon as they are whole; a trailing
 * partial command waits for the next packet. */
static u32 cs_buf[200000]; static u32 cs_n = 0;
static u32 sim_cmd_words(u32 w)
{
    u32 op = w >> 28, imm = w & 0x0FFFFFFF;
    switch (op) {
    case 1: return 2;
    case 3: return 1 + ((imm & 0x3FF) >> 2);
    case 4: return 1 + ((imm & 0x1FF) >> 2);
    case 5: return 1 + (imm & 0x7FFFF);
    default: return 1;
    }
}
static void parser_feed(const u32 *w, u32 n)
{
    u32 ready = 0, i;
    if (cs_n + n > 200000u) { fprintf(stderr, "sim: parser stream overflow\n"); violations++; cs_n = 0; return; }
    for (i = 0; i < n; i++) cs_buf[cs_n + i] = w[i];
    cs_n += n;
    while (ready < cs_n) {
        u32 c = sim_cmd_words(cs_buf[ready]);
        if (ready + c > cs_n) break;
        ready += c;
    }
    if (ready) {
        gpu_run_words(cs_buf, ready);
        for (i = ready; i < cs_n; i++) cs_buf[i - ready] = cs_buf[i];
        cs_n -= ready;
    }
}

static void fsc_feed(u32 word)
{
    if (fsc_resync_words > 0) { fsc_resync_words--; return; }
    if (fsc_state == 0) {
        u32 in = (word >> 28) & 3, out = (word >> 30) & 3, bytes = word & 0x0FFFFFFF;
        if (in == 0 && out == 0) return;        /* NOP */
        fsc_cmd = word; fsc_count = bytes / 4; fsc_state = 1;
        (void)in; (void)out;
    } else if (fsc_state == 1) {
        u32 in = (fsc_cmd >> 28) & 3;
        if (in == 3) {
            /* MEM -> ST1: fetch from DDR now */
            u32 addr = word, i;
            /* G4B (B1 run 5, D2 run 12): the FSC serves both list fetches and texture page fetches.
               A list fetch that starts while an OP_TEXTURE sent earlier has not been executed by the
               parser yet ends in a deadlock (the fetch stalls on the parser, the parser waits for
               pages the FSC cannot fetch).  Model: any queued, uncompleted job containing an
               OP_TEXTURE at the time a MEM->ST1 command arrives.  A real GPU hangs for good. */
            /* D2f: the same mechanism exists on G4C (the read credit changed what the list fetch
               does to the AXI read channel, not that the FSC is busy with it): checked in both models */
            {
                u32 k; advance_to(now_us);
                for (k = 0; k < gpu_q_n; k++) {
                    gpu_qjob *j = &gpu_q[(gpu_q_head + k) % GPU_Q_MAX];
                    if (j->has_tex && j->done_us > now_us) {
                        if (violations < 5) fprintf(stderr, "VIOLATION t=%.0fus DDR list fetch %08x queued behind a pending texture load (FSC deadlock, rule 11)\n", now_us, addr);
                        violations++; gpu_hung = 1; fsc_state = 0; return;
                    }
                }
            }
            if (!((addr >= RIX_WIN_BASE && addr + fsc_count * 4 <= RIX_WIN_END) || (addr >= ARENA_BASE && addr + fsc_count * 4 <= ARENA_BASE + sizeof(arena))) || (addr & 63) || (fsc_count & 15)) {
                fprintf(stderr, "FSC MEM fetch outside window or misaligned: %08x +%u words\n", addr, fsc_count); violations++;
                fsc_state = 0; return;
            }
            if (fsc_count > 160000u) { fprintf(stderr, "sim: list too large\n"); violations++; fsc_state = 0; return; }
            for (i = 0; i < fsc_count; i++) fsc_buf[i] = rd32(addr + i * 4);
            fsc_src_mem = 1; fsc_mem_addr = addr; fsc_list_off = cs_n;   /* the list follows the words left over in the parser stream */
            parser_feed(fsc_buf, fsc_count);
            fsc_src_mem = 0; fsc_mem_addr = 0;
            fsc_state = 0;
        } else {
            fsc_n = 0; fsc_state = 2;           /* address beat consumed; payload follows */
            if (fsc_count == 0) fsc_state = 0;
        }
    } else {
        fsc_buf[fsc_n++] = word;
        if (fsc_n == fsc_count) { parser_feed(fsc_buf, fsc_n); fsc_state = 0; }
    }
}

/* ------------------------------------------------------------------ */
/* PL330 model                                                         */
/* ------------------------------------------------------------------ */
static u32 dmac_csr7 = 0, dmac_fsrc = 0, dbginst0, dbginst1;
/* D2d: channel 6 pushes list words into the ST0 FIFO (TDFD, fixed address); modelled at 0.2 us/word */
static struct { int valid; double done_us; } dma6_job;
static u32 dma6_words;
static void dmac_exec(u32 pc, u32 channel)
{
    u32 sar = 0, dar = 0, ccr = 0, lc0 = 0, lc1 = 0, off = 0, bytes = 0, fifo_words_pushed = 0;
    u32 lp0_pc = 0, lp1_pc = 0;
    const unsigned char *p = sim_ptr(pc);
    int guard = 0;
    for (;;) {
        unsigned char op = p[off];
        if (++guard > 1000000) { fprintf(stderr, "PL330 runaway\n"); violations++; return; }
        if (op == 0xBC) { u32 v; memcpy(&v, p + off + 2, 4); if (p[off+1] == 0) sar = v; else if (p[off+1] == 1) ccr = v; else dar = v; off += 6; }
        else if ((op & 0xFD) == 0x20) { if (op & 2) { lc1 = p[off+1] + 1; lp1_pc = off + 2; } else { lc0 = p[off+1] + 1; lp0_pc = off + 2; } off += 2; }
        else if (op == 0x04) { off++; }
        else if (op == 0x08 && !((ccr >> 14) & 1) && dar == RIX_FIFO_BASE + FIFO_TDFD) {
            /* fixed-address store into the FIFO: dst burst = n_dst beats of sz_dst bytes, taken from sar */
            u32 n_dst = ((ccr >> 18) & 15) + 1, sz_dst = 1u << ((ccr >> 15) & 7), i, chunk = n_dst * sz_dst;
            const u32 *src = (const u32 *)sim_ptr(sar);
            if (sz_dst != 4) { fprintf(stderr, "sim: FIFO DMA with %u-byte beats\n", sz_dst); violations++; }
            for (i = 0; i < chunk / 4; i++) { if (fifo_n >= 512) { fifo_isr |= FIFO_INT_TPOE; violations++; break; } fifo_words[fifo_n++] = src[i]; fifo_words_pushed++; }
            sar += chunk; bytes += chunk; off++;
        }
        else if (op == 0x08) {
            u32 n = ((ccr >> 4) & 15) + 1, sz = 1u << ((ccr >> 1) & 7), i;
            u32 chunk = n * sz;
            if (sar != dar) { fprintf(stderr, "sim: only in-place supported\n"); violations++; }
            if (((ccr >> 28) & 7) == 1) {
                unsigned char *q = sim_ptr(sar);
                for (i = 0; i + 1 < chunk; i += 2) { unsigned char t = q[i]; q[i] = q[i+1]; q[i+1] = t; }
            }
            sar += chunk; dar += chunk; bytes += chunk; off++;
        }
        else if ((op & 0xF8) == 0x38 || (op & 0xF8) == 0x28 || op == 0x3C || op == 0x38) {
            u32 lc = (op >> 2) & 1, jump = p[off+1];
            if (lc == 0) { if (--lc0 != 0) { off = off - jump; continue; } }
            else { if (--lc1 != 0) { off = off - jump; continue; } }
            off += 2;
            (void)lp0_pc; (void)lp1_pc;
        }
        else if (op == 0x13 || op == 0x12) off++;
        else if (op == 0x00) break;
        else { fprintf(stderr, "PL330 bad opcode %02x at %u\n", op, off); violations++; return; }
    }
    if (channel == 6) {
        dma6_job.done_us = now_us + (double)fifo_words_pushed * 0.2;
        dma6_job.valid = 1;
        dma6_words += fifo_words_pushed;
        return;
    }
    dma_job.done_us = ((dma_job.valid && dma_job.done_us > now_us) ? dma_job.done_us : now_us) + (double)bytes * 2.0 / opt_dma_mb_s;  /* read+write */
    dma_job.valid = 1;
    if (gpu_any_pending() && gpu_writing(sar - bytes)) {
        fprintf(stderr, "VIOLATION: DMA swap of buffer %08x while GPU still writes it\n", sar - bytes); violations++;
    }
}

/* ------------------------------------------------------------------ */
/* timers / GIC                                                        */
/* ------------------------------------------------------------------ */
static u32 pt_load, pt_ctrl, pt_pending; static double pt_expire_us = 0;
static u32 gicd_isenabler0, gicc_ctlr, gicc_pmr, gic_active29;
static u32 pt_irq_pending(void)
{
    if (pt_ctrl & 1) { advance_to(now_us); if (now_us >= pt_expire_us) { pt_pending = 1; pt_ctrl &= ~1u; } }
    return pt_pending;
}
void sim_wfi(void)
{
    if (!(gicd_isenabler0 & (1u << 29)) || !(gicc_ctlr & 1) || gicc_pmr == 0) {
        fprintf(stderr, "sim: WFI with no wake-up source armed -> would hang forever\n"); violations++; exit(3);
    }
    if (pt_irq_pending()) return;
    if (!(pt_ctrl & 1)) { fprintf(stderr, "sim: WFI with timer stopped -> hang\n"); violations++; exit(3); }
    advance_to(pt_expire_us);
    pt_pending = 1; pt_ctrl &= ~1u;
}

/* ------------------------------------------------------------------ */
/* MMIO dispatch                                                       */
/* ------------------------------------------------------------------ */
u32 sim_mmio_rd(u32 a)
{
    advance_to(now_us + 0.1);
    if (a == RIX_ID_BASE) return RIX_HW_MAGIC;
    if (a == RIX_ID_BASE + 8) return RIX_HW_VERSION;
    if (a == RIX_FIFO_BASE + FIFO_ISR) return fifo_isr;
    if (a == RIX_FIFO_BASE + FIFO_TDFV) return 508u - fifo_n;
    if (a == VDMA_BASE + VDMA_MM2S_SA1) return vdma_sa;
    if (a == GT_BASE + GT_COUNT_LO) return sim_now_ticks();
    if (a == GT_BASE + GT_CTRL) return 1u;
    if (a == PT_BASE + PT_ISR) return pt_irq_pending();
    if (a == PT_BASE + PT_CTRL) return pt_ctrl;
    if (a == GIC_DIST_BASE + GICD_ISPENDR0) return pt_irq_pending() ? (1u << 29) : 0;
    if (a == GIC_DIST_BASE + GICD_IPRIORITYR + 28) return 0;
    if (a == GIC_CPU_BASE + GICC_CTLR) return gicc_ctlr;
    if (a == GIC_CPU_BASE + GICC_PMR) return gicc_pmr;
    if (a == GIC_CPU_BASE + GICC_HPPIR) return (pt_irq_pending() && (gicd_isenabler0 & (1u << 29)) && gicc_pmr) ? 29u : 1023u;
    if (a == GIC_CPU_BASE + GICC_IAR) { if (pt_irq_pending() && !gic_active29) { gic_active29 = 1; pt_pending = 0; return 29u; } return 1023u; }
    if (a == DMAC_S_BASE + DMAC_DSR) return 0;
    if (a == SLCR_APER_CLK_CTRL) return 0x01FFCCCDu;
    if (a == SLCR_DMAC_RST_CTRL) return 0u;
    if (a == DMAC_S_BASE + DMAC_DBGSTATUS) return 0;
    if (a == DMAC_S_BASE + DMAC_CSR(7)) { if (dma_job.valid) { advance_to(now_us); } return dma_job.valid ? 1u : 0u; }
    if (a == DMAC_S_BASE + DMAC_CSR(6)) { if (dma6_job.valid && now_us >= dma6_job.done_us) dma6_job.valid = 0; return dma6_job.valid ? 1u : 0u; }
    if (a == DMAC_S_BASE + DMAC_FSRC) return dmac_fsrc;
    if (a == DMAC_S_BASE + DMAC_FTR(7) || a == DMAC_S_BASE + DMAC_FTR(6)) return 0;
    fprintf(stderr, "sim: unmodelled MMIO read %08x\n", a); violations++;
    return 0;
}

void sim_mmio_wr(u32 a, u32 v)
{
    advance_to(now_us + 0.1);
    if (a == RIX_FIFO_BASE + FIFO_ISR) { fifo_isr &= ~v; return; }
    if (a == RIX_FIFO_BASE + FIFO_TDFR) { if (v == 0xA5) { fifo_n = 0; fifo_isr |= FIFO_INT_TRC; } return; }
    if (a == RIX_FIFO_BASE + FIFO_TDFD) { if (fifo_n >= 512) { fifo_isr |= FIFO_INT_TPOE; violations++; return; } fifo_words[fifo_n++] = v; fifo_mmio_words++; return; }
    if (a == RIX_FIFO_BASE + FIFO_TLF) {
        u32 n = v / 4, i;
        if (n > fifo_n) { fifo_isr |= FIFO_INT_TSE; violations++; return; }
        if (gpu_hung) return;                       /* FSC/parser stuck: no TC, FIFO stays full */
        for (i = 0; i < n; i++) { fsc_feed(fifo_words[i]); if (gpu_hung) { i++; break; } }
        memmove(fifo_words, fifo_words + i, (fifo_n - i) * 4); fifo_n -= i;
        if (!gpu_hung) fifo_isr |= FIFO_INT_TC;
        return;
    }
    if (a == PT_BASE + PT_CTRL) { pt_ctrl = v; if (v & 1) pt_expire_us = now_us + (double)pt_load / TICKS_PER_US; return; }
    if (a == PT_BASE + PT_LOAD) { pt_load = v; return; }
    if (a == PT_BASE + PT_ISR) { pt_pending = 0; return; }
    if (a == GIC_DIST_BASE + GICD_ISENABLER0) { gicd_isenabler0 |= v; return; }
    if (a == GIC_DIST_BASE + GICD_ICENABLER0) { gicd_isenabler0 &= ~v; return; }
    if (a == GIC_DIST_BASE + GICD_ICPENDR0) { if (v & (1u << 29)) pt_pending = 0; return; }
    if (a == GIC_DIST_BASE + GICD_IPRIORITYR + 28) return;
    if (a == GIC_CPU_BASE + GICC_CTLR) { gicc_ctlr = v; return; }
    if (a == GIC_CPU_BASE + GICC_PMR) { gicc_pmr = v; return; }
    if (a == GIC_CPU_BASE + GICC_EOIR) { gic_active29 = 0; return; }
    if (a == DMAC_S_BASE + DMAC_DBGINST0) { dbginst0 = v; return; }
    if (a == DMAC_S_BASE + DMAC_DBGINST1) { dbginst1 = v; return; }
    if (a == DMAC_S_BASE + DMAC_DBGCMD) {
        u32 opc = (dbginst0 >> 16) & 0xFF;
        if ((opc & 0xFD) == 0xA0) { u32 ch = (dbginst0 >> 24) & 7; if (ch != 7 && ch != 6) { fprintf(stderr, "DMAGO wrong channel\n"); violations++; } dmac_exec(dbginst1, ch); }
        else if (opc == 0x01) { if (((dbginst0 >> 8) & 7) == 6) dma6_job.valid = 0; else dma_job.valid = 0; }
        return;
    }
    fprintf(stderr, "sim: unmodelled MMIO write %08x = %08x\n", a, v); violations++;
}

/* ------------------------------------------------------------------ */
/* glue for the application                                            */
/* ------------------------------------------------------------------ */
#ifndef SIM_RIX_NO_APP
void mmu_init(void) { }
void mmu_prepare_return(void) { }
void zzgear_core1_main(void *env);
#endif

static void reset_model(void)
{
    memset(window, 0x5A, sizeof(window));   /* stale content from a previous run */
    memset(mbox, 0, 256);
    memset(regs, 0, sizeof(regs));
    if (opt_dirty_regs) {
        /* a previous client left the bank in a hostile state: scissor on
           with a tiny box, color mask off, odd resolution, TMU on */
        regs[0] = 0x7F; regs[3] = 0x00000005; regs[6] = (100u << 16) | 100u; regs[7] = (120u << 16) | 120u;
        regs[9] = (200u << 16) | 320u; regs[8] = 7; regs[16] = 0x01000000; regs[12] = 0xFFFFFFFF; regs[10] = 0xFFFFFFFF;
    }
    now_us = 0; next_vblank_us = 16667.0; next_68k_poll_us = 2000.0;
    vdma_sa = 0x01000000; vdma_scanning = vdma_sa; vdma_scanning_prev = 0;
    pan_pending = 0; last_ready_seen = 0; presenter_pans = 0; mbox_snapshot_ready = 0;
    dma_job.valid = 0; gpu_busy_until = 0; gpu_q_head = 0; gpu_q_n = 0; gpu_cur = NULL; gpu_q_overflow = 0; ring_hazards = 0;
    fifo_n = 0; fifo_isr = 0; fsc_state = 0; fsc_n = 0; cs_n = 0; fifo_mmio_words = 0; fsc_list_off = 0;
    gpu_hung = (opt_hang_mode == 1);
    pt_ctrl = 0; pt_pending = 0; gicd_isenabler0 = 0; gicc_ctlr = 0; gicc_pmr = 0; gic_active29 = 0;
    violations = 0; gpu_pixels_total = 0; gpu_cmds = 0; gpu_lists = 0;
}

/* model entry points for other host benches (ZZQuake2RIX host_rix: the engine drives
   librixgpu/ZZGL against this model; built with -DSIM_RIX_NO_APP, no ZZGear app) */
void sim_model_reset(void) { reset_model(); }
int  sim_model_violations(void) { return violations; }
double sim_model_now_us(void) { return now_us; }
u32  sim_model_pixels(void) { return gpu_pixels_total; }
u32  sim_model_fifo_words(void) { return fifo_mmio_words; }
void sim_model_set_read_credit(int on) { opt_read_credit = on; }

#ifndef SIM_RIX_NO_APP
static u32 mb(u32 idx) { return rd32(RIX_SHARED_ARM + idx * 4); }

static int run_case(const char *name, u32 mode, u32 flags, u32 limit, u32 unused_param2, int stop_pans, int coherent, int pre_resync_words)
{
    u32 v;
    reset_model();
    opt_coherent = coherent;
    stop_after_pans = stop_pans;
    fsc_resync_words = pre_resync_words;
    v = flags; memcpy(sim_ptr(RIX_SHARED_ARM + SH_FLAGS * 4), &v, 4);
    v = mode; memcpy(sim_ptr(RIX_SHARED_ARM + SH_MODE * 4), &v, 4);
    v = limit; memcpy(sim_ptr(RIX_SHARED_ARM + SH_PARAM1 * 4), &v, 4);
    v = unused_param2; memcpy(sim_ptr(RIX_SHARED_ARM + SH_PARAM2 * 4), &v, 4);
    zzgear_core1_main(NULL);
    printf("== %s: status=%02x error=%08x caps=%03x dl=%u swap=%u idle=%u\n", name, mb(SH_STATUS), mb(SH_ERROR), mb(SH_CAPS),
           mb(SH_STATS + RS_DL_MODE), mb(SH_STATS + RS_SWAP_MODE), mb(SH_STATS + RS_IDLE_MODE));
    printf("   frames built=%u submitted=%u fenced=%u swapped=%u published=%u flipped=%u dropped=%u stalls=%u fence_timeouts=%u\n",
           mb(SH_STATS + RS_FRAMES_BUILT), mb(SH_STATS + RS_FRAMES_SUBMITTED), mb(SH_STATS + RS_FRAMES_FENCED), mb(SH_STATS + RS_FRAMES_SWAPPED),
           mb(SH_STATS + RS_FRAMES_PUBLISHED), mb(SH_STATS + RS_FRAMES_FLIPPED), mb(SH_STATS + RS_FRAMES_DROPPED), mb(SH_STATS + RS_STALL_NOBUF), mb(SH_STATS + RS_FENCE_TIMEOUTS));
    printf("   tris/frame=%u (mesh %u, culled %u/frame) chunks=%u (%.1f/frame) words/frame=%u ringwait=%u us/frame xform=%u us/frame\n",
           mb(SH_STATS + RS_TRIS_PER_FRAME), mb(SH_STATS + RS_MESH_TRIS),
           mb(SH_STATS + RS_FRAMES_BUILT) ? mb(SH_STATS + RS_TRIS_CULLED) / mb(SH_STATS + RS_FRAMES_BUILT) : 0,
           mb(SH_STATS + RS_CHUNKS), mb(SH_STATS + RS_FRAMES_BUILT) ? (double)mb(SH_STATS + RS_CHUNKS) / mb(SH_STATS + RS_FRAMES_BUILT) : 0.0,
           mb(SH_STATS + RS_FRAMES_BUILT) ? mb(SH_STATS + RS_DL_WORDS) / mb(SH_STATS + RS_FRAMES_BUILT) : 0,
           mb(SH_STATS + RS_FRAMES_BUILT) ? mb(SH_STATS + RS_T_RINGWAIT) / mb(SH_STATS + RS_FRAMES_BUILT) : 0,
           mb(SH_STATS + RS_FRAMES_BUILT) ? mb(SH_STATS + RS_T_XFORM) / mb(SH_STATS + RS_FRAMES_BUILT) : 0);
    if (gpu_q_overflow) printf("   (model: %u deferred fence-row writes applied early)\n", gpu_q_overflow);
    printf("   virtual elapsed=%.1f ms  gpu avg=%u us min=%u max=%u  swap avg=%u us  loop avg=%u us  68k pans=%u  violations=%d\n",
           now_us / 1000.0,
           mb(SH_STATS + RS_FRAMES_FENCED) ? mb(SH_STATS + RS_T_GPU) / mb(SH_STATS + RS_FRAMES_FENCED) : 0, mb(SH_STATS + RS_T_GPU_MIN), mb(SH_STATS + RS_T_GPU_MAX),
           mb(SH_STATS + RS_FRAMES_SWAPPED) ? mb(SH_STATS + RS_T_SWAP) / mb(SH_STATS + RS_FRAMES_SWAPPED) : 0,
           mb(SH_STATS + RS_FRAMES_PUBLISHED) ? mb(SH_STATS + RS_T_LOOP) / mb(SH_STATS + RS_FRAMES_PUBLISHED) : 0,
           presenter_pans, violations);
    if (mode == RIX_MODE_SELFTEST) {
        printf("   selftest: fence fifo=%u us mem=%u us clear=%u/%u us frame=%u/%u us tris=%u/%u us cull=%u dirty=%u dma=%u(%u us) neon=%u us wfi=%u(%u us) ticks/s=%u\n",
               mb(SH_SELFTEST_DATA + SD_FENCE_FIFO_US), mb(SH_SELFTEST_DATA + SD_FENCE_MEM_US),
               mb(SH_SELFTEST_DATA + SD_CLEAR_US_MIN), mb(SH_SELFTEST_DATA + SD_CLEAR_US_AVG),
               mb(SH_SELFTEST_DATA + SD_FRAME_US_MIN), mb(SH_SELFTEST_DATA + SD_FRAME_US_AVG),
               mb(SH_SELFTEST_DATA + SD_TRIS_US_MIN), mb(SH_SELFTEST_DATA + SD_TRIS_US_AVG),
               mb(SH_SELFTEST_DATA + SD_CULL_US_MIN), mb(SH_SELFTEST_DATA + SD_DIRTY_US_MIN),
               mb(SH_SELFTEST_DATA + SD_DMA_TEST), mb(SH_SELFTEST_DATA + SD_DMA_US_MIN), mb(SH_SELFTEST_DATA + SD_NEON_US_MIN),
               mb(SH_SELFTEST_DATA + SD_WFI_TEST), mb(SH_SELFTEST_DATA + SD_WFI_SLEEP_US), mb(SH_SELFTEST_DATA + SD_SECOND_TICKS));
        {
            u32 k;
            printf("   ladder: stages=%u failed=%u |", mb(SH_SELFTEST_DATA + SD_LADDER_STAGES), mb(SH_SELFTEST_DATA + SD_LADDER_FAILED));
            for (k = 0; k < SD_LADDER_MAX; k++) {
                u32 b = SH_SELFTEST_DATA + SD_LADDER + k * SD_LADDER_SLOTS;
                if (mb(b) == 0xEEEEEEEEu) { printf(" L%u:-", k); continue; }
                printf(" L%u:%04x/%x", k, mb(b) & 0xFFFF, mb(b + 2));
            }
            printf("\n");
        }
    }
    if (mode != RIX_MODE_SELFTEST && unused_param2 != 0u) {
        u32 fr = mb(SH_SELFTEST_DATA + SD_ROOM_FRAMES), d = fr ? fr : 1u;
        printf("   ROOM scene %u: %u frames, vis %u/%u surf, tris world %u model %u fx %u, clipped %u (near %u), px %u, binds %u (+%u skipped, %u pages, %u us), regens %u (%u us), xform %u us, pool %u pages%s\n",
               mb(SH_SELFTEST_DATA + SD_ROOM_SCENE), fr, mb(SH_SELFTEST_DATA + SD_ROOM_SURF_VIS) / d, mb(SH_SELFTEST_DATA + SD_ROOM_SURFACES),
               mb(SH_SELFTEST_DATA + SD_ROOM_TRIS_WORLD) / d, mb(SH_SELFTEST_DATA + SD_ROOM_TRIS_MODEL) / d, mb(SH_SELFTEST_DATA + SD_ROOM_TRIS_FX) / d,
               mb(SH_SELFTEST_DATA + SD_ROOM_TRIS_CLIPPED) / d, mb(SH_SELFTEST_DATA + SD_ROOM_TRIS_NEAR) / d, mb(SH_SELFTEST_DATA + SD_ROOM_PIXELS) / d,
               mb(SH_SELFTEST_DATA + SD_ROOM_BINDS) / d, mb(SH_SELFTEST_DATA + SD_ROOM_BINDS_SKIPPED) / d, mb(SH_SELFTEST_DATA + SD_ROOM_BIND_PAGES) / d, mb(SH_SELFTEST_DATA + SD_ROOM_BIND_US) / d,
               mb(SH_SELFTEST_DATA + SD_ROOM_REGENS) / d, mb(SH_SELFTEST_DATA + SD_ROOM_T_REGEN_US) / d, mb(SH_SELFTEST_DATA + SD_ROOM_T_XFORM_US) / d,
               mb(SH_SELFTEST_DATA + SD_ROOM_TEX_PAGES), mb(SH_SELFTEST_DATA + SD_ROOM_ZTRICK) ? ", ztrick" : "");
        printf("   ROOM bind-fence waits: %u us/frame (model time)\n", mb(SH_SELFTEST_DATA + SD_ROOM_BIND_WAIT_US) / d);
        if (flags & RIXF_FULLCLEAR) {
            /* the room is closed: after a frame no pixel of the visible 640x480 may still hold the
               clear colour (magenta 0xF81F).  Checks the two buffers of the last two frames. */
            u32 holes = 0, b, k;
            for (b = 0; b < 2; b++) for (k = 0; k < 640u * 480u; k++) { u16 v; memcpy(&v, sim_ptr((b ? RIX_FB2_ARM : RIX_FB1_ARM) + k * 2u), 2); if (v == 0xF81Fu) { if (holes < 8) printf("   hole: FB%u x=%u memory-row=%u\n", b + 1u, k % 640u, k / 640u); holes++; } }
            printf("   ROOM holes (clear colour left in the last two frames): %u\n", holes);
            if (holes) { printf("   HOLES: the clipped scene does not cover the screen\n"); violations++; }
        }
        if (mb(SH_STATUS) != ST_STOPPED) { printf("   ROOM did not finish cleanly\n"); violations++; }
    }
    return violations;
}

int main(int argc, char **argv)
{
    int bad = 0;
    if (argc > 2 && strcmp(argv[1], "roomimage") == 0) {
        /* ROOM <scene> <frame>: renders that frame (OFFSCREEN NOSWAP NOPIPE FULLCLEAR) and writes
           room_frame.pgm with one grey level per triangle tag (structure check, not colours) */
        u32 scene = (u32)atoi(argv[2]), frame = (argc > 3) ? (u32)atoi(argv[3]) : 1u, k;
        FILE *o;
        extern u32 zzroom_dbg_x, zzroom_dbg_y, zzroom_dbg_frame;
        if (argc > 5) { zzroom_dbg_x = (u32)atoi(argv[4]); zzroom_dbg_y = (u32)atoi(argv[5]); zzroom_dbg_frame = frame - 1u; }
        bad += run_case("roomimage", RIX_MODE_OFFSCREEN, RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, frame, scene, 0, 1, 0);
        o = fopen("room_frame.pgm", "wb");
        fprintf(o, "P5\n640 480\n255\n");
        for (k = 0; k < 614400u; k += 2) { const unsigned char *p = (const unsigned char *)sim_ptr(((frame & 1) ? RIX_FB1_ARM : RIX_FB2_ARM) + k); unsigned v = p[0] | (p[1] << 8); fputc(v == 0xF81Fu ? 255 : (int)(32 + (v & 0x7F)), o); }
        fclose(o);
        printf("room_frame.pgm written (scene %u, frame %u)\n", scene, frame);
        return bad ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "dirtycheck") == 0) {
        /* OFFSCREEN NOSWAP NOPIPE: frames alternate FB1/FB2; the dirty-rectangle
           clear (default) must leave exactly the image of a full clear, frame
           after frame (stale pixels of the previous use of the buffer must be gone) */
        static unsigned char img_a[614400], img_b[614400];
        int frames = 24, f, diff = 0, k;
        for (f = 1; f <= frames; f++) {
            /* dirty first: the model fills the window with stale 0x5A5A before each run,
               so the first frame of each buffer must clear all of it (C1 found this) */
            bad += run_case("dirty", RIX_MODE_OFFSCREEN, RIXF_NOSWAP | RIXF_NOPIPE, f, 0, 0, 1, 0);
            memcpy(img_b, sim_ptr((f & 1) ? RIX_FB1_ARM : RIX_FB2_ARM), 614400);
            bad += run_case("fullclear", RIX_MODE_OFFSCREEN, RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, f, 0, 0, 1, 0);
            memcpy(img_a, sim_ptr((f & 1) ? RIX_FB1_ARM : RIX_FB2_ARM), 614400);
            { int d = 0, na = 0; for (k = 0; k < 614400; k += 2) { unsigned va = img_a[k] | (img_a[k+1] << 8), vb = img_b[k] | (img_b[k+1] << 8); if (va != vb) d++; if (va) na++; }
              if (d) printf("frame %d: %d px differ (gear covers %d px)\n", f, d, na);
              diff += d; }
        }
        printf("DIRTYCHECK: %d differing pixels over %d frames (expect 0)\n", diff, frames);
        return diff ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "image") == 0) {
        /* write the simulator's image of frame N (tags) as a PGM for a look */
        int f = (argc > 2) ? atoi(argv[2]) : 1; FILE *o; int k;
        bad += run_case("image", RIX_MODE_OFFSCREEN, RIXF_NOSWAP | RIXF_NOPIPE, f, 0, 0, 1, 0);
        o = fopen("sim_frame.pgm", "wb"); fprintf(o, "P5\n640 480\n255\n");
        for (k = 0; k < 614400; k += 2) { const unsigned char *p = (const unsigned char *)sim_ptr(((f & 1) ? RIX_FB1_ARM : RIX_FB2_ARM) + k); unsigned v = p[0] | (p[1] << 8); fputc(v ? (int)(64 + (v & 0x7F)) : 0, o); }
        fclose(o); printf("sim_frame.pgm written\n");
        return bad;
    }
    bad += run_case("SELFTEST", RIX_MODE_SELFTEST, 0, 0, 0, 0, 1, 0);
    bad += run_case("SELFTEST PROBE (model has no depth/blend/alpha: results 2/3 expected, transport must stay clean)", RIX_MODE_SELFTEST, RIXF_PROBE, 0, 0, 0, 1, 0);
    {
        u32 run = rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_RUN) * 4), f = rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_FAILED) * 4);
        printf("   probes run=%02x failed=%u depth=%u blend=%u alpha=%u tex64=%u us tex256=%u us state=%u us\n", run, f,
               rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_DEPTH) * 4), rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_BLEND) * 4),
               rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_ALPHA) * 4), rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_TEX64_US) * 4),
               rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_TEX256_US) * 4), rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_STATE_US) * 4));
        printf("   placement: %u (pix %08x) tex %08x z %08x\n", rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_PLACE) * 4), rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_PLACE_PIX) * 4),
               rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_PLACE_TEX) * 4), rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_PLACE_Z) * 4));
        printf("   modulate: %u (pix %04x) lightmap blend: %u (pix %04x)\n", rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_MOD) * 4), rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_MOD_PIX) * 4),
               rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_LMBLEND) * 4), rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_LMBLEND_PIX) * 4));
        if (run != 0x1FFu || f != 0u) { printf("   PROBES: not all run (expect 1ff) or one failed in the model\n"); bad++; }
        {   /* D2c: the five DDR-list read probes ran and read the expected pixels (the model has no AXI deadlock for reads: a real-GPU question) */
            u32 drun = rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_DDR_RUN) * 4), dres = rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_DDR_RES) * 4);
            printf("   DDR read probes: run=%x results=%05x (nibbles 9..13: 1 works) pix %08x %08x %08x\n", drun, dres,
                   rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_DDR_PIX0) * 4), rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_DDR_PIX1) * 4), rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_PROBE_DDR_PIX2) * 4));
            {   /* the model has no depth test or blending (pixels 2 expected there); what it checks is transport: all five sent, none hung */
                u32 k, hung = 0u;
                for (k = 0; k < 5u; k++) if (((dres >> (4u * k)) & 15u) == 3u) hung = 1u;
                if (drun != 0x1Fu || hung) { printf("   DDR read probes: not all run or one hung in the model\n"); bad++; }
            }
        }
    }
    bad += run_case("MONITOR coherent 68k, 300 frames", RIX_MODE_MONITOR, 0, 300, 0, 0, 1, 0);
    bad += run_case("MONITOR vblank-granular 68k, 120 frames", RIX_MODE_MONITOR, 0, 120, 0, 0, 0, 0);
    bad += run_case("MONITOR FULLCLEAR 120", RIX_MODE_MONITOR, RIXF_FULLCLEAR, 120, 0, 0, 1, 0);
    bad += run_case("MONITOR FIFO transport 60", RIX_MODE_MONITOR, RIXF_DLFIFO, 60, 0, 0, 1, 0);
    bad += run_case("MONITOR NEON swap SPIN 60", RIX_MODE_MONITOR, RIXF_NODMA | RIXF_SPIN, 60, 0, 0, 1, 0);
    bad += run_case("MONITOR PRESWAP 60", RIX_MODE_MONITOR, RIXF_PRESWAP, 60, 0, 0, 1, 0);
    bad += run_case("MONITOR NOTEX 60", RIX_MODE_MONITOR, RIXF_NOTEX, 60, 0, 0, 1, 0);
    bad += run_case("MONITOR STATIC 60", RIX_MODE_MONITOR, RIXF_STATIC, 60, 0, 0, 1, 0);
    bad += run_case("MONITOR stop from 68k after 50 pans", RIX_MODE_MONITOR, 0, 0, 0, 50, 1, 0);
    bad += run_case("MONITOR with stale FSC stream (resync)", RIX_MODE_MONITOR, 0, 30, 0, 0, 1, 700);
    opt_dirty_regs = 1;
    bad += run_case("MONITOR with hostile leftover register bank", RIX_MODE_MONITOR, 0, 30, 0, 0, 1, 0);
    bad += run_case("SELFTEST with hostile leftover register bank", RIX_MODE_SELFTEST, 0, 0, 0, 0, 1, 0);
    opt_dirty_regs = 0;
    bad += run_case("OFFSCREEN 500 pipelined", RIX_MODE_OFFSCREEN, RIXF_VERIFY, 500, 0, 0, 1, 0);
    bad += run_case("OFFSCREEN 200 NOPIPE NOSWAP", RIX_MODE_OFFSCREEN, RIXF_NOSWAP | RIXF_NOPIPE, 200, 0, 0, 1, 0);
    bad += run_case("OFFSCREEN 100 NOCLEAR", RIX_MODE_OFFSCREEN, RIXF_NOCLEAR, 100, 0, 0, 1, 0);
    bad += run_case("OFFSCREEN 60 FIFO transport (chunks as packets)", RIX_MODE_OFFSCREEN, RIXF_DLFIFO, 60, 0, 0, 1, 0);
    bad += run_case("STRESS (unsupported for the gear: expect E160)", RIX_MODE_STRESS, 0, 5, 0, 0, 1, 0);
    { u32 e = rd32(RIX_SHARED_ARM + SH_ERROR * 4); if ((e & 0xFFFF) != RIXE_BADMODE) { printf("   STRESS: expected E160, got %08x\n", e); bad++; } }
    opt_presenter_dead = 1;
    bad += run_case("MONITOR with dead presenter (expect E140 NOFLIP)", RIX_MODE_MONITOR, 0, 300, 0, 0, 1, 0);
    opt_presenter_dead = 0;
    /* hang models: the ladder must name the stage and send nothing afterwards */
    opt_hang_mode = 1;
    bad += run_case("SELFTEST on a GPU hung by an earlier run (expect E1A0, nothing else sent)", RIX_MODE_SELFTEST, 0, 0, 0, 0, 1, 0);
    {
        u32 e = rd32(RIX_SHARED_ARM + SH_ERROR * 4), st = rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_LADDER_STAGES) * 4);
        if ((e & 0xFFFF) != RIXE_GPU_HUNG || st != 1u || fifo_n == 0) { printf("   HANG MODEL 1: wrong report (error %08x stages %u fifo_n %u)\n", e, st, fifo_n); bad++; }
    }
    opt_hang_mode = 2;
    bad += run_case("SELFTEST on a GPU that hangs after a scissored memset (expect E1B6 at L6)", RIX_MODE_SELFTEST, 0, 0, 0, 0, 1, 0);
    {
        u32 e = rd32(RIX_SHARED_ARM + SH_ERROR * 4), f = rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_LADDER_FAILED) * 4);
        if ((e & 0xFFFF) != RIXE_LADDER + LAD_FENCE || f != LAD_FENCE + 1u) { printf("   HANG MODEL 2: wrong report (error %08x failed %u)\n", e, f); bad++; }
    }
    opt_hang_mode = 4; opt_read_credit = 0;   /* G4B model: BINDFIFO keeps L15 out of the ladder and the binds in the FIFO */
    bad += run_case("G4B: MONITOR 120 BINDFIFO on a GPU that deadlocks on a page table inside a DDR list (expect clean: tables go through the FIFO)", RIX_MODE_MONITOR, RIXF_BINDFIFO, 120, 0, 0, 1, 0);
    bad += run_case("G4B: ROOM A 5 BINDFIFO (bind fences, same GPU; expect clean)", RIX_MODE_OFFSCREEN, RIXF_BINDFIFO | RIXF_NOSWAP | RIXF_NOPIPE, 5, RIX_SCENE_ROOM_A, 0, 1, 0);
    bad += run_case("G4B: SELFTEST TEXDDR BINDFIFO on the same GPU (expect E1BF: L15 hung, nothing after)", RIX_MODE_SELFTEST, RIXF_TEXDDR | RIXF_BINDFIFO, 0, 0, 0, 1, 0);
    {
        u32 e = rd32(RIX_SHARED_ARM + SH_ERROR * 4), f = rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_LADDER_FAILED) * 4);
        if ((e & 0xFFFF) != RIXE_LADDER + LAD_TEX_DDR || f != LAD_TEX_DDR + 1u) { printf("   HANG MODEL 4: wrong report (error %08x failed %u)\n", e, f); bad++; }
    }
    opt_hang_mode = 3; opt_read_credit = 1;
    bad += run_case("SELFTEST on a GPU that hangs on a fully masked memset (run-1 behaviour; expect clean)", RIX_MODE_SELFTEST, 0, 0, 0, 0, 1, 0);
    opt_dirty_regs = 1;
    bad += run_case("MONITOR 60, same hang model, hostile register bank (expect clean)", RIX_MODE_MONITOR, 0, 60, 0, 0, 1, 0);
    opt_dirty_regs = 0;
    opt_hang_mode = 0;
    opt_poll_period_us = 25000.0;
    bad += run_case("MONITOR slow 68k poll 25 ms, 120 frames", RIX_MODE_MONITOR, 0, 120, 0, 0, 1, 0);
    opt_poll_period_us = 2000.0;
    /* ROOM benchmark scenes (D2): transport cleanliness, no holes after clipping, all strategies */
    bad += run_case("ROOM A 10 OFFSCREEN NOSWAP NOPIPE FULLCLEAR (holes check)", RIX_MODE_OFFSCREEN, RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, 10, RIX_SCENE_ROOM_A, 0, 1, 0);
    bad += run_case("ROOM B 10 OFFSCREEN NOSWAP NOPIPE FULLCLEAR (holes check)", RIX_MODE_OFFSCREEN, RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, 10, RIX_SCENE_ROOM_B, 0, 1, 0);
    bad += run_case("ROOM C 10 OFFSCREEN NOSWAP NOPIPE FULLCLEAR (holes check)", RIX_MODE_OFFSCREEN, RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, 10, RIX_SCENE_ROOM_C, 0, 1, 0);
    bad += run_case("ROOM B32 6 OFFSCREEN NOSWAP NOPIPE FULLCLEAR (holes check)", RIX_MODE_OFFSCREEN, RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, 6, RIX_SCENE_ROOM_B32, 0, 1, 0);
    bad += run_case("ROOM A 30 MONITOR ZTRICK (presented, pipelined)", RIX_MODE_MONITOR, RIXF_ZTRICK, 30, RIX_SCENE_ROOM_A, 0, 1, 0);
    bad += run_case("ROOM A 5 SYNC (ladder mode: every chunk and bind fence waited)", RIX_MODE_OFFSCREEN, RIXF_SYNC | RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, 5, RIX_SCENE_ROOM_A, 0, 1, 0);
    bad += run_case("ROOM B 3 SYNC", RIX_MODE_OFFSCREEN, RIXF_SYNC | RIXF_NOSWAP | RIXF_NOPIPE, 3, RIX_SCENE_ROOM_B, 0, 1, 0);
    /* ZZGL 0.1 (J1): the GL demo scene through the GL API; holes check with FULLCLEAR (magenta clear through glClear) */
    bad += run_case("GL 10 OFFSCREEN NOSWAP NOPIPE FULLCLEAR (ZZGL demo, holes check)", RIX_MODE_OFFSCREEN, RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, 10, RIX_SCENE_GL, 0, 1, 0);
    bad += run_case("GL 60 MONITOR (ZZGL demo, presented, pipelined)", RIX_MODE_MONITOR, 0, 60, RIX_SCENE_GL, 0, 1, 0);
    bad += run_case("GL 5 SYNC (ladder mode)", RIX_MODE_OFFSCREEN, RIXF_SYNC | RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, 5, RIX_SCENE_GL, 0, 1, 0);
    /* D2f (1.2.1): the default bind path is the D2d one (FIFO packet + bind fence) whatever L15 says;
       the model would now catch the 1.2 in-list binds (first ROOM A frame: thousands of words after
       the first page table).  BINDLIST: each bind as its own short DDR list. */
    bad += run_case("ROOM B 30 OFFSCREEN pipelined (D2g default: BINDLIST since L15 passed)", RIX_MODE_OFFSCREEN, 0, 30, RIX_SCENE_ROOM_B, 0, 1, 0);
    {
        u32 caps = rd32(RIX_SHARED_ARM + SH_CAPS * 4), chunks = rd32(RIX_SHARED_ARM + (SH_STATS + RS_CHUNKS) * 4), fw = fifo_mmio_words;
        printf("   default binds: caps %08x (TEX_DDR %s), chunks %u for 30 frames (%.1f/frame), FIFO words %u (%.0f/frame)\n", caps, (caps & RIXCAP_TEX_DDR) ? "set" : "MISSING", chunks, chunks / 30.0, fw, fw / 30.0);
        if (!(caps & RIXCAP_TEX_DDR) || chunks < 30u * 100u || fw > 30u * 1200u) { printf("   default binds: L15 capability missing, binds did not close the chunks, or FIFO words not reduced (BINDLIST not the default?)\n"); bad++; }
    }
    bad += run_case("ROOM B 30 OFFSCREEN BINDPKT (FIFO packet binds forced: the D2d path)", RIX_MODE_OFFSCREEN, RIXF_BINDPKT, 30, RIX_SCENE_ROOM_B, 0, 1, 0);
    {
        u32 chunks = rd32(RIX_SHARED_ARM + (SH_STATS + RS_CHUNKS) * 4), fw = fifo_mmio_words;
        printf("   BINDPKT: chunks %u for 30 frames (%.1f/frame), FIFO words %u (%.0f/frame)\n", chunks, chunks / 30.0, fw, fw / 30.0);
        if (chunks < 30u * 100u || fw < 30u * 2000u) { printf("   BINDPKT: FIFO words lower than a packet path would give\n"); bad++; }
    }
    bad += run_case("ROOM B 30 OFFSCREEN BINDLIST (each bind a short DDR list: 2 FIFO words instead of 80)", RIX_MODE_OFFSCREEN, RIXF_BINDLIST, 30, RIX_SCENE_ROOM_B, 0, 1, 0);
    {
        u32 chunks = rd32(RIX_SHARED_ARM + (SH_STATS + RS_CHUNKS) * 4), fw = fifo_mmio_words;
        printf("   BINDLIST: chunks %u for 30 frames (%.1f/frame), FIFO words %u (%.0f/frame)\n", chunks, chunks / 30.0, fw, fw / 30.0);
        if (chunks < 30u * 100u || fw > 30u * 1200u) { printf("   BINDLIST: chunks or FIFO words unexpected\n"); bad++; }
    }
    bad += run_case("ROOM A 10 OFFSCREEN BINDLIST FULLCLEAR (holes check)", RIX_MODE_OFFSCREEN, RIXF_BINDLIST | RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, 10, RIX_SCENE_ROOM_A, 0, 1, 0);
    bad += run_case("ROOM A 30 MONITOR BINDLIST (presented, pipelined)", RIX_MODE_MONITOR, RIXF_BINDLIST, 30, RIX_SCENE_ROOM_A, 0, 1, 0);
    bad += run_case("GL 10 OFFSCREEN BINDLIST FULLCLEAR (holes check)", RIX_MODE_OFFSCREEN, RIXF_BINDLIST | RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, 10, RIX_SCENE_GL, 0, 1, 0);
    bad += run_case("ROOM A 5 SYNC BINDLIST", RIX_MODE_OFFSCREEN, RIXF_BINDLIST | RIXF_SYNC | RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, 5, RIX_SCENE_ROOM_A, 0, 1, 0);
    bad += run_case("MONITOR 5 BINDLIST BINDFIFO (no L15 -> BINDLIST refused at init: expect E125 ring config)", RIX_MODE_MONITOR, RIXF_BINDLIST | RIXF_BINDFIFO, 5, 0, 0, 1, 0);
    { u32 e = rd32(RIX_SHARED_ARM + SH_ERROR * 4); if ((e & 0xFFFF) != RIXE_RING_CFG) { printf("   BINDLIST without L15: expected RING_CFG, got %08x\n", e); bad++; } }
    bad += run_case("ROOM A 5 MONITOR DLFIFO (FIFO transport, no DDR list)", RIX_MODE_MONITOR, RIXF_DLFIFO, 5, RIX_SCENE_ROOM_A, 0, 1, 0);
    /* D2d: FIFO fed by the PL330 (channel 6 -> TDFD); the model pushes words at 0.2 us each */
    bad += run_case("SELFTEST DLDMA (PL330 -> FIFO self-test: count, fence integrity, throughput)", RIX_MODE_SELFTEST, RIXF_DLDMA, 0, 0, 0, 1, 0);
    {
        u32 st = rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_DMAFIFO_STATUS) * 4), fp = rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_DMAFIFO_FENCE_PIX) * 4);
        printf("   DMA-FIFO: status %u (2 = 16-beat bursts usable), count %08x, fence pixel %04x, ns/word B16 %u B1 %u, dl_mode %u\n", st,
               rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_DMAFIFO_COUNT) * 4), fp, rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_DMAFIFO_NS_B16) * 4),
               rd32(RIX_SHARED_ARM + (SH_SELFTEST_DATA + SD_DMAFIFO_NS_B1) * 4), rd32(RIX_SHARED_ARM + (SH_STATS + RS_DL_MODE) * 4));
        if (st != 2u || fp != 0xD0D1u || rd32(RIX_SHARED_ARM + (SH_STATS + RS_DL_MODE) * 4) != 3u) { printf("   DMA-FIFO self-test wrong in the model\n"); bad++; }
    }
    bad += run_case("MONITOR DLDMA 60 (gear, chunks carried by the PL330 into the FIFO)", RIX_MODE_MONITOR, RIXF_DLDMA, 60, 0, 0, 1, 0);
    bad += run_case("OFFSCREEN 60 DLDMA", RIX_MODE_OFFSCREEN, RIXF_DLDMA, 60, 0, 0, 1, 0);
    bad += run_case("ROOM A 5 MONITOR DLDMA", RIX_MODE_MONITOR, RIXF_DLDMA, 5, RIX_SCENE_ROOM_A, 0, 1, 0);
    bad += run_case("ROOM B 3 OFFSCREEN DLDMA FULLCLEAR (holes check)", RIX_MODE_OFFSCREEN, RIXF_DLDMA | RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, 3, RIX_SCENE_ROOM_B, 0, 1, 0);
    bad += run_case("ROOM B 3 OFFSCREEN DLFIFO FULLCLEAR (holes check)", RIX_MODE_OFFSCREEN, RIXF_DLFIFO | RIXF_NOSWAP | RIXF_NOPIPE | RIXF_FULLCLEAR, 3, RIX_SCENE_ROOM_B, 0, 1, 0);
    printf("%s\n", bad ? "SIMULATION: VIOLATIONS" : "SIMULATION: ALL CASES CLEAN");
    return bad ? 1 : 0;
}
#endif /* SIM_RIX_NO_APP */
