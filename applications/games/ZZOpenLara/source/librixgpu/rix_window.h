/* rix_window.h - librixgpu 1.1 - public: the G4B GPU window layout the init
 * ladder (rix_init) is validated on, and the 640x481 buffer geometry.
 *
 * The window 0x06600000..0x06800000 is the DDR range the FAST1 firmware
 * allows the 68k to use for the GPU and that the 68k can display (PAN):
 * colour buffers that are shown MUST be here.  rix_init's ladder uses FB0,
 * DL2 and SCRATCH of this layout; the frame, texture and ring APIs accept
 * any DDR address (placement probe: Core1 arena 0x30000000 works).
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef RIX_WINDOW_H
#define RIX_WINDOW_H

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
#define RIX_BIND_FENCE_X      128UL           /* texture-bind fences (D2a): line 480, pixels 128..191 */
#define RIX_STAGE_ARM         0x067F0000UL    /* firmware staging: never touched */
#define RIX_FB_TO_68K         0x00200000UL    /* 68k offset = ARM - this */
#define RIX_STRESS_DL_ARM     RIX_FB2_ARM     /* stress mode: FB2 holds the big list */
#define RIX_STRESS_DL_BYTES   RIX_FB_STRIDE

/* screen clip rectangle for rixclip on a 640x481 buffer (RasterIX y-up): row 0 is the fence row, never
   drawn.  rix_tri_desc() ends the bounding box at floor(max + 1.5) (edge sample at integer pixel
   coordinates, +1 column/row of margin): a vertex at 639.46875 / 480.46875 gives an end of 640 / 481,
   the largest the demos ever sent to the hardware; 639.5 would already give 641 (D2 review).  The
   edge samples at x = 639 and y = 480 are still inside the triangle (no lost column/row). */
#define RIX_CLIP_X0 0.0f
#define RIX_CLIP_Y0 1.0f
#define RIX_CLIP_X1 639.46875f
#define RIX_CLIP_Y1 480.46875f

/* ---- 1.4 (ZZOpenLaraRIX A2.15): frame layout ------------------------
 * The init ladder (rix_init) always runs on the 640x481 layout above.
 * A renderer may then give its frames another buffer geometry with
 * rix_set_frame_layout(w, h_alloc) (h_alloc = shown lines + the fence
 * line, the last memory line): the fences, the full-buffer clears, the
 * RESOLUTION register of each frame and ZZGL's clip follow it.  Default:
 * 640 x 481 (the 1.1 behaviour).  Set it once, before rix_ring_init. */
extern unsigned int rix_lay_w, rix_lay_h, rix_lay_pitch, rix_lay_fence_off;   /* u32 */
void rix_set_frame_layout(unsigned int w, unsigned int h_alloc);

#endif /* RIX_WINDOW_H */
