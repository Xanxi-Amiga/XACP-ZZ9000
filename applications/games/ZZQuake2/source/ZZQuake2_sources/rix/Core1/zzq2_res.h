/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2_res.h - ZZQuake2: size of the Quake II frame (0.7).
 *
 * The screen is opened by the launcher before Core1 starts and the
 * release presentation code (zzquake_platform.c) is compiled for one
 * size (QUAKEGENERIC_RES_X / _RES_Y). This header gives the same size
 * to the Quake II side, with the matching sw_mode of ref_soft:
 *
 *   default            640 x 480   sw_mode 3   zzq2a0.bin
 *   ZZQ2_RES=320       320 x 240   sw_mode 0   zzq2_320.bin
 *   ZZQ2_RES=800       800 x 600   sw_mode 4   zzq2_800.bin
 *
 * build_arm.sh passes all four macros for a non-default size, and the
 * release objects get the same QUAKEGENERIC_RES_X / _RES_Y.
 *
 * ASCII only.
 */
#ifndef ZZQ2_RES_H
#define ZZQ2_RES_H

#ifndef ZZQ2_VID_W
#define ZZQ2_VID_W      640
#define ZZQ2_VID_H      480
#define ZZQ2_SW_MODE    3
#define ZZQ2_SW_MODE_S  "3"
#endif

#endif
