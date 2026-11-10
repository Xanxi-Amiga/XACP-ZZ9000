/* rix_diag.h - librixgpu 1.1 - public: driver flags, capabilities, statistics
 * and self-test slot indices, init ladder stages, error codes.
 *
 * The library never addresses a mailbox of its own: rix_config gives it a
 * statistics block (u32[RS_COUNT]) and a self-test block (u32[SD_COUNT]) to
 * fill, wherever the application keeps them (ZZGear: its 68k mailbox;
 * ZZQuake2: its own diagnostic area).  Indices are relative to those blocks.
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef RIX_DIAG_H
#define RIX_DIAG_H

/* ---- driver flags (rix_config.flags; the demos pass them from the 68k) ------------------------------------------- */
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
#define RIXF_TEXDDR           0x00020000UL /* diagnostic: run ladder L15 even with BINDFIFO (page table through a short DDR list; hangs a G4B) */
#define RIXF_FULLCLEAR        0x00040000UL /* ZZGear: full 640x481 clear each frame (default: dirty rectangle) */
#define RIXF_BINDFIFO         0x00800000UL /* G4B path: no ladder L15 at init (it hangs a G4B).  Binds go through the FIFO with a bind fence (rule 11) in every mode but BINDLIST */
#define RIXF_BINDLIST         0x01000000UL /* 1.2.1 (D2f): each bind is its own short DDR list (TMU config + page table + bind fence, 80 words at most) submitted with 2 FIFO words instead of up to 80; rule 11 kept.  Needs rix_ring_cfg.bind_base and RIXCAP_TEX_DDR (L15 passed).  D2g: the default whenever L15 passed (D2f runs 4-7: ROOM B OFFSCREEN and ROOM C, which reset the system with FIFO packets, ran 300 frames); the flag is kept for scripts */
#define RIXF_BINDPKT          0x02000000UL /* D2g: force the FIFO packet bind (the D2d path) even when L15 passed */
#define RIXF_DLDMA            0x00400000UL /* transport 3: chunks pushed into the ST0 FIFO by the PL330 (D2d), not by CPU MMIO; DDR lists unused */
#define RIXF_SYNC             0x00200000UL /* rixframe: wait every chunk fence and bind fence right after its submission (ladder mode: the first hang names its chunk) */
#define RIXF_ZTRICK           0x00100000UL /* ROOM: no depth clear, alternating depth range + LEQUAL/GEQUAL (gl_ztrick) */
#define RIXF_PROBE            0x00080000UL /* SELFTEST only: capability probes after the timings (depth, blending, alpha test, texture switch cost) */
#define RIXCAP_TRI_DDR        0x00000400UL /* a textured frame fetched from DDR completed (ladder L14) */
#define RIXCAP_TEX_DDR        0x00001000UL /* ladder L15 passed: an OP_TEXTURE page table inside a SHORT DDR list (80 words: the FSC has fetched all of it before the parser reaches the OP_TEXTURE) completes on this GPU.  It says nothing about longer lists: a bind in the middle of a frame chunk deadlocks the FSC (D2e, 14/10, power cycle).  Enables BINDLIST only */
#define RIXCAP_DMAFIFO        0x00000800UL /* D2d: the PL330 can feed the ST0 FIFO (count, integrity and throughput self-test passed) */

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

/* ---- RIX_STATS block (index into rix_config.stats, u32[RS_COUNT]) ------------------- */
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
#define RS_DL_MODE            21  /* 1 = DDR (MEM->ST1), 2 = FIFO (ST0->ST1, CPU MMIO), 3 = FIFO fed by the PL330 (D2d) */
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

/* ---- self-test data (index into rix_config.sd, u32[SD_COUNT]) ------------ */
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
/* probes 7/8 (D2): the two texture/lightmap paths of the ROOM benchmark and of ref_rix */
#define SD_PROBE_MOD          132 /* 1 = MODULATE env halves a white texel with a 0.5 vertex colour, 2 = texel unchanged (REPLACE behaviour), 3 = other, 0 = not run */
#define SD_PROBE_MOD_PIX      133 /* the pixel */
#define SD_PROBE_LMBLEND      134 /* 1 = blend ZERO,SRC_COLOR (GL lightmap pass) halves a white pixel with a 0.5 grey triangle, 2 = unchanged, 3 = other */
#define SD_PROBE_LMBLEND_PIX  135
/* ROOM benchmark (ZZGear D2, zzroom.c): sums over the run unless noted */
#define SD_ROOM_SCENE         136 /* 1 = A (two passes), 2 = B (precomposed 64x64), 3 = C (hybrid), 4 = B32 (precomposed 32x32) */
#define SD_ROOM_FRAMES        137
#define SD_ROOM_SURFACES      138 /* world surfaces of the scene */
#define SD_ROOM_SURF_VIS      139 /* visible (front-facing, in front of the near plane) surfaces, sum */
#define SD_ROOM_TRIS_WORLD    140 /* world triangles emitted (after clipping; pass 2 counted again), sum */
#define SD_ROOM_TRIS_MODEL    141 /* model triangles emitted, sum */
#define SD_ROOM_TRIS_FX       142 /* blended effect triangles emitted, sum */
#define SD_ROOM_TRIS_CLIPPED  143 /* input triangles that needed near or screen clipping, sum */
#define SD_ROOM_TRIS_NEAR     144 /* of which crossing the near plane, sum */
#define SD_ROOM_PIXELS        145 /* pixels covered by the emitted triangles (sum of triangle areas), sum */
#define SD_ROOM_BINDS         146 /* texture binds sent to the TMU (OP_TEXTURE), sum */
#define SD_ROOM_BINDS_SKIPPED 147 /* binds skipped because the texture was already in the TMU, sum */
#define SD_ROOM_BIND_PAGES    148 /* 2 KiB pages streamed into the TMU, sum */
#define SD_ROOM_BIND_US       149 /* Core1 time spent in the binds (chunk close + FIFO table), sum us */
#define SD_ROOM_REGENS        150 /* precomposed textures regenerated (B/B32) or lightmap blocks rewritten (A/C), sum */
#define SD_ROOM_T_REGEN_US    151 /* Core1 time of those regenerations, sum us */
#define SD_ROOM_T_XFORM_US    152 /* Core1 time: transform, cull, clip, descriptors (without submission waits), sum us */
#define SD_ROOM_TEX_PAGES     153 /* pool pages in use by the scene (memory cost) */
#define SD_ROOM_DYN_SURF      154 /* surfaces lit by the moving light, sum */
#define SD_ROOM_ZTRICK        155 /* 1 = depth clear replaced by the alternating depth range */
#define SD_PROBE_PLACE_EXPECT 156 /* placement probe (D2): the constant texel written this run (the sampled one must match it) */
#define SD_ROOM_BIND_WAIT_US  157 /* ROOM: Core1 time waiting for bind fences before the next DDR chunk (D2a), sum us */
/* frame diagnostics (D2b): filled by rixframe when a chunk or bind fence times out; SD_FD_PHASE is written by the application before each phase */
#define SD_FD_PHASE           160 /* application phase code (ROOM: 0x100 begin, 0x200+k bind k, 0x300 world pass 1, 0x310 pass 2, 0x400 models, 0x500 effects, 0x600 end, 0x700 frame wait) */
#define SD_FD_CHUNKS          161 /* chunks submitted in the frame so far */
#define SD_FD_LAST_CSEQ       162 /* sequence of the last chunk fence submitted */
#define SD_FD_CSEQ_PIX        163 /* pixel at the chunk-fence position of the frame's colour buffer */
#define SD_FD_BIND_SEQ        164 /* sequence of the pending bind fence (0 = none) */
#define SD_FD_BIND_PIX        165 /* pixel at the bind-fence position */
#define SD_FD_FRAME_PIX       166 /* pixel 0 of line 480 (frame fence position) */
#define SD_FD_FIFO_VAC        167 /* FIFO vacancy at the timeout (508 = the FSC took everything) */
#define SD_FD_FIFO_ISR        168 /* FIFO ISR at the timeout */
#define SD_FD_ALIVE           169 /* liveness after the timeout: 1 = 2 NOPs taken (parser alive), 2 = not taken (hung), 0 = not run */
#define SD_FD_WORDS           170 /* words of the last chunk submitted */
#define SD_FD_TRIS            171 /* triangles of the frame so far */
#define SD_FD_LAST_OPS        172 /* first command word of the last chunk << 0 (its op is the high nibble) */
#define SD_FD_FRAME           173 /* frame number */
/* Core1 exception record (D2c): the payload installs its own vector table; an undefined
   instruction, prefetch abort or data abort ends the run with RIXE_EXCEPTION + type instead of
   falling into the firmware's vectors (D2b ROOM A DLFIFO: Core1 "reset loop", empty log) */
#define SD_EXC_TYPE           174 /* 0 none, 1 undefined, 2 SVC, 3 prefetch abort, 4 data abort, 5 reserved */
#define SD_EXC_PC             175 /* faulting instruction */
#define SD_EXC_FAR            176 /* DFAR (data abort) / IFAR (prefetch abort) */
#define SD_EXC_FSR            177 /* DFSR / IFSR */
#define SD_EXC_SPSR           178 /* CPSR of the faulting context */
#define SD_EXC_IRQS           179 /* IRQs taken by the payload's vectors << 0, FIQs << 16 (expected 0: Core1 runs masked) */
#define SD_EXC_STAGE          180 /* SH_STAGE at the exception */
/* DDR-list read probes (D2c, PROBE): can a list fetched by the FSC carry fragments that READ
   the framebuffer (depth test, blending)?  D2b: the first depth-tested chunk of ROOM hung the
   parser while every DDR list before it carried writes only.  Smallest first, stop at the first
   hang: 9 depth 2 tris, 10 blend 2 tris, 11 depth 12 tris (~1 FIFO), 12 depth 40 tris (the ROOM
   chunk), 13 depth + texture 40 tris (ROOM's exact mix, texture bound through the FIFO before) */
#define SD_PROBE_DDR_RUN      181 /* bit k = probe 9+k was sent */
#define SD_PROBE_DDR_RES      182 /* nibble k = probe 9+k: 1 works, 2 wrong pixel, 3 hung (fence never passed), 4 skipped (no DDR lists) */
#define SD_PROBE_DDR_PIX0     183 /* probe 9 pixel << 16 | probe 10 pixel */
#define SD_PROBE_DDR_PIX1     184 /* probe 11 << 16 | probe 12 */
#define SD_PROBE_DDR_PIX2     185 /* probe 13 << 16 | words of the largest list */
/* PL330 -> ST0 FIFO transport self-test (D2d, SELFTEST DLDMA or at init with DLDMA): the DMAC writes
   command words into TDFD (fixed address) instead of Core1's 1.1 us/word MMIO stores */
#define SD_DMAFIFO_STATUS     186 /* 0 not run, 1 usable with 1-beat stores, 2 usable with 16-beat FIXED bursts, 3 unusable (count test failed or DMA fault), 4 integrity failed */
#define SD_DMAFIFO_NS_B1      187 /* ns per word, 1-beat stores (0 = not measured) */
#define SD_DMAFIFO_NS_B16     188 /* ns per word, 16-beat FIXED bursts (0 = not measured / unusable) */
#define SD_DMAFIFO_COUNT      189 /* count test: vacancy after 16 words by B16 << 16 | by B1 (492 expected each) */
#define SD_DMAFIFO_FENCE_PIX  190 /* integrity test: fence pixel written by a packet carried entirely by the DMA (D0D1 expected) */
#define SD_DMAFIFO_WORDS      191 /* words moved in the throughput test */
#define SD_COUNT              192

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

/* ---- error codes (low 16 bits; the demos report 0x5249xxxx) -------------- */
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
#define RIXE_CONFIG           0xE128UL  /* librixgpu: rix_config lacks stats/sd/dma_prog */
#define RIXE_BIND_TIMEOUT     0xE129UL  /* rixframe: the fence behind a texture bind never passed (GPU stopped at the OP_TEXTURE) */
#define RIXE_CHUNK_TIMEOUT    0xE12AUL  /* rixframe (SYNC): the fence of a chunk never passed */
#define RIXE_GEOMETRY         0xE12CUL  /* librixgpu 1.4: rix_set_geometry refused (size out of range, or the ring already set up) */
#define RIXE_DEFER            0xE12BUL  /* rixframe 1.3 (deferred): the queue could not move for 2 s, or a submission broke rule 11 (refused) */
#define RIXE_TEX_POOL         0xE126UL  /* librixgpu: texture pool exhausted or misconfigured */
#define RIXE_TEX_SIZE         0xE127UL  /* librixgpu: texture size not a power of two in 8..256 */
#define RIXE_DMAFIFO          0xE132UL  /* D2d: the PL330 -> FIFO transport failed its self-test (SD_DMAFIFO_STATUS) */
#define RIXE_EXCEPTION        0xE1F0UL  /* + SD_EXC_TYPE: Core1 took a CPU exception (SD_EXC_*), the run is over */

#endif /* RIX_DIAG_H */
