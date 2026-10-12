/* rix_shared.h - ZZCube RasterIX B1 / ZZGear RasterIX C1
 *
 * Shared-memory ABI between the 68k launcher (ZZGearRIX.c) and the
 * Core1 payload (zzgear_app.c / rixgpu.c).  Included by BOTH compilers,
 * so it contains only plain #defines (no types).
 *
 * Mailbox base: ARM 0x04700000 == 68k fb + 0x04500000 (A14 heritage).
 * All slots are 32-bit little-endian on the ARM side; the 68k uses
 * rd32le()/wr32le().  Index values below are u32 slot indices; the 68k
 * byte offset is index * 4.
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef RIX_SHARED_H
#define RIX_SHARED_H

#define RIX_SHARED_ARM        0x04700000UL
#define RIX_SHARED_FB         0x04500000UL   /* 68k: fb + this */
#define RIX_BLOB_ARM          0x04500000UL
#define RIX_BLOB_FB           0x04300000UL
#define RIX_BLOB_MAX          0x001F0000UL

#define RIX_CORE1_MAGIC       0xDEAD5249UL
#define RIX_EXIT_MARK         0xC0DEBAD1UL

/* ---- mailbox slots (u32 index) --------------------------------------
 * Cache-line discipline (Core0 caches this memory and only syncs it at
 * its vblank flush; the 68k polls line 0 coherently through OP_MEMCPY,
 * whose source invalidate would DISCARD unflushed 68k writes in the same
 * line): line 0 and lines >= 2 are written by Core1 only, line 1 is
 * written by the 68k only.                                              */
#define SH_MAGIC              0   /* Core1->68k: RIX_CORE1_MAGIC once booted      */
#define SH_HEARTBEAT          1   /* Core1->68k: main loop iterations             */
#define SH_STATUS             2   /* Core1->68k: ST_*                              */
#define SH_ERROR              3   /* Core1->68k: 0x5249xxxx on ST_FATAL            */
#define SH_STAGE              4   /* Core1->68k: progress marker                   */
#define SH_READY              5   /* Core1->68k: (seq << 2) | buffer index, 0=none */
#define SH_READY_OFF          6   /* Core1->68k: 68k fb offset of that buffer      */
#define SH_EXIT_MARK          7   /* Core1->68k: RIX_EXIT_MARK when returned        */

#define SH_STOP               8   /* 68k->Core1: 1 = stop and return              */
#define SH_FLAGS              9   /* 68k->Core1: RIXF_* (set before launch)        */
#define SH_MODE               10  /* 68k->Core1: RIX_MODE_*                        */
#define SH_PARAM1             11  /* 68k->Core1: frame limit (0 = unlimited)       */
#define SH_PARAM2             12  /* 68k->Core1: unused in C1 (was the stress cube count) */
#define SH_PARAM3             13  /* 68k->Core1: idle tick us (0 = default 100)    */
#define SH_PARAM4             14  /* 68k->Core1: ARM address of the P96 screen     */
#define SH_PAN_ISSUED         15  /* 68k->Core1: last SH_READY value PANned (diag) */

#define SH_CAPS               16  /* Core1->68k: RIXCAP_* achieved at init         */
#define SH_TIMER_HZ           17  /* Core1->68k: global timer ticks per second     */
#define SH_SELFTEST           18  /* Core1->68k: 1 = self test finished            */
#define SH_BUF0_OFF           19  /* Core1->68k: 68k offsets of the 3 buffers      */
#define SH_BUF1_OFF           20
#define SH_BUF2_OFF           21
#define SH_FENCE_LAST         22  /* Core1->68k: last fence value observed         */
#define SH_LOOP_STATE         23  /* Core1->68k: where the loop currently waits    */
#define SH_VDMA_SA            24  /* Core1->68k: last VDMA start address read      */
#define SH_SEC_TICK           25  /* Core1->68k: seconds elapsed (clock check)     */
#define SH_WATCHDOG           26  /* Core1->68k: timer snapshot of last activity    */
#define SH_INIT_ERROR         27  /* Core1->68k: rix_init() return code            */

#define SH_STATS              32  /* RIX_STATS block, u32[64]                      */
#define SH_HIST_GPU           96  /* u32[128] histogram of submit->fence, 100 us   */
#define SH_HIST_SWAP          224 /* u32[64]  histogram of swap time, 100 us       */
#define SH_HIST_LOOP          288 /* u32[128] histogram of loop time, 100 us       */
#define SH_SELFTEST_DATA      416 /* u32[144] self-test numbers (D1: 128 -> 144)    */
#define SH_END                560 /* first slot Core1 never writes (D1: was 544)    */
#define SH_COPY_LINE0         560 /* 68k-private: OP_MEMCPY destination of line 0 (32-byte aligned) */
#define SH_COPY_BLOCK         576 /* 68k-private: OP_MEMCPY destination, slots 16..543 (ends < 0x04702000 FWPAN) */

/* ---- status --------------------------------------------------------- */
#define ST_BOOT               0x01UL
#define ST_INIT               0x02UL
#define ST_RUNNING            0x10UL
#define ST_STOPPED            0xFFUL
#define ST_FATAL              0xEEUL

/* ---- modes ---------------------------------------------------------- */
#define RIX_MODE_MONITOR      0UL
#define RIX_MODE_OFFSCREEN    1UL
#define RIX_MODE_SELFTEST     2UL
#define RIX_MODE_STRESS       3UL

/* ---- flags (68k -> Core1) ------------------------------------------- */
#define RIXF_NOSWAP           0x00000001UL /* no endian conversion: wrong colors   */
#define RIXF_STATIC           0x00000002UL /* no rotation                         */
#define RIXF_CULL             0x00000004UL /* back-face culling (6 tris/frame)    */
#define RIXF_DIRTYCLEAR       0x00000008UL /* scissored clear of dirty rectangle  */
#define RIXF_PRESWAP          0x00000010UL /* nearest + pre-swapped texture       */
#define RIXF_DLFIFO           0x00000020UL /* force FIFO (ST0->ST1) submission    */
#define RIXF_NODMA            0x00000040UL /* NEON swap instead of PL330          */
#define RIXF_SPIN             0x00000080UL /* no WFI idle                         */
#define RIXF_NOCLEAR          0x00000100UL /* benchmark: skip the clear           */
#define RIXF_NOTEX            0x00000200UL /* flat colors, TMU off                */
#define RIXF_NORESYNC         0x00000400UL /* skip FSC resync NOP flush           */
#define RIXF_VERIFY           0x00000800UL /* sample-pixel verification per frame */
#define RIXF_NEAREST          0x00001000UL /* A04 NEAREST (mag filter off)        */
#define RIXF_ORIGTMU          0x00002000UL /* A03 0x2C88 (reproduces black faces) */
#define RIXF_NOPIPE           0x00004000UL /* no GPU/DMA overlap (sequential)     */
#define RIXF_FACES            0x00008000UL /* diagnostic flat face colors         */
#define RIXF_FWPAN            0x00010000UL /* present through the OPTIONAL Core0 RIXPAN mailbox (patched FAST1 only) */
#define RIXF_TEXDDR           0x00020000UL /* diagnostic: also run ladder L15 (texture page table through a DDR list) */
#define RIXF_FULLCLEAR        0x00040000UL /* ZZGear: full 640x481 clear each frame (default: dirty rectangle) */
#define RIXF_PROBE            0x00080000UL /* SELFTEST only: capability probes after the timings (depth, blending, alpha test, texture switch cost) */
#define RIXCAP_TRI_DDR        0x00000400UL /* a textured frame fetched from DDR completed (ladder L14) */

/* ---- capabilities (Core1 -> 68k) ------------------------------------ */
#define RIXCAP_ID             0x00000001UL /* RIX1 / 2.0 identified               */
#define RIXCAP_FIFO           0x00000002UL /* FIFO reset + TRC ok                 */
#define RIXCAP_FENCE_FIFO     0x00000004UL /* fence via FIFO-streamed DL works    */
#define RIXCAP_FENCE_MEM      0x00000008UL /* fence via DDR display list works    */
#define RIXCAP_DMA            0x00000010UL /* PL330 swap self-test passed         */
#define RIXCAP_WFI            0x00000020UL /* private timer wake-up verified      */
#define RIXCAP_VDMA           0x00000040UL /* VDMA start address readable         */
#define RIXCAP_TIMER          0x00000080UL /* global timer running                */
#define RIXCAP_NEON           0x00000100UL /* NEON swap self-test passed          */
#define RIXCAP_TEX            0x00000200UL /* texture written + page list sent    */

/* ---- RIX_STATS block (index relative to SH_STATS) ------------------- */
#define RS_FRAMES_BUILT       0
#define RS_FRAMES_SUBMITTED   1
#define RS_FRAMES_FENCED      2
#define RS_FRAMES_SWAPPED     3
#define RS_FRAMES_PUBLISHED   4
#define RS_FRAMES_FLIPPED     5
#define RS_FRAMES_DROPPED     6
#define RS_STALL_NOBUF        7
#define RS_FENCE_TIMEOUTS     8
#define RS_T_BUILD            9   /* all times: microseconds, sums unless noted */
#define RS_T_SUBMIT           10
#define RS_T_GPU              11  /* submit -> fence observed */
#define RS_T_GPU_MIN          12
#define RS_T_GPU_MAX          13
#define RS_T_SWAP             14
#define RS_T_SWAP_MAX         15
#define RS_T_WAITBUF          16
#define RS_T_LOOP             17
#define RS_T_IDLE             18  /* time spent in WFI/sleep */
#define RS_T_ELAPSED_LO       19
#define RS_DL_WORDS           20
#define RS_DL_MODE            21  /* 1 = DDR (MEM->ST1), 2 = FIFO (ST0->ST1) */
#define RS_SWAP_MODE          22  /* 0 none, 1 PL330 DMA, 2 NEON, 3 preswap */
#define RS_IDLE_MODE          23  /* 1 WFI, 2 spin */
#define RS_VDMA_SA_LAST       24
#define RS_VDMA_CHANGES       25
#define RS_CHECKSUM_LAST      26
#define RS_SAMPLE_CENTER      27
#define RS_SAMPLE_CORNER      28
#define RS_FENCE_POLLS        29
#define RS_T_GPU_CLEAR        30  /* selftest: clear-only GPU time (us) */
#define RS_T_GPU_TRIS         31  /* selftest: gear without clear (us) */
#define RS_TRIS_PER_FRAME     32
#define RS_T_SWAP_MIN         33
#define RS_DMA_FAULTS         34
#define RS_T_TEXLOAD          35
#define RS_FRAMES_VERIFIED    36
#define RS_VERIFY_FAIL        37
#define RS_T_ELAPSED_HI       38
#define RS_T_LOOP_MAX         39
#define RS_T_WAITBUF_MAX      40
#define RS_T_PUBLISH_TO_FLIP  41  /* sum of publish -> flip observed (us) */
#define RS_FLIP_SAMPLES       42
#define RS_T_FENCE_WAIT_MAX   43
#define RS_FIFO_VACANCY_MIN   44
#define RS_FSC_RESYNC_US      45
#define RS_BUILD_MAX          46
#define RS_FENCE_LATE_PUBLISH 47  /* frames published after their predecessor flipped */
#define RS_T_GPU_LAST         48
#define RS_T_SWAP_LAST        49
#define RS_GPU_BUSY_US        50  /* sum of GPU busy time (submit->fence, overlapped) */
#define RS_CHUNKS             51  /* ZZGear: chunks submitted (all frames) */
#define RS_T_RINGWAIT         52  /* ZZGear: time waiting for a free ring slot (us, sum) */
#define RS_TRIS_CULLED        53  /* ZZGear: back-facing triangles skipped (all frames) */
#define RS_T_XFORM            54  /* ZZGear: transform+cull+sort time (us, sum) */
#define RS_MESH_TRIS          55  /* ZZGear: triangles of the whole mesh (2160) */
#define RS_MESH_VERTS         56  /* ZZGear: vertices of the whole mesh */
#define RS_COUNT              64

/* ---- self-test data (index relative to SH_SELFTEST_DATA) ------------ */
#define SD_ID_MAGIC           0
#define SD_ID_VERSION         1
#define SD_FIFO_VACANCY       2
#define SD_FIFO_RESET_US      3
#define SD_RESYNC_US          4
#define SD_FENCE_FIFO_US      5   /* 0xFFFFFFFF = timeout */
#define SD_FENCE_MEM_US       6
#define SD_CLEAR_US_MIN       7
#define SD_CLEAR_US_AVG       8
#define SD_FRAME_US_MIN       9   /* full clear + textured gear (ZZGear) */
#define SD_FRAME_US_AVG       10
#define SD_TRIS_US_MIN        11  /* textured gear, no clear */
#define SD_TRIS_US_AVG        12
#define SD_DMA_TEST           13  /* 1 = pass, 2 = mismatch, 3 = fault, 4 = timeout, 0 = skipped */
#define SD_DMA_US_MIN         14  /* 614400-byte in-place swap */
#define SD_DMA_US_AVG         15
#define SD_NEON_US_MIN        16
#define SD_NEON_US_AVG        17
#define SD_WFI_TEST           18  /* 1 = pending bit seen, 2 = CPU interface shows it, 3 = wake verified */
#define SD_WFI_SLEEP_US       19  /* measured duration of a 1000 us sleep */
#define SD_VDMA_SA            20
#define SD_TIMER_HZ           21
#define SD_TEX_US             22
#define SD_FENCE_VALUE_FIFO   23
#define SD_FENCE_VALUE_MEM    24
#define SD_DMAC_DSR           25
#define SD_DMAC_BASE          26
#define SD_FENCE_FIFO_WORDS   27
#define SD_CLEAR_US_MAX       28
#define SD_FRAME_US_MAX       29
#define SD_TRIS_US_MAX        30
#define SD_DMA_US_MAX         31
#define SD_NEON_US_MAX        32
#define SD_CULL_US_MIN        33  /* ZZGear: untextured gear + dirty clear */
#define SD_CULL_US_AVG        34
#define SD_DIRTY_US_MIN       35  /* dirty-rect clear + textured gear (default mode) */
#define SD_DIRTY_US_AVG       36
#define SD_SECOND_TICKS       37  /* global timer ticks counted during 1 s wait */
#define SD_GT_CTRL            38
#define SD_ICDISPR            39
#define SD_FENCE_MAP_FIRST    40  /* after a failed FIFO probe: first FB0 line holding the fence color (0xFFFFFFFF none) */
#define SD_FENCE_MAP_LAST     41  /* last such line */
#define SD_FENCE_MAP_PIXELS   42  /* pixels of FB0 (481 lines) equal to the fence color */
#define SD_INIT_FIRST_ERROR   43  /* first RIXE_* met by rix_init (init continues to collect the rest) */
#define SD_BASELINE_WORDS     44  /* words of the register baseline list sent before the probes */
#define SD_LADDER_STAGES      45  /* number of ladder stages run (see SD_LADDER) */
#define SD_LADDER_FAILED      46  /* first failed stage + 1, 0 = none */
#define SD_LADDER             48  /* 4 slots per stage: rc, TC us, check bits, write map (first<<16|last, 0xFFFF none) */
#define SD_LADDER_SLOTS       4
#define SD_LADDER_MAX         16
/* ---- capability probes (SELFTEST PROBE, C1b): run after every other
 * measurement, one feature per packet, same discipline as the ladder (the
 * packet that is never taken is the last thing sent; RIXE_PROBE + index) ---- */
#define SD_PROBE_RUN          112 /* bit k = probe k was sent (0 depth, 1 blend, 2 alpha, 3 tex64, 4 tex256, 5 state-only) */
#define SD_PROBE_FAILED       113 /* probe index + 1 that hung the GPU or failed its liveness, 0 = none */
#define SD_PROBE_DEPTH        114 /* 1 = near wins in both orders (Z-buffer works), 2 = last drawn wins (no depth), 3 = inconsistent, 0 = not run */
#define SD_PROBE_DEPTH_PIX    115 /* pixel after near-then-far << 16 | pixel after far-then-near (RGB565) */
#define SD_PROBE_BLEND        116 /* 1 = ONE,ONE blend gives red+green, 2 = green only (no blending), 3 = other, 0 = not run */
#define SD_PROBE_BLEND_PIX    117 /* pixel after the blended draw */
#define SD_PROBE_ALPHA        118 /* 1 = alpha 0.25 rejected and 0.75 drawn (alpha test works), 2 = both drawn, 3 = other */
#define SD_PROBE_ALPHA_PIX    119 /* pixel after alpha 0.25 << 16 | pixel after alpha 0.75 */
#define SD_PROBE_TEX64_US     120 /* per texture switch, 64x64 (4 pages): GPU time, fence-measured, state-only run subtracted */
#define SD_PROBE_TEX256_US    121 /* per texture switch, 256x256 (64 pages) */
#define SD_PROBE_TEX64_PUSH   122 /* per switch: Core1 MMIO time writing the list words into the FIFO (its own cost) */
#define SD_PROBE_TEX256_PUSH  123
#define SD_PROBE_STATE_US     124 /* per "switch" without OP_TEXTURE (register writes + the same triangle): the baseline */
#define SD_PROBE_TEX_N        125 /* switches per run */
#define SD_PROBE_TEX_RAW      126 /* fence time of the whole 256x256 run (us), for a sanity check */
#define SD_PROBE_DEPTH_KEEP   127 /* pixel after far-then-near WITHOUT depth clear (buffer keeps 0.25): 0000 expected */
/* placement probe (D1): texture + Z-buffer OUTSIDE the G4B window (Core1 arena), colour in the window.
   Slots 128..131 (D1 run 1 had them at 108..111, inside the ladder record of L15: fixed) */
#define SD_PROBE_PLACE        128 /* 1 = textured + depth-tested triangle rendered from arena texture/Z (works), 2 = texture missing, 3 = depth wrong, 0 = not run */
#define SD_PROBE_PLACE_PIX    129 /* textured pixel << 16 | depth-order pixel */
#define SD_PROBE_PLACE_TEX    130 /* ARM address of the arena texture used */
#define SD_PROBE_PLACE_Z      131 /* ARM address of the arena Z-buffer used */
#define SD_COUNT              144

/* ---- init ladder (rix_init): one GPU packet per stage, each followed by a
 * liveness packet.  A stage that the GPU stops consuming (no FIFO TC within
 * RIX_TC_TIMEOUT) is the last thing ever sent: the GPU is hung and only a
 * power cycle (bitstream reload) resets it (G4B wires RasterIX, the FIFO and
 * the Zorro bridge to the same proc_sys_reset_0 / FCLK_RESET0_N net; there
 * is no GPU-only reset).  RIXE_LADDER + stage identifies the culprit. ---- */
#define LAD_ALIVE0            0   /* 2 OP_NOP words through FSC and CommandParser */
#define LAD_RESYNC            1   /* 9 x 508 FSC NOP words (flush a half-received stream) */
#define LAD_TEST2             2   /* FAST1 Test2 recipe: 64x32 memset into SCRATCH (known-good on G4B) */
#define LAD_CLEAR480          3   /* A07b recipe: 640x480 full memset into FB0 */
#define LAD_CLEAR481          4   /* 640x481 full memset (line 480 reachable?) */
#define LAD_SCISSOR_REGS      5   /* scissor registers written, scissor disabled, full memset */
#define LAD_FENCE             6   /* scissor enabled, 64 px memset into line 480 */
#define LAD_NOPS              7   /* OP_NOP words inside the command stream, then full memset */
#define LAD_FRAME_FENCE       8   /* rix_dl_frame_head + rix_dl_fence as the app sends them */
#define LAD_DDR_LIST          9   /* the same list fetched by the FSC from DDR (MEM->ST1) */
#define LAD_ALIVE_END         10  /* liveness after everything */
#define LAD_TMU_REGS          11  /* TMU0 env/env-color/config registers + fence (FIFO) */
#define LAD_TEX_FIFO          12  /* OP_TEXTURE page table (64 pages) + fence, FIFO transport */
#define LAD_TRI_FIFO          13  /* TMU0 on, one textured triangle on FB0 + fence, FIFO */
#define LAD_TRI_DDR           14  /* the same frame fetched from DDR (TMU reads + FSC reads share the AXI) */
#define LAD_TEX_DDR           15  /* OP_TEXTURE page table through a DDR list (only with TEXDDR) */

/* ---- error codes (SH_ERROR low 16 bits, high = 0x5249) -------------- */
#define RIXE_NOID             0xE110UL
#define RIXE_FIFO_RESET       0xE111UL
#define RIXE_FIFO_VACANCY     0xE112UL
#define RIXE_FIFO_ERR         0xE113UL
#define RIXE_FIFO_TC          0xE114UL
#define RIXE_FENCE_TIMEOUT    0xE120UL
#define RIXE_FENCE_VALUE      0xE121UL
#define RIXE_DL_OVERFLOW      0xE122UL
#define RIXE_DESC             0xE123UL
#define RIXE_DMA_FAULT        0xE130UL
#define RIXE_DMA_TIMEOUT      0xE131UL
#define RIXE_NOFLIP           0xE140UL
#define RIXE_TIMER            0xE150UL
#define RIXE_BADMODE          0xE160UL
#define RIXE_STRESS_TOO_BIG   0xE161UL
#define RIXE_FWPAN_ABSENT     0xE131UL
#define RIXE_GPU_HUNG         0xE1A0UL  /* GPU not consuming at launch: hung by an earlier run, power cycle needed */
#define RIXE_LADDER           0xE1B0UL  /* + stage index: that init stage failed (see SD_LADDER) */
#define RIXE_RING_TIMEOUT     0xE124UL  /* ZZGear: a chunk ring slot never became free (chunk fence not seen) */
#define RIXE_PROBE            0xE1C0UL  /* + probe index: that capability probe hung the GPU (SD_PROBE_FAILED) */
#define RIXE_RING_CFG         0xE125UL  /* librixgpu: bad ring configuration or frame API misuse */
#define RIXE_TEX_POOL         0xE126UL  /* librixgpu: texture pool exhausted or misconfigured */
#define RIXE_TEX_SIZE         0xE127UL  /* librixgpu: texture size not a power of two in 8..256 */

/* ---- OPTIONAL Core0 presentation mailbox (FAST1_OPTIONAL_PATCH) ----- */
#define RIX_FWPAN_REQ_ARM     0x04702000UL
#define RIX_FWPAN_ACK_ARM     0x04703000UL
#define RIX_FWPAN_MAGIC       0x52495850UL   /* RIXP */
#define RIX_FWPAN_HELLO       0x46505731UL   /* FPW1 */

/* ---- GPU window layout (ARM physical) -------------------------------
 * Each color buffer is 640 x 481 lines: 480 displayed lines plus one
 * undisplayed line that carries the per-buffer GPU fence (64 pixels).
 * The scanout only reads the first 480 lines (stride 1280 bytes).      */
#define RIX_WIN_BASE          0x06600000UL
#define RIX_WIN_END           0x06800000UL
#define RIX_W                 640UL
#define RIX_H                 480UL
#define RIX_H_ALLOC           481UL
#define RIX_PITCH             1280UL
#define RIX_FB_BYTES          614400UL        /* 640*480*2 displayed */
#define RIX_FB_STRIDE         615680UL        /* 640*481*2 = 0x96500 */
#define RIX_FENCE_ROW_OFF     614400UL        /* byte offset of line 480 */
#define RIX_FENCE_PIXELS      64UL
#define RIX_FB0_ARM           0x06600000UL
#define RIX_FB1_ARM           0x06696500UL
#define RIX_FB2_ARM           0x0672CA00UL    /* ends 0x067C2F00 */
#define RIX_TEX_ARM           0x067C3000UL    /* 64 pages of 2 KiB */
#define RIX_TEX_BYTES         0x00020000UL    /* ends 0x067E3000 */
#define RIX_DL0_ARM           0x067E3000UL    /* 3 x 16 KiB display lists */
#define RIX_DL1_ARM           0x067E7000UL
#define RIX_DL2_ARM           0x067EB000UL
#define RIX_DL_BYTES          0x00004000UL
#define RIX_SCRATCH_ARM       0x067EF000UL    /* 4 KiB: DMA/NEON self-test */
/* ZZGear: after rix_init the DL0/DL1/DL2/SCRATCH/STAGE area becomes one ring of
   display-list chunks (FAST1 only uses STAGE for OP_GPU_SUBMIT, never issued here) */
#define RIX_RING_ARM          0x067E3000UL
#define RIX_RING_BYTES        0x0001C000UL    /* 7 chunks of 16 KiB, ends 0x067FF000 < aperture end */
#define RIX_CHUNK_BYTES       0x00004000UL
#define RIX_RING_SLOTS        (RIX_RING_BYTES / RIX_CHUNK_BYTES)
#define RIX_CHUNK_FENCE_X     64UL            /* chunk fences: line 480, pixels 64..127 (frame fence: 0..63) */
#define RIX_STAGE_ARM         0x067F0000UL    /* firmware staging: never touched */
#define RIX_FB_TO_68K         0x00200000UL    /* 68k offset = ARM - this */
#define RIX_STRESS_DL_ARM     RIX_FB2_ARM     /* stress mode: FB2 holds the big list */
#define RIX_STRESS_DL_BYTES   RIX_FB_STRIDE

#endif /* RIX_SHARED_H */
