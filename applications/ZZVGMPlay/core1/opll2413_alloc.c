/* Tiny fixed arena allocator used only by emu2413 on Core1.
 * emu2413 allocates one OPLL object plus a small stereo rate-converter.
 * There is no general-purpose heap in the ZZ9000 bare-metal payload.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "opll2413_alloc.h"

#define ZZ_OPLL_POOL_BYTES (64u * 1024u)
static uint8_t g_pool[ZZ_OPLL_POOL_BYTES] __attribute__((aligned(16)));
static size_t g_used;

void zz_opll_pool_reset(void)
{
    g_used = 0;
}

void *zz_opll_malloc(size_t n)
{
    size_t a = (n + 15u) & ~(size_t)15u;
    void *p;
    if (!n || g_used > ZZ_OPLL_POOL_BYTES || a > ZZ_OPLL_POOL_BYTES - g_used)
        return NULL;
    p = &g_pool[g_used];
    g_used += a;
    return p;
}

void *zz_opll_calloc(size_t n, size_t s)
{
    size_t bytes;
    void *p;
    if (n && s > (size_t)-1 / n) return NULL;
    bytes = n * s;
    p = zz_opll_malloc(bytes);
    if (p) memset(p, 0, bytes);
    return p;
}

void zz_opll_free(void *p)
{
    (void)p; /* reclaimed in one shot by zz_opll_pool_reset() */
}
