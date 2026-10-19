/* rix_shared.h - ZZGear RasterIX D2 (demo launcher <-> Core1 mailbox ABI; library parts in rix_diag.h / rix_window.h)
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
#define SH_PARAM2             12  /* 68k->Core1: Solar Winds preset 1..6    */
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
#define SH_SELFTEST_DATA      416 /* u32[192] self-test numbers (D1: 144, D2: 176, D2c: 192) */
#define SH_END                608 /* first slot Core1 never writes (D1: 560, D2: 592) */
#define SH_COPY_LINE0         608 /* 68k-private: OP_MEMCPY destination of line 0 (32-byte aligned) */
#define SH_COPY_BLOCK         624 /* 68k-private: OP_MEMCPY destination, slots 16..607 (ends < 0x04702000 FWPAN) */

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
/* scenes (SH_PARAM2), D2 */
#define RIX_SCENE_GEAR        0UL
#define RIX_SCENE_ROOM_A      1UL
#define RIX_SCENE_ROOM_B      2UL
#define RIX_SCENE_ROOM_C      3UL
#define RIX_SCENE_ROOM_B32    4UL
#define RIX_SCENE_GL          5UL   /* ZZGL 0.1 demo (zzgl/zzgldemo.c): the ref_gl OpenGL subset on librixgpu */

#include "rix_diag.h"      /* RIXF_*, RIXCAP_*, RS_*, SD_*, LAD_*, RIXE_*: library-owned, shared with the 68k */
#include "rix_window.h"    /* G4B window layout: library-owned */

/* ---- OPTIONAL Core0 presentation mailbox (FAST1_OPTIONAL_PATCH) ----- */
#define RIX_FWPAN_REQ_ARM     0x04702000UL
#define RIX_FWPAN_ACK_ARM     0x04703000UL
#define RIX_FWPAN_MAGIC       0x52495850UL   /* RIXP */
#define RIX_FWPAN_HELLO       0x46505731UL   /* FPW1 */

#endif /* RIX_SHARED_H */
