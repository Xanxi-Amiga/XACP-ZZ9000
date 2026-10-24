/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
/* ZZOpenLara A2.6: the size of the P96 screen the launcher opened (SH_FB_WIDTH x SH_FB_HEIGHT): 640x480, or
 * 320x240 when the launcher was given RES=320 (it passes "320" to the release launcher, as ZZQuake2 does).
 * Used by the software blobs and by the RasterIX 320 blob; ol_init refuses any other size (E311).
 * ASCII only.
 */
#include "zzquake_config.h"

typedef unsigned int u32;

extern volatile u32 *shared;

void ol_screen_size(int *w, int *h)
{
    *w = (int)shared[SH_FB_WIDTH];
    *h = (int)shared[SH_FB_HEIGHT];
}
