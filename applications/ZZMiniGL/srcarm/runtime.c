/* Core1 bare-metal runtime: libc/newlib must not use AmigaOS syscalls.
 * This is linked into the ARM image, not the 68k minigl.library frontend.
 * The heap ends BEFORE the XACP control/command mailbox at 0x04650000.
 */
#include <stddef.h>
#include <stdint.h>

void *memset(void *dst, int n, size_t len) {
    unsigned char *d = (unsigned char *)dst;
    while (len--) *d++ = (unsigned char)n;
    return dst;
}
void *memcpy(void *dst, const void *src, size_t len) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (len--) *d++ = *s++;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d <= s) return memcpy(dst, src, n);
    while (n) { --n; d[n] = s[n]; }
    return dst;
}

extern unsigned char _bss_end; /* supplied by srcarm/zzmini.ld */
#define ZZ_HEAP_LIMIT ((uintptr_t)0x045F8000u)   /* A6: below the 32 KiB service stack (0x045F8000..0x04600000) */
static uintptr_t zz_heap_cursor;

void zzheap_reset(void) {
    zz_heap_cursor = ((uintptr_t)&_bss_end + 7u) & ~(uintptr_t)7u;
}

/* Newlib's malloc backend. Return (void *)-1 on exhaustion, never write
 * into Core1's control block or the existing command ring. */
void *_sbrk(ptrdiff_t increment) {
    uintptr_t old, bytes;
    if (zz_heap_cursor == 0) zzheap_reset();
    old = zz_heap_cursor;
    if (increment < 0 || old > ZZ_HEAP_LIMIT) return (void *)(intptr_t)-1;
    bytes = ((uintptr_t)increment + 7u) & ~(uintptr_t)7u;
    if ((uintptr_t)increment > UINTPTR_MAX - 7u || bytes > ZZ_HEAP_LIMIT - old)
        return (void *)(intptr_t)-1;
    zz_heap_cursor = old + bytes;
    return (void *)old;
}

/* No files, terminal or AmigaOS DOS on bare-metal Core1. If any newlib
 * routine unexpectedly requests I/O, fail instead of claiming success. */
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int offset, int whence) {
    (void)fd; (void)offset; (void)whence; return -1;
}
int _read(int fd, void *buf, unsigned int count) {
    (void)fd; (void)buf; (void)count; return -1;
}
int _write(int fd, const void *buf, unsigned int count) {
    (void)fd; (void)buf; (void)count; return -1;
}
