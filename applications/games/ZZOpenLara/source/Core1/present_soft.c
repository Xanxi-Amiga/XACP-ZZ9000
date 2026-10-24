/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
/* ZZOpenLara A2.4: presentation of the software renderers (ZZOpenLaraSW, ZZOpenLaraTGL).
 *
 * The picture is drawn at the size of the screen the launcher opened: 640x480, or a real 320x240
 * screen when the launcher was given RES=320 (it then passes "320" to the release launcher, as
 * ZZQuake2 does). present.c, built with -DOL_SOFT_PRESENT for these blobs, copies it 1:1 at that
 * size: RGB565 (SW, zzq2_present_rgb565) or 0x00RRGGBB (TinyGL, zzq2_present32).
 *
 * Also the GPU entry point that the common code names (platform.c, ol_fail): no GPU in these blobs.
 * ASCII only.
 */
#include "zzquake_config.h"

typedef unsigned int u32;

extern volatile u32 *shared;
extern void zzq2_present_rgb565(const unsigned short *pixels, unsigned pitch_bytes);
extern void ol_fail(const char *stage, int a, int b) __attribute__((noreturn));

void zzq2_present_rgb565_wh(const unsigned short *pixels, int w, int h)
{
    if (w != (int)shared[SH_FB_WIDTH] || h != (int)shared[SH_FB_HEIGHT]) ol_fail("E310 PRESENT SIZE", w, h);
    zzq2_present_rgb565(pixels, (unsigned)w * 2u);
}

void zz_rix_close(void)
{
}

/* A2.12: the global timer (low word, 333 MHz) for Core1/ol_memprof.c in the software blob (the RasterIX
   blobs take librixgpu's); read only, as the release diagnostic. Built for SW only (the TGL blob unchanged) */
#ifdef OL_PRESENT_SW
u32 rix_now(void)
{
    return *(volatile u32 *)0xF8F00200u;
}
#endif
