/* ZZSolarWindsRIX A0 Core1 presenter. Derived from the validated ZZGear/
 * GLExcess presenter: init ladder, ring, fence, PL330, VDMA observation and
 * exception return. New: previous-frame copy for temporal accumulation.
 * SPDX-License-Identifier: 0BSD */
#include <stddef.h>
#include "rix_shared.h"
#include "rixgpu.h"
#include "rixframe.h"
#include "rixtex.h"
#include "rixdesc.h"
#include "zzgldemo.h"
#include "solarwinds.h"
#include "history.h"

#define SW_POOL_ARM 0x30400000u
#define SW_POOL_PAGES 4096u
#define SW_ZBUF_ARM 0x30020000u
extern void mmu_init(void);
extern void mmu_prepare_return(void);

#ifdef RIX_HOST_SIM
#define sh ((volatile u32 *)rix_ptr(RIX_SHARED_ARM))
#else
static volatile u32 * const sh = (volatile u32 *)(uintptr_t)RIX_SHARED_ARM;
#endif
/* rix_config has a member named sh too: use a local alias when filling it */
#define ST(i)  sh[SH_STATS + (i)]
#define SD(i)  sh[SH_SELFTEST_DATA + (i)]
#define HG(i)  sh[SH_HIST_GPU + (i)]
#define HS(i)  sh[SH_HIST_SWAP + (i)]
#define HL(i)  sh[SH_HIST_LOOP + (i)]

static const u32 buf_arm[3] = { RIX_FB0_ARM, RIX_FB1_ARM, RIX_FB2_ARM };
/* Texture pool/bind tables share the validated inner-WB, outer-NC arena. */
#define SW_BIND_ARM 0x300C0000u

static rix_texture g_probe_texture; /* tiny white init-probe texture, not the scene */
static rix_frame g_frame;
static u32 g_flags, g_mode, g_features, g_tmu_cfg, g_clear_rgba;
static u32 g_scene, g_frame_no;            /* presenter scene fixed to GL; SH_PARAM2 selects the preset */
static u32 g_fence_seq;
static u32 sw_previous_buffer = 0xFFFFFFFFu;
static u32 g_t_start;
/* elapsed time: the global timer is read as 32-bit ticks (333 MHz: wraps every
   12.88 s), so the elapsed time is accumulated from deltas (C1 run 1 reported
   "elapsed 1.769 s" after 27.5 s: 2 wraps) */
static u32 g_el_last;
static unsigned long long g_el_us;
static void elapsed_update(void)
{
    u32 now = rix_now();
    g_el_us += rix_ticks_to_us(now - g_el_last);
    g_el_last = now;
    ST(RS_T_ELAPSED_LO) = (u32)g_el_us;
    ST(RS_T_ELAPSED_HI) = (u32)(g_el_us >> 32);
}
/* first chunk submitted for the frame being built into buffer b: the GPU is
   busy from there to the fence (RS_GPU_BUSY_US), the rest of "submit->fence"
   is Core1's transform + the part of the list build the GPU had to wait for */
static u32 g_first_submit[3];

static inline u32 now_us(u32 t0) { return rix_ticks_to_us((u32)(rix_now() - t0)); }
static inline u16 bswap16(u16 v) { return (u16)((v >> 8) | (v << 8)); }

static void hist_add(volatile u32 *h, u32 bins, u32 us)
{
    u32 b = us / 100u;
    if (b >= bins) b = bins - 1u;
    h[b]++;
}
/* loop histogram: 250 us bins (0..32 ms) so that a 60 Hz loop is not saturated */
static void hist_add_loop(volatile u32 *h, u32 bins, u32 us)
{
    u32 b = us / 250u;
    if (b >= bins) b = bins - 1u;
    h[b]++;
}


/* ------------------------------------------------------------------ */
/* texture: white 8x8 probe -> RGB565 pages in the                    */
/* window pool (librixgpu rixtex)                                      */
/* ------------------------------------------------------------------ */
static int texture_write(void)
{
    unsigned char white[8*8*3];
    u32 t0=rix_now();
    int rc=rix_texpool_init(RIX_TEX_ARM,RIX_TEX_BYTES/RIX_TEX_PAGE_BYTES,0);
    if(rc)return rc;
    rc=rix_tex_alloc(&g_probe_texture,8,8,1);if(rc)return rc;
    for(u32 i=0;i<sizeof white;i++)white[i]=255;
    rix_tex_write_rgb888(&g_probe_texture,white,0);
    ST(RS_T_TEXLOAD)=rix_now()-t0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* display lists                                                       */
/* ------------------------------------------------------------------ */
static u32 next_fence_value(void)
{
    g_fence_seq++;
    return (g_fence_seq % 65534u) + 1u;       /* never 0, never 0xFFFF */
}

/* one-time state: pipeline, TMU, texture pages (page table through the FIFO) */
static int init_scene(void)
{
    rix_dl dl;
    u32 i, fence, polls = 0u;
    int rc;
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    rix_dl_frame_head(&dl, RIX_FB0_ARM, g_features, g_clear_rgba, 0);
    rix_dl_reg(&dl, RIX_REG_RESOLUTION, RIX_XY(RIX_W, RIX_H_ALLOC));
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
    rix_dl_reg(&dl, RIX_REG_Y_OFFSET, 0u);
    rix_dl_reg(&dl, RIX_REG_SCISSOR_START, RIX_XY(0u, 0u));
    rix_dl_reg(&dl, RIX_REG_SCISSOR_END, RIX_XY(RIX_W, RIX_H_ALLOC));
    if (g_features & RIX_FEAT_TMU0) {
        rix_dl_reg(&dl, RIX_REG_TMU0_ENV, RIX_TMU_ENV_REPLACE);     /* GL_REPLACE as the demo */
        rix_dl_reg(&dl, RIX_REG_TMU0_ENVCOLOR, 0u);
        rix_dl_reg(&dl, RIX_REG_TMU0_CONFIG, g_probe_texture.cfg);         /* 8x8 RGB565, GL_LINEAR mag */
        rix_dl_texture(&dl, 0u, g_probe_texture.pages, g_probe_texture.base);
        (void)i;
    }
    fence = next_fence_value();
    rix_dl_fence(&dl, fence, g_features);
    rc = rix_submit_fifo(&dl);
    if (rc) return rc;
    rc = rix_fence_wait(RIX_FB0_ARM, fence, 500000u, &sh[SH_STOP], &polls);
    sh[SH_FENCE_LAST] = fence;
    return rc;
}

/* one textured triangle for the init ladder (L13/L14) */
static u32 g_probe_desc[RIX_TRI_DESC_WORDS];
static int build_probe_triangle(void)
{
    rix_vertex a, b, c;
    a.sx = 200.0f; a.sy = 151.0f; a.z = 0.5f; a.invw = 1.0f; a.u = 0.0f; a.v = 0.0f; a.r = a.g = a.b = a.a = 1.0f;
    b = a; b.sx = 440.0f; b.sy = 151.0f; b.u = 1.0f; b.v = 0.0f;
    c = a; c.sx = 320.0f; c.sy = 401.0f; c.u = 0.5f; c.v = 1.0f;
    return rix_tri_desc(&a, &b, &c, g_probe_desc);
}

/* chunk ring: librixgpu rixframe (7 x 16 KiB slots after rix_init) */
static const u32 g_color_bufs[3] = { RIX_FB0_ARM, RIX_FB1_ARM, RIX_FB2_ARM };
static int ring_setup(void)
{
    rix_ring_cfg rc;
    u32 b;
    rc.ring_base = RIX_RING_ARM; rc.ring_slots = RIX_RING_SLOTS; rc.chunk_bytes = RIX_CHUNK_BYTES;
    rc.fence_x = RIX_CHUNK_FENCE_X; rc.color_bufs = g_color_bufs; rc.color_count = 3u; rc.ring_cacheable = 0u;   /* ring in the NC window */
    rc.bind_base = SW_BIND_ARM; rc.bind_cacheable = 1u;   /* D2f BINDLIST: two short lists in the arena (inner WB / outer NC: cleaned by MVA) */
    rix_frame_set_sync((g_flags & RIXF_SYNC) ? 1 : 0);
    {
        int r = rix_ring_init(&rc);
        if (r) return r;
    }
    /* D2f: binds as FIFO packets (rule 11, the D2d path) unless BINDLIST asks for the short DDR
       lists; the 1.2 "binds inside the chunks" is gone (FSC deadlock on the first frame, D2e) */
    if (g_flags & RIXF_BINDLIST) return rix_frame_set_bind_mode(RIX_BIND_LIST);   /* RIXE_RING_CFG when L15 did not pass */
    return rix_frame_set_bind_mode(RIX_BIND_FIFO);
}

/* ------------------------------------------------------------------ */
/* one gear frame: transform, cull, sort, dirty clear, triangles, fence */
/* ------------------------------------------------------------------ */

static int frame_render(u32 b,u32 fence,u32 *tris_out)
{
    int rc;
    zzgl_target tg;
    u32 stamp=rix_now();
    if(sw_needs_history() && sw_previous_buffer!=0xFFFFFFFFu) {
        if(sw_previous_buffer==b)return RIXE_BADMODE;
#ifdef RIX_HOST_SIM
        extern void sim_check_history(u32 dst,u32 src);
        sim_check_history(buf_arm[b],buf_arm[sw_previous_buffer]);
#endif
        sw_history_copy(rix_ptr(buf_arm[b]),rix_ptr(buf_arm[sw_previous_buffer]),RIX_FB_BYTES);
        dsb();
    }
    /* Application-specific stats: otherwise-unused submit/xform sums. */
    ST(RS_T_SUBMIT)+=now_us(stamp);
    tg.color=buf_arm[b];tg.fence=fence;
    zzgl_set_target(&tg);
    stamp=rix_now();
    rc=zzgldemo_render(g_frame_no,tris_out);
    ST(RS_T_XFORM)+=now_us(stamp);
    if(!rc)rc=zzgl_swap();
    if(rc)return rc;
    g_frame=*zzgl_frame();g_frame_no++;
    g_first_submit[b]=g_frame.first_submit_tick;
    ST(RS_CHUNKS)+=g_frame.chunks;ST(RS_DL_WORDS)+=g_frame.words;
    ST(RS_T_RINGWAIT)+=g_frame.ringwait_us;
    return 0;
}

/* ------------------------------------------------------------------ */
/* scanout observation                                                 */
/* ------------------------------------------------------------------ */
static u32 vdma_cur, vdma_prev, pending_buf = 0xFFFFFFFFu, t_published;
#define fwpan_req ((volatile u32 *)rix_ptr(RIX_FWPAN_REQ_ARM))
#define fwpan_ack ((volatile u32 *)rix_ptr(RIX_FWPAN_ACK_ARM))

/* OPTIONAL: Core0 presentation mailbox (only with the patched FAST1) */
static int fwpan_hello(void)
{
    u32 t0 = rix_now();
    fwpan_ack[0] = 0u; fwpan_ack[1] = 0u; dsb();
    fwpan_req[2] = 0u; fwpan_req[3] = 0u; fwpan_req[1] = 0u; dsb();
    fwpan_req[0] = RIX_FWPAN_MAGIC; dsb();
    while (now_us(t0) < 500000u) {
        dmb();
        if (fwpan_ack[0] == RIX_FWPAN_HELLO) return 1;
        if (sh[SH_STOP]) return 0;
        rix_idle_us(200u);
    }
    return 0;
}
static void fwpan_request(u32 seq, u32 buf)
{
    fwpan_req[2] = buf - RIX_FB_TO_68K; fwpan_req[3] = RIX_PITCH; dsb();
    fwpan_req[1] = seq; dsb();
}
static u32 dropped_buf = 0xFFFFFFFFu, dropped_at_changes;

static int is_ours(u32 sa)
{
    return sa == RIX_FB0_ARM || sa == RIX_FB1_ARM || sa == RIX_FB2_ARM;
}

static void observe_vdma(void)
{
    u32 sa = rix_vdma_sa();
    if (sa != vdma_cur) {
        vdma_prev = vdma_cur;
        vdma_cur = sa;
        ST(RS_VDMA_CHANGES)++;
        sh[SH_VDMA_SA] = sa;
        if (is_ours(sa)) {
            if (sa == pending_buf) {
                /* Count each published image once, not all transient SA1
                   register changes. This is address observation, not proof
                   of completed optical scanout. Buffer guards are unchanged. */
                ST(RS_FRAMES_FLIPPED)++;
                ST(RS_T_PUBLISH_TO_FLIP) += now_us(t_published);
                ST(RS_FLIP_SAMPLES)++;
                pending_buf = 0xFFFFFFFFu;
            }
        }
    }
    ST(RS_VDMA_SA_LAST) = vdma_cur;
}

static int buffer_free(u32 b)
{
    u32 a = buf_arm[b];
    if (a == dropped_buf) {
        /* published, never seen flipping: the 68k may still PAN to it later */
        if (ST(RS_VDMA_CHANGES) - dropped_at_changes >= 2u) dropped_buf = 0xFFFFFFFFu;
        else return 0;
    }
    return a != vdma_cur && a != vdma_prev && a != pending_buf;
}

/* ------------------------------------------------------------------ */
/* MONITOR                                                             */
/* ------------------------------------------------------------------ */
static int run_monitor(u32 limit)
{
    u32 seq = 0u, b, t_loop0, t_x, t_gpu, t_swap, tris, fence;
    u32 wait_t0, last_draw = 0u;
    int rc;
    vdma_cur = rix_vdma_sa(); vdma_prev = 0u; pending_buf = 0xFFFFFFFFu;
    if (g_flags & RIXF_FWPAN) { if (!fwpan_hello()) return RIXE_FWPAN_ABSENT; }
    while (!sh[SH_STOP] && !(g_scene==RIX_SCENE_GL && zzgldemo_done())) {
        if (limit && seq >= limit) break;
        t_loop0 = rix_now();
        b = seq % 3u;

        /* 1. wait until buffer b is neither scanned, just-unscanned nor pending */
        sh[SH_LOOP_STATE] = 1u;
        observe_vdma();
        wait_t0 = rix_now();
        if (!buffer_free(b)) ST(RS_STALL_NOBUF)++;
        while (!buffer_free(b)) {
            if (sh[SH_STOP]) goto done;
            rix_idle_us(100u);
            observe_vdma();
            if (now_us(wait_t0) > 3000000u) {
                if (ST(RS_FRAMES_FLIPPED) == 0u && seq >= 3u) return RIXE_NOFLIP;
                wait_t0 = rix_now();     /* keep waiting; the 68k may be slow */
            }
        }
        {
            u32 w = now_us(wait_t0);
            ST(RS_T_WAITBUF) += w;
            if (w > ST(RS_T_WAITBUF_MAX)) ST(RS_T_WAITBUF_MAX) = w;
        }

        /* Fixed 60 Hz upper bound; original physics use 60 Hz simulation steps. */
        wait_t0=rix_now();
        if (seq) while (now_us(last_draw) < 16667u) {
            if (sh[SH_STOP]) goto done;
            rix_idle_us(100u); observe_vdma();
        }
        ST(RS_T_IDLE)+=now_us(wait_t0);
        last_draw = rix_now();
        /* 2. build + submit, chunk by chunk (the GPU starts on the first chunk) */
        sh[SH_LOOP_STATE] = 2u;
        t_x = rix_now();
        fence = next_fence_value();
        rc = frame_render(b, fence, &tris);
        if (rc) return rc;
        ST(RS_T_BUILD) += now_us(t_x);
        if (now_us(t_x) > ST(RS_BUILD_MAX)) ST(RS_BUILD_MAX) = now_us(t_x);
        ST(RS_FRAMES_BUILT)++;
        ST(RS_FRAMES_SUBMITTED)++;
        ST(RS_TRIS_PER_FRAME) = tris;

        /* 3. GPU fence (measured from the first submit) */
        sh[SH_LOOP_STATE] = 3u;
        {
            u32 polls = 0u;
            rc = rix_fence_wait(buf_arm[b], fence, 1000000u, &sh[SH_STOP], &polls);
            ST(RS_FENCE_POLLS) += polls;
        }
        t_gpu = now_us(t_x);
        if (rc) { if (sh[SH_STOP]) goto done; ST(RS_FENCE_TIMEOUTS)++; rix_frame_diag_last(); return rc; }   /* D2f: E120 reports the frame (chunks, fence pixels, FIFO, liveness); D2e printed zeros */
        sh[SH_FENCE_LAST] = fence;
        ST(RS_FRAMES_FENCED)++;
        ST(RS_T_GPU) += t_gpu;
        ST(RS_T_GPU_LAST) = t_gpu;
        ST(RS_GPU_BUSY_US) += now_us(g_first_submit[b]);
        if (ST(RS_T_GPU_MIN) == 0u || t_gpu < ST(RS_T_GPU_MIN)) ST(RS_T_GPU_MIN) = t_gpu;
        if (t_gpu > ST(RS_T_GPU_MAX)) ST(RS_T_GPU_MAX) = t_gpu;
        hist_add(&HG(0), 128u, t_gpu);

        /* 4. byte-order correction of the 480 displayed lines */
        sh[SH_LOOP_STATE] = 4u;
        t_x = rix_now();
        rc = rix_swap_start(buf_arm[b], RIX_FB_BYTES);
        if (rc == 0) rc = rix_swap_wait(500000u);
        t_swap = now_us(t_x);
        if (rc) return rc;
        sw_previous_buffer = b;
        ST(RS_FRAMES_SWAPPED)++;
        ST(RS_T_SWAP) += t_swap;
        ST(RS_T_SWAP_LAST) = t_swap;
        if (ST(RS_T_SWAP_MIN) == 0u || t_swap < ST(RS_T_SWAP_MIN)) ST(RS_T_SWAP_MIN) = t_swap;
        if (t_swap > ST(RS_T_SWAP_MAX)) ST(RS_T_SWAP_MAX) = t_swap;
        hist_add(&HS(0), 64u, t_swap);

        /* 5. publish: one outstanding presentation at a time (no drops) */
        sh[SH_LOOP_STATE] = 5u;
        wait_t0 = rix_now();
        observe_vdma();
        while (pending_buf != 0xFFFFFFFFu) {
            if (sh[SH_STOP]) goto done;
            rix_idle_us(100u);
            observe_vdma();
            if (now_us(wait_t0) > 3000000u) {
                if (ST(RS_FRAMES_FLIPPED) == 0u) return RIXE_NOFLIP;
                ST(RS_FRAMES_DROPPED)++;         /* 68k did not pick it up in 3 s */
                dropped_buf = pending_buf;
                dropped_at_changes = ST(RS_VDMA_CHANGES);
                pending_buf = 0xFFFFFFFFu;
            }
        }
        seq++;
        pending_buf = buf_arm[b];
        t_published = rix_now();
        sh[SH_READY_OFF] = buf_arm[b] - RIX_FB_TO_68K;
        dsb();
        sh[SH_READY] = (seq << 2) | b;
        dsb();
        if (g_flags & RIXF_FWPAN) fwpan_request(seq, buf_arm[b]);
        ST(RS_FRAMES_PUBLISHED)++;

        /* 6. bookkeeping */
        {
            u32 tl = now_us(t_loop0);
            ST(RS_T_LOOP) += tl;
            if (tl > ST(RS_T_LOOP_MAX)) ST(RS_T_LOOP_MAX) = tl;
            hist_add_loop(&HL(0), 128u, tl);
        }
        sh[SH_HEARTBEAT] = seq;
        sh[SH_WATCHDOG] = rix_now();
        elapsed_update();
        dsb();
    }
done:
    return 0;
}

/* ---- CPU exception path (D2c).  startup_return.S installs the payload's own vector table
   (VBAR) at entry and restores the firmware's at exit; an undefined instruction, prefetch abort
   or data abort lands in exc_common, which fills exc_info and calls this on a fresh stack, then
   takes the normal return path to the launcher: the run ends with RIXE_EXCEPTION + type and the
   faulting PC/FAR/FSR in SD_EXC_*, instead of Core1 falling into the firmware's handlers
   (D2b ROOM A DLFIFO: "reset loop", empty log). */
u32 exc_info[8];   /* [0] pc [1] far [2] fsr [3] spsr [4] type [5] - [6] irq count [7] fiq count */
void zzgear_exception(void)
{
    volatile u32 *sd = &sh[SH_SELFTEST_DATA];
    sd[SD_EXC_TYPE] = exc_info[4]; sd[SD_EXC_PC] = exc_info[0]; sd[SD_EXC_FAR] = exc_info[1];
    sd[SD_EXC_FSR] = exc_info[2]; sd[SD_EXC_SPSR] = exc_info[3]; sd[SD_EXC_STAGE] = sh[SH_STAGE];
    sd[SD_EXC_IRQS] = (exc_info[6] & 0xFFFFu) | (exc_info[7] << 16);
    rix_frame_diag_last();                     /* chunk/bind/fence state of the frame being built, if any */
    if (g_flags & RIXF_FWPAN) { fwpan_req[0] = 0u; fwpan_req[1] = 0u; dsb(); }
    rix_shutdown();
    sh[SH_ERROR] = 0x52490000u | (u32)(RIXE_EXCEPTION + (exc_info[4] & 0xFu));
    sh[SH_EXIT_MARK] = RIX_EXIT_MARK;
    sh[SH_STAGE] = 0xFECu;
    sh[SH_STATUS] = ST_FATAL;
    dsb();
    mmu_prepare_return();
}

void zzgear_core1_main(void *env)
{
    rix_config cfg;
    u32 i, limit;
    int rc;
    (void)env;

    /* line 0 and lines >= 2 belong to Core1: reset them before anything else */
    sh[SH_STATUS] = ST_BOOT; sh[SH_ERROR] = 0u; sh[SH_STAGE] = 0x100u;
    sh[SH_READY] = 0u; sh[SH_READY_OFF] = 0u; sh[SH_EXIT_MARK] = 0u; sh[SH_HEARTBEAT] = 0u;
    for (i = 16u; i < SH_END; i++) sh[i] = 0u;
    dsb();
    mmu_init();
    sh[SH_MAGIC] = RIX_CORE1_MAGIC;
    dsb();

    g_flags = sh[SH_FLAGS];
    g_mode = sh[SH_MODE];
    limit = sh[SH_PARAM1];
    g_scene = RIX_SCENE_GL;
    zzgldemo_preset(sh[SH_PARAM2]);
    sw_previous_buffer = 0xFFFFFFFFu;
    g_frame_no = 0u;
    g_features = (g_flags & RIXF_NOTEX) ? 0u : RIX_FEAT_TMU0;
    g_tmu_cfg = RIX_TMU_CFG_A04;              /* replaced by the texture's config after texture_write() */
    g_clear_rgba = 0u;                       /* glClearColor(0,0,0,1) */
    g_fence_seq = 0u;
    sh[SH_BUF0_OFF] = RIX_FB0_ARM - RIX_FB_TO_68K;
    sh[SH_BUF1_OFF] = RIX_FB1_ARM - RIX_FB_TO_68K;
    sh[SH_BUF2_OFF] = RIX_FB2_ARM - RIX_FB_TO_68K;

    cfg.flags = g_flags;
    cfg.idle_tick_us = sh[SH_PARAM3];
    cfg.stats = &sh[SH_STATS]; cfg.sd = &sh[SH_SELFTEST_DATA];
    cfg.caps_out = &sh[SH_CAPS]; cfg.timer_hz_out = &sh[SH_TIMER_HZ];
    cfg.dma_prog = RIX_SHARED_ARM + 0x4000u;
    cfg.probe_tri = NULL; cfg.probe_x = 320u; cfg.probe_y = (RIX_H_ALLOC - 1u) - 230u;   /* GL y=230 -> memory line 250 */
    cfg.tmu_env = RIX_TMU_ENV_REPLACE; cfg.tmu_env_color = 0u; cfg.tmu_cfg = g_tmu_cfg;
    cfg.tex_pages = 0u; cfg.tex_base = RIX_TEX_ARM;
    sh[SH_STAGE] = 0x1A0u;
    rc = texture_write();                       /* texels in DDR before the ladder's textured stages */
    if (rc) goto fatal;
    cfg.tmu_cfg = g_probe_texture.cfg; g_tmu_cfg = g_probe_texture.cfg;
    if (g_features & RIX_FEAT_TMU0) {
        if (build_probe_triangle()) { cfg.probe_tri = g_probe_desc; cfg.tex_pages = g_probe_texture.pages; cfg.tex_base = g_probe_texture.base; }
    }
    sh[SH_STATUS] = ST_INIT; sh[SH_STAGE] = 0x200u;
    dsb();
    rc = rix_init(&cfg);
    sh[SH_INIT_ERROR] = (u32)rc;
    if (rc) goto fatal;
    sh[SH_STAGE] = 0x220u;
    rc = init_scene();
    if (rc) goto fatal;
    rc = ring_setup();                          /* after the last init-time list has been fenced */
    if (rc) goto fatal;
    if (g_scene == RIX_SCENE_GL) {              /* Scene pool in the validated Core1 arena */
        sh[SH_STAGE] = 0x2B0u;
        rc = zzgldemo_init(SW_POOL_ARM, SW_POOL_PAGES, SW_ZBUF_ARM, (g_flags & RIXF_FULLCLEAR) ? 1 : 0);
        if (rc) goto fatal;
    }
    sh[SH_CAPS] |= RIXCAP_TEX;
    sh[SH_STAGE] = 0x300u;
    g_t_start = rix_now(); g_el_last = g_t_start; g_el_us = 0ull;
    sh[SH_STATUS] = ST_RUNNING;
    dsb();

    switch (g_mode) {
    case RIX_MODE_MONITOR:   rc = run_monitor(limit); break;
    case RIX_MODE_OFFSCREEN: rc = RIXE_BADMODE; break;
    case RIX_MODE_STRESS:    rc = RIXE_BADMODE; break;
    case RIX_MODE_SELFTEST:  rc = RIXE_BADMODE; break;
    default: rc = RIXE_BADMODE; break;
    }
    if (rc && !(sh[SH_STOP] && rc == 0xE204)) goto fatal;

    elapsed_update();
    sh[SH_SELFTEST_DATA + SD_EXC_IRQS] = (exc_info[6] & 0xFFFFu) | (exc_info[7] << 16);
    if (g_flags & RIXF_FWPAN) { fwpan_req[0] = 0u; fwpan_req[1] = 0u; dsb(); }
    rix_shutdown();
    sh[SH_STATUS] = ST_STOPPED; sh[SH_EXIT_MARK] = RIX_EXIT_MARK; sh[SH_STAGE] = 0xF00u;
    dsb();
    mmu_prepare_return();
    return;

fatal:
    sh[SH_SELFTEST_DATA + SD_EXC_IRQS] = (exc_info[6] & 0xFFFFu) | (exc_info[7] << 16);
    if (g_flags & RIXF_FWPAN) { fwpan_req[0] = 0u; fwpan_req[1] = 0u; dsb(); }
    rix_shutdown();
    sh[SH_ERROR] = 0x52490000u | ((u32)rc & 0xFFFFu);
    sh[SH_EXIT_MARK] = RIX_EXIT_MARK;
    sh[SH_STAGE] = 0xFEEu;
    sh[SH_STATUS] = ST_FATAL;
    dsb();
    mmu_prepare_return();
}
