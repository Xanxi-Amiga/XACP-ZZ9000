/* rixframe.h - librixgpu 1.0 - frames as a ring of display-list chunks
 *
 * A frame is built as a sequence of chunks (16 KiB each in the demos): each
 * chunk is a DDR display list (FSC MEM->ST1, two FIFO words) closed by its
 * own fence (line 480 of the target colour buffer, pixels fence_x..+63,
 * 16-bit sequence); a ring slot is rewritten only once the GPU has fenced
 * the chunk that last used it, so the GPU draws chunk 1 while Core1 builds
 * chunk 2 (ZZGear C1: 10.5 chunks of ~40 000 words per frame, 163 fenced
 * frames/s on G4B).  The last chunk also carries the frame fence (pixels
 * 0..63) the caller waits for with rix_frame_wait().
 *
 * State changes (features, fragment pipeline, TMU env/config, scissor...)
 * are ordinary OP_RENDER_CONFIG words inside the stream.  A texture bind is
 * the exception: the page table must travel through the FIFO (B1 run 5), so
 * rix_frame_texture() closes the current chunk, sends config + table as a
 * FIFO packet (ordered behind the chunk by the FSC) and opens a new chunk.
 *
 * Hardware rules applied here: colour memsets re-assert the colour mask,
 * depth memsets the depth mask (register 3), 64 drain NOPs before each
 * fence in DDR mode, never a COMMIT/READ of the depth buffer.
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef RIXFRAME_H
#define RIXFRAME_H
#include "rixgpu.h"
#include "rixtex.h"

#define RIX_RING_MAX_SLOTS 32u

typedef struct {
    u32 ring_base;        /* 64-byte aligned DDR the GPU can fetch (window or arena) */
    u32 ring_slots;       /* 2..32 */
    u32 chunk_bytes;      /* multiple of 64; 16 KiB in the demos */
    u32 fence_x;          /* chunk fence pixels on line 480: fence_x..fence_x+63 (64 in the demos; frame fence uses 0..63) */
    const u32 *color_bufs; u32 color_count;   /* 640x481 colour buffers (their chunk-fence pixels are zeroed) */
} rix_ring_cfg;

typedef struct {
    u32 color;            /* colour buffer (640x481x16, in the 68k-visible window if it is to be shown) */
    u32 zbuf;             /* depth buffer (640x481x16) or 0 */
    u32 features;         /* REG 0 for the frame body (RIX_FEAT_TMU0 | RIX_FEAT_DEPTH_TEST | ...), scissor bit managed here */
    u32 fragment;         /* REG 3: depth func/mask, alpha, blend, colour mask (RIX_FRAGMENT_A07B | ...) */
    u32 clear_rgba;       /* RIX_RGBA() */
    u32 clear_depth16;    /* 0xFFFF = far */
    int clear_color;      /* 1 = memset the colour buffer (rect below, or full) */
    int clear_depth;      /* 1 = memset the depth buffer (same rect) */
    u32 rect_x0, rect_y0, rect_x1, rect_y1;   /* clear rectangle, end exclusive; all 0 = full 640x481 */
} rix_frame_desc;

typedef struct {
    rix_dl dl;
    u32 color, features, fragment;
    u32 slot;
    int open;
    /* statistics of the frame */
    u32 chunks, words, tris, ringwait_us, first_submit_tick, tex_binds;
} rix_frame;

int  rix_ring_init(const rix_ring_cfg *cfg);       /* after rix_init(); zeroes the chunk fence pixels */
void rix_ring_reset(void);                         /* forget every slot (buffers must be idle) */
u32  rix_ring_seq(void);

int  rix_frame_begin(rix_frame *f, const rix_frame_desc *d);
int  rix_frame_tri(rix_frame *f, const u32 *desc39);
int  rix_frame_reg(rix_frame *f, u32 reg, u32 val);   /* any OP_RENDER_CONFIG register, in stream order */
int  rix_frame_features(rix_frame *f, u32 features);
int  rix_frame_fragment(rix_frame *f, u32 fragment);
int  rix_frame_scissor(rix_frame *f, u32 x0, u32 y0, u32 x1, u32 y1);   /* x1 == 0: scissor off */
int  rix_frame_texture(rix_frame *f, const rix_texture *t);              /* bind on TMU0: chunk close, FIFO table, chunk open */
int  rix_frame_end(rix_frame *f, u32 fence_value16);                    /* chunk fence + frame fence, submit */
int  rix_frame_wait(u32 color, u32 fence_value16, u32 timeout_us, volatile u32 *stop, u32 *polls);

/* fragment register helpers */
u32  rix_fragment(u32 depth_func, int depth_write, u32 alpha_func, u32 alpha_ref8, u32 blend_s, u32 blend_d);

#endif
