/*
 * safe_mem.c - byte-wise mem* for ZZDoom bare-metal
 * Overrides newlib's optimized versions that do unaligned word access.
 * ASCII only.
 */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    while(n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    if(d < s){
        while(n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while(n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char*)dst;
    while(n--) *d++ = (unsigned char)c;
    return dst;
}
