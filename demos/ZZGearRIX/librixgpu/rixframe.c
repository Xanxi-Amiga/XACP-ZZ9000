/* rixframe.c - librixgpu 1.0 - chunk ring and frame API (see rixframe.h)
 * Extracted from ZZGear C1c zzgear_app.c (validated on G4B: six runs).
 * SPDX-License-Identifier: 0BSD */
#include "rixframe.h"

typedef struct { u32 buf; u32 seq; } ring_slot;
static ring_slot g_ring[RIX_RING_MAX_SLOTS];
static rix_ring_cfg g_rc;
static u32 g_next, g_seq;
static int g_ring_ready;

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
        volatile u16 *row = (volatile u16 *)rix_ptr(g_rc.color_bufs[b] + RIX_FENCE_ROW_OFF);
        for (k = g_rc.fence_x; k < g_rc.fence_x + RIX_FENCE_PIXELS; k++) row[k] = 0u;
    }
    dsb();
    g_seq = 0u; g_next = 0u;
    for (k = 0; k < RIX_RING_MAX_SLOTS; k++) { g_ring[k].buf = 0u; g_ring[k].seq = 0u; }
}

int rix_ring_init(const rix_ring_cfg *cfg)
{
    if (cfg->ring_slots < 2u || cfg->ring_slots > RIX_RING_MAX_SLOTS) return (int)RIXE_RING_CFG;
    if ((cfg->ring_base & 63u) || (cfg->chunk_bytes & 63u) || cfg->chunk_bytes < 4096u) return (int)RIXE_RING_CFG;
    if (cfg->fence_x + RIX_FENCE_PIXELS > RIX_W || cfg->color_count == 0u) return (int)RIXE_RING_CFG;
    g_rc = *cfg;
    rix_ring_reset();
    g_ring_ready = 1;
    return 0;
}

/* wait until slot s is no longer referenced by the GPU */
static int ring_wait_slot(u32 s, u32 *wait_us)
{
    ring_slot *r = &g_ring[s];
    u32 t0 = rix_now();
    if (r->seq == 0u) return 0;
    while (!rix_fence_passed_at(r->buf, r->seq, g_rc.fence_x)) {
        if (rix_ticks_to_us((u32)(rix_now() - t0)) > 2000000u) return (int)RIXE_RING_TIMEOUT;
        rix_idle_us(50u);
    }
    if (wait_us) *wait_us += rix_ticks_to_us((u32)(rix_now() - t0));
    return 0;
}

static int chunk_open(rix_frame *f)
{
    int rc = ring_wait_slot(g_next, &f->ringwait_us);
    if (rc) return rc;
    f->slot = g_next;
    g_next = (g_next + 1u) % g_rc.ring_slots;
    rix_dl_begin(&f->dl, g_rc.ring_base + f->slot * g_rc.chunk_bytes, CHUNK_WORDS);
    f->dl.frag_shadow = f->fragment;
    f->open = 1;
    return 0;
}

static int chunk_close(rix_frame *f, u32 frame_fence)
{
    u32 cseq = next_seq();
    int rc;
    rix_dl_fence_at(&f->dl, cseq, f->features, g_rc.fence_x);
    if (frame_fence) rix_dl_fence(&f->dl, frame_fence, f->features);
    if (f->dl.overflow) return (int)RIXE_DL_OVERFLOW;
    g_ring[f->slot].buf = f->color;
    g_ring[f->slot].seq = cseq;
    rc = rix_submit(&f->dl);
    if (f->chunks == 0u) f->first_submit_tick = rix_now();
    f->chunks++;
    f->words += f->dl.n;
    f->open = 0;
    return rc;
}

/* a new chunk starts with the frame's registers restored (the fence of the
   previous chunk left the scissor on and the fragment register changed) */
static int chunk_rollover(rix_frame *f)
{
    int rc = chunk_close(f, 0u);
    if (rc) return rc;
    rc = chunk_open(f);
    if (rc) return rc;
    rix_dl_reg(&f->dl, RIX_REG_FRAGMENT, f->fragment);
    rix_dl_reg(&f->dl, RIX_REG_FEATURES, f->features);
    return 0;
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
    f->chunks = 0u; f->words = 0u; f->tris = 0u; f->ringwait_us = 0u; f->first_submit_tick = 0u; f->tex_binds = 0u;
    rc = chunk_open(f);
    if (rc) return rc;
    rix_dl_reg(&f->dl, RIX_REG_COLOR_ADDR, d->color);
    rix_dl_reg(&f->dl, RIX_REG_DEPTH_ADDR, d->zbuf);
    rix_dl_reg(&f->dl, RIX_REG_FRAGMENT, d->fragment);
    if (x1 == 0u && y1 == 0u) { x0 = 0u; y0 = 0u; x1 = RIX_W; y1 = RIX_H_ALLOC; }
    if ((d->clear_color || (d->clear_depth && d->zbuf)) && x1 > x0 && y1 > y0) {
        int full = (x0 == 0u && y0 == 0u && x1 == RIX_W && y1 == RIX_H_ALLOC);
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

int rix_frame_tri(rix_frame *f, const u32 *desc39)
{
    int rc = ensure_room(f, 1u + RIX_TRI_DESC_WORDS);
    if (rc) return rc;
    rix_dl_tri(&f->dl, desc39);
    f->tris++;
    return 0;
}

int rix_frame_reg(rix_frame *f, u32 reg, u32 val)
{
    int rc = ensure_room(f, 2u);
    if (rc) return rc;
    rix_dl_reg(&f->dl, reg, val);
    if (reg == RIX_REG_FRAGMENT) f->fragment = val;
    if (reg == RIX_REG_FEATURES) f->features = val & ~RIX_FEAT_SCISSOR;
    return 0;
}

int rix_frame_features(rix_frame *f, u32 features) { return rix_frame_reg(f, RIX_REG_FEATURES, features); }
int rix_frame_fragment(rix_frame *f, u32 fragment) { return rix_frame_reg(f, RIX_REG_FRAGMENT, fragment); }

int rix_frame_scissor(rix_frame *f, u32 x0, u32 y0, u32 x1, u32 y1)
{
    int rc = ensure_room(f, 6u);
    if (rc) return rc;
    if (x1 == 0u) { rix_dl_reg(&f->dl, RIX_REG_FEATURES, f->features); return 0; }
    rix_dl_reg(&f->dl, RIX_REG_SCISSOR_START, RIX_XY(x0, y0));
    rix_dl_reg(&f->dl, RIX_REG_SCISSOR_END, RIX_XY(x1, y1));
    rix_dl_reg(&f->dl, RIX_REG_FEATURES, f->features | RIX_FEAT_SCISSOR);
    return 0;
}

/* texture bind: the page table goes through the FIFO, in stream order
   between the chunk being closed and the next one */
static u32 g_tex_pkt[2u + 2u + 1u + 64u];
int rix_frame_texture(rix_frame *f, const rix_texture *t)
{
    u32 n = 0u, i;
    int rc;
    if (t->pages == 0u || t->pages > 64u) return (int)RIXE_TEX_SIZE;
    rc = chunk_close(f, 0u);
    if (rc) return rc;
    g_tex_pkt[n++] = RIX_REG(RIX_REG_TMU0_CONFIG); g_tex_pkt[n++] = t->cfg;
    g_tex_pkt[n++] = RIX_TEX_CMD(t->pages, 0u);
    for (i = 0; i < t->pages; i++) g_tex_pkt[n++] = t->base + i * RIX_TEX_PAGE_BYTES;
    rc = rix_submit_words_fifo(g_tex_pkt, n);
    if (rc) return rc;
    f->tex_binds++;
    rc = chunk_open(f);
    if (rc) return rc;
    rix_dl_reg(&f->dl, RIX_REG_FRAGMENT, f->fragment);
    rix_dl_reg(&f->dl, RIX_REG_FEATURES, f->features);
    return 0;
}

int rix_frame_end(rix_frame *f, u32 fence_value16)
{
    if (!f->open) return (int)RIXE_RING_CFG;
    return chunk_close(f, fence_value16);
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
