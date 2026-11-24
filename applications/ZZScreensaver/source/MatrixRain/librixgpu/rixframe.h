/* rixframe.h - librixgpu 1.1 - frames as a ring of display-list chunks
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
 * the exception: the page table must never sit inside a frame chunk (B1 run
 * 5 on G4B, D2e on G4C), so rix_frame_texture() closes the current chunk,
 * sends config + table + bind fence as a FIFO packet or as a short DDR list
 * of its own (ordered behind the chunk by the FSC) and opens a new chunk.
 *
 * Hardware rules applied here: colour memsets re-assert the colour mask,
 * depth memsets the depth mask (register 3), 64 drain NOPs before each
 * fence in DDR mode, never a COMMIT/READ of the depth buffer, and (D2a,
 * run 12) no DDR chunk submitted behind a texture bind until the fence that
 * follows the bind has passed: the FrameStreamingCore fetches lists AND
 * texture pages, a list fetch stalled on the parser while the parser waits
 * for pages is the B1 run 5 deadlock.
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
    const u32 *color_bufs; u32 color_count;   /* 640x481 colour buffers (their chunk-fence pixels are zeroed); must be Non-Cacheable for Core1 (fences are polled) */
    u32 ring_cacheable;   /* 1 when the ring lives in write-back memory (Core1 arena): each chunk is cleaned before submission (D2, not yet run on the hardware; the demos keep the ring in the window) */
    u32 bind_base;        /* 1.2.1 (BINDLIST): 2 x 512 bytes of 64-byte aligned DDR for the short bind lists, or 0 (then BINDLIST is refused: RIXE_RING_CFG) */
    u32 bind_cacheable;   /* 1 when bind_base is write-back memory: each bind list is cleaned before submission */
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
    u32 chunks, words, tris, ringwait_us, first_submit_tick;
    u32 tex_binds, tex_skipped, tex_bind_us;   /* binds sent / binds skipped (already in the TMU) / Core1 time of the binds */
    u32 tex_bind_wait_us;                      /* of which waits: ring slot (also in ringwait_us) and bind fences */
    u32 bind_wait_us;                          /* of which: waiting for the bind fences before the next DDR chunk (G4B rule, D2a) */
    u32 bind_seq;                              /* pending bind fence (0 = none) */
    /* 1.3 deferred mode only (rix_frame_set_deferred): */
    u32 dq_peak;                               /* most events (draw chunks + binds) waiting for submission at once */
    u32 dq_held;                               /* draw chunks closed while a bind was still in the GPU (held, not submitted) */
    u32 dq_wait_us;                            /* Core1 waiting for the queue to move: ring slot, full queue, end of frame */
    u32 dq_tex_us;                             /* of which: drains before a texture upload (rix_frame_drain) */
    u32 dq_end_us;                             /* of which: draining the queue at the end of the frame */
    u32 dq_refused;                            /* submissions the rule-11 guard refused (must stay 0) */
    u32 dq_starved;                            /* 1.3.1: draw chunks submitted when the GPU had already finished everything before them (it was idle) */
    int sc_on; u32 sc_x0, sc_y0, sc_x1, sc_y1; /* scissor restored after every chunk boundary */
} rix_frame;

int  rix_ring_init(const rix_ring_cfg *cfg);       /* after rix_init(); zeroes the chunk fence pixels */
void rix_frame_set_sync(int on);                   /* SYNC (ladder) mode: every chunk fence and bind fence waited right after submission; a hang names its chunk (SD_FD_*) */
void rix_ring_reset(void);                         /* forget every slot (buffers must be idle) */
u32  rix_ring_seq(void);

int  rix_frame_begin(rix_frame *f, const rix_frame_desc *d);
int  rix_frame_tri(rix_frame *f, const u32 *desc39);
int  rix_frame_clear(rix_frame *f, int color, u32 rgba, int depth, u32 depth16, u32 x0, u32 y0, u32 x1, u32 y1);   /* ZZGL: memsets in the middle of the stream (rect end exclusive, 0 0 0 0 = full) */
int  rix_frame_reg(rix_frame *f, u32 reg, u32 val);   /* any OP_RENDER_CONFIG register, in stream order */
int  rix_frame_features(rix_frame *f, u32 features);
int  rix_frame_fragment(rix_frame *f, u32 fragment);
int  rix_frame_scissor(rix_frame *f, u32 x0, u32 y0, u32 x1, u32 y1);   /* x1 == 0: scissor off */
int  rix_frame_texture(rix_frame *f, const rix_texture *t);              /* bind on TMU0: chunk close, table as a FIFO packet or a short DDR list (bind mode), chunk open; skipped when already loaded */
void rix_frame_texture_invalidate(u32 base_arm);                         /* forget the loaded texture (0 = whatever it is) */
int  rix_frame_end(rix_frame *f, u32 fence_value16);                    /* chunk fence + frame fence, submit */
int  rix_frame_wait(u32 color, u32 fence_value16, u32 timeout_us, volatile u32 *stop, u32 *polls);
/* 1.2.1: how a texture bind travels.  Both modes keep rule 11 (no DDR chunk submitted behind the
 * bind until its fence passed).  RIX_BIND_FIFO: TMU config + page table + bind fence as a FIFO
 * packet (up to 80 words of CPU MMIO per bind; G4B and G4C, the measured D2d path).  RIX_BIND_LIST:
 * the same 80 words as a short DDR list of their own, submitted with 2 FIFO words (the exact list
 * ladder L15 completes on G4C).  There is no "bind inside the frame chunk" mode: the FSC fetches
 * both the lists and the texture pages, and a bind the parser reaches while the FSC is still
 * fetching the rest of a long chunk deadlocks it (D2e, 14/10: first ROOM A frame, power cycle);
 * L15 passes only because its list is short enough to be fetched whole before the parser starts.
 * The application selects the mode once the ring is configured; default RIX_BIND_FIFO.  RIX_BIND_LIST
 * needs rix_ring_cfg.bind_base (else RIXE_RING_CFG). */
#define RIX_BIND_FIFO 0
#define RIX_BIND_LIST 1
#define RIX_BIND_LIST_WORDS 80u   /* 2 (TMU config) + 1 + 64 pages + 13 (bind fence): never more, L15 proved exactly this size */
int  rix_frame_set_bind_mode(int mode);
int  rix_frame_bind_mode(void);
/* 1.3: deferred submission.  Off (default): every chunk is submitted when it is closed, after the
 * fence of the bind before it (the 1.2.3 behaviour, unchanged).  On: closed chunks and binds go to
 * an ordered queue and are submitted by rix_frame_pump() as the GPU allows, so Core1 goes on
 * building the next chunks while the GPU works.  Rule 11 is kept by the scheduler: at most one bind
 * in the GPU at a time, and no chunk submitted while a bind is there (its fence not passed); the
 * queue keeps the stream order exactly.  Each chunk keeps its ring slot until its own fence passed
 * (as before); a bind keeps a private copy of its words until it is submitted, then goes through
 * one of the two bind lists (the other one is free: the bind before it passed).  The skip of a bind
 * of the texture already loaded is off in this mode.  Switch only between frames. */
void rix_frame_set_deferred(int on);
int  rix_frame_deferred(void);
int  rix_frame_pump(rix_frame *f);      /* submit what can be submitted now (cheap; ZZGL calls it at each batch) */
/* 1.3.1: the sequence of the last bind rix_frame_texture() queued or submitted (0 = none yet), and whether a
 * bind of the frame being built has executed (its fence passed: the TMU has read its pages).  A texture whose
 * last bind has not passed must not have its pages rewritten (ZZGL makes a new version instead). */
u32  rix_frame_last_bind_seq(void);
/* 1.3.2: rix_frame_tri pumps the deferred queue every n triangles (power of two, 1..256; default 32);
 * rix_frame_flush closes the chunk being built and opens the next one (the closed one is submitted at
 * once when nothing blocks it: ZZGL uses it after the frame's depth clear) */
void rix_frame_set_pump_interval(u32 n);
int  rix_frame_flush(rix_frame *f);
int  rix_frame_bind_passed(rix_frame *f, u32 seq);
int  rix_frame_drain(rix_frame *f);     /* deferred: everything submitted and the last bind's fence passed, so that no
                                           texture page is read any more by a pending bind (call before writing texels);
                                           direct mode: returns at once */
/* D2c: fill SD_FD_* for the frame being built (the application's exception handler calls it) */
void rix_frame_diag_last(void);

/* fragment register helpers */
u32  rix_fragment(u32 depth_func, int depth_write, u32 alpha_func, u32 alpha_ref8, u32 blend_s, u32 blend_d);

#endif
