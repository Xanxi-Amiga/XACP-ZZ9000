/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzqgl_diag.h - ZZQuakeGL breadcrumbs, published in the EXISTING shared
 * slot SH_DIAG (no new slot, no new protocol), as ZZQuake2 A0 did with
 * its 0x2Exx codes.  The release uses 0xA0xx / 0xA1xx; ZZQuakeGL uses
 * 0x61xx ("GL") so the three can never be confused in a launcher log.
 *
 * ASCII only.
 */
#ifndef ZZQGL_DIAG_H
#define ZZQGL_DIAG_H

/* --- the three gates of the brief (J2) ------------------------------ */
#define ZZQGL_DIAG_ENTER        0x6100u  /* Q1_ENTER: QG_Create reached, preload complete  */
#define ZZQGL_DIAG_ENTER_NOPRE  0x610Fu  /* Q1_ENTER: reached, SH_PRELOAD_STATE not START   */
#define ZZQGL_DIAG_INIT_OK      0x61C0u  /* Q1_INIT_OK: Host_Init returned, GPU open         */
#define ZZQGL_DIAG_FIRST_FRAME  0x61F0u  /* Q1_FIRST_FRAME: first GL_EndRendering            */
#define ZZQGL_DIAG_PRESENTED    0x61F1u  /* first frame through the release gate (SH_FRAME) */

/* --- finer steps between Q1_ENTER and Q1_INIT_OK ---------------------- */
#define ZZQGL_DIAG_BSS_CLEARED  0x6101u  /* .glbss cleared                                   */
#define ZZQGL_DIAG_LAYOUT_OK    0x6102u  /* pak / GPU reservation / .glbss checks passed     */
#define ZZQGL_DIAG_INIT_CALL    0x6103u  /* about to call Host_Init                          */
#define ZZQGL_DIAG_VID_INIT     0x6120u  /* VID_Init (GLQuake) entered                       */
#define ZZQGL_DIAG_GPU_OPEN     0x6121u  /* zz_rix_open returned (ladder, ring, ZZGL)        */
#define ZZQGL_DIAG_GL_INIT      0x6122u  /* GL_Init done (state defaults set)                */
#define ZZQGL_DIAG_PALETTE      0x6123u  /* VID_SetPalette done (d_8to24table)               */

/* --- 0.2: the launcher's watchdog during Host_Init ---------------------- */
#define ZZQGL_DIAG_EARLY_FRAME  0x61EFu  /* Q1_EARLY_FRAME: black frame handed over from VID_Init, before the
                                            self-test and the texture uploads; from here the keepalive below
                                            feeds the launcher's SH_RENDERS during the rest of Host_Init */
#define ZZQGL_DIAG_KEEPALIVE    0x61FEu  /* the last frame handed over again during a load (diagnostic only) */

/* --- end of run ------------------------------------------------------- */
#define ZZQGL_DIAG_QUIT         0x61E0u  /* Sys_Quit (engine "quit")                         */
#define ZZQGL_DIAG_STOP         0x61E1u  /* main loop left on the 68k STOP command           */

/* --- 0.2: init timeline ------------------------------------------------
   zzq_us_now() at each step (0 = not reached), kept in zzqgl_tl[] (zzqgl_qgmain.c) and written
   at the end of id1/rixstat.txt, in ms from QG_Create.  Measures where the seconds before the
   first image go (the launcher ends the run after 5 s + 3 s without an image). */
enum {
    ZZQGL_TL_CREATE = 0,    /* QG_Create entered                                  */
    ZZQGL_TL_INIT_CALL,     /* Host_Init called                                   */
    ZZQGL_TL_VID_INIT,      /* VID_Init entered                                   */
    ZZQGL_TL_GPU_OPEN,      /* zz_rix_open returned (MMU, ladder, ring, ZZGL)     */
    ZZQGL_TL_EARLY,         /* early black frame handed over                      */
    ZZQGL_TL_TESTS,         /* texel self-test and micro-benchmark done           */
    ZZQGL_TL_VID_DONE,      /* VID_Init returned                                  */
    ZZQGL_TL_INIT_OK,       /* Host_Init returned                                 */
    ZZQGL_TL_FIRST_FRAME,   /* first real frame handed over (console)             */
    ZZQGL_TL_NEWMAP,        /* first R_NewMap (first map loaded)                  */
    ZZQGL_TL_WORLD,         /* first frame with the world drawn handed over       */
    ZZQGL_TL_N
};
extern unsigned int zzqgl_tl[ZZQGL_TL_N];
extern unsigned int zzq_us_now(void);
#define ZZQGL_TL_MARK(i) do { if (!zzqgl_tl[(i)]) zzqgl_tl[(i)] = zzq_us_now(); } while (0)

#endif
