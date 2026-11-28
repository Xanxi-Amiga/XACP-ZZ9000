/*
 * zzs_platform.c - bare-metal libc glue for the ZZStrider Core1 blob.
 * Derived from zzrastan_platform.c. Changes: word-wise memset/memcpy (the byte loops of ZZRastan
 * are too slow for the 5 MiB ROM store and for 640x480 frames), heap window from zzs_shared.h.
 * ASCII only.
 */
#include <stddef.h>
#include <stdint.h>
#include "zzs_shared.h"

typedef unsigned char u8;
typedef unsigned int  u32;

void *memset(void *d, int c, size_t n)
{
    u8 *p = (u8 *)d;
    u32 v = (u8)c;
    v |= v << 8; v |= v << 16;
    while (n != 0u && ((uintptr_t)p & 3u) != 0u) { *p++ = (u8)c; n--; }
    {
        u32 *w = (u32 *)(void *)p;
        while (n >= 16u) { w[0] = v; w[1] = v; w[2] = v; w[3] = v; w += 4; n -= 16u; }
        while (n >= 4u)  { *w++ = v; n -= 4u; }
        p = (u8 *)(void *)w;
    }
    while (n-- != 0u) *p++ = (u8)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    u8 *pd = (u8 *)d;
    const u8 *ps = (const u8 *)s;
    if ((((uintptr_t)pd ^ (uintptr_t)ps) & 3u) == 0u) {
        while (n != 0u && ((uintptr_t)pd & 3u) != 0u) { *pd++ = *ps++; n--; }
        {
            u32 *wd = (u32 *)(void *)pd;
            const u32 *ws = (const u32 *)(const void *)ps;
            while (n >= 16u) {
                wd[0] = ws[0]; wd[1] = ws[1]; wd[2] = ws[2]; wd[3] = ws[3];
                wd += 4; ws += 4; n -= 16u;
            }
            while (n >= 4u) { *wd++ = *ws++; n -= 4u; }
            pd = (u8 *)(void *)wd;
            ps = (const u8 *)(const void *)ws;
        }
    }
    while (n-- != 0u) *pd++ = *ps++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    u8 *pd = (u8 *)d;
    const u8 *ps = (const u8 *)s;
    if (pd == ps || n == 0u)
        return d;
    if (pd < ps || pd >= ps + n)
        return memcpy(d, s, n);
    pd += n;
    ps += n;
    while (n-- != 0u)
        *--pd = *--ps;
    return d;
}

static char *heap_ptr;
void *_sbrk(int incr)
{
    char *old;
    if (heap_ptr == 0)
        heap_ptr = (char *)ZZS_ARM_SBRK;
    old = heap_ptr;
    if ((u32)(uintptr_t)(heap_ptr + incr) > ZZS_ARM_SBRK_END)
        return (void *)-1;
    heap_ptr += incr;
    return old;
}

int _close(int f) { (void)f; return -1; }
int _read(int f, char *b, int l) { (void)f; (void)b; (void)l; return 0; }
int _write(int f, char *b, int l) { (void)f; (void)b; return l; }
int _lseek(int f, int o, int w) { (void)f; (void)o; (void)w; return 0; }
int _fstat(int f, void *s) { (void)f; (void)s; return 0; }
int _isatty(int f) { (void)f; return 1; }
void _exit(int c) { (void)c; for (;;) __asm__ volatile("wfe"); }
int _kill(int p, int s) { (void)p; (void)s; return -1; }
int _getpid(void) { return 1; }
