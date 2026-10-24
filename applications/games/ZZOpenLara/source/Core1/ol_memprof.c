/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
/* ZZOpenLara A2.7: what memcpy / memset / memmove cost the RasterIX blobs.
 *
 * The release's mem* (runtime/safe_mem.c, byte by byte, -O0, linked first by rule) are not changed.
 * tools/build.py renames the calls of OUR objects only (Core1, ZZGL, librixgpu, the engine; not the
 * release runtime/) with objcopy --redefine-sym memcpy=ol_prof_memcpy (and memset, memmove): these
 * wrappers count the calls, the bytes and the global-timer ticks, then call the release function.
 * Compiled with -fno-builtin, and never renamed itself, so the calls below reach the real ones.
 * Read by Core1/ol_game.cpp (olstat.txt). ASCII only.
 */
#include <stddef.h>

typedef unsigned int u32;

extern void *memcpy(void *d, const void *s, size_t n);
extern void *memset(void *d, int c, size_t n);
extern void *memmove(void *d, const void *s, size_t n);
extern u32 rix_now(void);

/* [0] memcpy [1] memset [2] memmove: calls, bytes, global-timer ticks (333 MHz: 64 bits) of the run */
u32 ol_mem_calls[3];
unsigned long long ol_mem_bytes[3], ol_mem_ticks[3];

void *ol_prof_memcpy(void *d, const void *s, size_t n)
{
    u32 t0 = rix_now();
    void *r = memcpy(d, s, n);
    ol_mem_ticks[0] += (u32)(rix_now() - t0);
    ol_mem_calls[0]++;
    ol_mem_bytes[0] += n;
    return r;
}

void *ol_prof_memset(void *d, int c, size_t n)
{
    u32 t0 = rix_now();
    void *r = memset(d, c, n);
    ol_mem_ticks[1] += (u32)(rix_now() - t0);
    ol_mem_calls[1]++;
    ol_mem_bytes[1] += n;
    return r;
}

void *ol_prof_memmove(void *d, const void *s, size_t n)
{
    u32 t0 = rix_now();
    void *r = memmove(d, s, n);
    ol_mem_ticks[2] += (u32)(rix_now() - t0);
    ol_mem_calls[2]++;
    ol_mem_bytes[2] += n;
    return r;
}
