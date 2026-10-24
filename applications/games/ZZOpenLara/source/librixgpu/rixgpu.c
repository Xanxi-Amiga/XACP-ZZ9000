/* rixgpu.c - librixgpu 1.0 (ZZCube B1d / ZZGear C1c lineage) - Core1 RasterIX backend, core module
 *
 * See rixgpu.h for the design summary and ARCHITECTURE.md for the audit
 * that motivates every mechanism here.
 *
 * SPDX-License-Identifier: 0BSD
 */
#include <stddef.h>
#include "rixgpu.h"
/* NEON only when the compiler offers it (-mfpu=neon).  ZZQuake2RIX builds the library with the
   engine's VFP-only flags: the byte-swap falls back to the word routine (the Quake II blob never
   swaps: the GPU buffer is converted by the presenter).  Reason (14/10): the release's Core1 stack
   is 8-byte misaligned at entry (0x05ffff4c); GCC's NEON stores carry 64-bit alignment hints on
   stack arrays it believes AAPCS-aligned, and they fault (DFSR 0x801 on the stack). */
#if !defined(RIX_HOST_SIM) && defined(__ARM_NEON)
#include <arm_neon.h>
#define RIX_HAVE_NEON 1
#else
#define RIX_HAVE_NEON 0
#endif

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */
static rix_config g_cfg;
static volatile u32 *g_st;   /* rix_config.stats: u32[RS_COUNT] */
static volatile u32 *g_sd;   /* rix_config.sd: u32[SD_COUNT] */
static u32 g_caps;
static u32 g_dl_mode;      /* 1 DDR, 2 FIFO */
static u32 g_swap_mode;    /* 0 none, 1 DMA, 2 NEON, 3 preswap */
static u32 g_idle_mode;    /* 1 WFI, 2 spin */
static u32 g_timer_hz;
static u32 g_ticks_per_us_x16; /* timer ticks per microsecond, x16 */
static u32 g_dmac_base;
static u32 g_fifo_capacity = FIFO_DEPTH_WORDS - 4u; /* TDFV after reset (PG080: depth - 4) */
static u32 g_dmac_ns;
static u32 g_dma_busy;
static u32 g_dma_addr, g_dma_bytes;
static u32 g_gicc_ctlr_saved, g_gicc_pmr_saved;
static u32 g_wfi_armed;

/* PL330 program: shared NC memory, never inside the GPU window */
#define DMA_PROG_ARM  (g_cfg.dma_prog)   /* 64 bytes, Non-Cacheable, PL330-visible (rix_config.dma_prog) */
#define DMA_PROG_MAX  64u

#define FENCE_PIXELS RIX_FENCE_PIXELS
#define FENCE_DRAIN_NOPS_MEM  64u   /* DDR lists: cost is negligible */
#define FENCE_DRAIN_NOPS_FIFO 2u    /* FIFO transport: 12 triangles + fence must fit the vacancy */
/* 1.4: the frame layout (rix_window.h); 640 x 481 until rix_set_frame_layout */
unsigned int rix_lay_w = RIX_W, rix_lay_h = RIX_H_ALLOC, rix_lay_pitch = RIX_PITCH, rix_lay_fence_off = RIX_FENCE_ROW_OFF;
void rix_set_frame_layout(unsigned int w, unsigned int h_alloc)
{
    rix_lay_w = w; rix_lay_h = h_alloc; rix_lay_pitch = w * 2u; rix_lay_fence_off = (h_alloc - 1u) * w * 2u;
}
static inline volatile u16 *fence_row(u32 buf_arm)
{
    return (volatile u16 *)rix_ptr(buf_arm + rix_lay_fence_off);
}

/* ------------------------------------------------------------------ */
/* time: Cortex-A9 global timer (free running, shared, read only)      */
/* ------------------------------------------------------------------ */
static void timer_init(void)
{
    u32 ctrl = mmio_rd(GT_BASE + GT_CTRL);
    u32 presc = (ctrl >> 8) & 0xFFu;
    /* PERIPHCLK = CPU_3x2x = CPU clock / 2 on this FSBL (666666687 / 2). */
    g_timer_hz = 333333343u / (presc + 1u);
    g_ticks_per_us_x16 = (u32)(((uint64_t)g_timer_hz * 16u) / 1000000u);
    if (!(ctrl & 1u)) {
        /* Not expected: Core0's BSP starts it at boot.  Do not touch it. */
        g_caps &= ~RIXCAP_TIMER;
    } else {
        g_caps |= RIXCAP_TIMER;
    }
}

u32 rix_timer_hz(void) { return g_timer_hz; }

u32 rix_now(void)
{
    return mmio_rd(GT_BASE + GT_COUNT_LO);
}

u32 rix_ticks_to_us(u32 dt)
{
    return (u32)(((uint64_t)dt * 16u) / g_ticks_per_us_x16);
}

u32 rix_us_to_ticks(u32 us)
{
    return (u32)(((uint64_t)us * g_ticks_per_us_x16) / 16u);
}

void rix_spin_us(u32 us)
{
    u32 t0 = rix_now(), dt = rix_us_to_ticks(us);
    while ((u32)(rix_now() - t0) < dt) cpu_yield();
}

/* ------------------------------------------------------------------ */
/* idle: private timer (banked, CPU1 only) + GIC CPU interface 1 + WFI */
/* All registers touched are banked per CPU; Core0 is never affected.  */
/* ------------------------------------------------------------------ */
static void pt_stop(void)
{
    mmio_wr(PT_BASE + PT_CTRL, 0u);
    mmio_wr(PT_BASE + PT_ISR, 1u);
    dsb();
}

static void gic_drain_pt(void)
{
    u32 iar;
    int i;
    for (i = 0; i < 4; i++) {
        iar = mmio_rd(GIC_CPU_BASE + GICC_IAR);
        if ((iar & 0x3FFu) == PT_IRQ_ID) {
            mmio_wr(GIC_CPU_BASE + GICC_EOIR, iar);
        } else {
            break;
        }
    }
}

static void idle_init(void)
{
    g_idle_mode = 2u;
    g_wfi_armed = 0u;
    if (g_cfg.flags & RIXF_SPIN) return;
    g_gicc_ctlr_saved = mmio_rd(GIC_CPU_BASE + GICC_CTLR);
    g_gicc_pmr_saved  = mmio_rd(GIC_CPU_BASE + GICC_PMR);
    pt_stop();
    /* PPI 29 priority (banked for IDs 0..31): anything below PMR works. */
    {
        u32 a = GIC_DIST_BASE + GICD_IPRIORITYR + (PT_IRQ_ID & ~3u);
        u32 v = mmio_rd(a);
        v &= ~(0xFFu << ((PT_IRQ_ID & 3u) * 8u));
        v |= (0xA0u << ((PT_IRQ_ID & 3u) * 8u));
        mmio_wr(a, v);
    }
    mmio_wr(GIC_CPU_BASE + GICC_PMR, 0xF8u);
    mmio_wr(GIC_CPU_BASE + GICC_CTLR, 1u);
    mmio_wr(GIC_DIST_BASE + GICD_ISENABLER0, 1u << PT_IRQ_ID);
    dsb(); isb();
    g_wfi_armed = 1u;
}

static void idle_shutdown(void)
{
    if (!g_wfi_armed) return;
    pt_stop();
    mmio_wr(GIC_DIST_BASE + GICD_ICENABLER0, 1u << PT_IRQ_ID);
    mmio_wr(GIC_DIST_BASE + GICD_ICPENDR0, 1u << PT_IRQ_ID);
    gic_drain_pt();
    mmio_wr(GIC_CPU_BASE + GICC_PMR, g_gicc_pmr_saved);
    mmio_wr(GIC_CPU_BASE + GICC_CTLR, g_gicc_ctlr_saved);
    dsb(); isb();
    g_wfi_armed = 0u;
}

/* One bounded WFI: program the private timer, sleep, clean up.  CPSR.I
 * stays masked, so no exception is ever taken; WFI wakes on the pending
 * IRQ regardless of the mask (ARMv7 WFI semantics). */
static void wfi_once(u32 us)
{
    u32 ticks = rix_us_to_ticks(us);
    if (ticks < 64u) ticks = 64u;
    mmio_wr(PT_BASE + PT_CTRL, 0u);
    mmio_wr(PT_BASE + PT_ISR, 1u);
    mmio_wr(PT_BASE + PT_LOAD, ticks);
    mmio_wr(PT_BASE + PT_CTRL, 0x5u);          /* enable + IRQ enable, one shot */
    dsb();
    cpu_wfi();
    mmio_wr(PT_BASE + PT_CTRL, 0u);
    mmio_wr(PT_BASE + PT_ISR, 1u);
    dsb();
    gic_drain_pt();
}

/* Pre-flight without sleeping: can the private timer IRQ reach CPU1's
 * interface?  Only then is WFI guaranteed to return. */
int rix_selftest_wfi(volatile u32 *sd)
{
    u32 t0, i, pend = 0u, hppir = 0u, seen = 0u;
    sd[SD_WFI_TEST] = 0u;
    sd[SD_WFI_SLEEP_US] = 0u;
    if (!g_wfi_armed) return 0;
    mmio_wr(PT_BASE + PT_CTRL, 0u);
    mmio_wr(PT_BASE + PT_ISR, 1u);
    mmio_wr(PT_BASE + PT_LOAD, rix_us_to_ticks(50u));
    mmio_wr(PT_BASE + PT_CTRL, 0x5u);
    dsb();
    t0 = rix_now();
    for (i = 0; i < 200000u; i++) {
        pend = mmio_rd(GIC_DIST_BASE + GICD_ISPENDR0);
        if (pend & (1u << PT_IRQ_ID)) { seen = 1u; break; }
        if (rix_ticks_to_us((u32)(rix_now() - t0)) > 2000u) break;
    }
    sd[SD_ICDISPR] = pend;
    if (!seen) { pt_stop(); return 0; }
    sd[SD_WFI_TEST] = 1u;
    hppir = mmio_rd(GIC_CPU_BASE + GICC_HPPIR) & 0x3FFu;
    if (hppir != PT_IRQ_ID) {
        /* distributor has it, CPU interface would not signal it: unsafe */
        pt_stop();
        gic_drain_pt();
        mmio_wr(GIC_DIST_BASE + GICD_ICPENDR0, 1u << PT_IRQ_ID);
        return 0;
    }
    sd[SD_WFI_TEST] = 2u;
    pt_stop();
    gic_drain_pt();
    /* Real sleep: 1000 us, bounded by what we just proved. */
    t0 = rix_now();
    wfi_once(1000u);
    sd[SD_WFI_SLEEP_US] = rix_ticks_to_us((u32)(rix_now() - t0));
    if (sd[SD_WFI_SLEEP_US] >= 800u && sd[SD_WFI_SLEEP_US] <= 8000u) {
        sd[SD_WFI_TEST] = 3u;
        g_idle_mode = 1u;
        g_caps |= RIXCAP_WFI;
        return 1;
    }
    return 0;
}

void rix_idle_us(u32 us)
{
    if (g_idle_mode == 1u) wfi_once(us);
    else rix_spin_us(us);
}

u32 rix_idle_mode(void) { return g_idle_mode; }

/* ------------------------------------------------------------------ */
/* FIFO (ST0) transport                                                */
/* ------------------------------------------------------------------ */
static int fifo_reset(void)
{
    u32 i, isr;
    isr = mmio_rd(RIX_FIFO_BASE + FIFO_ISR);
    mmio_wr(RIX_FIFO_BASE + FIFO_ISR, isr);
    mmio_wr(RIX_FIFO_BASE + FIFO_TDFR, FIFO_RESET_KEY);
    for (i = 0; i < 1000000u; i++) {
        isr = mmio_rd(RIX_FIFO_BASE + FIFO_ISR);
        if (isr & FIFO_INT_TRC) break;
    }
    if (i == 1000000u) return RIXE_FIFO_RESET;
    mmio_wr(RIX_FIFO_BASE + FIFO_ISR, FIFO_INT_TRC);
    return 0;
}

/* Push one packet of n words (n <= FIFO_DEPTH_WORDS) and wait for the
 * FIFO to have handed it to the FrameStreamingCore (TC).  TC is NOT a
 * render fence; it only means the words left the FIFO. */
static u32 g_tc_timeout_us = 100000u;   /* a GPU that has not taken a packet in 100 ms is hung */
static u32 g_last_tc_us;
static u32 g_mmio_us;       /* time spent writing words into the FIFO (AXI-Lite MMIO), accumulated; probes read and reset it */

static int fifo_send(const u32 *w, u32 n, u32 wait_tc)
{
    u32 i, isr, vac, t0;
    if (n == 0u || n > g_fifo_capacity) return RIXE_FIFO_VACANCY;
    vac = mmio_rd(RIX_FIFO_BASE + FIFO_TDFV);
    if (vac < n) {
        /* wait for the FSC to drain previous packets (bounded) */
        t0 = rix_now();
        while (vac < n) {
            if (rix_ticks_to_us((u32)(rix_now() - t0)) > 500000u) return RIXE_FIFO_VACANCY;
            cpu_yield();
            vac = mmio_rd(RIX_FIFO_BASE + FIFO_TDFV);
        }
    }
    isr = mmio_rd(RIX_FIFO_BASE + FIFO_ISR);          /* an overrun/size error of an earlier no-wait packet must not be wiped unseen */
    if (isr & FIFO_INT_ERR) { mmio_wr(RIX_FIFO_BASE + FIFO_ISR, FIFO_INT_ERR); return RIXE_FIFO_ERR; }
    mmio_wr(RIX_FIFO_BASE + FIFO_ISR, FIFO_INT_TC | FIFO_INT_ERR);
    t0 = rix_now();
    for (i = 0; i < n; i++) mmio_wr(RIX_FIFO_BASE + FIFO_TDFD, w[i]);
    dsb();
    mmio_wr(RIX_FIFO_BASE + FIFO_TLF, n * 4u);
    g_mmio_us += rix_ticks_to_us((u32)(rix_now() - t0));
    if (!wait_tc) return 0;
    t0 = rix_now();
    for (;;) {
        isr = mmio_rd(RIX_FIFO_BASE + FIFO_ISR);
        if (isr & FIFO_INT_ERR) return RIXE_FIFO_ERR;
        if (isr & FIFO_INT_TC) break;
        g_last_tc_us = rix_ticks_to_us((u32)(rix_now() - t0));
        if (g_last_tc_us > g_tc_timeout_us) return RIXE_FIFO_TC;
    }
    g_last_tc_us = rix_ticks_to_us((u32)(rix_now() - t0));
    return 0;
}

/* payload = RasterIX words; the FSC envelope (header + unused address
 * word) is added here.  Lists longer than the FIFO are sent as several
 * ST0->ST1 packets: the FSC only counts bytes (FrameStreamingCore.v,
 * STREAM state) and the CommandParser never looks at tlast
 * (CommandParser.v declares s_cmd_axis_tlast and does not use it), so a
 * packet boundary may fall anywhere in the command stream.  Each packet
 * waits for the vacancy the FSC frees while the GPU consumes the previous
 * one, which is the back-pressure A14D1 measured as "FIFO drain". */
/* Length in words of the CommandParser command starting with word w. */
static u32 cmd_words(u32 w)
{
    u32 op = w >> 28, imm = w & 0x0FFFFFFFu;
    switch (op) {
    case 1u: return 2u;                                  /* RENDER_CONFIG: reg + value */
    case 3u: return 1u + ((imm & 0x3FFu) >> 2);          /* TRIANGLE: size in bytes */
    case 4u: return 1u + ((imm & 0x1FFu) >> 2);          /* FOG LUT: size in bytes */
    case 5u: return 1u + (imm & 0x7FFFFu);               /* TEXTURE: page count */
    default: return 1u;                                  /* NOP, FRAMEBUFFER, others */
    }
}

static int submit_words_fifo(const u32 *payload, u32 words, int wait_last_tc)
{
    static u32 pkt[FIFO_DEPTH_WORDS];
    u32 i, off = 0u, chunk_max = g_fifo_capacity - 2u;
    int rc;
    if (words == 0u) return 0;
    while (off < words) {
        /* packets end on a command boundary whenever one fits (conservative:
           the RTL would accept any split, see above) */
        u32 n = 0u;
        while (off + n < words) {
            u32 c = cmd_words(payload[off + n]);
            if (n + c > chunk_max) break;
            n += c;
        }
        if (n == 0u) n = (words - off > chunk_max) ? chunk_max : (words - off); /* single oversized command */
        pkt[0] = FSC_ST0_TO_ST1(n * 4u);
        pkt[1] = 0u;
        for (i = 0; i < n; i++) pkt[2u + i] = payload[off + i];
        off += n;
        rc = fifo_send(pkt, n + 2u, (off >= words && wait_last_tc) ? 1u : 0u);
        if (rc) return rc;
    }
    return 0;
}
int rix_submit_words_fifo(const u32 *payload, u32 words) { return submit_words_fifo(payload, words, 1); }
int rix_submit_words_fifo_nowait(const u32 *payload, u32 words) { return submit_words_fifo(payload, words, 0); }

u32 rix_version(void) { return RIXGPU_VERSION; }
volatile u32 *rix_sd(void) { return g_sd; }
u32 rix_fifo_vacancy(void) { return mmio_rd(RIX_FIFO_BASE + FIFO_TDFV); }
u32 rix_fifo_isr(void) { return mmio_rd(RIX_FIFO_BASE + FIFO_ISR); }
extern void rix_frame_texture_invalidate(u32 base_arm);   /* rixframe.c */

static u32 hwcap_of(u32 probe_bit, u32 result)
{
    if (!(g_sd[SD_PROBE_RUN] & probe_bit)) return RIX_HWCAP_UNKNOWN;
    return (result == 1u) ? RIX_HWCAP_YES : RIX_HWCAP_NO;
}
void rix_hw_caps(rix_hwcaps *o)
{
    u32 caps = rix_caps();
    o->api_version = RIXGPU_VERSION;
    o->hw_magic = g_sd ? g_sd[SD_ID_MAGIC] : 0u; o->hw_version = g_sd ? g_sd[SD_ID_VERSION] : 0u;
    o->init_caps = caps;
    o->ddr_lists = (caps & RIXCAP_FENCE_MEM) ? RIX_HWCAP_YES : ((g_cfg.flags & RIXF_DLFIFO) || !(caps & RIXCAP_FENCE_FIFO)) ? RIX_HWCAP_UNKNOWN : RIX_HWCAP_NO;
    /* textures: the ladder's textured triangle (L13) rendered a texel inside and the clear outside (check bits 7) */
    o->textures = RIX_HWCAP_UNKNOWN;
    if (g_sd && g_sd[SD_LADDER_STAGES] > LAD_TRI_FIFO) {
        u32 rec = g_sd[SD_LADDER + LAD_TRI_FIFO * SD_LADDER_SLOTS];
        if (rec != 0xEEEEEEEEu) o->textures = ((g_sd[SD_LADDER + LAD_TRI_FIFO * SD_LADDER_SLOTS + 2u] & 7u) == 7u) ? RIX_HWCAP_YES : RIX_HWCAP_NO;
    }
    if (!g_sd) { o->depth_test = o->blending = o->alpha_test = o->arena_placement = o->tmu_modulate = o->lightmap_blend = RIX_HWCAP_UNKNOWN; o->tex_switch_64_us = o->tex_switch_256_us = 0u; }
    else {
        o->depth_test = hwcap_of(1u, g_sd[SD_PROBE_DEPTH]);
        o->blending = hwcap_of(2u, g_sd[SD_PROBE_BLEND]);
        o->alpha_test = hwcap_of(4u, g_sd[SD_PROBE_ALPHA]);
        o->arena_placement = hwcap_of(64u, g_sd[SD_PROBE_PLACE]);
        o->tmu_modulate = hwcap_of(128u, g_sd[SD_PROBE_MOD]);
        o->lightmap_blend = hwcap_of(256u, g_sd[SD_PROBE_LMBLEND]);
        o->tex_switch_64_us = (g_sd[SD_PROBE_RUN] & 8u) ? g_sd[SD_PROBE_TEX64_US] : 0u;
        o->tex_switch_256_us = (g_sd[SD_PROBE_RUN] & 16u) ? g_sd[SD_PROBE_TEX256_US] : 0u;
    }
    o->tmu_count = 1u; o->tex_max_size = 256u; o->tex_page_bytes = 2048u; o->depth_bits = 16u; o->desc_words = RIX_TRI_DESC_WORDS;
    o->fifo_words = g_fifo_capacity; o->timer_hz = rix_timer_hz();
}

#ifdef RIX_HOST_SIM
void rix_mem_clean(u32 addr, u32 bytes) { (void)addr; (void)bytes; }
#else
extern void dcache_clean_range(void *addr, uint32_t len);
void rix_mem_clean(u32 addr, u32 bytes) { dcache_clean_range((void *)(uintptr_t)addr, bytes); }
#endif

/* The FrameStreamingCore is a byte-counting state machine with no reset
 * from software.  If an earlier run died in the middle of an ST0->ST1
 * stream, the FSC is still waiting for the rest of that payload and would
 * swallow our first commands.  NOP words (0) are both an FSC NOP and a
 * CommandParser NOP, so flushing more words than the largest packet the
 * firmware or any client ever sends (4096 words = 16 KiB) guarantees the
 * FSC is back in COMMAND state. */
static int fsc_resync(void)
{
    static u32 zeros[FIFO_DEPTH_WORDS];
    u32 i, t0 = rix_now();
    int rc = 0;
    u32 chunk = (g_fifo_capacity < FIFO_DEPTH_WORDS) ? g_fifo_capacity : FIFO_DEPTH_WORDS;
    for (i = 0; i < FIFO_DEPTH_WORDS; i++) zeros[i] = 0u;
    for (i = 0; i < 10u; i++) {           /* >= 4608 words > largest packet ever sent (4096) */
        rc = fifo_send(zeros, chunk, 1u);
        if (rc) break;
    }
    g_st[RS_FSC_RESYNC_US] = rix_ticks_to_us((u32)(rix_now() - t0));
    return rc;
}

/* ------------------------------------------------------------------ */
/* display list builder                                                */
/* ------------------------------------------------------------------ */
void rix_dl_begin(rix_dl *dl, u32 base, u32 cap_words)
{
    dl->base = base;
    dl->cap_words = cap_words;
    dl->n = 0u;
    dl->overflow = 0u;
    dl->fence_value = 0u;
    dl->frag_shadow = RIX_FRAGMENT_A07B;
}

void rix_dl_word(rix_dl *dl, u32 w)
{
    if (dl->n >= dl->cap_words) { dl->overflow = 1u; return; }
    ((volatile u32 *)rix_ptr(dl->base))[dl->n++] = w;
}

void rix_dl_reg(rix_dl *dl, u32 reg, u32 val)
{
    rix_dl_word(dl, RIX_REG(reg));
    rix_dl_word(dl, val);
    if (reg == RIX_REG_FRAGMENT) dl->frag_shadow = val;
}

/* Hardware rule (run 1, 11/10): a color memset issued while the color mask
 * of register 3 is 0 produced no write and left the CommandParser waiting
 * forever (power cycle needed); the same memsets with the mask on ran clean
 * (run 3, ladder L3..L8).  The library therefore never issues a memset
 * without re-asserting the mask bits just before it. */
void rix_dl_clear(rix_dl *dl, u32 pixels)
{
    if ((dl->frag_shadow & RIX_FRAG_COLOR_MASK) != RIX_FRAG_COLOR_MASK)
        rix_dl_reg(dl, RIX_REG_FRAGMENT, dl->frag_shadow | RIX_FRAG_COLOR_MASK);
    rix_dl_word(dl, RIX_FB_CMD(pixels, RIX_FB_COLOR_SELECT | RIX_FB_MEMSET));
}

/* Same rule for the depth buffer (C1b probe run, 11/10): StreamFramebuffer
 * strobes the depth memset with confMask = {depthMask, depthMask}; a depth
 * memset issued with the depth mask of register 3 clear wrote nothing (it did
 * not hang).  The mask is re-asserted just before the memset. */
void rix_dl_clear_depth(rix_dl *dl, u32 pixels)
{
    if (!(dl->frag_shadow & RIX_FRAG_DEPTH_MASK))
        rix_dl_reg(dl, RIX_REG_FRAGMENT, dl->frag_shadow | RIX_FRAG_DEPTH_MASK);
    rix_dl_word(dl, RIX_FB_CMD(pixels, RIX_FB_DEPTH_SELECT | RIX_FB_MEMSET));
}

void rix_dl_tri(rix_dl *dl, const u32 *desc39)
{
    volatile u32 *p;
    u32 i;
    if (dl->n + 1u + RIX_TRI_DESC_WORDS > dl->cap_words) { dl->overflow = 1u; return; }
    p = (volatile u32 *)rix_ptr(dl->base) + dl->n;
    p[0] = RIX_TRI_CMD;
    for (i = 0; i < RIX_TRI_DESC_WORDS; i++) p[1u + i] = desc39[i];
    dl->n += 1u + RIX_TRI_DESC_WORDS;
}

void rix_dl_texture(rix_dl *dl, u32 tmu, u32 pages, u32 base_arm)
{
    u32 i;
    rix_dl_word(dl, RIX_TEX_CMD(pages, tmu));
    for (i = 0; i < pages; i++) rix_dl_word(dl, base_arm + i * 2048u);
}

u32 rix_fence_color(u32 v)
{
    u32 r = (v >> 11) & 31u, g = (v >> 5) & 63u, b = v & 31u;
    return RIX_RGBA(r << 3, g << 2, b << 3, 0u);
}

/* Frame head: color buffer address and scissor-off are only ever changed
 * here, i.e. at a point where the GPU is idle (the previous list ended
 * with its fence, which Core1 observed before submitting this one). */
void rix_dl_frame_head(rix_dl *dl, u32 buf_arm, u32 features, u32 clear_rgba, int do_clear)
{
    rix_dl_reg(dl, RIX_REG_COLOR_ADDR, buf_arm);
    rix_dl_reg(dl, RIX_REG_FEATURES, features & ~RIX_FEAT_SCISSOR);
    if (do_clear) {
        rix_dl_reg(dl, RIX_REG_CLEAR_COLOR, clear_rgba);
        rix_dl_clear(dl, RIX_W * RIX_H_ALLOC);
    }
}

/* Every one of the 19 OP_RENDER_CONFIG registers, written to a known
 * value.  The register bank has no reset value the software can rely on
 * (power-on = 0, otherwise whatever the previous client left), and a bank
 * at 0 masks every color write (see RIX_FRAG_COLOR_MASK).  rix_init sends
 * this before its first probe; a renderer may send it again whenever it
 * wants to start from a known state.  No memory is written by this list. */
void rix_dl_baseline(rix_dl *dl, u32 buf_arm)
{
    rix_dl_reg(dl, RIX_REG_FEATURES, 0u);
    rix_dl_reg(dl, RIX_REG_CLEAR_COLOR, 0u);
    rix_dl_reg(dl, RIX_REG_CLEAR_DEPTH, 0u);
    rix_dl_reg(dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
    rix_dl_reg(dl, RIX_REG_STENCIL, 0u);
    rix_dl_reg(dl, RIX_REG_FOG_COLOR, 0u);
    rix_dl_reg(dl, RIX_REG_SCISSOR_START, RIX_XY(0u, 0u));
    rix_dl_reg(dl, RIX_REG_SCISSOR_END, RIX_XY(RIX_W, RIX_H_ALLOC));
    rix_dl_reg(dl, RIX_REG_Y_OFFSET, 0u);
    rix_dl_reg(dl, RIX_REG_RESOLUTION, RIX_XY(RIX_W, RIX_H_ALLOC));
    rix_dl_reg(dl, RIX_REG_TMU0_ENV, RIX_TMU_ENV_REPLACE);
    rix_dl_reg(dl, RIX_REG_TMU0_ENVCOLOR, 0u);
    rix_dl_reg(dl, RIX_REG_TMU0_CONFIG, RIX_TMU_CFG_A04);
    rix_dl_reg(dl, RIX_REG_TMU1_ENV, 0u);
    rix_dl_reg(dl, RIX_REG_TMU1_ENVCOLOR, 0u);
    rix_dl_reg(dl, RIX_REG_TMU1_CONFIG, 0u);
    rix_dl_reg(dl, RIX_REG_COLOR_ADDR, buf_arm);
    rix_dl_reg(dl, RIX_REG_DEPTH_ADDR, 0u);
    rix_dl_reg(dl, RIX_REG_STENCIL_ADDR, 0u);
}

/* Fence = scissored 64-pixel memset into GL line y=0, which is memory
 * line 480: allocated (481-line buffers) but never scanned out.  Neither
 * the color buffer address nor the resolution changes, so a fragment of
 * the last triangle still sitting in the fragment broadcast register when
 * the scissor is enabled (a few-cycle window that only opens under DDR
 * back-pressure) is scissored away instead of being written to a wrong
 * address.  The NOP drain in front makes even that window practically
 * unreachable.  The memset travels through the same scissor/strobe/MMU/
 * adapter/coalescer/AXI chain as every pixel before it
 * (StreamFramebuffer.v, FramebufferWriterClear.v), so when Core1 reads
 * the fence value back from DDR, all earlier pixel writes of this list
 * have landed.
 *
 * The write strobe of that memset is gated by the color mask of register
 * 3 like every pixel (FramebufferWriterStrobeGen.v), so the fence sets
 * register 3 itself: a renderer that masked channels for its own drawing
 * must restore its value at the head of the next list. */
void rix_dl_fence_at(rix_dl *dl, u32 value16, u32 features, u32 x0)
{
    rix_dl_fence_at_drain(dl, value16, features, x0, (g_dl_mode == 1u) ? FENCE_DRAIN_NOPS_MEM : FENCE_DRAIN_NOPS_FIFO);
}

void rix_dl_fence_at_drain(rix_dl *dl, u32 value16, u32 features, u32 x0, u32 drain)
{
    u32 i;
    for (i = 0; i < drain; i++) rix_dl_word(dl, RIX_OP_NOP);
    rix_dl_reg(dl, RIX_REG_FEATURES, features | RIX_FEAT_SCISSOR);
    rix_dl_reg(dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
    rix_dl_reg(dl, RIX_REG_SCISSOR_START, RIX_XY(x0, 0u));
    rix_dl_reg(dl, RIX_REG_SCISSOR_END, RIX_XY(x0 + FENCE_PIXELS, 1u));
    rix_dl_reg(dl, RIX_REG_CLEAR_COLOR, rix_fence_color(value16));
    rix_dl_clear(dl, FENCE_PIXELS);
    if (x0 == 0u) dl->fence_value = value16;
}

void rix_dl_fence(rix_dl *dl, u32 value16, u32 features)
{
    rix_dl_fence_at(dl, value16, features, 0u);
}

/* A fence pixel holds the LAST value written there (chunk fences: a later chunk of the same
   buffer may already have overwritten ours).  It is "passed" when it shows our value or one
   written after it; "after" is bounded (a slot is re-checked at most ring_slots chunks later, a
   frame fence right after its submission), so the accepted window is small: a stale value from
   long ago cannot wrap into it (the former signed-16 test accepted anything within 32767 ahead,
   e.g. a zeroed pixel against a sequence > 32768).  0 is never a fence value. */
#define FENCE_WINDOW 1024u
int rix_fence_passed_at(u32 buf_arm, u32 value16, u32 x0)
{
    volatile u16 *f = fence_row(buf_arm);
    u16 cur;
    dmb();
    cur = f[x0];
    if (cur == 0u) return 0;
    return ((u16)(cur - (u16)value16)) < FENCE_WINDOW;
}

u32 rix_dl_finish(rix_dl *dl)
{
    /* DDR lists are fetched in 64-byte bursts (LinearAddressGenerator.v):
       pad to 16 words.  FIFO packets carry an exact byte count. */
    if (g_dl_mode == 1u)
        while ((dl->n & 15u) != 0u && !dl->overflow) rix_dl_word(dl, RIX_OP_NOP);
    dsb();
    return dl->n * 4u;
}

/* ------------------------------------------------------------------ */
/* submission                                                          */
/* ------------------------------------------------------------------ */
u32 rix_dl_mode(void) { return g_dl_mode; }
void rix_force_dl_mode(u32 mode) { g_dl_mode = mode; }

/* FIFO transport whatever the current list mode: used for texture page
 * tables, so that the TMU never has to share the AXI read channel with a
 * FrameStreamingCore that is itself paused on the parser (ladder L15). */
int rix_submit_fifo(rix_dl *dl)
{
    u32 saved = g_dl_mode;
    int rc;
    if (dl->overflow) return RIXE_DL_OVERFLOW;
    g_dl_mode = 2u;
    rix_dl_finish(dl);
    rc = rix_submit_words_fifo((const u32 *)rix_ptr(dl->base), dl->n);
    g_dl_mode = saved;
    return rc;
}

static int submit_dl_dma(rix_dl *dl, u32 wait_last_tc);
int rix_submit(rix_dl *dl)
{
    u32 bytes;
    if (dl->overflow) return RIXE_DL_OVERFLOW;
    bytes = rix_dl_finish(dl);
    if (g_dl_mode == 3u) return submit_dl_dma(dl, 0u);
    if (g_dl_mode == 1u) {
        u32 cmd[2];
        cmd[0] = FSC_MEM_TO_ST1(bytes);
        cmd[1] = dl->base;
        return fifo_send(cmd, 2u, 0u);
    }
    return rix_submit_words_fifo((const u32 *)rix_ptr(dl->base), dl->n);
}

/* ------------------------------------------------------------------ */
/* fence                                                               */
/* ------------------------------------------------------------------ */
void rix_fence_reset(u32 buf_arm)
{
    volatile u16 *f = fence_row(buf_arm);
    u32 i;
    for (i = 0; i < rix_lay_w; i++) f[i] = 0u;    /* whole fence line (480 in 640 x 481) */
    dsb();
}

int rix_fence_check(u32 buf_arm, u32 value16)
{
    volatile u16 *f = fence_row(buf_arm);
    u32 i;
    dmb();
    if (f[FENCE_PIXELS - 1u] != (u16)value16) return 0;
    if (f[0] != (u16)value16) return 0;
    for (i = 1; i < FENCE_PIXELS - 1u; i++)
        if (f[i] != (u16)value16) return 0;
    return 1;
}

int rix_fence_wait(u32 buf_arm, u32 value16, u32 timeout_us, volatile u32 *stop, u32 *polls)
{
    u32 t0 = rix_now(), n = 0u, tick = g_cfg.idle_tick_us;
    for (;;) {
        n++;
        if (rix_fence_check(buf_arm, value16)) { if (polls) *polls = n; return 0; }
        if (rix_ticks_to_us((u32)(rix_now() - t0)) > timeout_us) { if (polls) *polls = n; return RIXE_FENCE_TIMEOUT; }
        if (stop && *stop) { if (polls) *polls = n; return RIXE_FENCE_TIMEOUT; }
        rix_idle_us(tick);
    }
}

/* ------------------------------------------------------------------ */
/* PL330 DMAC: in-place 16-bit byte swap                                */
/* ------------------------------------------------------------------ */
static u32 dmac_rd(u32 off) { return mmio_rd(g_dmac_base + off); }
static void dmac_wr(u32 off, u32 v) { mmio_wr(g_dmac_base + off, v); }

/* An APB access to an unclocked or held-in-reset DMAC could stall the bus
 * forever (no power-cycle-free recovery), so the SLCR is consulted first:
 * both registers are always readable. */
static int dmac_probe(void)
{
    u32 aper = mmio_rd(SLCR_APER_CLK_CTRL), rst = mmio_rd(SLCR_DMAC_RST_CTRL), dsr;
    g_sd[SD_DMAC_DSR] = (aper & 1u) | ((rst & 1u) << 1) | 0x80000000u;
    if (!(aper & 1u) || (rst & 1u)) { g_dmac_base = 0u; return -1; }
    dsr = mmio_rd(DMAC_S_BASE + DMAC_DSR);
    g_dmac_ns = (dsr >> 9) & 1u;
    g_dmac_base = g_dmac_ns ? DMAC_NS_BASE : DMAC_S_BASE;
    return 0;
}

static u32 dma_emit32(volatile u8 *p, u32 off, u32 v)
{
    p[off + 0u] = (u8)(v);
    p[off + 1u] = (u8)(v >> 8);
    p[off + 2u] = (u8)(v >> 16);
    p[off + 3u] = (u8)(v >> 24);
    return off + 4u;
}

/* count = number of 128-byte LD/ST pairs; factor into inner*outer <= 256 each */
static int dma_factor(u32 count, u32 *inner, u32 *outer)
{
    u32 n;
    for (n = (count + 255u) / 256u; n <= 256u; n++) {
        if (count % n == 0u) { *outer = n; *inner = count / n; return 1; }
    }
    return 0;
}

static int dma_build(u32 addr, u32 bytes, u32 ccr)
{
    volatile u8 *p = (volatile u8 *)rix_ptr(DMA_PROG_ARM);
    u32 count = bytes / PL330_BYTES_PER_LDST, inner, outer, off = 0u, body;
    if ((bytes % PL330_BYTES_PER_LDST) != 0u || count == 0u) return RIXE_DMA_FAULT;
    if (!dma_factor(count, &inner, &outer)) return RIXE_DMA_FAULT;
    p[off++] = PL330_DMAMOV; p[off++] = PL330_RD_SAR; off = dma_emit32(p, off, addr);
    p[off++] = PL330_DMAMOV; p[off++] = PL330_RD_DAR; off = dma_emit32(p, off, addr);
    p[off++] = PL330_DMAMOV; p[off++] = PL330_RD_CCR; off = dma_emit32(p, off, ccr);
    p[off++] = (u8)(PL330_DMALP | 2u); p[off++] = (u8)(outer - 1u);   /* lc1 */
    body = off;
    p[off++] = PL330_DMALP;            p[off++] = (u8)(inner - 1u);   /* lc0 */
    p[off++] = PL330_DMALD;
    p[off++] = PL330_DMAST;
    p[off++] = (u8)(PL330_DMALPEND | 0x10u);        p[off++] = 2u;    /* lc0, nf, jump 2 */
    {
        u32 lpend1 = off;                                  /* address of the outer DMALPEND */
        p[off++] = (u8)(PL330_DMALPEND | 0x10u | 0x4u);
        p[off++] = (u8)(lpend1 - body);                    /* backward jump to inner DMALP */
    }
    p[off++] = PL330_DMAWMB;
    p[off++] = PL330_DMAEND;
    while (off < DMA_PROG_MAX) p[off++] = PL330_DMAEND;
    dsb();
    return 0;
}

static int dma_go(void)
{
    u32 i;
    for (i = 0; i < 100000u; i++) if ((dmac_rd(DMAC_DBGSTATUS) & 1u) == 0u) break;
    if (i == 100000u) return RIXE_DMA_TIMEOUT;
    /* DMAGO executed by the manager thread: DBGINST0[23:16]=opcode, [31:24]=channel */
    dmac_wr(DMAC_DBGINST0, ((PL330_DMAGO | (g_dmac_ns ? 2u : 0u)) << 16) | (DMAC_CHANNEL << 24));
    dmac_wr(DMAC_DBGINST1, DMA_PROG_ARM);
    dsb();
    dmac_wr(DMAC_DBGCMD, 0u);
    dsb();
    return 0;
}

static void dma_kill(void)
{
    u32 i;
    for (i = 0; i < 100000u; i++) if ((dmac_rd(DMAC_DBGSTATUS) & 1u) == 0u) break;
    dmac_wr(DMAC_DBGINST0, (PL330_DMAKILL << 16) | (DMAC_CHANNEL << 8) | 1u);
    dmac_wr(DMAC_DBGINST1, 0u);
    dsb();
    dmac_wr(DMAC_DBGCMD, 0u);
    dsb();
}

static int dma_poll_done(u32 *faulted)
{
    u32 st = dmac_rd(DMAC_CSR(DMAC_CHANNEL)) & DMAC_CSR_STATE_MASK;
    u32 fs = dmac_rd(DMAC_FSRC);
    *faulted = 0u;
    if ((fs & (1u << DMAC_CHANNEL)) || st == DMAC_CSR_FAULTING || st == DMAC_CSR_FAULTING_COMPLETING) {
        *faulted = 1u;
        return 1;
    }
    return st == DMAC_CSR_STOPPED;
}

static int dma_wait(u32 timeout_us);
static int dma_start(u32 addr, u32 bytes, u32 ccr)
{
    int rc;
    if (g_dma_busy) { rc = dma_wait(500000u); if (rc) return rc; }
    rc = dma_build(addr, bytes, ccr);
    if (rc) return rc;
    rc = dma_go();
    if (rc) return rc;
    g_dma_busy = 1u;
    return 0;
}

static int dma_wait(u32 timeout_us)
{
    u32 t0 = rix_now(), faulted;
    if (!g_dma_busy) return 0;
    for (;;) {
        if (dma_poll_done(&faulted)) {
            g_dma_busy = 0u;
            if (faulted) {
                u32 ftr = dmac_rd(DMAC_FTR(DMAC_CHANNEL));
                g_st[RS_DMA_FAULTS]++;
                g_sd[SD_DMAC_DSR] = ftr;
                dma_kill();
                return RIXE_DMA_FAULT;
            }
            dsb();
            return 0;
        }
        if (rix_ticks_to_us((u32)(rix_now() - t0)) > timeout_us) {
            dma_kill();
            g_dma_busy = 0u;
            return RIXE_DMA_TIMEOUT;
        }
        rix_idle_us(g_cfg.idle_tick_us);
    }
}

/* ------------------------------------------------------------------ */
/* PL330 -> ST0 FIFO transport (D2d)                                   */
/* ------------------------------------------------------------------ */
/* D2b/D2c on the Amiga: a DDR list (FSC MEM->ST1) hangs the parser as soon as a fragment reads
   the framebuffer (probe 9: two depth-tested triangles), while the same words through the ST0
   FIFO render everything (ROOM in DLFIFO) - at 1.1 us per word of Core1 MMIO.  Here the DMAC
   moves the words from the list in DDR into TDFD (fixed address); Core1 only writes the 2-word
   FSC header and TLR per packet.  Channel 6 (the endian swap keeps channel 7), its own program
   area (dma_prog + 0x200, same Non-Cacheable page).  Packets of DMAFIFO_PKT words so that two
   fit in the FIFO: the DMA of packet k+1 overlaps the parser's consumption of packet k.
   The self-test (dmafifo_selftest) decides between 1-beat stores and 16-beat FIXED bursts and
   proves count, integrity (a fence carried by the DMA) and throughput before the mode is used. */
#define DMAFIFO_CHANNEL   6u
#define DMAFIFO_PROG_ARM  (g_cfg.dma_prog + 0x200u)
#define DMAFIFO_PROG_MAX  128u
#define DMAFIFO_PKT       248u           /* payload words per packet (+2 header = 250; 2 x 250 <= 508) */
static u32 g_dmafifo_burst;              /* beats per DMAST: 1 or 16; 0 = transport unusable */
static u32 g_dmafifo_us;                 /* Core1 time spent in DMA-FIFO submissions (per-frame diag) */
u32 rix_dmafifo_us(void) { u32 v = g_dmafifo_us; g_dmafifo_us = 0u; return v; }

static u32 ccr_fifo(u32 src_beats, u32 dst_beats)
{   /* src incrementing, 4-byte beats, src_beats per DMALD; dst fixed address (TDFD), 4-byte beats, dst_beats per DMAST */
    return 1u | (2u << 1) | ((src_beats - 1u) << 4) | (2u << 15) | ((dst_beats - 1u) << 18);
}

static int dmafifo_build(u32 src, u32 words, u32 dst_beats)
{
    volatile u8 *p = (volatile u8 *)rix_ptr(DMAFIFO_PROG_ARM);
    u32 off = 0u, blocks = words / 16u, tail = words % 16u, body = 0u, st = 0u, lp;
    if (words == 0u || blocks > 256u) return RIXE_DMA_FAULT;
    p[off++] = PL330_DMAMOV; p[off++] = PL330_RD_SAR; off = dma_emit32(p, off, src);
    p[off++] = PL330_DMAMOV; p[off++] = PL330_RD_DAR; off = dma_emit32(p, off, RIX_FIFO_BASE + FIFO_TDFD);
    if (blocks) {
        p[off++] = PL330_DMAMOV; p[off++] = PL330_RD_CCR; off = dma_emit32(p, off, ccr_fifo(16u, dst_beats));
        p[off++] = (u8)(PL330_DMALP | 2u); p[off++] = (u8)(blocks - 1u);     /* lc1: 64-byte blocks */
        body = off;
        p[off++] = PL330_DMALD;                                              /* 16 words into the MFIFO */
        if (dst_beats == 16u) {
            p[off++] = PL330_DMAST;                                          /* one 16-beat FIXED burst to TDFD */
        } else {
            p[off++] = PL330_DMALP; p[off++] = 15u;                          /* lc0: 16 single-beat stores */
            st = off;
            p[off++] = PL330_DMAST;
            lp = off; p[off++] = (u8)(PL330_DMALPEND | 0x10u); p[off++] = (u8)(lp - st);
        }
        lp = off; p[off++] = (u8)(PL330_DMALPEND | 0x10u | 0x4u); p[off++] = (u8)(lp - body);
    }
    if (tail) {
        p[off++] = PL330_DMAMOV; p[off++] = PL330_RD_CCR; off = dma_emit32(p, off, ccr_fifo(tail, (dst_beats == 16u) ? tail : 1u));
        p[off++] = PL330_DMALD;
        if (dst_beats == 16u) {
            p[off++] = PL330_DMAST;
        } else {
            p[off++] = PL330_DMALP; p[off++] = (u8)(tail - 1u);
            st = off;
            p[off++] = PL330_DMAST;
            lp = off; p[off++] = (u8)(PL330_DMALPEND | 0x10u); p[off++] = (u8)(lp - st);
        }
    }
    p[off++] = PL330_DMAWMB;
    p[off++] = PL330_DMAEND;
    while (off < DMAFIFO_PROG_MAX) p[off++] = PL330_DMAEND;
    dsb();
    return 0;
}

static int dmafifo_go(u32 src, u32 words, u32 dst_beats)
{
    u32 i;
    int rc = dmafifo_build(src, words, dst_beats);
    if (rc) return rc;
    for (i = 0; i < 100000u; i++) if ((dmac_rd(DMAC_DBGSTATUS) & 1u) == 0u) break;
    if (i == 100000u) return RIXE_DMA_TIMEOUT;
    dmac_wr(DMAC_DBGINST0, ((PL330_DMAGO | (g_dmac_ns ? 2u : 0u)) << 16) | (DMAFIFO_CHANNEL << 24));
    dmac_wr(DMAC_DBGINST1, DMAFIFO_PROG_ARM);
    dsb();
    dmac_wr(DMAC_DBGCMD, 0u);
    dsb();
    return 0;
}

static void dmafifo_kill(void)
{
    u32 i;
    for (i = 0; i < 100000u; i++) if ((dmac_rd(DMAC_DBGSTATUS) & 1u) == 0u) break;
    dmac_wr(DMAC_DBGINST0, (PL330_DMAKILL << 16) | (DMAFIFO_CHANNEL << 8) | 1u);
    dmac_wr(DMAC_DBGINST1, 0u);
    dsb();
    dmac_wr(DMAC_DBGCMD, 0u);
    dsb();
}

static int dmafifo_wait(u32 timeout_us)
{
    u32 t0 = rix_now();
    for (;;) {
        u32 st = dmac_rd(DMAC_CSR(DMAFIFO_CHANNEL)) & DMAC_CSR_STATE_MASK;
        u32 fs = dmac_rd(DMAC_FSRC);
        if ((fs & (1u << DMAFIFO_CHANNEL)) || st == DMAC_CSR_FAULTING || st == DMAC_CSR_FAULTING_COMPLETING) {
            g_sd[SD_DMAC_DSR] = dmac_rd(DMAC_FTR(DMAFIFO_CHANNEL));
            g_st[RS_DMA_FAULTS]++;
            dmafifo_kill();
            return RIXE_DMA_FAULT;
        }
        if (st == DMAC_CSR_STOPPED) { dsb(); return 0; }
        if (rix_ticks_to_us((u32)(rix_now() - t0)) > timeout_us) { dmafifo_kill(); return RIXE_DMA_TIMEOUT; }
        cpu_yield();
    }
}

/* one packet: header by MMIO, payload by the DMA, TLR by MMIO; vacancy waited like fifo_send */
static int dmafifo_send_packet(u32 src, u32 words, u32 dst_beats, u32 wait_tc)
{
    u32 n = words + 2u, vac, isr, t0;
    int rc;
    if (n > g_fifo_capacity) return RIXE_FIFO_VACANCY;
    vac = mmio_rd(RIX_FIFO_BASE + FIFO_TDFV);
    if (vac < n) {
        t0 = rix_now();
        while (vac < n) {
            if (rix_ticks_to_us((u32)(rix_now() - t0)) > 500000u) return RIXE_FIFO_VACANCY;
            cpu_yield();
            vac = mmio_rd(RIX_FIFO_BASE + FIFO_TDFV);
        }
    }
    isr = mmio_rd(RIX_FIFO_BASE + FIFO_ISR);
    if (isr & FIFO_INT_ERR) { mmio_wr(RIX_FIFO_BASE + FIFO_ISR, FIFO_INT_ERR); return RIXE_FIFO_ERR; }
    mmio_wr(RIX_FIFO_BASE + FIFO_ISR, FIFO_INT_TC | FIFO_INT_ERR);
    t0 = rix_now();
    mmio_wr(RIX_FIFO_BASE + FIFO_TDFD, FSC_ST0_TO_ST1(words * 4u));
    mmio_wr(RIX_FIFO_BASE + FIFO_TDFD, 0u);
    rc = dmafifo_go(src, words, dst_beats);
    if (rc) return rc;
    rc = dmafifo_wait(100000u);
    if (rc) return rc;
    mmio_wr(RIX_FIFO_BASE + FIFO_TLF, n * 4u);
    g_dmafifo_us += rix_ticks_to_us((u32)(rix_now() - t0));
    if (!wait_tc) return 0;
    t0 = rix_now();
    for (;;) {
        isr = mmio_rd(RIX_FIFO_BASE + FIFO_ISR);
        if (isr & FIFO_INT_ERR) return RIXE_FIFO_ERR;
        if (isr & FIFO_INT_TC) break;
        g_last_tc_us = rix_ticks_to_us((u32)(rix_now() - t0));
        if (g_last_tc_us > g_tc_timeout_us) return RIXE_FIFO_TC;
    }
    g_last_tc_us = rix_ticks_to_us((u32)(rix_now() - t0));
    return 0;
}

/* a list in DDR as FIFO packets carried by the DMA (packets end on command boundaries) */
static int submit_dl_dma(rix_dl *dl, u32 wait_last_tc)
{
    const u32 *payload = (const u32 *)rix_ptr(dl->base);
    u32 off = 0u, words = dl->n;
    int rc;
    if (g_dmafifo_burst == 0u) return RIXE_DMAFIFO;
    while (off < words) {
        u32 n = 0u;
        while (off + n < words) {
            u32 c = cmd_words(payload[off + n]);
            if (n + c > DMAFIFO_PKT) break;
            n += c;
        }
        if (n == 0u) n = (words - off > DMAFIFO_PKT) ? DMAFIFO_PKT : (words - off);
        rc = dmafifo_send_packet(dl->base + off * 4u, n, g_dmafifo_burst, (off + n >= words) ? wait_last_tc : 0u);
        if (rc) return rc;
        off += n;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* NEON / scalar byte swap (fallbacks)                                  */
/* ------------------------------------------------------------------ */
void rix_swap16_neon(u32 addr, u32 bytes)
{
#if !RIX_HAVE_NEON
    rix_swap16_cpu32(addr, bytes);
#else
    uint8_t *p = (uint8_t *)rix_ptr(addr);
    u32 n = bytes / 64u, i;
    for (i = 0; i < n; i++) {
        uint8x16_t a = vld1q_u8(p), b = vld1q_u8(p + 16), c = vld1q_u8(p + 32), d = vld1q_u8(p + 48);
        vst1q_u8(p, vrev16q_u8(a));
        vst1q_u8(p + 16, vrev16q_u8(b));
        vst1q_u8(p + 32, vrev16q_u8(c));
        vst1q_u8(p + 48, vrev16q_u8(d));
        p += 64;
    }
    rix_swap16_cpu32(addr + n * 64u, bytes - n * 64u);
    dsb();
#endif
}

void rix_swap16_cpu32(u32 addr, u32 bytes)
{
    volatile u32 *p = (volatile u32 *)rix_ptr(addr);
    u32 n = bytes / 4u, i;
    for (i = 0; i < n; i++) {
        u32 v = p[i];
        p[i] = ((v & 0x00FF00FFu) << 8) | ((v & 0xFF00FF00u) >> 8);
    }
    dsb();
}

/* ------------------------------------------------------------------ */
/* swap front end                                                      */
/* ------------------------------------------------------------------ */
u32 rix_swap_mode(void) { return g_swap_mode; }
void rix_force_swap_mode(u32 mode) { g_swap_mode = mode; }
int rix_swap_busy(void) { return (int)g_dma_busy; }

int rix_swap_start(u32 addr, u32 bytes)
{
    if (g_swap_mode == 1u) {
        g_dma_addr = addr; g_dma_bytes = bytes;
        return dma_start(addr, bytes, PL330_CCR_SWAP16);
    }
    if (g_swap_mode == 2u) { rix_swap16_neon(addr, bytes); return 0; }
    return 0;
}

int rix_swap_wait(u32 timeout_us)
{
    if (g_swap_mode == 1u) {
        int rc = dma_wait(timeout_us);
        if (rc == RIXE_DMA_FAULT || rc == RIXE_DMA_TIMEOUT) {
            /* finish this frame on the CPU so the display stays correct,
               then stay on NEON for the rest of the run */
            rix_swap16_neon(g_dma_addr, g_dma_bytes);
            g_swap_mode = 2u;
            g_st[RS_SWAP_MODE] = 2u;
        }
        return 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* scanout observation                                                 */
/* ------------------------------------------------------------------ */
u32 rix_vdma_sa(void)
{
    return mmio_rd(VDMA_BASE + VDMA_MM2S_SA1);
}

/* ------------------------------------------------------------------ */
/* self tests                                                          */
/* ------------------------------------------------------------------ */
static void pattern_fill(u32 addr, u32 bytes, u32 seed)
{
    volatile u32 *p = (volatile u32 *)rix_ptr(addr);
    u32 i, n = bytes / 4u, x = seed;
    for (i = 0; i < n; i++) { x = x * 1664525u + 1013904223u; p[i] = x; }
    dsb();
}

static int pattern_check_swapped(u32 addr, u32 bytes, u32 seed)
{
    volatile u32 *p = (volatile u32 *)rix_ptr(addr);
    u32 i, n = bytes / 4u, x = seed;
    dmb();
    for (i = 0; i < n; i++) {
        u32 e;
        x = x * 1664525u + 1013904223u;
        e = ((x & 0x00FF00FFu) << 8) | ((x & 0xFF00FF00u) >> 8);
        if (p[i] != e) return 0;
    }
    return 1;
}

int rix_selftest_dma(volatile u32 *sd)
{
    u32 t0, dt, i, mn = 0xFFFFFFFFu, mx = 0u, sum = 0u;
    int rc;
    sd[SD_DMA_TEST] = 0u;
    sd[SD_DMAC_BASE] = g_dmac_base;
    if (g_dmac_base == 0u) { sd[SD_DMA_TEST] = 5u; return 0; }   /* unclocked / in reset */
    sd[SD_DMAC_DSR] = mmio_rd(DMAC_S_BASE + DMAC_DSR);
    if (g_cfg.flags & RIXF_NODMA) return 0;
    pattern_fill(RIX_SCRATCH_ARM, 4096u, 0x1234567u);
    rc = dma_start(RIX_SCRATCH_ARM, 4096u, PL330_CCR_SWAP16);
    if (rc) { sd[SD_DMA_TEST] = 3u; return 0; }
    rc = dma_wait(100000u);
    if (rc == RIXE_DMA_TIMEOUT) { sd[SD_DMA_TEST] = 4u; return 0; }
    if (rc) { sd[SD_DMA_TEST] = 3u; return 0; }
    if (!pattern_check_swapped(RIX_SCRATCH_ARM, 4096u, 0x1234567u)) { sd[SD_DMA_TEST] = 2u; return 0; }
    /* timing on a real framebuffer-sized region (FB2 is free at init) */
    for (i = 0; i < 4u; i++) {
        t0 = rix_now();
        if (dma_start(RIX_FB2_ARM, RIX_FB_BYTES, PL330_CCR_SWAP16)) { sd[SD_DMA_TEST] = 3u; return 0; }
        if (dma_wait(200000u)) { sd[SD_DMA_TEST] = 4u; return 0; }
        dt = rix_ticks_to_us((u32)(rix_now() - t0));
        if (dt < mn) mn = dt;
        if (dt > mx) mx = dt;
        sum += dt;
    }
    sd[SD_DMA_US_MIN] = mn; sd[SD_DMA_US_MAX] = mx; sd[SD_DMA_US_AVG] = sum / 4u;
    sd[SD_DMA_TEST] = 1u;
    g_caps |= RIXCAP_DMA;
    return 1;
}

int rix_selftest_neon(volatile u32 *sd)
{
    u32 t0, dt, i, mn = 0xFFFFFFFFu, mx = 0u, sum = 0u;
    pattern_fill(RIX_SCRATCH_ARM, 4096u, 0x7654321u);
    rix_swap16_neon(RIX_SCRATCH_ARM, 4096u);
    if (!pattern_check_swapped(RIX_SCRATCH_ARM, 4096u, 0x7654321u)) return 0;
    for (i = 0; i < 3u; i++) {
        t0 = rix_now();
        rix_swap16_neon(RIX_FB2_ARM, RIX_FB_BYTES);
        dt = rix_ticks_to_us((u32)(rix_now() - t0));
        if (dt < mn) mn = dt;
        if (dt > mx) mx = dt;
        sum += dt;
    }
    sd[SD_NEON_US_MIN] = mn; sd[SD_NEON_US_MAX] = mx; sd[SD_NEON_US_AVG] = sum / 3u;
    g_caps |= RIXCAP_NEON;
    return 1;
}

/* ------------------------------------------------------------------ */
/* init / shutdown                                                     */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* init ladder                                                         */
/* ------------------------------------------------------------------ */
static u32 g_gpu_hung;      /* a ladder stage was never taken: nothing may be sent any more */

static void lad_record(u32 stage, int rc, u32 checks, u32 map)
{
    volatile u32 *r = g_sd + SD_LADDER + stage * SD_LADDER_SLOTS;
    r[0] = (u32)rc;
    r[1] = g_last_tc_us;
    r[2] = checks;
    r[3] = map;
    g_sd[SD_LADDER_STAGES] = stage + 1u;
}

static inline u32 px_at(u32 buf, u32 line, u32 x)
{
    return ((volatile u16 *)rix_ptr(buf))[line * RIX_W + x];
}

/* first<<16 | last line of buf (nlines lines) holding value16; 0xFFFF = none */
static u32 write_map(u32 buf, u32 nlines, u32 value16)
{
    volatile u16 *p = (volatile u16 *)rix_ptr(buf);
    u32 y, x, first = 0xFFFFu, last = 0xFFFFu;
    for (y = 0; y < nlines; y++) {
        for (x = 0; x < RIX_W; x++) if (p[y * RIX_W + x] == (u16)value16) break;
        if (x < RIX_W) { if (first == 0xFFFFu) first = y; last = y; }
    }
    return (first << 16) | last;
}

/* Two OP_NOP words through FSC and CommandParser.  The FSC preloads one
 * beat on its own, so the second word is only taken (and TC only fires)
 * once the parser has consumed the first: TC proves the parser is alive. */
static int lad_alive(void)
{
    u32 w[2] = { RIX_OP_NOP, RIX_OP_NOP };
    return rix_submit_words_fifo(w, 2u);
}
int rix_alive(void) { return lad_alive(); }

/* wait (<= 50 ms) until 16-bit pixel index idx of buf reads value16 */
static int lad_wait_idx(u32 buf, u32 idx, u32 value16)
{
    volatile u16 *p = (volatile u16 *)rix_ptr(buf);
    u32 t0 = rix_now();
    for (;;) {
        dmb();
        if (p[idx] == value16) return 1;
        if (rix_ticks_to_us((u32)(rix_now() - t0)) > 50000u) return 0;
        cpu_yield();
    }
}
static int lad_wait_px(u32 buf, u32 line, u32 x, u32 value16)
{
    return lad_wait_idx(buf, line * RIX_W + x, value16);
}

/* memset recipe shared by the stages: all 19 registers known, then one
 * color-buffer memset of `pixels` at resolution (xres, yres) into buf. */
static void lad_memset_list(rix_dl *dl, u32 buf, u32 xres, u32 yres, u32 value16, u32 pixels, u32 features)
{
    rix_dl_baseline(dl, buf);
    rix_dl_reg(dl, RIX_REG_RESOLUTION, RIX_XY(xres, yres));
    rix_dl_reg(dl, RIX_REG_SCISSOR_END, RIX_XY(xres, yres));
    rix_dl_reg(dl, RIX_REG_FEATURES, features);
    rix_dl_reg(dl, RIX_REG_CLEAR_COLOR, rix_fence_color(value16));
    rix_dl_clear(dl, pixels);
}

static int lad_submit(rix_dl *dl)
{
    rix_dl_finish(dl);
    return rix_submit_words_fifo((const u32 *)rix_ptr(dl->base), dl->n);
}

/* run one stage: send, check pixels, then liveness.  Returns 0, or the
 * ladder error; *hung = 1 when the GPU stopped consuming (stop the ladder). */
static int lad_finish(u32 stage, int rc_send, u32 checks, u32 checks_expected, u32 map_buf, u32 map_lines, u32 value16, int *hung)
{
    int rc = 0, rc_alive;
    u32 map = 0xFFFFFFFFu;
    *hung = 0;
    if (rc_send) {
        lad_record(stage, rc_send, checks, map);
        *hung = 1; g_gpu_hung = 1u;
        return (stage == LAD_ALIVE0) ? (int)RIXE_GPU_HUNG : (int)(RIXE_LADDER + stage);
    }
    if (checks != checks_expected) {
        if (map_buf) map = write_map(map_buf, map_lines, value16);
        rc = (int)(RIXE_LADDER + stage);
    }
    rc_alive = lad_alive();
    if (rc_alive) {
        /* the stage itself was taken, but the parser never came back: the
           stage hung the GPU */
        lad_record(stage, rc_alive, checks | 0x80000000u, map);
        *hung = 1; g_gpu_hung = 1u;
        return (int)(RIXE_LADDER + stage);
    }
    lad_record(stage, rc, checks, map);
    return rc;
}

#define LAD_C_TEST2   0x07E0u
#define LAD_C_480     0x2A5Bu
#define LAD_C_481     0x4C7Du
#define LAD_C_SCREGS  0x6E9Fu
#define LAD_C_FENCE   0xA5A1u
#define LAD_C_NOPS    0x1357u
#define LAD_C_BODY    0x2468u
#define LAD_C_FENCE2  0xA5A2u
#define LAD_C_BODY3   0x3579u
#define LAD_C_FENCE3  0xA5A3u

static int ladder_run_fifo_stages(void)
{
    rix_dl dl;
    int rc, first = 0, hung = 0;
    u32 checks, t0;

    /* L0: is the GPU consuming at all? */
    rc = lad_alive();
    lad_record(LAD_ALIVE0, rc, 0u, 0xFFFFFFFFu);
    if (rc) { g_sd[SD_LADDER_FAILED] = LAD_ALIVE0 + 1u; g_gpu_hung = 1u; return (int)RIXE_GPU_HUNG; }

    /* L1: flush a half-received ST0 stream (9 x 508 FSC NOP words) */
    t0 = rix_now();
    rc = 0;
    if (!(g_cfg.flags & RIXF_NORESYNC)) rc = fsc_resync();
    g_sd[SD_RESYNC_US] = rix_ticks_to_us((u32)(rix_now() - t0));
    rc = lad_finish(LAD_RESYNC, rc, 0u, 0u, 0u, 0u, 0u, &hung);
    if (hung) { g_sd[SD_LADDER_FAILED] = LAD_RESYNC + 1u; return rc; }
    if (rc && !first) first = rc;

    /* L2: FAST1 Test2 recipe, known good on G4B: 64x32, 2048 px into SCRATCH */
    {
        volatile u16 *scr = (volatile u16 *)rix_ptr(RIX_SCRATCH_ARM);
        u32 i;
        for (i = 0; i < 2048u + 64u; i++) scr[i] = 0u;
        dsb();
        rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
        lad_memset_list(&dl, RIX_SCRATCH_ARM, 64u, 32u, LAD_C_TEST2, 2048u, 0u);
        rc = lad_submit(&dl);
        checks = 0u;
        if (rc == 0) {
            /* GL y=0 is memory line 31 (index 1984..2047), written first;
               GL y=31 is memory line 0 (index 0..63), written last */
            if (lad_wait_idx(RIX_SCRATCH_ARM, 63u, LAD_C_TEST2)) checks |= 1u;
            dmb();
            if (scr[0] == LAD_C_TEST2) checks |= 2u;
            if (scr[1024] == LAD_C_TEST2) checks |= 4u;
            if (scr[2047] == LAD_C_TEST2) checks |= 8u;
            if (scr[2048] == 0u && scr[2048 + 63] == 0u) checks |= 16u;   /* nothing past the 4 KiB page */
        }
        rc = lad_finish(LAD_TEST2, rc, checks, 31u, 0u, 0u, 0u, &hung);
        if (hung) { g_sd[SD_LADDER_FAILED] = LAD_TEST2 + 1u; return rc; }
        if (rc && !first) first = rc;
    }

    /* L3: A07b recipe from Core1: 640x480 full memset into FB0 (line 480 untouched) */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    lad_memset_list(&dl, RIX_FB0_ARM, RIX_W, RIX_H, LAD_C_480, RIX_W * RIX_H, 0u);
    rc = lad_submit(&dl);
    checks = 0u;
    if (rc == 0) {
        if (lad_wait_px(RIX_FB0_ARM, 0u, 639u, LAD_C_480)) checks |= 1u;      /* GL y=479: last pixel written */
        dmb();
        if (px_at(RIX_FB0_ARM, 0u, 639u) == LAD_C_480) checks |= 2u;
        if (px_at(RIX_FB0_ARM, 479u, 0u) == LAD_C_480) checks |= 4u;
        if (px_at(RIX_FB0_ARM, 479u, 639u) == LAD_C_480) checks |= 8u;
        if (px_at(RIX_FB0_ARM, 240u, 320u) == LAD_C_480) checks |= 16u;
        if (px_at(RIX_FB0_ARM, 480u, 0u) == 0u) checks |= 32u;              /* line 480 not touched */
        if (px_at(RIX_FB0_ARM, 480u, 639u) == 0u) checks |= 64u;
    }
    rc = lad_finish(LAD_CLEAR480, rc, checks, 127u, RIX_FB0_ARM, RIX_H_ALLOC, LAD_C_480, &hung);
    if (hung) { g_sd[SD_LADDER_FAILED] = LAD_CLEAR480 + 1u; return rc; }
    if (rc && !first) first = rc;

    /* L4: 640x481 full memset: line 480 reachable through the resolution register */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    lad_memset_list(&dl, RIX_FB0_ARM, RIX_W, RIX_H_ALLOC, LAD_C_481, RIX_W * RIX_H_ALLOC, 0u);
    rc = lad_submit(&dl);
    checks = 0u;
    if (rc == 0) {
        /* the clear writes GL y=0 (memory line 480) first and line 0 last */
        if (lad_wait_px(RIX_FB0_ARM, 0u, 639u, LAD_C_481)) checks |= 1u;
        if (lad_wait_px(RIX_FB0_ARM, 480u, 0u, LAD_C_481)) checks |= 4u;
        dmb();
        if (px_at(RIX_FB0_ARM, 479u, 639u) == LAD_C_481) checks |= 2u;
        if (px_at(RIX_FB0_ARM, 480u, 639u) == LAD_C_481) checks |= 8u;
        if (px_at(RIX_FB0_ARM, 240u, 320u) == LAD_C_481) checks |= 16u;
    }
    rc = lad_finish(LAD_CLEAR481, rc, checks, 31u, RIX_FB0_ARM, RIX_H_ALLOC, LAD_C_481, &hung);
    if (hung) { g_sd[SD_LADDER_FAILED] = LAD_CLEAR481 + 1u; return rc; }
    if (rc && !first) first = rc;

    /* L5: scissor registers programmed to the fence box, scissor disabled: full memset */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    lad_memset_list(&dl, RIX_FB0_ARM, RIX_W, RIX_H_ALLOC, LAD_C_SCREGS, RIX_W * RIX_H_ALLOC, 0u);
    rix_dl_reg(&dl, RIX_REG_SCISSOR_START, RIX_XY(0u, 0u));
    rix_dl_reg(&dl, RIX_REG_SCISSOR_END, RIX_XY(FENCE_PIXELS, 1u));
    rix_dl_reg(&dl, RIX_REG_CLEAR_COLOR, rix_fence_color(LAD_C_SCREGS));
    rix_dl_clear(&dl, RIX_W * RIX_H_ALLOC);     /* second memset with the registers set */
    rc = lad_submit(&dl);
    checks = 0u;
    if (rc == 0) {
        if (lad_wait_px(RIX_FB0_ARM, 0u, 639u, LAD_C_SCREGS)) checks |= 1u;
        if (lad_wait_px(RIX_FB0_ARM, 480u, 0u, LAD_C_SCREGS)) checks |= 2u;
        dmb();
        if (px_at(RIX_FB0_ARM, 240u, 320u) == LAD_C_SCREGS) checks |= 4u;
    }
    rc = lad_finish(LAD_SCISSOR_REGS, rc, checks, 7u, RIX_FB0_ARM, RIX_H_ALLOC, LAD_C_SCREGS, &hung);
    if (hung) { g_sd[SD_LADDER_FAILED] = LAD_SCISSOR_REGS + 1u; return rc; }
    if (rc && !first) first = rc;

    /* L6: the fence: scissor on, 64 px memset into GL line 0 = memory line 480 */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    rix_dl_reg(&dl, RIX_REG_FEATURES, RIX_FEAT_SCISSOR);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
    rix_dl_reg(&dl, RIX_REG_SCISSOR_START, RIX_XY(0u, 0u));
    rix_dl_reg(&dl, RIX_REG_SCISSOR_END, RIX_XY(FENCE_PIXELS, 1u));
    rix_dl_reg(&dl, RIX_REG_CLEAR_COLOR, rix_fence_color(LAD_C_FENCE));
    rix_dl_clear(&dl, FENCE_PIXELS);
    rc = lad_submit(&dl);
    checks = 0u;
    if (rc == 0) {
        if (lad_wait_px(RIX_FB0_ARM, 480u, 0u, LAD_C_FENCE)) checks |= 1u;
        dmb();
        if (px_at(RIX_FB0_ARM, 480u, 63u) == LAD_C_FENCE) checks |= 2u;
        if (px_at(RIX_FB0_ARM, 480u, 64u) == LAD_C_SCREGS) checks |= 4u;   /* box respected */
        if (px_at(RIX_FB0_ARM, 479u, 0u) == LAD_C_SCREGS) checks |= 8u;    /* line above untouched */
        if (px_at(RIX_FB0_ARM, 0u, 0u) == LAD_C_SCREGS) checks |= 16u;     /* no flip mistake */
        if (rix_fence_check(RIX_FB0_ARM, LAD_C_FENCE)) checks |= 32u;      /* the real fence test */
    }
    rc = lad_finish(LAD_FENCE, rc, checks, 63u, RIX_FB0_ARM, RIX_H_ALLOC, LAD_C_FENCE, &hung);
    if (hung) { g_sd[SD_LADDER_FAILED] = LAD_FENCE + 1u; return rc; }
    if (rc && !first) first = rc;

    /* L7: OP_NOP words inside the command stream, scissor off again, full memset */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    rix_dl_word(&dl, RIX_OP_NOP);
    rix_dl_word(&dl, RIX_OP_NOP);
    rix_dl_reg(&dl, RIX_REG_FEATURES, 0u);
    rix_dl_word(&dl, RIX_OP_NOP);
    rix_dl_reg(&dl, RIX_REG_CLEAR_COLOR, rix_fence_color(LAD_C_NOPS));
    rix_dl_clear(&dl, RIX_W * RIX_H_ALLOC);
    rc = lad_submit(&dl);
    checks = 0u;
    if (rc == 0) {
        if (lad_wait_px(RIX_FB0_ARM, 0u, 639u, LAD_C_NOPS)) checks |= 1u;
        if (lad_wait_px(RIX_FB0_ARM, 480u, 0u, LAD_C_NOPS)) checks |= 2u;
        dmb();
        if (px_at(RIX_FB0_ARM, 240u, 320u) == LAD_C_NOPS) checks |= 4u;
    }
    rc = lad_finish(LAD_NOPS, rc, checks, 7u, RIX_FB0_ARM, RIX_H_ALLOC, LAD_C_NOPS, &hung);
    if (hung) { g_sd[SD_LADDER_FAILED] = LAD_NOPS + 1u; return rc; }
    if (rc && !first) first = rc;

    /* L8: exactly what the application sends: frame head with clear + fence */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    rix_dl_baseline(&dl, RIX_FB0_ARM);
    g_sd[SD_BASELINE_WORDS] = dl.n;
    rix_dl_frame_head(&dl, RIX_FB0_ARM, 0u, rix_fence_color(LAD_C_BODY), 1);
    rix_dl_fence(&dl, LAD_C_FENCE2, 0u);
    g_sd[SD_FENCE_FIFO_WORDS] = dl.n;
    t0 = rix_now();
    rc = lad_submit(&dl);
    checks = 0u;
    if (rc == 0) {
        if (rix_fence_wait(RIX_FB0_ARM, LAD_C_FENCE2, 200000u, NULL, NULL) == 0) checks |= 1u;
        g_sd[SD_FENCE_FIFO_US] = rix_ticks_to_us((u32)(rix_now() - t0));
        dmb();
        if (px_at(RIX_FB0_ARM, 0u, 0u) == LAD_C_BODY) checks |= 2u;
        if (px_at(RIX_FB0_ARM, 479u, 639u) == LAD_C_BODY) checks |= 4u;
        if (px_at(RIX_FB0_ARM, 480u, 64u) == LAD_C_BODY) checks |= 8u;     /* head clear covers 481 lines */
    }
    g_sd[SD_FENCE_VALUE_FIFO] = px_at(RIX_FB0_ARM, 480u, 0u);
    if (!(checks & 1u)) g_sd[SD_FENCE_FIFO_US] = 0xFFFFFFFFu;
    rc = lad_finish(LAD_FRAME_FENCE, rc, checks, 15u, RIX_FB0_ARM, RIX_H_ALLOC, LAD_C_FENCE2, &hung);
    if (hung) { g_sd[SD_LADDER_FAILED] = LAD_FRAME_FENCE + 1u; return rc; }
    if (rc && !first) first = rc;
    if ((checks & 15u) == 15u) g_caps |= RIXCAP_FENCE_FIFO;

    if (first && g_sd[SD_LADDER_FAILED] == 0u) g_sd[SD_LADDER_FAILED] = (u32)(first - (int)RIXE_LADDER) + 1u;
    g_sd[SD_INIT_FIRST_ERROR] = (u32)first;
    /* L2..L7 are diagnostics: a wrong pixel there is reported, not fatal.
       The application needs L8 (its own list + fence) and a live GPU. */
    return (g_caps & RIXCAP_FENCE_FIFO) ? 0 : (int)(RIXE_LADDER + LAD_FRAME_FENCE);
}

/* L11..L15: TMU registers, texture page table, one textured triangle (FIFO),
 * the same frame from DDR, and (diagnostic only) the page table from DDR. */
static int ladder_run_texture_stages(void)
{
    rix_dl dl;
    int rc, hung = 0, first = 0, tri_ok = 0;
    u32 checks, feat = RIX_FEAT_TMU0, px_before;
    const rix_config *c = &g_cfg;
    if (!c->probe_tri || !c->tex_pages) return 0;

    /* L11: TMU registers (REPLACE env, config) + fence, scissor off, TMU0 still off */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    rix_dl_baseline(&dl, RIX_FB0_ARM);
    rix_dl_reg(&dl, RIX_REG_TMU0_ENV, c->tmu_env);
    rix_dl_reg(&dl, RIX_REG_TMU0_ENVCOLOR, c->tmu_env_color);
    rix_dl_reg(&dl, RIX_REG_TMU0_CONFIG, c->tmu_cfg);
    rix_dl_fence(&dl, 0xA5B1u, 0u);
    rc = rix_submit_fifo(&dl);
    checks = 0u;
    if (rc == 0 && rix_fence_wait(RIX_FB0_ARM, 0xA5B1u, 200000u, NULL, NULL) == 0) checks |= 1u;
    rc = lad_finish(LAD_TMU_REGS, rc, checks, 1u, RIX_FB0_ARM, RIX_H_ALLOC, 0xA5B1u, &hung);
    if (hung) { g_sd[SD_LADDER_FAILED] = LAD_TMU_REGS + 1u; return rc; }
    if (rc && !first) first = rc;

    /* L12: the page table through the FIFO + fence */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    rix_dl_reg(&dl, RIX_REG_FEATURES, feat);
    rix_dl_texture(&dl, 0u, c->tex_pages, c->tex_base);
    rix_dl_fence(&dl, 0xA5B2u, feat);
    rc = rix_submit_fifo(&dl);
    checks = 0u;
    if (rc == 0 && rix_fence_wait(RIX_FB0_ARM, 0xA5B2u, 200000u, NULL, NULL) == 0) checks |= 1u;
    rc = lad_finish(LAD_TEX_FIFO, rc, checks, 1u, RIX_FB0_ARM, RIX_H_ALLOC, 0xA5B2u, &hung);
    if (hung) { g_sd[SD_LADDER_FAILED] = LAD_TEX_FIFO + 1u; return rc; }
    if (rc && !first) first = rc;
    g_caps |= RIXCAP_TEX;

    /* L13: clear + one textured triangle + fence, FIFO: the first real draw */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    rix_dl_frame_head(&dl, RIX_FB0_ARM, feat, rix_fence_color(0x0000u), 1);
    rix_dl_tri(&dl, c->probe_tri);
    rix_dl_fence(&dl, 0xA5B3u, feat);
    rc = rix_submit_fifo(&dl);
    checks = 0u;
    if (rc == 0) {
        if (rix_fence_wait(RIX_FB0_ARM, 0xA5B3u, 200000u, NULL, NULL) == 0) checks |= 1u;
        dmb();
        px_before = px_at(RIX_FB0_ARM, c->probe_y, c->probe_x);
        if (px_before != 0x0000u) checks |= 2u;                 /* a texel landed inside the triangle */
        if (px_at(RIX_FB0_ARM, 0u, 0u) == 0x0000u) checks |= 4u; /* clear color outside */
    }
    rc = lad_finish(LAD_TRI_FIFO, rc, checks, 7u, RIX_FB0_ARM, RIX_H_ALLOC, 0xA5B3u, &hung);
    if (hung) { g_sd[SD_LADDER_FAILED] = LAD_TRI_FIFO + 1u; return rc; }
    if (rc && !first) first = rc;
    tri_ok = ((checks & 7u) == 7u);

    /* L14: the same frame fetched from DDR: TMU texel reads and FSC list reads
       now share the AXI read channel.  Decides the transport for frames. */
    if (!(g_cfg.flags & RIXF_DLFIFO) && (g_caps & RIXCAP_FENCE_MEM)) {
        g_dl_mode = 1u;
        rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
        rix_dl_frame_head(&dl, RIX_FB0_ARM, feat, rix_fence_color(0x0841u), 1);
        rix_dl_tri(&dl, c->probe_tri);
        rix_dl_fence(&dl, 0xA5B4u, feat);
        rc = rix_submit(&dl);
        checks = 0u;
        if (rc == 0) {
            if (rix_fence_wait(RIX_FB0_ARM, 0xA5B4u, 200000u, NULL, NULL) == 0) checks |= 1u;
            dmb();
            if (px_at(RIX_FB0_ARM, c->probe_y, c->probe_x) != 0x0841u) checks |= 2u;
            if (px_at(RIX_FB0_ARM, 0u, 0u) == 0x0841u) checks |= 4u;
        }
        rc = lad_finish(LAD_TRI_DDR, rc, checks, 7u, RIX_FB0_ARM, RIX_H_ALLOC, 0xA5B4u, &hung);
        if (hung) { g_sd[SD_LADDER_FAILED] = LAD_TRI_DDR + 1u; g_dl_mode = 2u; return rc; }
        if ((checks & 7u) == 7u) g_caps |= RIXCAP_TRI_DDR;
        else { g_dl_mode = 2u; if (!first) first = rc; }
    } else {
        lad_record(LAD_TRI_DDR, 0, 0u, 0xFFFFFFFFu);
    }

    /* L15: the page table inside a SHORT DDR list (144 words with the burst padding: FEATURES,
       OP_TEXTURE + 64 pages, fence with its 64 drain NOPs; 77 words after the table).  The parser pauses the FSC while the
       TMU takes the table; if the TMU then needs the AXI read channel the FSC is holding, nothing
       moves again.  G4B: hangs (B1 run 5), pass BINDFIFO there.  G4C: completes - because the FSC
       has fetched the whole list before the parser reaches the OP_TEXTURE, NOT because an
       OP_TEXTURE may sit anywhere in a list: the 1.2 "binds inside the frame chunks" built on this
       stage deadlocked on the first ROOM A frame (D2e, 14/10, power cycle).  RIXCAP_TEX_DDR now
       only licenses BINDLIST (a bind as a list shorter than this one, rixframe.h). */
    if (((g_cfg.flags & RIXF_TEXDDR) || !(g_cfg.flags & RIXF_BINDFIFO)) && (g_caps & RIXCAP_FENCE_MEM) && tri_ok) {
        u32 saved = g_dl_mode;
        g_dl_mode = 1u;
        rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
        rix_dl_reg(&dl, RIX_REG_FEATURES, feat);
        rix_dl_texture(&dl, 0u, c->tex_pages, c->tex_base);
        rix_dl_fence(&dl, 0xA5B5u, feat);
        rc = rix_submit(&dl);
        checks = 0u;
        if (rc == 0 && rix_fence_wait(RIX_FB0_ARM, 0xA5B5u, 200000u, NULL, NULL) == 0) checks |= 1u;
        rc = lad_finish(LAD_TEX_DDR, rc, checks, 1u, RIX_FB0_ARM, RIX_H_ALLOC, 0xA5B5u, &hung);
        g_dl_mode = saved;
        if (hung) { g_sd[SD_LADDER_FAILED] = LAD_TEX_DDR + 1u; return rc; }
        if (rc && !first) first = rc;
        if (rc == 0 && (checks & 1u)) g_caps |= RIXCAP_TEX_DDR;
    }
    if (first && g_sd[SD_LADDER_FAILED] == 0u) g_sd[SD_LADDER_FAILED] = (u32)(first - (int)RIXE_LADDER) + 1u;
    if (first && g_sd[SD_INIT_FIRST_ERROR] == 0u) g_sd[SD_INIT_FIRST_ERROR] = (u32)first;
    /* a textured draw must work through at least the FIFO */
    if (!tri_ok) return first ? first : (int)(RIXE_LADDER + LAD_TRI_FIFO);
    return 0;
}

/* ------------------------------------------------------------------ */
/* capability probes (SELFTEST PROBE, C1b)                             */
/* ------------------------------------------------------------------ */
/* flat-colored triangle descriptor (edge math as cube_make_desc, which is
   the bit-exact A07b reference): screen coordinates in the RasterIX y-up
   convention, constant depth z, colour r/g/b/a in 0..1 floats */
static u32 fl_bits(float f) { union { float f; u32 u; } v; v.f = f; return v.u; }
static s32 pr_edge(const s32 a[2], const s32 b[2], const s32 c[2])
{
    return (s32)(((int64_t)(c[0] - a[0]) * (b[1] - a[1])) - ((int64_t)(c[1] - a[1]) * (b[0] - a[0])));
}
static int probe_make_desc(float x0, float y0, float x1, float y1, float x2, float y2,
                           float r, float g, float b, float a, float z, u32 *out)
{
    s32 v0[2], v1[2], v2[2], p[2], area, sign, bsx, bsy, bex, bey, wi[3], wx[3], wy[3];
    u32 k = 0u, i;
    v0[0] = (s32)(x0 * 32.0f + 0.5f); v0[1] = (s32)(y0 * 32.0f + 0.5f);
    v1[0] = (s32)(x1 * 32.0f + 0.5f); v1[1] = (s32)(y1 * 32.0f + 0.5f);
    v2[0] = (s32)(x2 * 32.0f + 0.5f); v2[1] = (s32)(y2 * 32.0f + 0.5f);
    area = pr_edge(v0, v1, v2); sign = (area <= 0) ? -1 : 1; area *= sign; if (area <= 0) return 0;
    bsx = v0[0]; if (v1[0] < bsx) bsx = v1[0]; if (v2[0] < bsx) bsx = v2[0];
    bsy = v0[1]; if (v1[1] < bsy) bsy = v1[1]; if (v2[1] < bsy) bsy = v2[1];
    bex = v0[0]; if (v1[0] > bex) bex = v1[0]; if (v2[0] > bex) bex = v2[0];
    bey = v0[1]; if (v1[1] > bey) bey = v1[1]; if (v2[1] > bey) bey = v2[1];
    bsx = (bsx + 16) >> 5; bsy = (bsy + 16) >> 5; bex = (bex + 32 + 16) >> 5; bey = (bey + 32 + 16) >> 5;
    if (bsx < 0 || bsy < 0 || bex <= bsx || bey <= bsy || bex > 641 || bey > 482) return 0;
    p[0] = bsx << 5; p[1] = bsy << 5;
    wi[0] = pr_edge(v1, v2, p) * sign; wi[1] = pr_edge(v2, v0, p) * sign; wi[2] = pr_edge(v0, v1, p) * sign;
    wx[0] = (v2[1] - v1[1]) * 32 * sign; wx[1] = (v0[1] - v2[1]) * 32 * sign; wx[2] = (v1[1] - v0[1]) * 32 * sign;
    wy[0] = (v1[0] - v2[0]) * 32 * sign; wy[1] = (v2[0] - v0[0]) * 32 * sign; wy[2] = (v0[0] - v1[0]) * 32 * sign;
    out[k++] = 0u;
    out[k++] = ((u32)(u16)bsy << 16) | (u32)(u16)bsx;
    out[k++] = ((u32)(u16)bey << 16) | (u32)(u16)bex;
    for (i = 0; i < 3u; i++) out[k++] = (u32)wi[i];
    for (i = 0; i < 3u; i++) out[k++] = (u32)wx[i];
    for (i = 0; i < 3u; i++) out[k++] = (u32)wy[i];
    out[k++] = fl_bits(r); out[k++] = fl_bits(g); out[k++] = fl_bits(b); out[k++] = fl_bits(a);   /* constant colour */
    for (i = 0; i < 8u; i++) out[k++] = 0u;                                                        /* colour increments */
    out[k++] = fl_bits(z); out[k++] = fl_bits(1.0f);                                               /* depth z, w */
    for (i = 0; i < 4u; i++) out[k++] = 0u;                                                        /* depth increments */
    out[k++] = fl_bits(0.5f); out[k++] = fl_bits(0.5f); out[k++] = fl_bits(1.0f);                  /* tex s,t,q (TMU off) */
    for (i = 0; i < 6u; i++) out[k++] = 0u;
    return k == RIX_TRI_DESC_WORDS;
}

static u32 g_probe_desc_a[RIX_TRI_DESC_WORDS], g_probe_desc_b[RIX_TRI_DESC_WORDS];

/* one probe packet: send through the FIFO, wait its fence, then liveness.
   Returns 0 ok, RIXE_PROBE + idx when the GPU stopped (and marks it). */
static int probe_send(u32 idx, rix_dl *dl, u32 fence, u32 color_arm, u32 *mmio_us)
{
    int rc;
    g_sd[SD_PROBE_RUN] |= (1u << idx);
    g_mmio_us = 0u;
    rc = rix_submit_fifo(dl);
    if (mmio_us) *mmio_us = g_mmio_us;          /* Core1's own cost: the MMIO writes, not the GPU's consumption */
    if (rc) { g_gpu_hung = 1u; g_sd[SD_PROBE_FAILED] = idx + 1u; return (int)(RIXE_PROBE + idx); }
    if (rix_fence_wait(color_arm, fence, 500000u, NULL, NULL)) { g_sd[SD_PROBE_FAILED] = idx + 1u; return (int)(RIXE_PROBE + idx); }
    if (lad_alive()) { g_gpu_hung = 1u; g_sd[SD_PROBE_FAILED] = idx + 1u; return (int)(RIXE_PROBE + idx); }
    return 0;
}

#define PR_X 320u
#define PR_Y 240u          /* probe pixel (RasterIX y-up): memory line 480 - 240 = 240 */
#define PR_LINE (RIX_H_ALLOC - 1u - PR_Y)

/* common head: known registers, colour buffer, clear colour 0, and a 64x64
   scissored clear around the probe pixel (a full 640x481 clear costs 3.1 ms
   and would dominate the texture-switch baseline: C1b run) */
static void probe_head(rix_dl *dl, u32 color_arm, u32 features)
{
    rix_dl_baseline(dl, color_arm);
    rix_dl_reg(dl, RIX_REG_CLEAR_COLOR, 0u);
    rix_dl_reg(dl, RIX_REG_SCISSOR_START, RIX_XY(PR_X - 32u, PR_Y - 32u));
    rix_dl_reg(dl, RIX_REG_SCISSOR_END, RIX_XY(PR_X + 32u, PR_Y + 32u));
    rix_dl_reg(dl, RIX_REG_FEATURES, features | RIX_FEAT_SCISSOR);
    rix_dl_clear(dl, 64u * 64u);
    rix_dl_reg(dl, RIX_REG_FEATURES, features);
}

/* RGB565 of "half white": 127..128 per channel (rounding of the FPGA's fixed point unknown) */
static int probe_is_half(u32 px)
{
    u32 r = (px >> 11) & 31u, g = (px >> 5) & 63u, b = px & 31u;
    return (r >= 15u && r <= 16u) && (g >= 31u && g <= 32u) && (b >= 15u && b <= 16u);
}

int rix_run_probes(u32 color_arm, u32 zbuf_arm, u32 tex_arm)
{
    rix_dl dl;
    int rc;
    u32 px1, px2, k, push, t0, us_state = 0u, us64 = 0u, us256 = 0u;
    const u32 N = 8u;
    if (g_gpu_hung) return (int)RIXE_GPU_HUNG;
    g_sd[SD_PROBE_RUN] = 0u; g_sd[SD_PROBE_FAILED] = 0u;
    /* triangles: A covers the probe pixel and more, B too; both flat */
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.25f, g_probe_desc_a);   /* red, near */
    probe_make_desc(220.0f, 170.0f, 420.0f, 170.0f, 320.0f, 380.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.75f, g_probe_desc_b);   /* green, far */

    /* ---- probe 0: depth test.  Z-buffer at zbuf_arm, cleared to 0xFFFF (far),
       LESS, depth writes on.  near-then-far must stay red; far-then-near red too. */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, 0u);
    rix_dl_reg(&dl, RIX_REG_DEPTH_ADDR, zbuf_arm);
    rix_dl_reg(&dl, RIX_REG_CLEAR_DEPTH, 0xFFFFu);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B | RIX_FRAG_DEPTH_FUNC(RIX_TF_LESS) | RIX_FRAG_DEPTH_MASK);
    rix_dl_clear_depth(&dl, RIX_W * RIX_H_ALLOC);          /* mask set BEFORE the depth memset (C1b run: otherwise nothing is written) */
    rix_dl_reg(&dl, RIX_REG_FEATURES, RIX_FEAT_DEPTH_TEST);
    rix_dl_tri(&dl, g_probe_desc_a);
    rix_dl_tri(&dl, g_probe_desc_b);
    rix_dl_reg(&dl, RIX_REG_FEATURES, 0u);
    rix_dl_fence(&dl, 0xC001u, 0u);
    rc = probe_send(0u, &dl, 0xC001u, color_arm, NULL);
    if (rc) return rc;
    px1 = px_at(color_arm, PR_LINE, PR_X);
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, 0u);
    rix_dl_reg(&dl, RIX_REG_DEPTH_ADDR, zbuf_arm);
    rix_dl_reg(&dl, RIX_REG_CLEAR_DEPTH, 0xFFFFu);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B | RIX_FRAG_DEPTH_FUNC(RIX_TF_LESS) | RIX_FRAG_DEPTH_MASK);
    rix_dl_clear_depth(&dl, RIX_W * RIX_H_ALLOC);
    rix_dl_reg(&dl, RIX_REG_FEATURES, RIX_FEAT_DEPTH_TEST);
    rix_dl_tri(&dl, g_probe_desc_b);
    rix_dl_tri(&dl, g_probe_desc_a);
    rix_dl_reg(&dl, RIX_REG_FEATURES, 0u);
    rix_dl_fence(&dl, 0xC002u, 0u);
    rc = probe_send(0u, &dl, 0xC002u, color_arm, NULL);
    if (rc) return rc;
    px2 = px_at(color_arm, PR_LINE, PR_X);
    g_sd[SD_PROBE_DEPTH_PIX] = (px1 << 16) | px2;
    g_sd[SD_PROBE_DEPTH] = (px1 == 0xF800u && px2 == 0xF800u) ? 1u : (px1 == 0x07E0u && px2 == 0xF800u) ? 2u : 3u;
    /* third packet: no depth clear; the buffer holds z = 0.25 at the probe
       pixel from the packet above, so LESS must reject both triangles: black.
       (C1b run: this is exactly what the unmasked memset produced, which is
       how the mask rule was found.) */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, 0u);
    rix_dl_reg(&dl, RIX_REG_DEPTH_ADDR, zbuf_arm);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B | RIX_FRAG_DEPTH_FUNC(RIX_TF_LESS) | RIX_FRAG_DEPTH_MASK);
    rix_dl_reg(&dl, RIX_REG_FEATURES, RIX_FEAT_DEPTH_TEST);
    rix_dl_tri(&dl, g_probe_desc_b);
    rix_dl_tri(&dl, g_probe_desc_a);
    rix_dl_reg(&dl, RIX_REG_FEATURES, 0u);
    rix_dl_fence(&dl, 0xC00Au, 0u);
    rc = probe_send(0u, &dl, 0xC00Au, color_arm, NULL);
    if (rc) return rc;
    g_sd[SD_PROBE_DEPTH_KEEP] = px_at(color_arm, PR_LINE, PR_X);   /* 0000 expected when the Z-buffer keeps its values */

    /* ---- probe 1: blending ONE,ONE: red then green blended -> yellow */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, 0u);
    rix_dl_tri(&dl, g_probe_desc_a);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B | RIX_FRAG_BLEND(RIX_BF_ONE, RIX_BF_ONE));
    rix_dl_reg(&dl, RIX_REG_FEATURES, RIX_FEAT_BLENDING);
    rix_dl_tri(&dl, g_probe_desc_b);
    rix_dl_reg(&dl, RIX_REG_FEATURES, 0u);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
    rix_dl_fence(&dl, 0xC003u, 0u);
    rc = probe_send(1u, &dl, 0xC003u, color_arm, NULL);
    if (rc) return rc;
    px1 = px_at(color_arm, PR_LINE, PR_X);
    g_sd[SD_PROBE_BLEND_PIX] = px1;
    g_sd[SD_PROBE_BLEND] = (px1 == 0xFFE0u) ? 1u : (px1 == 0x07E0u) ? 2u : 3u;

    /* ---- probe 2: alpha test GREATER 0.5: alpha 0.25 rejected, 0.75 drawn */
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 0.0f, 0.0f, 1.0f, 0.25f, 0.5f, g_probe_desc_a);   /* blue, a=0.25 */
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 1.0f, 1.0f, 1.0f, 0.75f, 0.5f, g_probe_desc_b);   /* white, a=0.75 */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, 0u);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B | RIX_FRAG_ALPHA_FUNC(RIX_TF_GREATER) | RIX_FRAG_ALPHA_REF(128u));
    rix_dl_reg(&dl, RIX_REG_FEATURES, RIX_FEAT_ALPHA_TEST);
    rix_dl_tri(&dl, g_probe_desc_a);
    rix_dl_reg(&dl, RIX_REG_FEATURES, 0u);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
    rix_dl_fence(&dl, 0xC004u, 0u);
    rc = probe_send(2u, &dl, 0xC004u, color_arm, NULL);
    if (rc) return rc;
    px1 = px_at(color_arm, PR_LINE, PR_X);
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, 0u);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B | RIX_FRAG_ALPHA_FUNC(RIX_TF_GREATER) | RIX_FRAG_ALPHA_REF(128u));
    rix_dl_reg(&dl, RIX_REG_FEATURES, RIX_FEAT_ALPHA_TEST);
    rix_dl_tri(&dl, g_probe_desc_b);
    rix_dl_reg(&dl, RIX_REG_FEATURES, 0u);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
    rix_dl_fence(&dl, 0xC005u, 0u);
    rc = probe_send(2u, &dl, 0xC005u, color_arm, NULL);
    if (rc) return rc;
    px2 = px_at(color_arm, PR_LINE, PR_X);
    g_sd[SD_PROBE_ALPHA_PIX] = (px1 << 16) | px2;
    g_sd[SD_PROBE_ALPHA] = (px1 == 0x0000u && px2 == 0xFFFFu) ? 1u : (px1 == 0x001Fu && px2 == 0xFFFFu) ? 2u : 3u;

    /* ---- probes 3/4/5: texture switch cost.  N times: TMU config + OP_TEXTURE
       (page table through the FIFO) + one small textured triangle; the
       state-only run (same registers and triangles, no OP_TEXTURE) is the
       baseline subtracted from the two texture runs. */
    probe_make_desc(8.0f, 8.0f, 24.0f, 8.0f, 16.0f, 24.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, g_probe_desc_a);
    g_sd[SD_PROBE_TEX_N] = N;
    /* 5: state only */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, RIX_FEAT_TMU0);
    for (k = 0; k < N; k++) {
        rix_dl_reg(&dl, RIX_REG_TMU0_CONFIG, RIX_TMU_CFG_RGB565(6u, 6u, 1u));
        rix_dl_tri(&dl, g_probe_desc_a);
    }
    rix_dl_fence(&dl, 0xC006u, RIX_FEAT_TMU0);
    t0 = rix_now();
    rc = probe_send(5u, &dl, 0xC006u, color_arm, &push);
    if (rc) return rc;
    us_state = rix_ticks_to_us((u32)(rix_now() - t0));
    g_sd[SD_PROBE_STATE_US] = us_state / N;
    /* 3: 64x64 = 4 pages */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, RIX_FEAT_TMU0);
    for (k = 0; k < N; k++) {
        rix_dl_reg(&dl, RIX_REG_TMU0_CONFIG, RIX_TMU_CFG_RGB565(6u, 6u, 1u));
        rix_dl_texture(&dl, 0u, 4u, tex_arm + ((k & 1u) ? 8192u : 0u));
        rix_dl_tri(&dl, g_probe_desc_a);
    }
    rix_dl_fence(&dl, 0xC007u, RIX_FEAT_TMU0);
    t0 = rix_now();
    rc = probe_send(3u, &dl, 0xC007u, color_arm, &push);
    if (rc) return rc;
    us64 = rix_ticks_to_us((u32)(rix_now() - t0));
    g_sd[SD_PROBE_TEX64_US] = (us64 > us_state) ? (us64 - us_state) / N : 0u;
    g_sd[SD_PROBE_TEX64_PUSH] = push / N;
    /* 4: 256x256 = 64 pages (the whole chrome texture, twice the same base: the
       TMU has no cache, a reload is a reload) */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, RIX_FEAT_TMU0);
    for (k = 0; k < N; k++) {
        rix_dl_reg(&dl, RIX_REG_TMU0_CONFIG, RIX_TMU_CFG_RGB565(8u, 8u, 1u));
        rix_dl_texture(&dl, 0u, 64u, tex_arm);
        rix_dl_tri(&dl, g_probe_desc_a);
    }
    rix_dl_fence(&dl, 0xC008u, RIX_FEAT_TMU0);
    t0 = rix_now();
    rc = probe_send(4u, &dl, 0xC008u, color_arm, &push);
    if (rc) return rc;
    us256 = rix_ticks_to_us((u32)(rix_now() - t0));
    g_sd[SD_PROBE_TEX_RAW] = us256;
    g_sd[SD_PROBE_TEX256_US] = (us256 > us_state) ? (us256 - us_state) / N : 0u;
    g_sd[SD_PROBE_TEX256_PUSH] = push / N;
    /* ---- probe 7 (D2): MODULATE env.  A 32x32 white texture (one page written
       into SCRATCH, non-cacheable window) sampled by a 0.5 grey vertex colour:
       half intensity expected (RGB565 0x7BEF or 0x8410), 0xFFFF = REPLACE behaviour. */
    {
        volatile u16 *wt = (volatile u16 *)rix_ptr(RIX_SCRATCH_ARM);
        for (k = 0; k < 1024u; k++) wt[k] = 0xFFFFu;
        dsb();
    }
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.5f, g_probe_desc_a);
    g_probe_desc_a[30] = fl_bits(0.0f); g_probe_desc_a[31] = fl_bits(0.0f); g_probe_desc_a[32] = fl_bits(1.0f);
    g_probe_desc_a[33] = fl_bits(1.0f / 240.0f); g_probe_desc_a[34] = fl_bits(0.0f); g_probe_desc_a[35] = fl_bits(0.0f);
    g_probe_desc_a[36] = fl_bits(0.0f); g_probe_desc_a[37] = fl_bits(1.0f / 250.0f); g_probe_desc_a[38] = fl_bits(0.0f);
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, 0u);
    rix_dl_reg(&dl, RIX_REG_TMU0_ENV, RIX_TMU_ENV_MODULATE);
    rix_dl_reg(&dl, RIX_REG_TMU0_ENVCOLOR, 0u);
    rix_dl_reg(&dl, RIX_REG_TMU0_CONFIG, RIX_TMU_CFG_RGB565(5u, 5u, 1u));
    rix_dl_texture(&dl, 0u, 1u, RIX_SCRATCH_ARM);
    rix_dl_reg(&dl, RIX_REG_FEATURES, RIX_FEAT_TMU0);
    rix_dl_tri(&dl, g_probe_desc_a);
    rix_dl_reg(&dl, RIX_REG_FEATURES, 0u);
    rix_dl_reg(&dl, RIX_REG_TMU0_ENV, g_cfg.tmu_env);
    rix_dl_fence(&dl, 0xC012u, 0u);
    rc = probe_send(7u, &dl, 0xC012u, color_arm, NULL);
    if (rc) return rc;
    px1 = px_at(color_arm, PR_LINE, PR_X);
    g_sd[SD_PROBE_MOD_PIX] = px1;
    g_sd[SD_PROBE_MOD] = probe_is_half(px1) ? 1u : (px1 == 0xFFFFu) ? 2u : 3u;

    /* ---- probe 8 (D2): the GL lightmap pass, blend ZERO,SRC_COLOR (dst * src):
       a white flat triangle, then a 0.5 grey one blended: half intensity expected. */
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, g_probe_desc_a);
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.5f, g_probe_desc_b);
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, 0u);
    rix_dl_tri(&dl, g_probe_desc_a);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B | RIX_FRAG_BLEND(RIX_BF_ZERO, RIX_BF_SRC_COLOR));
    rix_dl_reg(&dl, RIX_REG_FEATURES, RIX_FEAT_BLENDING);
    rix_dl_tri(&dl, g_probe_desc_b);
    rix_dl_reg(&dl, RIX_REG_FEATURES, 0u);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
    rix_dl_fence(&dl, 0xC013u, 0u);
    rc = probe_send(8u, &dl, 0xC013u, color_arm, NULL);
    if (rc) return rc;
    px1 = px_at(color_arm, PR_LINE, PR_X);
    g_sd[SD_PROBE_LMBLEND_PIX] = px1;
    g_sd[SD_PROBE_LMBLEND] = probe_is_half(px1) ? 1u : (px1 == 0xFFFFu) ? 2u : 3u;

    /* leave the TMU as the application expects it (its CONTENT is a probe texture: rixframe forgets its bound texture) */
    rix_frame_texture_invalidate(0u);
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    rix_dl_reg(&dl, RIX_REG_TMU0_ENV, g_cfg.tmu_env);
    rix_dl_reg(&dl, RIX_REG_TMU0_CONFIG, g_cfg.tmu_cfg);
    rix_dl_reg(&dl, RIX_REG_DEPTH_ADDR, 0u);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
    rix_dl_fence(&dl, 0xC009u, 0u);
    return probe_send(5u, &dl, 0xC009u, color_arm, NULL);
}

/* ---- placement probe (D1): the GPU reads a texture and reads/writes a
   Z-buffer that live OUTSIDE the 2 MiB G4B window, in Core1's arena (DDR the
   FAST1 sandbox never allowed but HP3 addresses like any other).  The
   texture is written by Core1 (L1 cleaned; the mapping must be outer-non-cacheable
   or NC, see zzgear_mmu.c), the Z-buffer is only ever touched by the GPU.  Colour
   stays in the window. */
int rix_probe_placement(u32 color_arm, u32 tex_src_arm, u32 tex_dst_arm, u32 zbuf_arm)
{
    rix_dl dl;
    int rc;
    u32 px_tex, px_z, k;
    volatile u32 *dst = (volatile u32 *)rix_ptr(tex_dst_arm);
    u32 expect, fill;
    (void)tex_src_arm;
    if (g_gpu_hung) return (int)RIXE_GPU_HUNG;
    g_sd[SD_PROBE_PLACE_TEX] = tex_dst_arm; g_sd[SD_PROBE_PLACE_Z] = zbuf_arm;
    /* a constant texel chosen per run (timer bits): a stale texture left in DDR by an earlier run
       cannot pass (the D1 version copied the same chrome texture every run and only tested != 0,
       which the previous run's copy, or ZZQuake2's pak data at 0x30000000, also satisfies) */
    expect = ((rix_now() >> 6) & 0x7BEFu) | 0x0841u;   /* every channel >= 1 */
    fill = expect | (expect << 16);
    for (k = 0; k < 64u * 2048u / 4u; k++) dst[k] = fill;
    dsb();
    rix_mem_clean(tex_dst_arm, 64u * 2048u);
    g_sd[SD_PROBE_PLACE_EXPECT] = expect;
    /* packet 1: textured triangle from the arena texture, REPLACE env */
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.25f, g_probe_desc_a);
    /* give it texture coordinates: s,t,q at bbox origin + increments (u=0..1 across the bbox) */
    g_probe_desc_a[30] = fl_bits(0.0f); g_probe_desc_a[31] = fl_bits(0.0f); g_probe_desc_a[32] = fl_bits(1.0f);
    g_probe_desc_a[33] = fl_bits(1.0f / 240.0f); g_probe_desc_a[34] = fl_bits(0.0f); g_probe_desc_a[35] = fl_bits(0.0f);
    g_probe_desc_a[36] = fl_bits(0.0f); g_probe_desc_a[37] = fl_bits(1.0f / 250.0f); g_probe_desc_a[38] = fl_bits(0.0f);
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, 0u);
    rix_dl_reg(&dl, RIX_REG_TMU0_ENV, RIX_TMU_ENV_REPLACE);
    rix_dl_reg(&dl, RIX_REG_TMU0_ENVCOLOR, 0u);
    rix_dl_reg(&dl, RIX_REG_TMU0_CONFIG, RIX_TMU_CFG_RGB565(8u, 8u, 1u));
    rix_dl_texture(&dl, 0u, 64u, tex_dst_arm);
    rix_dl_reg(&dl, RIX_REG_FEATURES, RIX_FEAT_TMU0);
    rix_dl_tri(&dl, g_probe_desc_a);
    rix_dl_reg(&dl, RIX_REG_FEATURES, 0u);
    rix_dl_fence(&dl, 0xC010u, 0u);
    rc = probe_send(6u, &dl, 0xC010u, color_arm, NULL);
    if (rc) return rc;
    px_tex = px_at(color_arm, PR_LINE, PR_X);
    /* packet 2: depth test with the arena Z-buffer, far-then-near after a masked clear: red expected */
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.25f, g_probe_desc_a);
    probe_make_desc(220.0f, 170.0f, 420.0f, 170.0f, 320.0f, 380.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.75f, g_probe_desc_b);
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, 0u);
    rix_dl_reg(&dl, RIX_REG_DEPTH_ADDR, zbuf_arm);
    rix_dl_reg(&dl, RIX_REG_CLEAR_DEPTH, 0xFFFFu);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B | RIX_FRAG_DEPTH_FUNC(RIX_TF_LESS) | RIX_FRAG_DEPTH_MASK);
    rix_dl_clear_depth(&dl, RIX_W * RIX_H_ALLOC);
    rix_dl_reg(&dl, RIX_REG_FEATURES, RIX_FEAT_DEPTH_TEST);
    rix_dl_tri(&dl, g_probe_desc_b);
    rix_dl_tri(&dl, g_probe_desc_a);
    rix_dl_reg(&dl, RIX_REG_FEATURES, 0u);
    rix_dl_reg(&dl, RIX_REG_DEPTH_ADDR, 0u);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
    rix_dl_reg(&dl, RIX_REG_TMU0_CONFIG, g_cfg.tmu_cfg);
    rix_dl_fence(&dl, 0xC011u, 0u);
    rc = probe_send(6u, &dl, 0xC011u, color_arm, NULL);
    if (rc) return rc;
    px_z = px_at(color_arm, PR_LINE, PR_X);
    rix_frame_texture_invalidate(0u);
    g_sd[SD_PROBE_PLACE_PIX] = (px_tex << 16) | px_z;
    {
        /* exact, or within one unit per channel (fixed-point rounding of the bilinear filter) */
        u32 dr = ((px_tex >> 11) & 31u) > ((expect >> 11) & 31u) ? ((px_tex >> 11) & 31u) - ((expect >> 11) & 31u) : ((expect >> 11) & 31u) - ((px_tex >> 11) & 31u);
        u32 dg = ((px_tex >> 5) & 63u) > ((expect >> 5) & 63u) ? ((px_tex >> 5) & 63u) - ((expect >> 5) & 63u) : ((expect >> 5) & 63u) - ((px_tex >> 5) & 63u);
        u32 db = (px_tex & 31u) > (expect & 31u) ? (px_tex & 31u) - (expect & 31u) : (expect & 31u) - (px_tex & 31u);
        int tex_ok = (dr <= 1u && dg <= 1u && db <= 1u);
        g_sd[SD_PROBE_PLACE] = (tex_ok && px_z == 0xF800u) ? 1u : (!tex_ok) ? 2u : 3u;
    }
    return 0;
}

/* ---- DDR-list read probes (D2c).  D2b on the Amiga: ROOM's first chunk of depth-tested
   triangles, fetched by the FSC from DDR, stopped the parser for good (FRAME DIAG: chunk 1 with
   the depth memset executed, the FIFO bind executed, chunk 2 = 37 textured depth-tested
   triangles never reached its fence, FIFO empty, 2 NOPs not taken); the same frames render
   through the FIFO (DLFIFO).  Every DDR list validated before D2b carried WRITES only (memsets,
   opaque triangles without depth): a fragment that must READ the framebuffer (depth test,
   blending) while the FSC still streams the list is the TMU-load deadlock of B1 run 5 again,
   per fragment this time.  These probes measure it on the real GPU, smallest list first, and
   stop at the first hang (one power cycle at most): */
static u32 g_probe_desc_c[RIX_TRI_DESC_WORDS];
static int probe_send_ddr(u32 k, rix_dl *dl, u32 fence, u32 color_arm)
{
    int rc;
    g_sd[SD_PROBE_DDR_RUN] |= (1u << k);
    g_sd[SD_PROBE_DDR_PIX2] = (g_sd[SD_PROBE_DDR_PIX2] & 0xFFFF0000u) | (dl->n > (g_sd[SD_PROBE_DDR_PIX2] & 0xFFFFu) ? dl->n : (g_sd[SD_PROBE_DDR_PIX2] & 0xFFFFu));
    rc = rix_submit(dl);                        /* g_dl_mode 1: FSC_MEM_TO_ST1, the FSC fetches the list */
    if (rc == 0 && rix_fence_wait(color_arm, fence, 500000u, NULL, NULL)) rc = -1;
    if (rc == 0 && lad_alive()) rc = -1;
    if (rc) {
        g_gpu_hung = 1u;
        g_sd[SD_PROBE_FAILED] = 9u + k + 1u;
        g_sd[SD_PROBE_DDR_RES] |= 3u << (4u * k);
        return (int)(RIXE_PROBE + 9u + k);
    }
    return 0;
}
static void probe_ddr_result(u32 k, u32 px, int ok)
{
    g_sd[SD_PROBE_DDR_RES] |= (ok ? 1u : 2u) << (4u * k);
    if (k == 0u) g_sd[SD_PROBE_DDR_PIX0] = (g_sd[SD_PROBE_DDR_PIX0] & 0x0000FFFFu) | (px << 16);
    else if (k == 1u) g_sd[SD_PROBE_DDR_PIX0] = (g_sd[SD_PROBE_DDR_PIX0] & 0xFFFF0000u) | (px & 0xFFFFu);
    else if (k == 2u) g_sd[SD_PROBE_DDR_PIX1] = (g_sd[SD_PROBE_DDR_PIX1] & 0x0000FFFFu) | (px << 16);
    else if (k == 3u) g_sd[SD_PROBE_DDR_PIX1] = (g_sd[SD_PROBE_DDR_PIX1] & 0xFFFF0000u) | (px & 0xFFFFu);
    else g_sd[SD_PROBE_DDR_PIX2] = (g_sd[SD_PROBE_DDR_PIX2] & 0x0000FFFFu) | (px << 16);
}
/* depth list: masked clear, LESS, then n triangles alternating far (green) / near (red): red expected */
static void probe_ddr_depth_list(rix_dl *dl, u32 color_arm, u32 zbuf_arm, u32 n, u32 features, const u32 *far_d, const u32 *near_d)
{
    u32 i;
    probe_head(dl, color_arm, 0u);                                  /* writes only: scissored colour clear */
    rix_dl_reg(dl, RIX_REG_DEPTH_ADDR, zbuf_arm);
    rix_dl_reg(dl, RIX_REG_CLEAR_DEPTH, 0xFFFFu);
    rix_dl_reg(dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B | RIX_FRAG_DEPTH_FUNC(RIX_TF_LESS) | RIX_FRAG_DEPTH_MASK);
    rix_dl_clear_depth(dl, RIX_W * RIX_H_ALLOC);                    /* a write: ROOM's chunk 1 did this from DDR and passed */
    rix_dl_reg(dl, RIX_REG_FEATURES, RIX_FEAT_DEPTH_TEST | features);
    for (i = 0; i < n; i++) rix_dl_tri(dl, (i & 1u) ? near_d : far_d);   /* far, near, far, near ... : the last near one wins */
    rix_dl_reg(dl, RIX_REG_FEATURES, 0u);
    rix_dl_reg(dl, RIX_REG_DEPTH_ADDR, 0u);
    rix_dl_reg(dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
}
int rix_probe_ddr_reads(u32 color_arm, u32 zbuf_arm, u32 tex_arm)
{
    rix_dl dl;
    int rc;
    u32 px;
    if (g_gpu_hung) return (int)RIXE_GPU_HUNG;
    g_sd[SD_PROBE_DDR_RUN] = 0u; g_sd[SD_PROBE_DDR_RES] = 0u;
    g_sd[SD_PROBE_DDR_PIX0] = 0u; g_sd[SD_PROBE_DDR_PIX1] = 0u; g_sd[SD_PROBE_DDR_PIX2] = 0u;
    if (g_dl_mode != 1u) { g_sd[SD_PROBE_DDR_RES] = 0x44444u; return 0; }   /* no DDR lists (DLFIFO or L9 failed): nothing to measure */
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.25f, g_probe_desc_a);   /* red, near */
    probe_make_desc(220.0f, 170.0f, 420.0f, 170.0f, 320.0f, 380.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.75f, g_probe_desc_b);   /* green, far */
    /* probe 9: depth test, 2 triangles (the smallest list that reads) */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_ddr_depth_list(&dl, color_arm, zbuf_arm, 2u, 0u, g_probe_desc_b, g_probe_desc_a);
    rix_dl_fence(&dl, 0xC020u, 0u);
    rc = probe_send_ddr(0u, &dl, 0xC020u, color_arm);
    if (rc) return rc;
    px = px_at(color_arm, PR_LINE, PR_X); probe_ddr_result(0u, px, px == 0xF800u);
    /* probe 10: blending ZERO,SRC_COLOR (a colour read per fragment), 2 triangles: half white expected */
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, g_probe_desc_c);
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.5f, g_probe_desc_a);
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_head(&dl, color_arm, 0u);
    rix_dl_tri(&dl, g_probe_desc_c);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B | RIX_FRAG_BLEND(RIX_BF_ZERO, RIX_BF_SRC_COLOR));
    rix_dl_reg(&dl, RIX_REG_FEATURES, RIX_FEAT_BLENDING);
    rix_dl_tri(&dl, g_probe_desc_a);
    rix_dl_reg(&dl, RIX_REG_FEATURES, 0u);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
    rix_dl_fence(&dl, 0xC021u, 0u);
    rc = probe_send_ddr(1u, &dl, 0xC021u, color_arm);
    if (rc) return rc;
    px = px_at(color_arm, PR_LINE, PR_X); probe_ddr_result(1u, px, probe_is_half(px));
    /* probe 11: depth test, 12 triangles (about one ST0 FIFO of words) */
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.25f, g_probe_desc_a);
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_ddr_depth_list(&dl, color_arm, zbuf_arm, 12u, 0u, g_probe_desc_b, g_probe_desc_a);
    rix_dl_fence(&dl, 0xC022u, 0u);
    rc = probe_send_ddr(2u, &dl, 0xC022u, color_arm);
    if (rc) return rc;
    px = px_at(color_arm, PR_LINE, PR_X); probe_ddr_result(2u, px, px == 0xF800u);
    /* probe 12: depth test, 40 triangles (the size of the ROOM chunk that hung) */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_ddr_depth_list(&dl, color_arm, zbuf_arm, 40u, 0u, g_probe_desc_b, g_probe_desc_a);
    rix_dl_fence(&dl, 0xC023u, 0u);
    rc = probe_send_ddr(3u, &dl, 0xC023u, color_arm);
    if (rc) return rc;
    px = px_at(color_arm, PR_LINE, PR_X); probe_ddr_result(3u, px, px == 0xF800u);
    /* probe 13: ROOM's exact mix: texture bound through the FIFO (rule 11), then a DDR list of
       40 textured, depth-tested triangles.  The red one carries texture coordinates (white vertex
       colour, REPLACE env): the pixel is a texel of the chrome texture, anything but black/green. */
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    rix_dl_reg(&dl, RIX_REG_TMU0_ENV, RIX_TMU_ENV_REPLACE);
    rix_dl_reg(&dl, RIX_REG_TMU0_ENVCOLOR, 0u);
    rix_dl_reg(&dl, RIX_REG_TMU0_CONFIG, g_cfg.tmu_cfg);
    rix_dl_texture(&dl, 0u, 64u, tex_arm);
    rix_dl_fence(&dl, 0xC024u, 0u);
    rc = rix_submit_fifo(&dl);                                /* FIFO, waited: the TMU holds the texture before the list is fetched */
    if (rc == 0 && rix_fence_wait(color_arm, 0xC024u, 500000u, NULL, NULL)) rc = -1;
    if (rc == 0 && lad_alive()) rc = -1;
    if (rc) { g_gpu_hung = 1u; g_sd[SD_PROBE_FAILED] = 14u; g_sd[SD_PROBE_DDR_RES] |= 3u << 16; return (int)(RIXE_PROBE + 13u); }
    probe_make_desc(200.0f, 150.0f, 440.0f, 150.0f, 320.0f, 400.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.25f, g_probe_desc_c);
    g_probe_desc_c[30] = fl_bits(0.0f); g_probe_desc_c[31] = fl_bits(0.0f); g_probe_desc_c[32] = fl_bits(1.0f);
    g_probe_desc_c[33] = fl_bits(1.0f / 240.0f); g_probe_desc_c[34] = fl_bits(0.0f); g_probe_desc_c[35] = fl_bits(0.0f);
    g_probe_desc_c[36] = fl_bits(0.0f); g_probe_desc_c[37] = fl_bits(1.0f / 250.0f); g_probe_desc_c[38] = fl_bits(0.0f);
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    probe_ddr_depth_list(&dl, color_arm, zbuf_arm, 40u, RIX_FEAT_TMU0, g_probe_desc_b, g_probe_desc_c);
    rix_dl_fence(&dl, 0xC025u, 0u);
    rc = probe_send_ddr(4u, &dl, 0xC025u, color_arm);
    if (rc) return rc;
    px = px_at(color_arm, PR_LINE, PR_X); probe_ddr_result(4u, px, px != 0u && px != 0x07E0u);
    /* leave the GPU as rix_run_probes does */
    rix_frame_texture_invalidate(0u);
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    rix_dl_reg(&dl, RIX_REG_TMU0_ENV, g_cfg.tmu_env);
    rix_dl_reg(&dl, RIX_REG_TMU0_CONFIG, g_cfg.tmu_cfg);
    rix_dl_reg(&dl, RIX_REG_DEPTH_ADDR, 0u);
    rix_dl_reg(&dl, RIX_REG_FRAGMENT, RIX_FRAGMENT_A07B);
    rix_dl_fence(&dl, 0xC026u, 0u);
    return probe_send(5u, &dl, 0xC026u, color_arm, NULL);
}

/* self-test, no GPU risk: (1) count: 16 zero words (= 16 NOPs) by 16-beat FIXED bursts, then by
   single beats, without TLR: TDFV must read capacity-16 each time, then the FIFO is reset (the
   words never reach the parser; a FIXED burst turned INCR by the interconnect would write TLR
   with 0, a size error at worst, and the count would be off); (2) integrity: a fence packet whose
   payload is carried by the DMA must write its value at pixel 0 of line 480 of `color`;
   (3) throughput: 8 packets of 248 NOPs. */
int rix_dmafifo_selftest(u32 color_arm)
{
    volatile u32 *z = (volatile u32 *)rix_ptr(RIX_DL2_ARM);
    u32 k, cnt16 = 0u, cnt1 = 0u, best = 0u, t0;
    int rc;
    g_sd[SD_DMAFIFO_STATUS] = 0u; g_sd[SD_DMAFIFO_NS_B1] = 0u; g_sd[SD_DMAFIFO_NS_B16] = 0u;
    g_sd[SD_DMAFIFO_COUNT] = 0u; g_sd[SD_DMAFIFO_FENCE_PIX] = 0u; g_sd[SD_DMAFIFO_WORDS] = 0u;
    g_dmafifo_burst = 0u;
    if (g_dmac_base == 0u || g_gpu_hung) { g_sd[SD_DMAFIFO_STATUS] = 3u; return (int)RIXE_DMAFIFO; }
    for (k = 0; k < 4096u; k++) z[k] = RIX_OP_NOP;
    dsb();
    /* (1) count, 16-beat FIXED bursts */
    rc = fifo_reset(); if (rc) return rc;
    rc = dmafifo_go(RIX_DL2_ARM, 16u, 16u);
    if (rc == 0) rc = dmafifo_wait(100000u);
    if (rc == 0) cnt16 = mmio_rd(RIX_FIFO_BASE + FIFO_TDFV);
    mmio_wr(RIX_FIFO_BASE + FIFO_ISR, FIFO_INT_ERR | FIFO_INT_TC);
    rc = fifo_reset(); if (rc) return rc;
    /* (1) count, single-beat stores */
    rc = dmafifo_go(RIX_DL2_ARM, 16u, 1u);
    if (rc == 0) rc = dmafifo_wait(100000u);
    if (rc == 0) cnt1 = mmio_rd(RIX_FIFO_BASE + FIFO_TDFV);
    mmio_wr(RIX_FIFO_BASE + FIFO_ISR, FIFO_INT_ERR | FIFO_INT_TC);
    rc = fifo_reset(); if (rc) return rc;
    g_sd[SD_DMAFIFO_COUNT] = (cnt16 << 16) | (cnt1 & 0xFFFFu);
    if (cnt16 == g_fifo_capacity - 16u) best = 16u;
    else if (cnt1 == g_fifo_capacity - 16u) best = 1u;
    if (best == 0u) { g_sd[SD_DMAFIFO_STATUS] = 3u; return (int)RIXE_DMAFIFO; }
    if (cnt1 != g_fifo_capacity - 16u && best == 16u) { /* bursts work, single beats do not: keep bursts */ }
    /* (2) integrity: a fence packet carried by the DMA */
    {
        rix_dl dl;
        u32 saved = g_dl_mode;
        g_dl_mode = 2u;
        rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
        rix_dl_baseline(&dl, color_arm);
        rix_dl_fence(&dl, 0xD0D1u, 0u);
        rix_dl_finish(&dl);
        g_dl_mode = saved;
        rc = dmafifo_send_packet(dl.base, dl.n, best, 1u);
        if (rc == 0) rc = rix_fence_wait(color_arm, 0xD0D1u, 500000u, NULL, NULL);
        g_sd[SD_DMAFIFO_FENCE_PIX] = px_at(color_arm, RIX_H_ALLOC - 1u, 0u);
        if (rc || lad_alive()) { g_sd[SD_DMAFIFO_STATUS] = 4u; if (rc == 0) g_gpu_hung = 1u; return (int)RIXE_DMAFIFO; }
    }
    /* (3) throughput: 8 packets of 248 NOPs, best mode, then the other one if it also counted right */
    {
        u32 modes[2], nm = 0u, m;
        modes[nm++] = best;
        if (best == 16u && cnt1 == g_fifo_capacity - 16u) modes[nm++] = 1u;
        for (m = 0; m < nm; m++) {
            u32 ns;
            t0 = rix_now();
            for (k = 0; k < 8u; k++) { rc = dmafifo_send_packet(RIX_DL2_ARM, DMAFIFO_PKT, modes[m], (k == 7u) ? 1u : 0u); if (rc) break; }
            ns = (u32)(((uint64_t)rix_ticks_to_us((u32)(rix_now() - t0)) * 1000u) / (8u * DMAFIFO_PKT));
            if (rc) { g_sd[SD_DMAFIFO_STATUS] = 3u; return rc; }
            if (modes[m] == 16u) g_sd[SD_DMAFIFO_NS_B16] = ns; else g_sd[SD_DMAFIFO_NS_B1] = ns;
            g_sd[SD_DMAFIFO_WORDS] += 8u * DMAFIFO_PKT;
            if (lad_alive()) { g_gpu_hung = 1u; g_sd[SD_DMAFIFO_STATUS] = 4u; return (int)RIXE_DMAFIFO; }
        }
    }
    g_dmafifo_burst = best;
    g_sd[SD_DMAFIFO_STATUS] = (best == 16u) ? 2u : 1u;
    g_caps |= RIXCAP_DMAFIFO;
    g_dmafifo_us = 0u;
    return 0;
}

/* L9: the frame list fetched by the FSC itself from DDR, then L10 liveness. */
static int ladder_run_ddr_stage(void)
{
    rix_dl dl;
    int rc, hung = 0;
    u32 checks = 0u, t0;
    if (g_cfg.flags & RIXF_DLFIFO) {
        lad_record(LAD_DDR_LIST, 0, 0u, 0xFFFFFFFFu);
        g_sd[SD_FENCE_MEM_US] = 0u;
        g_dl_mode = 2u;
        return 0;
    }
    g_dl_mode = 1u;
    rix_dl_begin(&dl, RIX_DL2_ARM, RIX_DL_BYTES / 4u);
    rix_dl_baseline(&dl, RIX_FB0_ARM);
    rix_dl_frame_head(&dl, RIX_FB0_ARM, 0u, rix_fence_color(LAD_C_BODY3), 1);
    rix_dl_fence(&dl, LAD_C_FENCE3, 0u);
    rix_dl_finish(&dl);
    t0 = rix_now();
    {
        u32 cmd[2];
        cmd[0] = FSC_MEM_TO_ST1(dl.n * 4u);
        cmd[1] = dl.base;
        rc = fifo_send(cmd, 2u, 1u);
    }
    if (rc == 0) {
        if (rix_fence_wait(RIX_FB0_ARM, LAD_C_FENCE3, 200000u, NULL, NULL) == 0) checks |= 1u;
        g_sd[SD_FENCE_MEM_US] = rix_ticks_to_us((u32)(rix_now() - t0));
        dmb();
        if (px_at(RIX_FB0_ARM, 0u, 0u) == LAD_C_BODY3) checks |= 2u;
        if (px_at(RIX_FB0_ARM, 479u, 639u) == LAD_C_BODY3) checks |= 4u;
    }
    g_sd[SD_FENCE_VALUE_MEM] = px_at(RIX_FB0_ARM, 480u, 0u);
    if (!(checks & 1u)) g_sd[SD_FENCE_MEM_US] = 0xFFFFFFFFu;
    rc = lad_finish(LAD_DDR_LIST, rc, checks, 7u, RIX_FB0_ARM, RIX_H_ALLOC, LAD_C_FENCE3, &hung);
    if (hung) {
        g_sd[SD_LADDER_FAILED] = LAD_DDR_LIST + 1u;
        g_dl_mode = 2u;
        return rc;
    }
    if ((checks & 7u) == 7u) {
        g_caps |= RIXCAP_FENCE_MEM;
        g_dl_mode = 1u;
    } else {
        g_dl_mode = 2u;
    }
    /* L10: still alive after everything */
    {
        int ra = lad_alive();
        lad_record(LAD_ALIVE_END, ra, 0u, 0xFFFFFFFFu);
        if (ra) { g_sd[SD_LADDER_FAILED] = LAD_ALIVE_END + 1u; return (int)(RIXE_LADDER + LAD_ALIVE_END); }
    }
    /* a DDR failure with a live GPU is a fallback, not an error */
    return 0;
}

u32 rix_caps(void) { return g_caps; }

int rix_init(const rix_config *cfg)
{
    volatile u32 *sd;
    int rc, first_rc;
    u32 t0;

    if (!cfg->stats || !cfg->sd || !cfg->dma_prog) return (int)RIXE_CONFIG;
    g_cfg = *cfg;
    g_st = cfg->stats; g_sd = cfg->sd; sd = g_sd;
    if (g_cfg.idle_tick_us == 0u) g_cfg.idle_tick_us = 100u;
    g_caps = 0u;
    g_dma_busy = 0u;
    g_swap_mode = 0u;
    g_idle_mode = 2u;          /* spin until the WFI path is proven */
    g_wfi_armed = 0u;
    g_dl_mode = 2u;

    timer_init();
    if (cfg->timer_hz_out) *cfg->timer_hz_out = g_timer_hz;
    sd[SD_TIMER_HZ] = g_timer_hz;
    sd[SD_GT_CTRL] = mmio_rd(GT_BASE + GT_CTRL);

    sd[SD_ID_MAGIC] = mmio_rd(RIX_ID_BASE + RIX_ID_MAGIC_OFF);
    sd[SD_ID_VERSION] = mmio_rd(RIX_ID_BASE + RIX_ID_VERSION_OFF);
    if (sd[SD_ID_MAGIC] != RIX_HW_MAGIC || sd[SD_ID_VERSION] != RIX_HW_VERSION) return RIXE_NOID;
    g_caps |= RIXCAP_ID;

    sd[SD_VDMA_SA] = rix_vdma_sa();
    g_caps |= RIXCAP_VDMA;

    /* FIFO: one reset at init, never per frame. */
    t0 = rix_now();
    rc = fifo_reset();
    sd[SD_FIFO_RESET_US] = rix_ticks_to_us((u32)(rix_now() - t0));
    if (rc) return rc;
    sd[SD_FIFO_VACANCY] = mmio_rd(RIX_FIFO_BASE + FIFO_TDFV);
    g_fifo_capacity = sd[SD_FIFO_VACANCY];
    if (g_fifo_capacity < 64u || g_fifo_capacity > FIFO_DEPTH_WORDS) g_fifo_capacity = FIFO_DEPTH_WORDS - 4u;
    g_st[RS_FIFO_VACANCY_MIN] = g_fifo_capacity;
    g_caps |= RIXCAP_FIFO;

    rix_fence_reset(RIX_FB0_ARM);
    rix_fence_reset(RIX_FB1_ARM);
    rix_fence_reset(RIX_FB2_ARM);

    /* ---- GPU init ladder --------------------------------------------- */
    /* One packet per stage, a liveness packet after each.  The first stage
       the GPU stops consuming is the last one ever sent (there is no GPU
       reset short of a power cycle on G4B).  Results in SD_LADDER. */
    first_rc = 0;
    g_gpu_hung = 0u;
    sd[SD_INIT_FIRST_ERROR] = 0u;
    sd[SD_LADDER_STAGES] = 0u;
    sd[SD_LADDER_FAILED] = 0u;
    sd[SD_FENCE_FIFO_US] = 0u; sd[SD_FENCE_MEM_US] = 0xEEEEEEEEu; sd[SD_RESYNC_US] = 0u;
    sd[SD_FENCE_VALUE_FIFO] = 0u; sd[SD_FENCE_VALUE_MEM] = 0u; sd[SD_FENCE_FIFO_WORDS] = 0u; sd[SD_BASELINE_WORDS] = 0u;
    {
        u32 i;
        for (i = 0; i < SD_LADDER_MAX * SD_LADDER_SLOTS; i++) sd[SD_LADDER + i] = 0xEEEEEEEEu; /* not run */
    }
    g_dl_mode = 2u;
    rc = ladder_run_fifo_stages();
    if (rc && !first_rc) first_rc = rc;


    /* byte-order correction engine (independent of the GPU) */
    (void)dmac_probe();
    if (g_cfg.flags & RIXF_PRESWAP) {
        g_swap_mode = 3u;
    } else if (g_cfg.flags & RIXF_NOSWAP) {
        g_swap_mode = 0u;
    } else {
        if (rix_selftest_dma(sd)) g_swap_mode = 1u;
        else g_swap_mode = 2u;
    }
    rix_selftest_neon(sd);

    /* low-power waiting (independent of the GPU) */
    idle_init();
    rix_selftest_wfi(sd);

    /* last: the DDR-list transport.  Runs only on a GPU that passed every
       FIFO stage; a FSC left waiting on an AXI read could not be recovered. */
    if (!g_gpu_hung) {
        rc = ladder_run_ddr_stage();
        if (rc && !first_rc) first_rc = rc;
    }
    if (!g_gpu_hung) {
        rc = ladder_run_texture_stages();
        if (rc && !first_rc) first_rc = rc;
    }

    /* D2d: FIFO fed by the PL330 instead of DDR lists (DLDMA); proven by its self-test first,
       CPU FIFO otherwise (the DDR mode is never chosen under DLDMA: it hangs on fragment reads) */
    if ((g_cfg.flags & RIXF_DLDMA) && !g_gpu_hung) {
        rc = rix_dmafifo_selftest(RIX_FB1_ARM);
        if (rc == 0) g_dl_mode = 3u;
        else { g_dl_mode = 2u; if (!first_rc && g_gpu_hung) first_rc = rc; }
    }
    g_st[RS_DL_MODE] = g_dl_mode;
    g_st[RS_SWAP_MODE] = g_swap_mode;
    g_st[RS_IDLE_MODE] = g_idle_mode;
    if (cfg->caps_out) *cfg->caps_out = g_caps;
    if (first_rc && sd[SD_INIT_FIRST_ERROR] == 0u) sd[SD_INIT_FIRST_ERROR] = (u32)first_rc;
    dsb();
    return first_rc;
}

void rix_shutdown(void)
{
    if (g_dma_busy) dma_wait(200000u);
    idle_shutdown();
    dsb();
}
