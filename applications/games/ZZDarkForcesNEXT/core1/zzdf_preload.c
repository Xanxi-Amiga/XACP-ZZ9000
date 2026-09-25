 
























#include "zzdf_config.h"

typedef unsigned int u32;
typedef unsigned char u8;

 









u32 zzdf_preload_validate_limit(u32 src, u32 dst, u32 sz, u32 asset_limit)
{
    /* the limit must itself be sane and 1 MiB aligned */
    if (asset_limit < ZZDF_HIGH_DDR_BASE)               return 10;
    if (asset_limit > ZZDF_HEAP_END)                    return 11;
    if (asset_limit & (ZZDF_HEAP_ALIGN - 1))            return 12;

    if (sz == 0)                                        return 1;
    if (dst < ZZDF_ASSET_ARENA_BASE)                    return 2;
    if (sz  > (ZZDF_ASSET_ARENA_END - ZZDF_ASSET_ARENA_BASE)) return 3;
    if (dst > ZZDF_ASSET_ARENA_END - sz)                return 4;
    if (dst + sz < dst)                                 return 5;
    if (src < ZZDF_STAGING_ARM)                         return 6;
    if (sz  > ZZDF_STAGING_SIZE)                        return 7;
    if (src > ZZDF_STAGING_ARM + ZZDF_STAGING_SIZE - sz) return 8;
    if (src + sz < src)                                 return 9;

    /* dynamic bound: never into the live heap */
    if (dst >= asset_limit)                             return 13;
    if (sz  > asset_limit - dst)                        return 14;
    return 0;
}

/* Static-bound variant kept for callers that have no limit yet;
   equivalent to a limit at the guard. */
u32 zzdf_preload_validate(u32 src, u32 dst, u32 sz)
{
    return zzdf_preload_validate_limit(src, dst, sz, ZZDF_HEAP_END);
}

/* Word copy with byte tail, volatile on both sides: staging is
   non-cacheable and the arena is write-back, the dsb after the copy
   is what publishes it. Identical to the ZZQuake loop. */
void zzdf_preload_copy(u32 src, u32 dst, u32 sz)
{
    const volatile u32 *s32 = (const volatile u32 *)src;
    volatile u32 *d32 = (volatile u32 *)dst;
    u32 words = sz >> 2, k;
    for (k = 0; k < words; k++) d32[k] = s32[k];
    if (sz & 3u) {
        const volatile u8 *s8 = (const volatile u8 *)(src + (words << 2));
        volatile u8 *d8 = (volatile u8 *)(dst + (words << 2));
        u32 t = sz & 3u;
        for (k = 0; k < t; k++) d8[k] = s8[k];
    }
}

/* One step of the state machine. Called in a loop by the blob (or by
   the host test). Returns:
 *   0  keep looping
 *   1  START received: leave the loop, the game may begin
 *  -1  request refused: STATE/ERR are set, the caller must park
 */
int zzdf_preload_step(volatile u32 *sh, u32 *last_ack)
{
    u32 seq, src, dst, sz, why;

    if (sh[SH_PRELOAD_STATE] == ZZDF_PRELOAD_START) return 1;
    if (sh[SH_PRELOAD_STATE] == ZZDF_PRELOAD_ERROR) return -1;

    seq = sh[SH_PRELOAD_SEQ];
    if (seq == *last_ack) return 0;

    src = sh[SH_PRELOAD_SRC];
    dst = sh[SH_PRELOAD_DST];
    sz  = sh[SH_PRELOAD_SIZE];

    /* the asset limit is the live heap base published by the
       launcher before ARM_RUN - independent Core1-side protection */
    why = zzdf_preload_validate_limit(src, dst, sz, sh[SH_HEAP_BASE]);
    if (why) {
        sh[SH_PRELOAD_ERR]   = dst;
        sh[SH_PRELOAD_STATE] = ZZDF_PRELOAD_ERROR;
        sh[SH_ERROR]         = 0xE0002000UL | why;
        return -1;
    }

    zzdf_preload_copy(src, dst, sz);
#ifndef ZZDF_TEST_BUILD
    __asm__ volatile("dsb sy" ::: "memory");
#endif
    *last_ack = seq;
    sh[SH_PRELOAD_COUNT] = sh[SH_PRELOAD_COUNT] + 1;
    sh[SH_PRELOAD_BYTES] = sh[SH_PRELOAD_BYTES] + sz;
    sh[SH_PRELOAD_ACK]   = seq;
#ifndef ZZDF_TEST_BUILD
    __asm__ volatile("dsb sy" ::: "memory");
#endif
    return 0;
}

/* Full service, target only: announce READY, then loop until START
   or a refused request. Returns 1 on START, -1 on error (caller
   parks). Active poll, no wfe: absolute rule. */
#ifndef ZZDF_TEST_BUILD
int zzdf_preload_service(volatile u32 *sh)
{
    u32 last_ack = 0;
    int r;

    if (sh[SH_PRELOAD_STATE] != ZZDF_PRELOAD_COPY)
        return 1;                     /* not requested: nothing to do */

    sh[SH_PRELOAD_COUNT] = 0;
    sh[SH_PRELOAD_BYTES] = 0;
    sh[SH_PRELOAD_ACK]   = 0;
    sh[SH_PRELOAD_STATE] = ZZDF_PRELOAD_READY;
    __asm__ volatile("dsb sy" ::: "memory");

    for (;;) {
        r = zzdf_preload_step(sh, &last_ack);
        if (r != 0) return r;
    }
}
#endif
