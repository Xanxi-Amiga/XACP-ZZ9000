/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2_diag.h - ZZQuake2 A0 breadcrumbs, published in the EXISTING
 * shared slot SH_DIAG (no new slot, no new protocol).
 *
 * The release uses 0xA0xx / 0xA1xx. ZZQuake2 uses 0x2Exx so the two
 * can never be confused in a launcher log ("diag=0x2e..").
 *
 * ASCII only.
 */
#ifndef ZZQ2_DIAG_H
#define ZZQ2_DIAG_H

/* --- gates asked for by the A0 brief ----------------------------- */
#define ZZQ2_DIAG_ENTER        0x2E00u  /* Q2_ENTER  : QG_Create call point reached,
                                           preload handshake seen complete        */
#define ZZQ2_DIAG_ENTER_NOPRE  0x2E0Fu  /* Q2_ENTER  : reached, but SH_PRELOAD_STATE
                                           was NOT "START" (unexpected with the
                                           launcher; execution continues)        */
#define ZZQ2_DIAG_INIT_OK      0x2EC0u  /* Q2_INIT_OK: Quake2_Init() returned     */
#define ZZQ2_DIAG_FIRST_FRAME  0x2EF0u  /* Q2_FIRST_FRAME: first SWimp_EndFrame() */
#define ZZQ2_DIAG_PRESENTED    0x2EF1u  /* first frame that passed the release
                                           presentation gate (SH_FRAME moved)     */

/* --- finer steps between Q2_ENTER and Q2_INIT_OK ----------------- */
#define ZZQ2_DIAG_BSS_CLEARED  0x2E01u  /* .q2bss cleared                         */
#define ZZQ2_DIAG_ARENA_READY  0x2E02u  /* arena allocator ready                  */
#define ZZQ2_DIAG_INIT_CALL    0x2E03u  /* about to call Quake2_Init()            */
#define ZZQ2_DIAG_FIRST_TIME   0x2E10u  /* first QG_Milliseconds() (Qcommon_Init) */
#define ZZQ2_DIAG_FIRST_FOPEN  0x2E11u  /* first file open (FS_InitFilesystem)    */
#define ZZQ2_DIAG_SWIMP_INIT   0x2E20u  /* SWimp_Init (ref_soft R_Init)           */
#define ZZQ2_DIAG_SWIMP_MODE   0x2E21u  /* SWimp_SetMode                          */
#define ZZQ2_DIAG_SWIMP_PAL    0x2E22u  /* first SWimp_SetPalette                 */

/* --- end of run -------------------------------------------------- */
#define ZZQ2_DIAG_QUIT         0x2EE0u  /* Sys_Quit (engine "quit")               */
#define ZZQ2_DIAG_STOP         0x2EE1u  /* main loop left on the 68k STOP command */

#endif
