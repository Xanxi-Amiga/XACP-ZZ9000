/* rixgpu.h - librixgpu 1.1 (ZZCube B1d / ZZGear C1c / D1a lineage) - Core1 RasterIX backend, core module
 *
 * Scope: everything a renderer (cube B1, gear C1, ZZQuake2 tomorrow) needs
 * from the G4B RasterIX on the ZZ9000, driven entirely by Core1:
 *
 *   - display lists built in DDR inside the GPU window and fetched by the
 *     FrameStreamingCore itself (MEM->ST1, two FIFO words per submit),
 *     with the 512-word FIFO path kept as a fallback;
 *   - a real GPU completion fence: the last command of every submission
 *     is a 64-pixel color-buffer memset of a per-frame value into a
 *     dedicated DDR page.  It travels through the same in-order color
 *     write path as every pixel before it, so when Core1 reads it back
 *     from DDR, every earlier pixel write of that frame has landed;
 *   - RGB565 byte-order correction done by the PS PL330 DMAC (endian
 *     swap 16) with a NEON fallback, never by a strongly-ordered loop;
 *   - low-power waiting with the Core1 private timer + WFI;
 *   - scanout observation through the AXI VDMA start-address register.
 *
 * Nothing here touches Core0 state, the firmware, or the FPGA.
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef RIXGPU_H
#define RIXGPU_H

#include "rix_hw.h"

#define RIXGPU_VERSION        0x00010302u   /* 1.3.2: rix_frame_set_pump_interval, rix_frame_flush. 1.3.1: rix_frame_last_bind_seq / rix_frame_bind_passed, dq_starved, queue of 80. 1.3.0: deferred submission (rix_frame_set_deferred, off by default; rule 11 kept). 1.2.3: fill rule off by default (rix_fill_rule), rix_tex_wnorm. 1.2.2: top-left fill rule in rix_tri_desc (shared edges drawn once). 1.2.1: binds as FIFO packets by default (rule 11, D2d path) or as short DDR lists (BINDLIST, L15's case); the 1.2 "binds inside the frame chunks" is withdrawn (FSC deadlock, D2e); rix_frame_clear; ZZGL */
u32  rix_version(void);
volatile u32 *rix_sd(void);                 /* the diagnostic block given in rix_config (NULL before init) */
int  rix_alive(void);                       /* 2 NOPs through the FIFO, TC waited (100 ms): 0 = taken, else RIXE_FIFO_TC (hung) */
u32  rix_fifo_vacancy(void);
u32  rix_fifo_isr(void);

typedef struct {
    u32 flags;           /* RIXF_* (rix_diag.h) */
    u32 idle_tick_us;    /* WFI tick granularity, default 100 */
    /* diagnostics the library fills (any memory the application owns) */
    volatile u32 *stats; /* u32[RS_COUNT] */
    volatile u32 *sd;    /* u32[SD_COUNT] */
    volatile u32 *caps_out;     /* optional: receives rix_caps() at the end of init */
    volatile u32 *timer_hz_out; /* optional: receives the global timer frequency */
    u32 dma_prog;        /* 64 bytes of Non-Cacheable DDR for the PL330 program (the demos: mailbox + 0x4000) */
    /* texture stages of the init ladder (L11..L15): a 39-word descriptor of
       one textured triangle on a 640x481 buffer, a pixel (x, y in memory
       lines) strictly inside it, the TMU registers and the page table.
       All optional: NULL/0 skips those stages. */
    const u32 *probe_tri;
    u32 probe_x, probe_y;
    u32 tmu_env, tmu_env_color, tmu_cfg;
    u32 tex_pages;       /* page count */
    u32 tex_base;        /* ARM address of page 0 (2 KiB pages) */
} rix_config;

typedef struct {
    u32 base;            /* ARM address of the list (64-byte aligned) */
    u32 cap_words;
    u32 n;               /* words written */
    u32 overflow;
    u32 fence_value;     /* fence appended by rix_dl_fence(), 0 if none */
    u32 frag_shadow;     /* last OP_RENDER_CONFIG_FRAGMENT value written in this list */
} rix_dl;

/* ---- effective capabilities of the GPU this library is driving ------------
 * Filled by rix_hw_caps() from what rix_init() (ladder) and rix_run_probes() /
 * rix_probe_placement() actually observed on THIS hardware: a field is
 * RIX_HWCAP_YES only after the corresponding probe passed, RIX_HWCAP_NO when
 * it ran and failed, RIX_HWCAP_UNKNOWN when it was not run.  Fixed limits of
 * the G4B RasterIX_EF configuration (one TMU, 39-word descriptors, 2 KiB
 * texture pages, 256x256 maximum, 16-bit depth) are constants. */
#define RIX_HWCAP_UNKNOWN 0u
#define RIX_HWCAP_YES     1u
#define RIX_HWCAP_NO      2u
typedef struct {
    u32 api_version;          /* RIXGPU_VERSION */
    u32 hw_magic, hw_version; /* RIX1 identification registers */
    u32 init_caps;            /* RIXCAP_* bits from the init ladder */
    u32 ddr_lists;            /* YES: display lists fetched from DDR (MEM->ST1) work */
    u32 textures;             /* YES: a textured triangle rendered (ladder L13 check bits), NO when L13 ran and failed */
    u32 depth_test;           /* probe 0 */
    u32 blending;             /* probe 1 (ONE,ONE) */
    u32 alpha_test;           /* probe 2 */
    u32 arena_placement;      /* placement probe: texture + Z outside the window */
    u32 tmu_modulate;         /* probe 7 */
    u32 lightmap_blend;       /* probe 8 (ZERO,SRC_COLOR) */
    u32 tex_switch_64_us;     /* measured GPU cost of a 64x64 bind (0 = not measured) */
    u32 tex_switch_256_us;    /* ... and of a 256x256 bind */
    u32 tmu_count;            /* 1 */
    u32 tex_max_size;         /* 256 */
    u32 tex_page_bytes;       /* 2048 */
    u32 depth_bits;           /* 16 */
    u32 desc_words;           /* 39 */
    u32 fifo_words;           /* FIFO capacity measured at init */
    u32 timer_hz;
} rix_hwcaps;
void rix_hw_caps(rix_hwcaps *out);

/* ---- lifecycle ------------------------------------------------------ */
int  rix_init(const rix_config *cfg);         /* 0 or RIXE_* */
void rix_shutdown(void);
u32  rix_caps(void);
u32  rix_dl_mode(void);                       /* 1 = DDR, 2 = FIFO */
u32  rix_swap_mode(void);                     /* 0 none, 1 DMA, 2 NEON, 3 preswap */
u32  rix_idle_mode(void);                     /* 1 WFI, 2 spin */
void rix_force_dl_mode(u32 mode);
void rix_force_swap_mode(u32 mode);

/* ---- time ----------------------------------------------------------- */
u32  rix_timer_hz(void);
u32  rix_now(void);                            /* global timer, low 32 bits */
u32  rix_ticks_to_us(u32 dt);
u32  rix_us_to_ticks(u32 us);
void rix_idle_us(u32 us);                      /* WFI (or spin) for about us */
void rix_spin_us(u32 us);

/* ---- display list builder ------------------------------------------- */
void rix_dl_begin(rix_dl *dl, u32 base, u32 cap_words);
void rix_dl_word(rix_dl *dl, u32 w);
void rix_dl_reg(rix_dl *dl, u32 reg, u32 val);
void rix_dl_clear(rix_dl *dl, u32 pixels);          /* color memset; forces the color mask on first (never issue a fully masked memset: it hung G4B) */
void rix_dl_tri(rix_dl *dl, const u32 *desc39);
void rix_dl_baseline(rix_dl *dl, u32 buf_arm);  /* all 19 registers to known values (no memory write) */
void rix_dl_fence(rix_dl *dl, u32 value16, u32 features); /* scissored memset into line 480 px 0..63; sets REG 3 */
void rix_dl_fence_at(rix_dl *dl, u32 value16, u32 features, u32 x0); /* same, pixels x0..x0+63 (chunk fences: RIX_CHUNK_FENCE_X) */
void rix_dl_fence_at_drain(rix_dl *dl, u32 value16, u32 features, u32 x0, u32 drain_nops);   /* explicit drain NOP count (FIFO packets: 2) */
int  rix_fence_passed_at(u32 buf_arm, u32 value16, u32 x0);

/* Capability probes (SELFTEST PROBE): depth test with a Z-buffer in zbuf_arm
 * (640x481x16 bits), blending ONE/ONE, alpha test GREATER 0.5, and the cost of
 * a texture switch (OP_TEXTURE page table through the FIFO, 64x64 and 256x256
 * textures at tex_arm, which must hold >= 64 pages).  Everything goes through
 * the FIFO, one feature per packet, liveness after each; on a hang nothing
 * else is sent and RIXE_PROBE + index is returned.  Results in SD_PROBE_*. */
int  rix_run_probes(u32 color_arm, u32 zbuf_arm, u32 tex_arm);
/* D2c: probes 9..13, DDR lists whose fragments READ the framebuffer (depth test, blending,
 * textured + depth), smallest list first, stopping at the first hang; needs the chrome texture
 * (64 pages) at tex_arm and a Z-buffer at zbuf_arm.  Results in SD_PROBE_DDR_*. */
int  rix_probe_ddr_reads(u32 color_arm, u32 zbuf_arm, u32 tex_arm);
/* D2d: PL330 -> ST0 FIFO transport self-test (count, integrity, throughput; no GPU risk); run at
 * init under RIXF_DLDMA, or explicitly.  0 = usable (transport 3 selected by init), RIXE_DMAFIFO
 * otherwise; results in SD_DMAFIFO_*.  rix_dmafifo_us(): Core1 time spent submitting since the last call. */
int  rix_dmafifo_selftest(u32 color_arm);
u32  rix_dmafifo_us(void);
/* depth memset of `pixels` into the buffer of REG 17; re-asserts the depth mask first (hardware rule) */
void rix_dl_clear_depth(rix_dl *dl, u32 pixels); /* 1 when pixel x0 of line 480 holds value16 or a later one (16-bit wrap-aware) */
u32  rix_dl_finish(rix_dl *dl);               /* pads with NOPs, returns bytes */
u32  rix_fence_color(u32 value16);            /* RGBA8888 that packs to value16 */
void rix_dl_frame_head(rix_dl *dl, u32 buf_arm, u32 features, u32 clear_rgba, int do_clear);

void rix_dl_texture(rix_dl *dl, u32 tmu, u32 pages, u32 base_arm); /* OP_TEXTURE page table */
int  rix_submit_fifo(rix_dl *dl);             /* always the FIFO transport (texture tables) */

/* ---- submission and fences ------------------------------------------ */
int  rix_submit(rix_dl *dl);                  /* 0 or RIXE_* */
int  rix_submit_words_fifo(const u32 *payload, u32 words); /* ST0->ST1 raw, waits the last packet's TC (hang detection) */
int  rix_submit_words_fifo_nowait(const u32 *payload, u32 words); /* same without the TC wait: in-stream texture tables (frame API) */
/* cache maintenance for DDR the GPU reads and Core1 maps cacheable (textures, lists outside the window) */
void rix_mem_clean(u32 addr, u32 bytes);   /* L1 clean by MVA (dcache_clean_range of the application): the region must be outer-non-cacheable or NC for a GPU reader */
/* placement probe (SELFTEST PROBE, D1): a 256x256 texture copied to tex_dst_arm and a Z-buffer at
   zbuf_arm, both outside the G4B window (Core1 arena, cacheable: cleaned), rendered into color_arm.
   Results in SD_PROBE_PLACE*. */
int  rix_probe_placement(u32 color_arm, u32 tex_src_arm, u32 tex_dst_arm, u32 zbuf_arm);
int  rix_fence_check(u32 buf_arm, u32 value16); /* 1 when all 64 px of line 480 match */
int  rix_fence_wait(u32 buf_arm, u32 value16, u32 timeout_us, volatile u32 *stop, u32 *polls);
void rix_fence_reset(u32 buf_arm);

/* ---- RGB565 byte-order correction ----------------------------------- */
int  rix_swap_start(u32 addr, u32 bytes);     /* async for DMA, sync for NEON */
int  rix_swap_wait(u32 timeout_us);           /* 0 ok, RIXE_DMA_* */
int  rix_swap_busy(void);
void rix_swap16_neon(u32 addr, u32 bytes);
void rix_swap16_cpu32(u32 addr, u32 bytes);

/* ---- scanout observation -------------------------------------------- */
u32  rix_vdma_sa(void);

/* ---- self tests (results in the sd block) ---------------------------- */
int  rix_selftest_dma(volatile u32 *sd);
int  rix_selftest_neon(volatile u32 *sd);
int  rix_selftest_wfi(volatile u32 *sd);

#endif /* RIXGPU_H */
