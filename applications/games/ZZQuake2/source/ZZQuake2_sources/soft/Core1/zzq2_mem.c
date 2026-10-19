/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2_mem.c - ZZQuake2 A0 memory: the Quake II heap in the ARM-only
 * arena, the Quake II Hunk_* API, and the large-allocation side of
 * malloc.
 *
 * WHY THIS FILE EXISTS (measured, see README_A0.txt):
 *
 *   other/q_hunk.c of quake2generic implements Hunk_Begin(maxsize) as
 *   malloc(maxsize) + memset, and Hunk_End never gives anything back.
 *   ref_soft reserves 16 MiB per brush model and 2 MiB per alias
 *   model. On a desktop that is lazy virtual memory; on the release
 *   heap (16 MiB at 0x07000000, bounded by _sbrk in zzquake_main.c,
 *   which is not modified) the first brush model cannot even be
 *   reserved.
 *
 * WHAT IT USES:
 *
 *   [ HEAP_START , HEAP_LIMIT )  inside the arena of the release
 *   (ZZQ_PACK_ARENA_BASE..ZZQ_PACK_ARENA_END, already mapped WB/WA by
 *   the release mmu_init_zzquake()):
 *
 *     HEAP_START = first MiB boundary at or after the real end of the
 *                  preloaded pak
 *     HEAP_LIMIT = start of .q2bss (zzq2a0.ld)
 *
 *   Both are computed and checked by QG_Create (zzq2_glue.c) and
 *   handed to zzq2_arena_init(). Blocks are given out lowest address
 *   first: what is really used stays next to the pak, far from .q2bss.
 *   No address of the release is changed and no MMU section is added.
 *
 * THE RULE: nothing is ever handed out at or above HEAP_LIMIT. A
 * request that does not fit ends the run through zzq2_heap_fail()
 * (zzq2_glue.c: the release Sys_Error path, with the size asked and
 * the state of the heap). It never returns a block that would reach
 * .q2bss and it never returns NULL to code that does not test it.
 *
 * The allocator is deliberately simple: a table of used blocks sorted
 * by address, first fit, 4 KiB granularity. A few hundred blocks at
 * most are alive at any time.
 *
 * This file writes no shared slot.
 *
 * ASCII only.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int u32;

/* zzq2_glue.c. Does not return. */
extern void zzq2_heap_fail(const char *what, u32 asked, u32 rounded,
                           u32 used, u32 peak, u32 total);

#define ZZQ2_BLK_ALIGN   4096u
#define ZZQ2_MAX_BLOCKS  2048
#define ZZQ2_BIG_ALLOC   (64u * 1024u)

typedef struct { u32 base; u32 size; } zzq2_blk_t;

static zzq2_blk_t g_blk[ZZQ2_MAX_BLOCKS];
static int g_nblk;
static u32 g_lo, g_hi;          /* HEAP_START, HEAP_LIMIT              */
static u32 g_used;              /* bytes in blocks, now                */
static u32 g_peak;              /* HEAP_PEAK: highest value of g_used  */
static u32 g_top;               /* highest block end ever handed out   */

/* Counters, read by zzq2_glue.c (and by a debugger or a test bench). */
u32 zzq2_arena_lo(void)     { return g_lo; }
u32 zzq2_arena_hi(void)     { return g_hi; }
u32 zzq2_arena_used(void)   { return g_used; }
u32 zzq2_arena_peak(void)   { return g_peak; }
u32 zzq2_arena_top(void)    { return g_top; }
u32 zzq2_arena_blocks(void) { return (u32)g_nblk; }

void zzq2_arena_init(u32 lo, u32 hi)
{
    g_lo = (lo + (ZZQ2_BLK_ALIGN - 1u)) & ~(ZZQ2_BLK_ALIGN - 1u);
    g_hi = hi & ~(ZZQ2_BLK_ALIGN - 1u);
    g_nblk = 0;
    g_used = 0;
    g_peak = 0;
    g_top  = g_lo;
}

static void heap_fail(const char *what, u32 asked, u32 rounded)
{
    zzq2_heap_fail(what, asked, rounded, g_used, g_peak,
                   (g_hi > g_lo) ? g_hi - g_lo : 0u);
    for (;;) { }                /* zzq2_heap_fail does not return */
}

static u32 round_blk(u32 size)
{
    if (size == 0) size = 1;
    return (size + (ZZQ2_BLK_ALIGN - 1u)) & ~(ZZQ2_BLK_ALIGN - 1u);
}

/* First fit, in address order. Returns 0 when nothing fits.
   Every block lies inside [g_lo, g_hi): the gap that is searched last
   ends at g_hi, and the result is checked once more before it is
   entered in the table. */
static u32 arena_alloc(u32 size)
{
    u32 prev_end = g_lo;
    int i, k;

    if (g_hi <= g_lo) return 0;
    if (size > g_hi - g_lo) return 0;
    size = round_blk(size);
    if (size > g_hi - g_lo) return 0;
    if (g_nblk >= ZZQ2_MAX_BLOCKS) return 0;

    for (i = 0; i <= g_nblk; i++) {
        u32 gap_end = (i < g_nblk) ? g_blk[i].base : g_hi;
        if (gap_end >= prev_end && gap_end - prev_end >= size) {
            /* HEAP_LIMIT guard: never a block outside the heap */
            if (prev_end < g_lo || prev_end > g_hi ||
                size > g_hi - prev_end)
                return 0;
            for (k = g_nblk; k > i; k--) g_blk[k] = g_blk[k - 1];
            g_blk[i].base = prev_end;
            g_blk[i].size = size;
            g_nblk++;
            g_used += size;
            if (g_used > g_peak) g_peak = g_used;
            if (prev_end + size > g_top) g_top = prev_end + size;
            return prev_end;
        }
        if (i < g_nblk) prev_end = g_blk[i].base + g_blk[i].size;
    }
    return 0;
}

/* Index of the block that starts exactly at base, or -1. */
static int arena_find(u32 base)
{
    int lo = 0, hi = g_nblk - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (g_blk[mid].base == base) return mid;
        if (g_blk[mid].base < base) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static void arena_free_idx(int i)
{
    int k;
    g_used -= g_blk[i].size;
    for (k = i; k < g_nblk - 1; k++) g_blk[k] = g_blk[k + 1];
    g_nblk--;
}

static int arena_owns(const void *p)
{
    u32 a = (u32)(unsigned long)p;
    return (g_hi > g_lo && a >= g_lo && a < g_hi);
}

/* ------------------------------------------------------------------
   Quake II Hunk API (replaces other/q_hunk.c).
   Hunk_Begin RESERVES maxsize, Hunk_Alloc hands out zeroed memory
   from the reservation, Hunk_End gives the unused tail back.
   ------------------------------------------------------------------ */

static u32 hunk_base, hunk_max, hunk_cur;
int hunkcount;

void *Hunk_Begin(int maxsize)
{
    u32 want, base;

    if (maxsize <= 0) heap_fail("Hunk_Begin", (u32)maxsize, 0);
    want = round_blk((u32)maxsize);
    base = arena_alloc(want);
    if (!base) heap_fail("Hunk_Begin", (u32)maxsize, want);
    hunk_base = base;
    hunk_max  = want;
    hunk_cur  = 0;
    return (void *)(unsigned long)base;
}

void *Hunk_Alloc(int size)
{
    u32 n;
    void *p;

    if (size < 0 || (u32)size > hunk_max) heap_fail("Hunk_Alloc", (u32)size, 0);
    n = ((u32)size + 31u) & ~31u;          /* round to cacheline */
    /* stays inside the reservation, which is a block of the heap */
    if (hunk_base == 0 || n > hunk_max - hunk_cur)
        heap_fail("Hunk_Alloc", (u32)size, n);
    p = (void *)(unsigned long)(hunk_base + hunk_cur);
    hunk_cur += n;
    memset(p, 0, n);
    return p;
}

int Hunk_End(void)
{
    int i = arena_find(hunk_base);
    int used = (int)hunk_cur;
    if (i >= 0) {
        u32 keep = round_blk(hunk_cur);
        if (keep < g_blk[i].size) {
            g_used -= (g_blk[i].size - keep);
            g_blk[i].size = keep;
        }
    }
    hunkcount++;
    hunk_base = 0;
    hunk_max = 0;
    hunk_cur = 0;
    return used;
}

void Hunk_Free(void *base)
{
    if (base) {
        int i = arena_find((u32)(unsigned long)base);
        if (i >= 0) arena_free_idx(i);
    }
    hunkcount--;
}

/* ------------------------------------------------------------------
   malloc/free as seen by the Quake II objects (zzq2_stdio.h).
   Small requests stay on the release newlib heap. Requests of
   64 KiB and more (file buffers, textures, z-buffer, surface cache,
   game edicts) take whole blocks of the heap above.

   Several callers in Quake II use the result without testing it
   (surface cache, z-buffer, images). A request that cannot be served
   therefore ends the run with a diagnostic instead of returning NULL.
   ------------------------------------------------------------------ */

void *zzq2_malloc(size_t size)
{
    void *p;
    u32 a;

    if (size >= ZZQ2_BIG_ALLOC) {
        if (size > 0xFFFFF000u) heap_fail("malloc", (u32)size, 0);
        a = arena_alloc((u32)size);
        if (a) return (void *)(unsigned long)a;
        /* does not fit below HEAP_LIMIT: last resort, the newlib heap
           (bounded by the release _sbrk, far from the arena) */
        p = malloc(size);
        if (!p) heap_fail("malloc", (u32)size, round_blk((u32)size));
        return p;
    }
    p = malloc(size);
    if (!p && size) {
        /* newlib heap exhausted: one block of the arena heap */
        a = arena_alloc((u32)size);
        if (!a) heap_fail("malloc", (u32)size, round_blk((u32)size));
        return (void *)(unsigned long)a;
    }
    return p;
}

void zzq2_free(void *p)
{
    if (!p) return;
    if (arena_owns(p)) {
        int i = arena_find((u32)(unsigned long)p);
        if (i >= 0) arena_free_idx(i);
        return;
    }
    free(p);
}
