/* rix_hw.h - ZZCube RasterIX B1 - hardware definitions for Core1
 *
 * Sources (file/line references in ARCHITECTURE.md):
 *   FIFO/ID registers  : FAST1 ZZ9000OS/src/main.c lines 187-211
 *   VDMA               : FAST1 video.c init_vdma / dump_vdma_status
 *   RasterIX commands  : rtl/RasterIX/RegisterAndDescriptorDefines.vh @9269a01c
 *   FrameStreamingCore : rtl/RasterIX/FrameStreamingCore.v @9269a01c
 *   Zynq PS blocks     : UG585 (GIC, private/global timer, PL330 DMAC)
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef RIX_HW_H
#define RIX_HW_H

#include <stdint.h>
#include "rix_shared.h"

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;
typedef int32_t  s32;
typedef int16_t  s16;

/* ---- RasterIX G4B AXI-Lite blocks ----------------------------------- */
#define RIX_ID_BASE           0x83C60000u
#define RIX_ID_MAGIC_OFF      0x00u
#define RIX_ID_VERSION_OFF    0x08u
#define RIX_HW_MAGIC          0x52495831u   /* "RIX1" */
#define RIX_HW_VERSION        0x00020000u   /* G4B interface 2.0 */

/* Xilinx AXI-Stream FIFO (TX only, AXI4-Lite data interface). */
#define RIX_FIFO_BASE         0x83C70000u
#define FIFO_ISR              0x00u
#define FIFO_IER              0x04u
#define FIFO_TDFR             0x08u
#define FIFO_TDFV             0x0Cu
#define FIFO_TDFD             0x10u
#define FIFO_TLF              0x14u
#define FIFO_RESET_KEY        0xA5u
#define FIFO_INT_TC           0x08000000u   /* transmit complete */
#define FIFO_INT_TRC          0x01000000u   /* transmit reset complete */
#define FIFO_INT_TPOE         0x10000000u   /* transmit packet overrun */
#define FIFO_INT_TSE          0x02000000u   /* transmit size error */
#define FIFO_INT_ERR          (FIFO_INT_TPOE | FIFO_INT_TSE)
#define FIFO_DEPTH_WORDS      512u

/* ---- AXI VDMA (scanout) : read-only observation from Core1 ---------- */
#define VDMA_BASE             0x83000000u
#define VDMA_MM2S_CR          0x00u
#define VDMA_MM2S_SR          0x04u
#define VDMA_MM2S_VSIZE       0x50u
#define VDMA_MM2S_HSIZE       0x54u
#define VDMA_MM2S_STRIDE      0x58u
#define VDMA_MM2S_SA1         0x5Cu         /* MM2S start address 1 */

/* ---- Cortex-A9 MPCore private peripherals --------------------------- */
#define SCU_BASE              0xF8F00000u
#define GIC_CPU_BASE          0xF8F00100u
#define GICC_CTLR             0x00u
#define GICC_PMR              0x04u
#define GICC_BPR              0x08u
#define GICC_IAR              0x0Cu
#define GICC_EOIR             0x10u
#define GICC_HPPIR            0x18u
#define GT_BASE               0xF8F00200u   /* global timer */
#define GT_COUNT_LO           0x00u
#define GT_COUNT_HI           0x04u
#define GT_CTRL               0x08u
#define PT_BASE               0xF8F00600u   /* private timer (banked per CPU) */
#define PT_LOAD               0x00u
#define PT_COUNTER            0x04u
#define PT_CTRL               0x08u
#define PT_ISR                0x0Cu
#define GIC_DIST_BASE         0xF8F01000u
#define GICD_CTLR             0x000u
#define GICD_ISENABLER0       0x100u        /* banked for SGI/PPI */
#define GICD_ICENABLER0       0x180u
#define GICD_ISPENDR0         0x200u
#define GICD_ICPENDR0         0x280u
#define GICD_IPRIORITYR       0x400u
#define PT_IRQ_ID             29u

/* ---- SLCR (read only from Core1) ------------------------------------ */
#define SLCR_APER_CLK_CTRL    0xF800012Cu   /* bit 0: DMA_CPU_2XCLKACT */
#define SLCR_DMAC_RST_CTRL    0xF800020Cu   /* bit 0: DMAC reset asserted */

/* ---- PL330 DMAC ----------------------------------------------------- */
#define DMAC_S_BASE           0xF8003000u   /* secure APB */
#define DMAC_NS_BASE          0xF8004000u   /* non-secure APB */
#define DMAC_DSR              0x000u
#define DMAC_DPC              0x004u
#define DMAC_INTEN            0x020u
#define DMAC_INT_EVENT_RIS    0x024u
#define DMAC_INTMIS           0x028u
#define DMAC_INTCLR           0x02Cu
#define DMAC_FSRD             0x030u
#define DMAC_FSRC             0x034u
#define DMAC_FTRD             0x038u
#define DMAC_FTR(n)           (0x040u + 4u * (n))
#define DMAC_CSR(n)           (0x100u + 8u * (n))
#define DMAC_CPC(n)           (0x104u + 8u * (n))
#define DMAC_SAR(n)           (0x400u + 0x20u * (n))
#define DMAC_DAR(n)           (0x404u + 0x20u * (n))
#define DMAC_CCR(n)           (0x408u + 0x20u * (n))
#define DMAC_LC0(n)           (0x40Cu + 0x20u * (n))
#define DMAC_LC1(n)           (0x410u + 0x20u * (n))
#define DMAC_DBGSTATUS        0xD00u
#define DMAC_DBGCMD           0xD04u
#define DMAC_DBGINST0         0xD08u
#define DMAC_DBGINST1         0xD0Cu
#define DMAC_CR0              0xE00u
#define DMAC_CHANNEL          7u
#define DMAC_CSR_STATE_MASK   0x0Fu
#define DMAC_CSR_STOPPED      0x00u
#define DMAC_CSR_FAULTING_COMPLETING 0x0Eu
#define DMAC_CSR_FAULTING     0x0Fu

/* PL330 instruction opcodes */
#define PL330_DMAMOV          0xBCu
#define PL330_DMALP           0x20u
#define PL330_DMALPEND        0x28u
#define PL330_DMALD           0x04u
#define PL330_DMAST           0x08u
#define PL330_DMAWMB          0x13u
#define PL330_DMARMB          0x12u
#define PL330_DMAEND          0x00u
#define PL330_DMAGO           0xA0u
#define PL330_DMAKILL         0x01u
#define PL330_RD_SAR          0u
#define PL330_RD_CCR          1u
#define PL330_RD_DAR          2u

/* CCR: SAI | SS64 | SB16 | DAI | DS64 | DB16 | ES16 (endian swap 16) */
#define PL330_CCR_SWAP16      (1u | (3u << 1) | (15u << 4) | (1u << 14) | (3u << 15) | (15u << 18) | (1u << 28))
#define PL330_CCR_COPY        (1u | (3u << 1) | (15u << 4) | (1u << 14) | (3u << 15) | (15u << 18))
#define PL330_BYTES_PER_LDST  128u          /* 16 beats x 8 bytes */

/* ---- RasterIX command stream (CommandParser) ------------------------ */
#define RIX_OP_NOP            0x00000000u
#define RIX_OP_RENDER_CONFIG  0x10000000u
#define RIX_OP_FRAMEBUFFER    0x20000000u
#define RIX_OP_TRIANGLE       0x30000000u
#define RIX_OP_FOG_LUT        0x40000000u
#define RIX_OP_TEXTURE        0x50000000u
#define RIX_REG(r)            (RIX_OP_RENDER_CONFIG | ((u32)(r) & 0x1Fu))
#define RIX_REG_FEATURES      0u
#define RIX_REG_CLEAR_COLOR   1u
#define RIX_REG_CLEAR_DEPTH   2u
#define RIX_REG_FRAGMENT      3u
#define RIX_REG_STENCIL       4u
#define RIX_REG_FOG_COLOR     5u
#define RIX_REG_SCISSOR_START 6u
#define RIX_REG_SCISSOR_END   7u
#define RIX_REG_Y_OFFSET      8u
#define RIX_REG_RESOLUTION    9u
#define RIX_REG_TMU0_ENV      10u
#define RIX_REG_TMU0_ENVCOLOR 11u
#define RIX_REG_TMU0_CONFIG   12u
#define RIX_REG_TMU1_ENV      13u
#define RIX_REG_TMU1_ENVCOLOR 14u
#define RIX_REG_TMU1_CONFIG   15u
#define RIX_REG_COLOR_ADDR    16u
#define RIX_REG_DEPTH_ADDR    17u
#define RIX_REG_STENCIL_ADDR  18u
#define RIX_REG_COUNT         19u
/* OP_RENDER_CONFIG_FRAGMENT_PIPELINE bits 15..18 = color mask A,B,G,R.
 * FramebufferWriterStrobeGen.v: m_frag_tstrb = tstrb ? confMask : 0 with
 * confMask = {G, B} mask bits.  A register bank at its power-on value (0)
 * therefore strobes NOTHING: pixels and memsets alike never reach DDR.
 * Every list that writes the color buffer must run with these bits set. */
#define RIX_FRAG_COLOR_MASK   0x00078000u
#define RIX_FEAT_SCISSOR      0x20u
#define RIX_FEAT_TMU0         0x40u
#define RIX_FB_COMMIT         0x01u
#define RIX_FB_MEMSET         0x02u
#define RIX_FB_COLOR_SELECT   0x10u
#define RIX_FB_DEPTH_SELECT   0x20u
/* register 0 feature bits (RegisterAndDescriptorDefines.vh, FEATURE_ENABLE) */
#define RIX_FEAT_FOG          0x01u
#define RIX_FEAT_BLENDING     0x02u
#define RIX_FEAT_DEPTH_TEST   0x04u
#define RIX_FEAT_ALPHA_TEST   0x08u
#define RIX_FEAT_STENCIL      0x10u
/* register 3 fragment pipeline fields: depth func [2:0], alpha func [5:3], alpha ref
   [13:6], depth mask [14], color mask A/B/G/R [15..18], blend s [22:19], d [26:23] */
#define RIX_TF_ALWAYS         0u
#define RIX_TF_NEVER          1u
#define RIX_TF_LESS           2u
#define RIX_TF_EQUAL          3u
#define RIX_TF_LEQUAL         4u
#define RIX_TF_GREATER        5u
#define RIX_FRAG_DEPTH_FUNC(f) ((u32)(f) & 7u)
#define RIX_FRAG_ALPHA_FUNC(f) (((u32)(f) & 7u) << 3)
#define RIX_FRAG_ALPHA_REF(v)  (((u32)(v) & 0xFFu) << 6)
#define RIX_FRAG_DEPTH_MASK    0x00004000u
#define RIX_BF_ZERO           0u
#define RIX_BF_ONE            1u
#define RIX_BF_DST_COLOR      2u
#define RIX_BF_SRC_COLOR      3u
#define RIX_BF_SRC_ALPHA      6u
#define RIX_BF_ONE_MINUS_SRC_ALPHA 7u
#define RIX_FRAG_BLEND(s, d)  ((((u32)(s) & 15u) << 19) | (((u32)(d) & 15u) << 23))
/* TMU config: width/height as log2 in [3:0]/[7:4], mag filter bit 10, RGB565 = 2 << 12 */
#define RIX_TMU_CFG_RGB565(log2w, log2h, mag) (((u32)(log2w) & 15u) | (((u32)(log2h) & 15u) << 4) | ((mag) ? 0x400u : 0u) | 0x2000u)
/* triangle descriptor word indices (TMU_COUNT = 1) */
#define RIX_DESC_DEPTH_Z      24u
#define RIX_DESC_DEPTH_W      25u
#define RIX_FB_CMD(pixels, ctrl) (RIX_OP_FRAMEBUFFER | (((u32)(pixels) & 0xFFFFFu) << 8) | ((u32)(ctrl) & 0xFFu))
#define RIX_TRI_DESC_WORDS    39u           /* TMU_COUNT = 1 */
#define RIX_TRI_CMD           (RIX_OP_TRIANGLE | (RIX_TRI_DESC_WORDS * 4u))
#define RIX_TEX_CMD(pages, tmu) (RIX_OP_TEXTURE | (((u32)(tmu) & 3u) << 19) | ((u32)(pages) & 0x7FFFFu))
#define RIX_XY(x, y)          ((((u32)(y) & 0x7FFu) << 16) | ((u32)(x) & 0x7FFu))
#define RIX_RGBA(r, g, b, a)  ((((u32)(r) & 0xFFu) << 24) | (((u32)(g) & 0xFFu) << 16) | (((u32)(b) & 0xFFu) << 8) | ((u32)(a) & 0xFFu))
/* A04 validated TMU config: 256x256, RGB565, mag filter on, min filter (mipmap) OFF */
#define RIX_TMU_CFG_A04       0x00002488u
#define RIX_TMU_CFG_ORIG      0x00002C88u
#define RIX_TMU_CFG_MAG_BIT   0x00000400u
#define RIX_TMU_ENV_REPLACE   0x00A9C700u   /* A07b: REPLACE, texture source */
#define RIX_FRAGMENT_A07B     0x00078000u   /* color mask RGBA on */

/* ---- FrameStreamingCore envelope ------------------------------------ */
#define FSC_CH_INT            0u
#define FSC_CH_ST0            1u
#define FSC_CH_ST1            2u
#define FSC_CH_MEM            3u
#define FSC_CMD(out, in, bytes) ((((u32)(out) & 3u) << 30) | (((u32)(in) & 3u) << 28) | ((u32)(bytes) & 0x0FFFFFFFu))
#define FSC_ST0_TO_ST1(bytes) FSC_CMD(FSC_CH_ST1, FSC_CH_ST0, bytes)
#define FSC_MEM_TO_ST1(bytes) FSC_CMD(FSC_CH_ST1, FSC_CH_MEM, bytes)
#define FSC_MEM_ALIGN         64u

/* ---- access primitives ----------------------------------------------
 * On the target these are the real thing.  With RIX_HOST_SIM defined the
 * same library and application code runs on a PC against a behavioural
 * model (host_tests/sim_rix.c): every MMIO access, every DDR pointer and
 * WFI go through the simulator.  No other line differs between the two. */
#ifdef RIX_HOST_SIM
u32  sim_mmio_rd(u32 a);
void sim_mmio_wr(u32 a, u32 v);
void *sim_ptr(u32 a);
void sim_wfi(void);
void sim_barrier(void);
static inline u32 mmio_rd(u32 a) { return sim_mmio_rd(a); }
static inline void mmio_wr(u32 a, u32 v) { sim_mmio_wr(a, v); }
static inline void *rix_ptr(u32 a) { return sim_ptr(a); }
static inline void dsb(void) { sim_barrier(); }
static inline void dmb(void) { sim_barrier(); }
static inline void isb(void) { }
static inline void cpu_yield(void) { sim_barrier(); }
static inline void cpu_wfi(void) { sim_wfi(); }
#else
static inline u32 mmio_rd(u32 a) { return *(volatile u32 *)(uintptr_t)a; }
static inline void mmio_wr(u32 a, u32 v) { *(volatile u32 *)(uintptr_t)a = v; }
static inline void *rix_ptr(u32 a) { return (void *)(uintptr_t)a; }
static inline void dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void dmb(void) { __asm__ volatile("dmb sy" ::: "memory"); }
static inline void isb(void) { __asm__ volatile("isb" ::: "memory"); }
static inline void cpu_yield(void) { __asm__ volatile("yield" ::: "memory"); }
static inline void cpu_wfi(void) { __asm__ volatile("wfi" ::: "memory"); }
#endif

#endif /* RIX_HW_H */
