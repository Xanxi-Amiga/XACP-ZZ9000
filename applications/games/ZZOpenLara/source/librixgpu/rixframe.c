/* rixframe.c - librixgpu 1.3 - chunk ring and frame API (see rixframe.h)
 * Extracted from ZZGear C1c zzgear_app.c (validated on G4B: six runs).
 * SPDX-License-Identifier: 0BSD */
#include "rixframe.h"

typedef struct { u32 buf; u32 seq; } ring_slot;
static ring_slot g_ring[RIX_RING_MAX_SLOTS];
static rix_ring_cfg g_rc;
static u32 g_next, g_seq;
static int g_ring_ready;

/* ---- 1.3: deferred submission (rixframe.h) ----------------------------------------------------
   Events in stream order: DQ_DRAW = a closed chunk (its list, fence and padding written, its ring
   slot owned until its chunk fence passes, as in direct mode), DQ_BIND = a private copy of a bind
   packet (TMU config + page table + bind fence).  rix_frame_pump() submits from the front while no
   bind is in the GPU: a draw is never submitted behind a bind whose fence has not passed (rule 11),
   and a bind only when the previous one passed (one bind list in use; the other one is free). */
#define DQ_DRAW 1u
#define DQ_BIND 2u
#define DQ_MAX  80u                       /* 1.3.1: up to 32 ring slots: at most 32 draws and 33 binds pending */
typedef struct { u32 kind, seq, n; rix_dl dl; u32 w[2u + 2u + 1u + 64u + 16u]; } dq_ev;
static dq_ev g_dq[DQ_MAX];
static u32 g_dq_head, g_dq_n;
static int g_defer;
static u32 g_hw_bind_seq;                 /* the bind in the GPU (submitted, fence not seen yet), 0 = none */
static u32 g_last_bind_seq;               /* 1.3.1: last bind queued or submitted (both modes) */
static u32 g_last_draw_seq;               /* 1.3.1: chunk fence of the last draw submitted (deferred mode) */
static u32 g_pump_mask = 31u;             /* 1.3.2: rix_frame_set_pump_interval */
static rix_frame *g_cur_frame;
static void frame_diag(rix_frame *f);

#define CHUNK_WORDS  (g_rc.chunk_bytes / 4u)
/* reserve at the end of a chunk: chunk fence + frame fence (64 drain NOPs +
   11 words each in DDR mode) + 16-word padding, plus a state change */
#define CHUNK_TAIL   200u

u32 rix_ring_seq(void) { return g_seq; }

static u32 next_seq(void)
{
    g_seq++;
    if (g_seq >= 65535u) g_seq = 1u;
    return g_seq;
}

void rix_ring_reset(void)
{
    u32 b, k;
    /* the chunk-fence pixels hold whatever earlier memsets left there (the
       init ladder colours pass the signed 16-bit test!): zero them */
    for (b = 0; b < g_rc.color_count; b++) {
        volatile u16 *row = (volatile u16 *)rix_ptr(g_rc.color_bufs[b] + rix_lay_fence_off);
        for (k = g_rc.fence_x; k < g_rc.fence_x + RIX_FENCE_PIXELS; k++) row[k] = 0u;
        for (k = RIX_BIND_FENCE_X; k < RIX_BIND_FENCE_X + RIX_FENCE_PIXELS; k++) row[k] = 0u;
    }
    dsb();
    g_seq = 0u; g_next = 0u;
    for (k = 0; k < RIX_RING_MAX_SLOTS; k++) { g_ring[k].buf = 0u; g_ring[k].seq = 0u; }
    g_dq_head = 0u; g_dq_n = 0u; g_hw_bind_seq = 0u;
    rix_frame_texture_invalidate(0u);
}

static u32 g_flags_sync;


void rix_frame_set_sync(int on) { g_flags_sync = on ? 1u : 0u; if (on) g_defer = 0; }
void rix_frame_set_deferred(int on) { if (g_dq_n == 0u && !g_flags_sync) g_defer = on ? 1 : 0; }
int  rix_frame_deferred(void) { return g_defer; }
int rix_ring_init(const rix_ring_cfg *cfg)
{
    if (cfg->ring_slots < 2u || cfg->ring_slots > RIX_RING_MAX_SLOTS) return (int)RIXE_RING_CFG;
    if ((cfg->ring_base & 63u) || (cfg->chunk_bytes & 63u) || cfg->chunk_bytes < 4096u) return (int)RIXE_RING_CFG;
    if (cfg->fence_x + RIX_FENCE_PIXELS > rix_lay_w || cfg->color_count == 0u) return (int)RIXE_RING_CFG;
    if (cfg->bind_base & 63u) return (int)RIXE_RING_CFG;
    g_rc = *cfg;
    rix_ring_reset();
    g_ring_ready = 1;
    return 0;
}

/* 1.2.1: bind transport (rixframe.h).  RIX_BIND_LIST needs the bind-list area and a GPU that
   completed ladder L15 (RIXCAP_TEX_DDR: a short DDR list carrying a page table). */
static int g_bind_mode = RIX_BIND_FIFO;
int rix_frame_set_bind_mode(int mode)
{
    if (mode == RIX_BIND_FIFO) { g_bind_mode = RIX_BIND_FIFO; return 0; }
    if (mode != RIX_BIND_LIST) return (int)RIXE_RING_CFG;
    if (!g_ring_ready || g_rc.bind_base == 0u) return (int)RIXE_RING_CFG;
    if (!(rix_caps() & RIXCAP_TEX_DDR)) return (int)RIXE_RING_CFG;
    g_bind_mode = RIX_BIND_LIST;
    return 0;
}
int rix_frame_bind_mode(void) { return g_bind_mode; }

/* wait until slot s is no longer referenced by the GPU (1.3 deferred: its chunk may still be in
   the queue, so the queue is pumped while waiting) */
static int ring_wait_slot(rix_frame *f, u32 s, u32 *wait_us)
{
    ring_slot *r = &g_ring[s];
    u32 t0 = rix_now(), us;
    if (r->seq == 0u) return 0;
    while (!rix_fence_passed_at(r->buf, r->seq, g_rc.fence_x)) {
        if (g_defer) { int rc = rix_frame_pump(f); if (rc) return rc; }
        if (rix_ticks_to_us((u32)(rix_now() - t0)) > 2000000u) return (int)RIXE_RING_TIMEOUT;
        rix_idle_us(g_defer ? 10u : 50u);
    }
    us = rix_ticks_to_us((u32)(rix_now() - t0));
    if (wait_us) *wait_us += us;
    if (g_defer) f->dq_wait_us += us;
    return 0;
}

static int chunk_open(rix_frame *f)
{
    int rc = ring_wait_slot(f, g_next, &f->ringwait_us);
    if (rc) return rc;
    f->slot = g_next;
    g_next = (g_next + 1u) % g_rc.ring_slots;
    rix_dl_begin(&f->dl, g_rc.ring_base + f->slot * g_rc.chunk_bytes, CHUNK_WORDS);
    f->dl.frag_shadow = f->fragment;
    f->open = 1;
    return 0;
}

/* G4B rule (B1 run 5, D2 run 12): the FrameStreamingCore fetches both the DDR lists and the
   texture pages.  If a MEM->ST1 list fetch is in progress (and stalled on the parser) when the
   parser executes an OP_TEXTURE, nobody can fetch the pages: deadlock, power cycle.  So a bind
   packet carries its own fence, and no DDR chunk is submitted behind it until that fence passed
   (the parser has executed the OP_TEXTURE and the pages are loaded).  Core1 keeps building the
   next chunk meanwhile; only its submission waits. */
#define BIND_FENCE_WORDS 13u
static u32 bind_fence_words(u32 *w, u32 seq)
{
    u32 n = 0u;
    w[n++] = RIX_OP_NOP; w[n++] = RIX_OP_NOP;                                   /* FIFO drain count (rixgpu.c) */
    w[n++] = RIX_REG(RIX_REG_FEATURES);      w[n++] = RIX_FEAT_SCISSOR;         /* pipeline features off during the memset */
    w[n++] = RIX_REG(RIX_REG_FRAGMENT);      w[n++] = RIX_FRAGMENT_A07B;        /* colour mask on (rule 1) */
    w[n++] = RIX_REG(RIX_REG_SCISSOR_START); w[n++] = RIX_XY(RIX_BIND_FENCE_X, 0u);
    w[n++] = RIX_REG(RIX_REG_SCISSOR_END);   w[n++] = RIX_XY(RIX_BIND_FENCE_X + RIX_FENCE_PIXELS, 1u);
    w[n++] = RIX_REG(RIX_REG_CLEAR_COLOR);   w[n++] = rix_fence_color(seq);
    w[n++] = RIX_FB_CMD(RIX_FENCE_PIXELS, RIX_FB_COLOR_SELECT | RIX_FB_MEMSET);
    return n;
}

/* a chunk or bind fence never passed: record what the GPU did and did not do (launcher: E129/E12A block) */
static u32 g_last_cseq, g_last_words, g_last_op, g_frame_no_diag;
/* g_cur_frame (declared above): the frame being built (rix_frame_begin .. rix_frame_end): the exception path reports it */
static void frame_diag(rix_frame *f)
{
    volatile u32 *sd = rix_sd();
    volatile u16 *row;
    if (!sd || !f || !f->color) return;
    row = (volatile u16 *)rix_ptr(f->color + rix_lay_fence_off);
    dmb();
    sd[SD_FD_CHUNKS] = f->chunks; sd[SD_FD_LAST_CSEQ] = g_last_cseq; sd[SD_FD_CSEQ_PIX] = row[g_rc.fence_x];
    sd[SD_FD_BIND_SEQ] = f->bind_seq; sd[SD_FD_BIND_PIX] = row[RIX_BIND_FENCE_X]; sd[SD_FD_FRAME_PIX] = row[0];
    sd[SD_FD_FIFO_VAC] = rix_fifo_vacancy(); sd[SD_FD_FIFO_ISR] = rix_fifo_isr();
    sd[SD_FD_WORDS] = g_last_words; sd[SD_FD_TRIS] = f->tris; sd[SD_FD_LAST_OPS] = g_last_op; sd[SD_FD_FRAME] = g_frame_no_diag;
    sd[SD_FD_ALIVE] = (rix_alive() == 0) ? 1u : 2u;
}

void rix_frame_diag_last(void) { if (g_cur_frame) frame_diag(g_cur_frame); }

static int bind_fence_wait(rix_frame *f)
{
    u32 t0 = rix_now(), spin = 0u;
    if (f->bind_seq == 0u) return 0;
    while (!rix_fence_passed_at(f->color, f->bind_seq, RIX_BIND_FENCE_X)) {
        u32 us = rix_ticks_to_us((u32)(rix_now() - t0));
        if (us > 2000000u) { frame_diag(f); return (int)RIXE_BIND_TIMEOUT; }
        if (spin < 400u) { cpu_yield(); spin++; } else rix_idle_us(50u);   /* short loads (16 us) are caught by the spin */
    }
    {
        u32 w = rix_ticks_to_us((u32)(rix_now() - t0));
        f->tex_bind_us += w; f->tex_bind_wait_us += w; f->bind_wait_us += w;
    }
    f->bind_seq = 0u;
    return 0;
}

/* ---- 1.3: the deferred queue ---- */
static int dq_bind_busy(rix_frame *f)
{
    if (g_hw_bind_seq == 0u) return 0;
    if (!rix_fence_passed_at(f->color, g_hw_bind_seq, RIX_BIND_FENCE_X)) return 1;
    g_hw_bind_seq = 0u;
    return 0;
}
static u32 g_bind_slot;                            /* BINDLIST: the two 512-byte lists alternate (the previous one was consumed before its fence passed, which chunk_close waited for) */
static int dq_submit_bind(const dq_ev *e)
{
    int rc;
    if (g_bind_mode == RIX_BIND_LIST && g_rc.bind_base != 0u) {
        /* the list written now, at submission: the previous bind passed, so the list it used is
           consumed, and this one alternates with it */
        rix_dl bl;
        u32 i, base = g_rc.bind_base + g_bind_slot * 512u;
        g_bind_slot ^= 1u;
        rix_dl_begin(&bl, base, 128u);
        for (i = 0; i < e->n; i++) rix_dl_word(&bl, e->w[i]);
        if (bl.overflow || bl.n > RIX_BIND_LIST_WORDS) return (int)RIXE_DL_OVERFLOW;
        rix_dl_finish(&bl);
        if (g_rc.bind_cacheable) { dsb(); rix_mem_clean(bl.base, bl.n * 4u); }
        rc = rix_submit(&bl);
    } else {
        rc = rix_submit_words_fifo_nowait(e->w, e->n);
    }
    if (rc == 0) g_hw_bind_seq = e->seq;
    return rc;
}
int rix_frame_pump(rix_frame *f)
{
    int rc;
    if (!g_defer) return 0;
    while (g_dq_n) {
        dq_ev *e = &g_dq[g_dq_head];
        if (dq_bind_busy(f)) break;                /* rule 11, and one bind in the GPU at a time */
        if (e->kind == DQ_DRAW) {
            if (g_hw_bind_seq != 0u) { f->dq_refused++; return (int)RIXE_DEFER; }   /* guard: never reached */
            if (g_last_draw_seq != 0u && rix_fence_passed_at(f->color, g_last_draw_seq, g_rc.fence_x)) f->dq_starved++;
            g_last_draw_seq = e->seq;
            rc = rix_submit(&e->dl);
            if (rc == 0 && f->first_submit_tick == 0u) f->first_submit_tick = rix_now();
        } else rc = dq_submit_bind(e);
        if (rc) return rc;
        g_dq_head = (g_dq_head + 1u) % DQ_MAX;
        g_dq_n--;
    }
    return 0;
}
/* one turn of a wait on the queue: pump, then a short pause; RIXE_DEFER after 2 s */
static int dq_wait_turn(rix_frame *f, u32 t0, u32 *spin)
{
    int rc = rix_frame_pump(f);
    if (rc) return rc;
    if (rix_ticks_to_us((u32)(rix_now() - t0)) > 2000000u) { frame_diag(f); return (int)RIXE_DEFER; }
    if (*spin < 400u) { cpu_yield(); (*spin)++; } else rix_idle_us(10u);
    return 0;
}
static dq_ev *dq_push(rix_frame *f, u32 kind, int *rc)
{
    dq_ev *e;
    *rc = 0;
    if (g_dq_n == DQ_MAX) {
        u32 t0 = rix_now(), spin = 0u;
        while (g_dq_n == DQ_MAX) { *rc = dq_wait_turn(f, t0, &spin); if (*rc) return 0; }
        f->dq_wait_us += rix_ticks_to_us((u32)(rix_now() - t0));
    }
    e = &g_dq[(g_dq_head + g_dq_n) % DQ_MAX];
    g_dq_n++;
    if (g_dq_n > f->dq_peak) f->dq_peak = g_dq_n;
    e->kind = kind;
    return e;
}
u32 rix_frame_last_bind_seq(void) { return g_last_bind_seq; }
void rix_frame_set_pump_interval(u32 n)
{
    u32 m = 1u;
    if (n < 1u) n = 1u;
    if (n > 256u) n = 256u;
    while (m < n) m <<= 1;
    g_pump_mask = m - 1u;
}
int rix_frame_bind_passed(rix_frame *f, u32 seq)
{
    if (seq == 0u || !f || !f->color) return 1;
    return rix_fence_passed_at(f->color, seq, RIX_BIND_FENCE_X) ? 1 : 0;
}
int rix_frame_drain(rix_frame *f)
{
    u32 t0, spin = 0u, us;
    int rc;
    if (!g_defer || !f || !f->color) return 0;
    t0 = rix_now();
    while (g_dq_n || dq_bind_busy(f)) { rc = dq_wait_turn(f, t0, &spin); if (rc) return rc; }
    us = rix_ticks_to_us((u32)(rix_now() - t0));
    f->dq_tex_us += us; f->dq_wait_us += us;
    return 0;
}

static int chunk_close(rix_frame *f, u32 frame_fence)
{
    u32 cseq = next_seq();
    int rc;
    if (!g_defer) {
        rc = bind_fence_wait(f);          /* never a DDR fetch behind a pending texture load */
        if (rc) return rc;
    }
    rix_dl_fence_at(&f->dl, cseq, 0u, g_rc.fence_x);          /* memsets with the pipeline features off, as the ladder and the probes do */
    if (frame_fence) rix_dl_fence(&f->dl, frame_fence, 0u);
    if (f->dl.overflow) return (int)RIXE_DL_OVERFLOW;
    g_ring[f->slot].buf = f->color;
    g_ring[f->slot].seq = cseq;
    rix_dl_finish(&f->dl);                                                         /* 16-word padding before the clean (D2f: it used to be added after it) */
    if (g_rc.ring_cacheable) { dsb(); rix_mem_clean(f->dl.base, f->dl.n * 4u); }   /* the FSC reads DDR, not Core1's cache */
    g_last_cseq = cseq; g_last_words = f->dl.n; g_last_op = ((volatile u32 *)rix_ptr(f->dl.base))[0];
    if (g_defer) {
        /* 1.3: the chunk is complete (fence, padding, clean): queued, submitted by the pump */
        dq_ev *e;
        if (g_hw_bind_seq != 0u || g_dq_n) f->dq_held++;
        e = dq_push(f, DQ_DRAW, &rc);
        if (!e) return rc;
        e->dl = f->dl; e->seq = cseq; e->n = 0u;
        f->chunks++;
        f->words += f->dl.n;
        f->open = 0;
        return rix_frame_pump(f);
    }
    rc = rix_submit(&f->dl);
    if (f->chunks == 0u) f->first_submit_tick = rix_now();
    f->chunks++;
    f->words += f->dl.n;
    f->open = 0;
    if (rc == 0 && (g_flags_sync)) {
        /* SYNC (ladder mode): wait for this chunk's fence before anything else is sent */
        u32 t0 = rix_now();
        while (!rix_fence_passed_at(f->color, cseq, g_rc.fence_x)) {
            if (rix_ticks_to_us((u32)(rix_now() - t0)) > 2000000u) { frame_diag(f); return (int)RIXE_CHUNK_TIMEOUT; }
            rix_idle_us(50u);
        }
        f->ringwait_us += rix_ticks_to_us((u32)(rix_now() - t0));
    }
    return rc;
}

/* a new chunk starts with the frame's registers restored (the fence of the
   previous chunk left the scissor on and the fragment register changed) */
static void chunk_restore(rix_frame *f)
{
    rix_dl_reg(&f->dl, RIX_REG_FRAGMENT, f->fragment);
    if (f->sc_on) {
        rix_dl_reg(&f->dl, RIX_REG_SCISSOR_START, RIX_XY(f->sc_x0, f->sc_y0));
        rix_dl_reg(&f->dl, RIX_REG_SCISSOR_END, RIX_XY(f->sc_x1, f->sc_y1));
        rix_dl_reg(&f->dl, RIX_REG_FEATURES, f->features | RIX_FEAT_SCISSOR);
    } else rix_dl_reg(&f->dl, RIX_REG_FEATURES, f->features);
}

static int chunk_rollover(rix_frame *f)
{
    int rc = chunk_close(f, 0u);
    if (rc) return rc;
    rc = chunk_open(f);
    if (rc) return rc;
    chunk_restore(f);
    return 0;
}

int rix_frame_flush(rix_frame *f)
{
    if (!f || !f->open) return 0;
    return chunk_rollover(f);
}

static int ensure_room(rix_frame *f, u32 words)
{
    if (f->dl.n + words + CHUNK_TAIL > CHUNK_WORDS) return chunk_rollover(f);
    return 0;
}

int rix_frame_begin(rix_frame *f, const rix_frame_desc *d)
{
    int rc;
    u32 x0 = d->rect_x0, y0 = d->rect_y0, x1 = d->rect_x1, y1 = d->rect_y1;
    if (!g_ring_ready) return (int)RIXE_RING_CFG;
    f->color = d->color; f->features = d->features & ~RIX_FEAT_SCISSOR; f->fragment = d->fragment;
    f->chunks = 0u; f->words = 0u; f->tris = 0u; f->ringwait_us = 0u; f->first_submit_tick = 0u;
    f->tex_binds = 0u; f->tex_skipped = 0u; f->tex_bind_us = 0u; f->tex_bind_wait_us = 0u;
    f->sc_on = 0; f->sc_x0 = f->sc_y0 = f->sc_x1 = f->sc_y1 = 0u;
    /* contract: a colour buffer is reused only after its frame fence was waited, so every chunk
       that targeted it is complete: free those slots and zero its chunk-fence pixels, so that a
       stale sequence from an earlier use of the buffer can never be taken for a new one */
    {
        u32 k;
        volatile u16 *row = (volatile u16 *)rix_ptr(d->color + rix_lay_fence_off);
        for (k = 0; k < g_rc.ring_slots; k++) if (g_ring[k].buf == d->color) { g_ring[k].seq = 0u; g_ring[k].buf = 0u; }
        for (k = g_rc.fence_x; k < g_rc.fence_x + RIX_FENCE_PIXELS; k++) row[k] = 0u;
        for (k = RIX_BIND_FENCE_X; k < RIX_BIND_FENCE_X + RIX_FENCE_PIXELS; k++) row[k] = 0u;
        dsb();
    }
    f->bind_seq = 0u; f->bind_wait_us = 0u; g_frame_no_diag++; g_cur_frame = f;
    /* 1.3: the previous frame was waited (contract above): its binds passed, nothing is queued */
    if (g_dq_n) return (int)RIXE_DEFER;
    g_hw_bind_seq = 0u; g_last_draw_seq = 0u; g_last_bind_seq = 0u; f->dq_starved = 0u;
    f->dq_peak = 0u; f->dq_held = 0u; f->dq_wait_us = 0u; f->dq_tex_us = 0u; f->dq_end_us = 0u; f->dq_refused = 0u;
    rc = chunk_open(f);
    if (rc) return rc;
    rix_dl_reg(&f->dl, RIX_REG_COLOR_ADDR, d->color);
    rix_dl_reg(&f->dl, RIX_REG_DEPTH_ADDR, d->zbuf);
    rix_dl_reg(&f->dl, RIX_REG_FRAGMENT, d->fragment);
    if (rix_lay_w != RIX_W || rix_lay_h != RIX_H_ALLOC) {   /* 1.4: another frame layout than the ladder's */
        rix_dl_reg(&f->dl, RIX_REG_RESOLUTION, RIX_XY(rix_lay_w, rix_lay_h));
        rix_dl_reg(&f->dl, RIX_REG_SCISSOR_START, RIX_XY(0u, 0u));
        rix_dl_reg(&f->dl, RIX_REG_SCISSOR_END, RIX_XY(rix_lay_w, rix_lay_h));
    }
    if (x1 == 0u && y1 == 0u) { x0 = 0u; y0 = 0u; x1 = rix_lay_w; y1 = rix_lay_h; }
    if ((d->clear_color || (d->clear_depth && d->zbuf)) && x1 > x0 && y1 > y0) {
        int full = (x0 == 0u && y0 == 0u && x1 == rix_lay_w && y1 == rix_lay_h);
        u32 pixels = (x1 - x0) * (y1 - y0);
        if (!full) {
            rix_dl_reg(&f->dl, RIX_REG_SCISSOR_START, RIX_XY(x0, y0));
            rix_dl_reg(&f->dl, RIX_REG_SCISSOR_END, RIX_XY(x1, y1));
        }
        rix_dl_reg(&f->dl, RIX_REG_FEATURES, full ? 0u : RIX_FEAT_SCISSOR);   /* no TMU/depth/blend during memsets */
        if (d->clear_color) {
            rix_dl_reg(&f->dl, RIX_REG_CLEAR_COLOR, d->clear_rgba);
            rix_dl_clear(&f->dl, pixels);
        }
        if (d->clear_depth && d->zbuf) {
            rix_dl_reg(&f->dl, RIX_REG_CLEAR_DEPTH, d->clear_depth16 & 0xFFFFu);
            rix_dl_clear_depth(&f->dl, pixels);
        }
        if (f->dl.frag_shadow != d->fragment) rix_dl_reg(&f->dl, RIX_REG_FRAGMENT, d->fragment);   /* masks forced by the clears */
    }
    rix_dl_reg(&f->dl, RIX_REG_FEATURES, f->features);
    return f->dl.overflow ? (int)RIXE_DL_OVERFLOW : 0;
}

/* ZZGL 0.1: a clear in the middle of the stream (glClear after 2D draws, or a depth clear before the
   3D part): scissored to rect when given (end exclusive; all 0 = full 640x481), masks re-asserted by
   the memset helpers, the frame's registers restored after.  A 640x481 memset costs about 3 ms of
   GPU time (colour or depth) and is a write: safe on G4B and G4C. */
int rix_frame_clear(rix_frame *f, int color, u32 rgba, int depth, u32 depth16, u32 x0, u32 y0, u32 x1, u32 y1)
{
    u32 pixels;
    int full, rc;
    if (!color && !depth) return 0;
    if (x1 == 0u && y1 == 0u) { x0 = 0u; y0 = 0u; x1 = rix_lay_w; y1 = rix_lay_h; }
    if (x1 <= x0 || y1 <= y0) return 0;
    full = (x0 == 0u && y0 == 0u && x1 == rix_lay_w && y1 == rix_lay_h);
    pixels = (x1 - x0) * (y1 - y0);
    rc = ensure_room(f, 20u);
    if (rc) return rc;
    if (!full) {
        rix_dl_reg(&f->dl, RIX_REG_SCISSOR_START, RIX_XY(x0, y0));
        rix_dl_reg(&f->dl, RIX_REG_SCISSOR_END, RIX_XY(x1, y1));
    }
    rix_dl_reg(&f->dl, RIX_REG_FEATURES, full ? 0u : RIX_FEAT_SCISSOR);
    if (color) { rix_dl_reg(&f->dl, RIX_REG_CLEAR_COLOR, rgba); rix_dl_clear(&f->dl, pixels); }
    if (depth) { rix_dl_reg(&f->dl, RIX_REG_CLEAR_DEPTH, depth16 & 0xFFFFu); rix_dl_clear_depth(&f->dl, pixels); }
    chunk_restore(f);                                         /* FRAGMENT, the frame's scissor, FEATURES */
    return f->dl.overflow ? (int)RIXE_DL_OVERFLOW : 0;
}

int rix_frame_tri(rix_frame *f, const u32 *desc39)
{
    int rc = ensure_room(f, 1u + RIX_TRI_DESC_WORDS);
    if (rc) return rc;
    rix_dl_tri(&f->dl, desc39);
    f->tris++;
    if (g_defer && (f->tris & g_pump_mask) == 0u) return rix_frame_pump(f);   /* 1.3: keep the GPU fed in long batches */
    return 0;
}

int rix_frame_reg(rix_frame *f, u32 reg, u32 val)
{
    int rc = ensure_room(f, 2u);
    if (rc) return rc;
    if (reg == RIX_REG_FEATURES) { f->features = val & ~RIX_FEAT_SCISSOR; if (f->sc_on) val |= RIX_FEAT_SCISSOR; }
    rix_dl_reg(&f->dl, reg, val);
    if (reg == RIX_REG_FRAGMENT) f->fragment = val;
    return 0;
}

int rix_frame_features(rix_frame *f, u32 features) { return rix_frame_reg(f, RIX_REG_FEATURES, features); }
int rix_frame_fragment(rix_frame *f, u32 fragment) { return rix_frame_reg(f, RIX_REG_FRAGMENT, fragment); }

int rix_frame_scissor(rix_frame *f, u32 x0, u32 y0, u32 x1, u32 y1)
{
    int rc = ensure_room(f, 6u);
    if (rc) return rc;
    if (x1 == 0u) { f->sc_on = 0; rix_dl_reg(&f->dl, RIX_REG_FEATURES, f->features); return 0; }
    f->sc_on = 1; f->sc_x0 = x0; f->sc_y0 = y0; f->sc_x1 = x1; f->sc_y1 = y1;   /* restored after every chunk boundary */
    rix_dl_reg(&f->dl, RIX_REG_SCISSOR_START, RIX_XY(x0, y0));
    rix_dl_reg(&f->dl, RIX_REG_SCISSOR_END, RIX_XY(x1, y1));
    rix_dl_reg(&f->dl, RIX_REG_FEATURES, f->features | RIX_FEAT_SCISSOR);
    return 0;
}

/* texture bind: the page table travels in stream order between the chunk
   being closed and the next one, as a FIFO packet or as a short DDR list of
   its own (bind mode, rixframe.h); never inside a frame chunk (FSC deadlock,
   D2e).  The TMU keeps its texture across frames: a bind of the texture
   already loaded (same base, pages and config, not rewritten since) is
   skipped. */
static u32 g_tex_pkt[2u + 2u + 1u + 64u + 16u];   /* config + table + bind fence */
static u32 g_bound_base, g_bound_pages, g_bound_cfg;
void rix_frame_texture_invalidate(u32 base_arm)
{
    if (base_arm == 0u || base_arm == g_bound_base) { g_bound_base = 0u; g_bound_pages = 0u; g_bound_cfg = 0u; }
}

int rix_frame_texture(rix_frame *f, const rix_texture *t)
{
    u32 n = 0u, i, t0;
    int rc;
    if (t->pages == 0u || t->pages > 64u) return (int)RIXE_TEX_SIZE;
    if (g_defer) {
        /* 1.3: the chunk queued, the bind queued with its own copy of the packet, the next chunk
           opened; no skip of an already loaded texture in this mode */
        dq_ev *e;
        t0 = rix_now();
        rc = chunk_close(f, 0u);
        if (rc) return rc;
        e = dq_push(f, DQ_BIND, &rc);
        if (!e) return rc;
        e->w[n++] = RIX_REG(RIX_REG_TMU0_CONFIG); e->w[n++] = t->cfg;
        e->w[n++] = RIX_TEX_CMD(t->pages, 0u);
        for (i = 0; i < t->pages; i++) e->w[n++] = t->base + i * RIX_TEX_PAGE_BYTES;
        e->seq = next_seq();
        g_last_bind_seq = e->seq;
        n += bind_fence_words(&e->w[n], e->seq);
        e->n = n;
        g_bound_base = 0u; g_bound_pages = 0u; g_bound_cfg = 0u;
        f->tex_binds++;
        rc = rix_frame_pump(f);
        if (rc) return rc;
        {
            u32 w0 = f->ringwait_us;
            rc = chunk_open(f);
            if (rc) return rc;
            f->tex_bind_wait_us += f->ringwait_us - w0;
        }
        chunk_restore(f);
        f->tex_bind_us += rix_ticks_to_us((u32)(rix_now() - t0));
        return 0;
    }
    if (t->base == g_bound_base && t->pages == g_bound_pages && t->cfg == g_bound_cfg) { f->tex_skipped++; return 0; }
    t0 = rix_now();
    rc = chunk_close(f, 0u);               /* waits for a pending bind fence first (rule 11) */
    if (rc) return rc;
    g_tex_pkt[n++] = RIX_REG(RIX_REG_TMU0_CONFIG); g_tex_pkt[n++] = t->cfg;
    g_tex_pkt[n++] = RIX_TEX_CMD(t->pages, 0u);
    for (i = 0; i < t->pages; i++) g_tex_pkt[n++] = t->base + i * RIX_TEX_PAGE_BYTES;
    f->bind_seq = next_seq();
    g_last_bind_seq = f->bind_seq;
    n += bind_fence_words(&g_tex_pkt[n], f->bind_seq);   /* executed by the parser right after the texture load */
    if (g_bind_mode == RIX_BIND_LIST && g_rc.bind_base != 0u) {
        /* the same words as a DDR list: 2 FIFO words instead of n.  The list is shorter than ladder
           L15's in both measures that matter to the FSC (80 words at most against 144; 13 to 28 words
           after the page table, padding included, against 77), so the FSC has fetched all of it before the parser
           reaches the OP_TEXTURE: L15's case exactly, nothing more. */
        rix_dl bl;
        u32 base = g_rc.bind_base + g_bind_slot * 512u;
        g_bind_slot ^= 1u;
        rix_dl_begin(&bl, base, 128u);
        for (i = 0; i < n; i++) rix_dl_word(&bl, g_tex_pkt[i]);
        if (bl.overflow || bl.n > RIX_BIND_LIST_WORDS) return (int)RIXE_DL_OVERFLOW;
        rix_dl_finish(&bl);                                   /* 16-word padding now, so that the clean below covers it */
        if (g_rc.bind_cacheable) { dsb(); rix_mem_clean(bl.base, bl.n * 4u); }
        rc = rix_submit(&bl);
    } else {
        rc = rix_submit_words_fifo_nowait(g_tex_pkt, n);   /* no TC wait: the FIFO drains at the GPU's pace, Core1 goes on building */
    }
    if (rc) return rc;
    g_bound_base = t->base; g_bound_pages = t->pages; g_bound_cfg = t->cfg;
    f->tex_binds++;
    if (g_flags_sync) { rc = bind_fence_wait(f); if (rc) return rc; }
    {
        u32 w0 = f->ringwait_us;
        rc = chunk_open(f);
        if (rc) return rc;
        f->tex_bind_wait_us += f->ringwait_us - w0;          /* the ring wait inside the bind (also counted in ringwait_us) */
    }
    chunk_restore(f);
    f->tex_bind_us += rix_ticks_to_us((u32)(rix_now() - t0));
    return 0;
}

int rix_frame_end(rix_frame *f, u32 fence_value16)
{
    int rc;
    if (!f->open) return (int)RIXE_RING_CFG;
    rc = chunk_close(f, fence_value16);
    if (rc || !g_defer) return rc;
    {   /* 1.3: everything submitted before the caller waits for the frame fence */
        u32 t0 = rix_now(), spin = 0u, us;
        while (g_dq_n) { rc = dq_wait_turn(f, t0, &spin); if (rc) return rc; }
        us = rix_ticks_to_us((u32)(rix_now() - t0));
        f->dq_end_us += us; f->dq_wait_us += us;
    }
    return 0;
}

int rix_frame_wait(u32 color, u32 fence_value16, u32 timeout_us, volatile u32 *stop, u32 *polls)
{
    return rix_fence_wait(color, fence_value16, timeout_us, stop, polls);
}

u32 rix_fragment(u32 depth_func, int depth_write, u32 alpha_func, u32 alpha_ref8, u32 blend_s, u32 blend_d)
{
    return RIX_FRAG_COLOR_MASK | RIX_FRAG_DEPTH_FUNC(depth_func) | (depth_write ? RIX_FRAG_DEPTH_MASK : 0u)
         | RIX_FRAG_ALPHA_FUNC(alpha_func) | RIX_FRAG_ALPHA_REF(alpha_ref8) | RIX_FRAG_BLEND(blend_s, blend_d);
}
