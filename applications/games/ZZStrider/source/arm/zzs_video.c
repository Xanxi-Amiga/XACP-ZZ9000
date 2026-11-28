/*
 * zzs_video.c - ZZStrider presentation: INDEX16 compositor output -> Picasso96 buffer.
 *
 * Architecture kept from zzrastan_video.c (P2A/P3):
 *   - the machine composes PALETTE INDICES into a private INDEX16 frame in BSS (WB/WA, Core1
 *     only, no flush);
 *   - ONE pass converts index -> 0xFFRRGGBB into the Picasso96 buffer the 68k authorised
 *     (SH_RENDER_SEQ / SH_P96_BASE: never FRONT, never PENDING), then dcache_clean_range;
 *   - render_done_seq / frame_ready are published last, with barriers;
 *   - averages are taken over RENDERED frames only.
 *
 * New for CPS-1: the native picture is 384 x 224 with non-square pixels, shown 4:3 on the
 * arcade monitor. No standard ZZ9000 mode is 384 pixels wide, so the launcher opens 640 x 480
 * (4:3) and this file scales while converting: 3 source pixels -> 5 screen pixels, 7 source
 * lines -> 15 screen lines, nearest neighbour sampled at pixel centres. The picture fills the
 * 4:3 screen exactly; nothing is cropped. Any other geometry goes through a generic table path
 * (a 384 x 224 screen would be copied 1:1).
 *
 * ASCII only.
 */
#include "zzs.h"

#define SRC_W ZZS_SRC_W
#define SRC_H ZZS_SRC_H
#define GUARD_ROWS 32u
#define MAX_W 1280u
#define MAX_H 1024u

/* Private INDEX16 frame, with guard rows on both sides: the CPS-1 plotters clip, the guard is
   there so that a clipping mistake could only ever scribble on memory nobody reads. */
static zzs_u16 g_index_store[SRC_W * (SRC_H + 2u * GUARD_ROWS)];
#define frame_index (g_index_store + SRC_W * GUARD_ROWS)

static zzs_u32 g_line[MAX_W] __attribute__((aligned(32)));
static zzs_u16 g_xmap[MAX_W];
static zzs_u16 g_ymap[MAX_H];
static zzs_u32 g_map_w, g_map_h;

static zzs_u32 g_last_render_seq;
static zzs_u32 g_frame_seq;
static zzs_u32 g_gate_block_run;
static zzs_u32 g_rendered;
static zzs_u64 g_sum_video, g_sum_layers, g_sum_sprites, g_sum_convert, g_sum_clean;
static zzs_u32 g_video_max;
static zzs_u32 g_conv_hist[6], g_conv_max, g_block_run_total;
static zzs_u64 g_conv_l1d, g_conv_dstall;
static zzs_u32 g_mapped[4];
static int g_nmap;
static zzs_u32 g_buf_n[4], g_buf_max[4], g_buf_bad[4];
static zzs_u64 g_buf_sum[4];

void zzs_video_reset(void)
{
    zzs_u32 i;
    g_last_render_seq = 0;
    g_frame_seq = 0;
    g_gate_block_run = 0;
    g_rendered = 0;
    g_sum_video = g_sum_layers = g_sum_sprites = g_sum_convert = g_sum_clean = 0;
    g_video_max = 0;
    for (i = 0; i < 6u; i++) g_conv_hist[i] = 0;
    g_conv_max = 0; g_block_run_total = 0; g_conv_l1d = 0; g_conv_dstall = 0;
    g_nmap = 0;
    for (i = 0; i < 4u; i++) { g_buf_n[i] = 0; g_buf_max[i] = 0; g_buf_sum[i] = 0; g_buf_bad[i] = 0; }
    g_map_w = g_map_h = 0;
    for (i = 0; i < sizeof(g_index_store) / sizeof(g_index_store[0]); i++) g_index_store[i] = 0;

    ZZS_SH(ZZS_SH_FRAME_READY) = 0;
    ZZS_SH(ZZS_SH_RENDER_DONE) = 0;
    ZZS_SH(ZZS_SH_RENDERED) = 0;
    ZZS_SH(ZZS_SH_VP_CALLS) = 0;
    ZZS_SH(ZZS_SH_VP_GATE_PASS) = 0;
    ZZS_SH(ZZS_SH_VP_GATE_BLOCK) = 0;
    ZZS_SH(ZZS_SH_VP_BLOCK_CONS_MAX) = 0;
    ZZS_SH(ZZS_SH_MMU_BSS_DESC) = mmu_get_desc(ZZS_ARM_BSS);
    ZZS_SH(ZZS_SH_MMU_ROM_DESC) = mmu_get_desc(ZZS_ARM_ROMSTORE);
    ZZS_SH(ZZS_SH_MMU_STAGE_DESC) = mmu_get_desc(ZZS_ARM_STAGE);
}

static int ensure_wbwa(zzs_u32 base, zzs_u32 len)
{
    int k;
    for (k = 0; k < g_nmap; k++)
        if (g_mapped[k] == base)
            return k;
    if (g_nmap < 4) {
        g_zzs_l2_fix = (ZZS_SH(ZZS_SH_RUN_FLAGS) & ZZS_RUNF_NO_L2FIX) ? 0 : 1;
        ZZS_SH(ZZS_SH_L2_FIX) = (zzs_u32)g_zzs_l2_fix;
        ZZS_SH(ZZS_SH_L2_ENABLED) = zzs_l2_enabled();
        mmu_set_wbwa(base, len);
        g_mapped[g_nmap] = base;
        if (g_nmap < 3) ZZS_SH(ZZS_SH_BUF_BASE(g_nmap)) = base;
        ZZS_SH(ZZS_SH_MMU_P96_DESC) = mmu_get_desc(base);
        return g_nmap++;
    }
    return -1;
}

static void copy_row(zzs_u32 *dst, const zzs_u32 *src, zzs_u32 n)
{
    while (n >= 8u) {
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
        dst[4] = src[4]; dst[5] = src[5]; dst[6] = src[6]; dst[7] = src[7];
        dst += 8; src += 8; n -= 8u;
    }
    while (n-- != 0u) *dst++ = *src++;
}

/* 384 x 224 -> 640 x 480. Horizontal 3 -> 5 with centre sampling: source index for the five
   screen pixels of a group is 0,0,1,2,2. Vertical 7 -> 15: the seven source lines of a group
   are shown 2,2,2,3,2,2,2 times. */
static void convert_640x480(const zzs_u32 *pal, zzs_u32 *fb, zzs_u32 stride_px)
{
    static const zzs_u8 reps[7] = { 2, 2, 2, 3, 2, 2, 2 };
    zzs_u32 sy, y = 0;

    for (sy = 0; sy < SRC_H; sy++) {
        const zzs_u16 *s = frame_index + sy * SRC_W;
        zzs_u32 *d = g_line;
        zzs_u32 i, r;

        for (i = 0; i < SRC_W / 3u; i++) {
            zzs_u32 a = pal[s[0] & 0xFFFu];
            zzs_u32 b = pal[s[1] & 0xFFFu];
            zzs_u32 c = pal[s[2] & 0xFFFu];
            d[0] = a; d[1] = a; d[2] = b; d[3] = c; d[4] = c;
            s += 3; d += 5;
        }
        for (r = reps[sy % 7u]; r != 0u; r--) {
            copy_row(fb + y * stride_px, g_line, 640u);
            y++;
        }
    }
}

/* The same 384 x 224 -> 640 x 480 picture, written as ONE sequential stream (A2).
 *
 * Measured on the A4000 with the loop above: 2.7 ms on the title screens, 8 to 10 ms on average
 * in game, up to 20 ms. The work is the same (86 016 palette look-ups, 1.2 MiB written); what
 * changes with the picture is which palette lines are needed, i.e. how the look-ups and the
 * 38 400 destination cache lines fight over the 32 KiB L1 data cache. This version removes
 * that interaction with documented Cortex-A9 means:
 *   - the palette table and the line buffer are touched first, with normal allocation;
 *   - ACTLR "Alloc in one way" is then set for the duration of the conversion (TRM 4.3.10,
 *     "for use with memory copy operations to reduce cache pollution"): the destination lines
 *     can only replace one way of the cache, the other three keep the tables;
 *   - (prefetch) every destination line is requested four lines ahead and the source row one
 *     row ahead with PLD, so that the linefills overlap the copy instead of stalling it; a PLD
 *     stops the automatic prefetcher (TRM 7.6.2), which is why both streams are covered.
 * The PCM ring is topped up every 32 source rows. Requires stride == 640 (rows contiguous).
 */
static void copy_row_stream(zzs_u32 *dst, const zzs_u32 *src, const zzs_u32 *limit, int prefetch)
{
    zzs_u32 n = 640u / 8u;
    while (n-- != 0u) {
        if (prefetch && dst + 32 < limit) __builtin_prefetch(dst + 32, 1);
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
        dst[4] = src[4]; dst[5] = src[5]; dst[6] = src[6]; dst[7] = src[7];
        dst += 8; src += 8;
    }
}

static void convert_640x480_stream(const zzs_u32 *pal, zzs_u32 *fb, int prefetch)
{
    static const zzs_u8 reps[7] = { 2, 2, 2, 3, 2, 2, 2 };
    const zzs_u32 *limit = fb + 640u * 480u;
    volatile zzs_u32 sink = 0;
    zzs_u32 sy, i, acc = 0;

    for (i = 0; i < 0x1000u; i += 8u) acc += pal[i];          /* one touch per cache line */
    for (i = 0; i < 640u; i += 8u) acc += g_line[i];
    sink = acc; (void)sink;
    ZZS_SH(ZZS_SH_ACTLR) = zzs_actlr_one_way(1);

    for (sy = 0; sy < SRC_H; sy++) {
        const zzs_u16 *s = frame_index + sy * SRC_W;
        zzs_u32 *d = g_line;
        zzs_u32 r;

        if (prefetch && sy + 1u < SRC_H)
            for (i = 0; i < SRC_W; i += 16u) __builtin_prefetch(s + SRC_W + i, 0);
        for (i = 0; i < SRC_W / 3u; i++) {
            zzs_u32 a = pal[s[0] & 0xFFFu];
            zzs_u32 b = pal[s[1] & 0xFFFu];
            zzs_u32 c = pal[s[2] & 0xFFFu];
            d[0] = a; d[1] = a; d[2] = b; d[3] = c; d[4] = c;
            s += 3; d += 5;
        }
        for (r = reps[sy % 7u]; r != 0u; r--) {
            copy_row_stream(fb, g_line, limit, prefetch);
            fb += 640u;
        }
        if ((sy & 31u) == 31u) zzs_audio_pump();
    }
    (void)zzs_actlr_one_way(0);
}

/* A2d - 640 x 240 picture, doubled vertically by the ZZ9000 (scale mode bit 1), so that the
 * ZZScanlines logic of the video formatter applies (it is enabled when the vertical scaler is
 * on): every arcade line becomes exactly two screen lines, one of them darkened.
 * rows = 240: 640x480 output, the 224 game lines in rows 8..231, black borders.
 * rows = 224 (A2e, scan448): custom 640x448 output, no border at all.
 * Horizontal: same 3 -> 5 pattern. Half the bytes of the 640 x 480 path. Requires stride == 640. */
static void convert_640xN_stream(const zzs_u32 *pal, zzs_u32 *fb, zzs_u32 rows, int prefetch)
{
    const zzs_u32 *limit = fb + 640u * rows;
    zzs_u32 border = (rows - SRC_H) / 2u;
    volatile zzs_u32 sink = 0;
    zzs_u32 sy, i, acc = 0, *row;

    for (i = 0; i < 0x1000u; i += 8u) acc += pal[i];
    for (i = 0; i < 640u; i += 8u) acc += g_line[i];
    sink = acc; (void)sink;
    ZZS_SH(ZZS_SH_ACTLR) = zzs_actlr_one_way(1);

    for (row = fb, i = 0; i < 640u * border; i++) row[i] = 0xFF000000u;        /* top border */
    for (row = fb + 640u * (border + SRC_H), i = 0; i < 640u * (rows - SRC_H - border); i++) row[i] = 0xFF000000u;
    fb += 640u * border;
    for (sy = 0; sy < SRC_H; sy++) {
        const zzs_u16 *s = frame_index + sy * SRC_W;
        zzs_u32 *d = fb;
        if (prefetch && sy + 1u < SRC_H)
            for (i = 0; i < SRC_W; i += 16u) __builtin_prefetch(s + SRC_W + i, 0);
        for (i = 0; i < SRC_W / 3u; i++) {
            zzs_u32 a = pal[s[0] & 0xFFFu];
            zzs_u32 b = pal[s[1] & 0xFFFu];
            zzs_u32 c = pal[s[2] & 0xFFFu];
            if (prefetch && (i & 7u) == 0u && d + 40 < limit) __builtin_prefetch(d + 40, 1);
            d[0] = a; d[1] = a; d[2] = b; d[3] = c; d[4] = c;
            s += 3; d += 5;
        }
        fb += 640u;
        if ((sy & 31u) == 31u) zzs_audio_pump();
    }
    (void)zzs_actlr_one_way(0);
}

static void build_maps(zzs_u32 w, zzs_u32 h)
{
    zzs_u32 i;
    for (i = 0; i < w; i++) g_xmap[i] = (zzs_u16)(((2u * i + 1u) * SRC_W) / (2u * w));
    for (i = 0; i < h; i++) g_ymap[i] = (zzs_u16)(((2u * i + 1u) * SRC_H) / (2u * h));
    g_map_w = w;
    g_map_h = h;
}

static void convert_generic(const zzs_u32 *pal, zzs_u32 *fb, zzs_u32 stride_px, zzs_u32 w, zzs_u32 h)
{
    zzs_u32 y, x, last = 0xFFFFFFFFu;
    if (g_map_w != w || g_map_h != h)
        build_maps(w, h);
    for (y = 0; y < h; y++) {
        zzs_u32 sy = g_ymap[y];
        if (sy != last) {
            const zzs_u16 *s = frame_index + sy * SRC_W;
            for (x = 0; x < w; x++)
                g_line[x] = pal[s[g_xmap[x]] & 0xFFFu];
            last = sy;
        }
        copy_row(fb + y * stride_px, g_line, w);
    }
}

void zzs_video_present(void)
{
    zzs_u32 base, pitch, stride, w, h, t0, t1, tc, t2, obj, total;
    int bufk;

    if (!ZZS_SH(ZZS_SH_P96_ENABLE)) return;
    if (ZZS_SH(ZZS_SH_RUN_FLAGS) & ZZS_RUNF_NO_VIDEO) return;

    ZZS_SH(ZZS_SH_VP_CALLS)++;
    /* P2A gate: render whenever the 68k has AUTHORISED a new frame, whether or not a PAN is
       still in flight. The 68k guarantees SH_P96_BASE is neither FRONT nor PENDING. */
    if (ZZS_SH(ZZS_SH_RENDER_SEQ) == g_last_render_seq) {
        ZZS_SH(ZZS_SH_VP_GATE_BLOCK)++;
        g_gate_block_run++;
        if (g_rendered != 0u) { g_block_run_total++; ZZS_SH(ZZS_SH_VP_BLOCK_RUN) = g_block_run_total; }
        if (g_gate_block_run > ZZS_SH(ZZS_SH_VP_BLOCK_CONS_MAX))
            ZZS_SH(ZZS_SH_VP_BLOCK_CONS_MAX) = g_gate_block_run;
        return;
    }
    g_gate_block_run = 0;
    ZZS_SH(ZZS_SH_VP_GATE_PASS)++;
    g_last_render_seq = ZZS_SH(ZZS_SH_RENDER_SEQ);

    base  = ZZS_SH(ZZS_SH_P96_BASE);
    pitch = ZZS_SH(ZZS_SH_P96_PITCH);
    w     = ZZS_SH(ZZS_SH_P96_W);
    h     = ZZS_SH(ZZS_SH_P96_H);
    stride = pitch >> 2;
    if (base == 0u) return;
    if (w < 320u || h < 200u || w > MAX_W || h > MAX_H || stride < w || (pitch & 3u) != 0u ||
        base < ZZS_ARM_FB_PHYS || base >= ZZS_ARM_BLOB || h * pitch > ZZS_ARM_BLOB - base) {
        /* The 68k published a buffer that is not a 32-bit Picasso96 bitmap below our own
           windows: refuse to write anything. */
        zzs_fatal(ZZS_ERR_P96_GEOMETRY, (w << 16) | h);
        return;
    }

    bufk = ensure_wbwa(base, h * pitch);
    __asm__ volatile("dsb" ::: "memory");
    if (bufk >= 0 && bufk < 3) {                /* both ends of the buffer, as the MMU sees them */
        zzs_u32 p0 = zzs_mmu_par(base), p1 = zzs_mmu_par(base + h * pitch - 4u);
        zzs_u32 bad = ((p0 & 1u) || ((p0 >> 4) & 7u) != 5u) ? p0 :
                      (((p1 & 1u) || ((p1 >> 4) & 7u) != 5u) ? p1 : 0u);   /* 5 = WB/WA */
        if (bad) { g_buf_bad[bufk]++; ZZS_SH(ZZS_SH_BUF_PAR(bufk)) = bad; }      /* sticky: first wrong */
        else if (g_buf_bad[bufk] == 0u) ZZS_SH(ZZS_SH_BUF_PAR(bufk)) = p0;
        ZZS_SH(ZZS_SH_BUF_BAD(bufk)) = g_buf_bad[bufk];
    }

    t0 = zzs_pmu_cycles();
    (void)zzs_machine_take_obj_cycles();
    zzs_machine_draw(frame_index);
    {
        zzs_u32 flags = ZZS_SH(ZZS_SH_RUN_FLAGS), e0[ZZS_PMU_NEV], e1[ZZS_PMU_NEV], us, bucket;
        zzs_u32 method = (flags & ZZS_RUNF_CONV_PLAIN) ? 0u : ((flags & ZZS_RUNF_CONV_NOPLD) ? 1u : 2u);
        const zzs_u32 *pal = zzs_machine_palette();

        zzs_audio_pump();
        zzs_pmu_events(e0);
        t1 = zzs_pmu_cycles();
        if (w == 640u && (h == 240u || h == 224u) && stride == 640u)   /* doubled by the card */
            convert_640xN_stream(pal, (zzs_u32 *)base, h, method == 2u);
        else if (w == 640u && h == 480u && stride == 640u && method != 0u)
            convert_640x480_stream(pal, (zzs_u32 *)base, method == 2u);
        else if (w == 640u && h == 480u)
            convert_640x480(pal, (zzs_u32 *)base, stride);
        else
            convert_generic(pal, (zzs_u32 *)base, stride, w, h);
        tc = zzs_pmu_cycles();
        zzs_pmu_events(e1);
        g_conv_l1d    += (zzs_u64)(e1[2] - e0[2]);
        g_conv_dstall += (zzs_u64)(e1[4] - e0[4]);
        us = (tc - t1) / ZZS_PMU_PER_US;
        bucket = (us < 3000u) ? 0u : (us < 5000u) ? 1u : (us < 8000u) ? 2u : (us < 12000u) ? 3u : (us < 16000u) ? 4u : 5u;
        g_conv_hist[bucket]++;
        if (bufk >= 0 && bufk < 3) {
            g_buf_n[bufk]++;
            g_buf_sum[bufk] += (zzs_u64)us;
            if (us > g_buf_max[bufk]) g_buf_max[bufk] = us;
            ZZS_SH(ZZS_SH_BUF_COUNT(bufk))  = g_buf_n[bufk];
            ZZS_SH(ZZS_SH_BUF_AVG_US(bufk)) = (zzs_u32)(g_buf_sum[bufk] / g_buf_n[bufk]);
            ZZS_SH(ZZS_SH_BUF_MAX_US(bufk)) = g_buf_max[bufk];
        }
        if (us > g_conv_max) g_conv_max = us;
        ZZS_SH(ZZS_SH_CONV_H0 + bucket) = g_conv_hist[bucket];
        ZZS_SH(ZZS_SH_CONV_MAX_US) = g_conv_max;
        ZZS_SH(ZZS_SH_CONV_METHOD) = method;
    }
    dcache_clean_range((void *)base, h * pitch);
    t2 = zzs_pmu_cycles();

    obj = zzs_machine_take_obj_cycles();
    total = t2 - t0;
    g_rendered++;
    g_sum_video   += (zzs_u64)total;
    g_sum_sprites += (zzs_u64)obj;
    g_sum_layers  += (zzs_u64)((t1 - t0) - obj);
    g_sum_convert += (zzs_u64)(tc - t1);
    g_sum_clean   += (zzs_u64)(t2 - tc);
    if (total > g_video_max) g_video_max = total;
    ZZS_SH(ZZS_SH_PROF_VIDEO_US)     = (zzs_u32)(g_sum_video   / g_rendered / ZZS_PMU_PER_US);
    ZZS_SH(ZZS_SH_PROF_SPRITES_US)   = (zzs_u32)(g_sum_sprites / g_rendered / ZZS_PMU_PER_US);
    ZZS_SH(ZZS_SH_PROF_LAYERS_US)    = (zzs_u32)(g_sum_layers  / g_rendered / ZZS_PMU_PER_US);
    ZZS_SH(ZZS_SH_PROF_CONVERT_US)   = (zzs_u32)(g_sum_convert / g_rendered / ZZS_PMU_PER_US);
    ZZS_SH(ZZS_SH_PROF_CLEAN_US)     = (zzs_u32)(g_sum_clean   / g_rendered / ZZS_PMU_PER_US);
    ZZS_SH(ZZS_SH_PROF_VIDEO_MAX_US) = g_video_max / ZZS_PMU_PER_US;
    ZZS_SH(ZZS_SH_CONV_L1D)          = (zzs_u32)(g_conv_l1d / g_rendered);
    ZZS_SH(ZZS_SH_CONV_DSTALL_US)    = (zzs_u32)(g_conv_dstall / g_rendered / ZZS_PMU_PER_US);

    g_frame_seq++;
    ZZS_SH(ZZS_SH_RENDERED) = g_frame_seq;
    __asm__ volatile("dsb" ::: "memory");
    ZZS_SH(ZZS_SH_RENDER_DONE) = g_last_render_seq;
    __asm__ volatile("dsb" ::: "memory");
    ZZS_SH(ZZS_SH_FRAME_READY) = g_last_render_seq;     /* publish the authorisation we completed */
    __asm__ volatile("dsb" ::: "memory");
}

/* test harness access (tests/ only; not used by the blob) */
const zzs_u16 *zzs_video_index_frame(void) { return frame_index; }
