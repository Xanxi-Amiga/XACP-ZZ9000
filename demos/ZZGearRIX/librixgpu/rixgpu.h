/* rixgpu.h - librixgpu 1.0 (ZZCube B1d / ZZGear C1c lineage) - Core1 RasterIX backend, core module
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

#define RIXGPU_VERSION        0x00010000u   /* 1.0: core (this) + rixframe + rixtex + rixdesc */
u32  rix_version(void);

typedef struct {
    u32 flags;           /* RIXF_* from the launcher */
    u32 idle_tick_us;    /* WFI tick granularity, default 100 */
    volatile u32 *mbox;  /* mailbox base (RIX_SHARED_ARM) */
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
int  rix_fence_passed_at(u32 buf_arm, u32 value16, u32 x0);

/* Capability probes (SELFTEST PROBE): depth test with a Z-buffer in zbuf_arm
 * (640x481x16 bits), blending ONE/ONE, alpha test GREATER 0.5, and the cost of
 * a texture switch (OP_TEXTURE page table through the FIFO, 64x64 and 256x256
 * textures at tex_arm, which must hold >= 64 pages).  Everything goes through
 * the FIFO, one feature per packet, liveness after each; on a hang nothing
 * else is sent and RIXE_PROBE + index is returned.  Results in SD_PROBE_*. */
int  rix_run_probes(u32 color_arm, u32 zbuf_arm, u32 tex_arm);
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
void rix_mem_clean(u32 addr, u32 bytes);
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

/* ---- self tests (results in SH_SELFTEST_DATA) ----------------------- */
int  rix_selftest_dma(volatile u32 *sd);
int  rix_selftest_neon(volatile u32 *sd);
int  rix_selftest_wfi(volatile u32 *sd);

#endif /* RIXGPU_H */
