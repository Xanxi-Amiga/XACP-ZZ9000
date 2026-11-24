/* ZZScreensaverR: memory and random numbers of the MatrixView scene.
 * Copyright (C) 2026 Xanxi. GPL-2.0-or-later. See COPYING. No warranty.
 *
 * The scene allocates four tables once per grid size. They come from a static
 * pool sized for the largest grid (MX_MAX_COLS x MX_MAX_ROWS); nothing is ever
 * returned to it, mv_sys_reset() empties it before a new grid.
 */
#include <string.h>
#include "gl_common.h"
#include "matrix_scene.h"

/* tables of one grid: columns + cells * (1 + 1 + sizeof(float)), each rounded up to 8 bytes */
#define MX_CELLS ((MX_MAX_COLS) * ((MX_MAX_ROWS) + 1))
#define MX_POOL_BYTES ((MX_MAX_COLS) + (MX_CELLS) * 6 + 64)

static union { unsigned char bytes[MX_POOL_BYTES]; double align; } pool;
static size_t pool_used;
static int pool_failed;
static uint32_t rng = 0x58414e58u;

void mv_sys_reset(uint32_t seed)
{
    pool_used = 0;
    pool_failed = 0;
    rng = seed ? seed : 0x58414e58u;
}

int mv_sys_failed(void) { return pool_failed; }

void *mv_calloc(size_t count, size_t size)
{
    size_t bytes, start = (pool_used + 7u) & ~(size_t)7u;
    void *block;
    if (size && count > (size_t)MX_POOL_BYTES / size) { pool_failed = 1; return NULL; }
    bytes = count * size;
    if (start > (size_t)MX_POOL_BYTES || bytes > (size_t)MX_POOL_BYTES - start) { pool_failed = 1; return NULL; }
    block = pool.bytes + start;
    memset(block, 0, bytes);
    pool_used = start + bytes;
    return block;
}

void mv_free(void *block) { (void)block; }

/* xorshift32: never 0 from a non-zero seed; 31 bits returned, like rand() */
int mv_rand(void)
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return (int)(x & 0x7fffffffu);
}
