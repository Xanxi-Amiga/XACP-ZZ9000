/*
 * zzpico_platform.c - minimal OS surface for the PicoDrive core (bare-metal).
 *
 * Provides only what the minimal Mega Drive C build references:
 *   - lprintf (no-op; elprintf is compiled out)
 *   - plat_mmap / plat_mremap / plat_munmap (signatures from pico/pico.h)
 *   - memcpy / memset / memmove (the compiler emits calls to these)
 *   - _sbrk + minimal newlib syscall stubs
 *
 * _sbrk heap is the validated ZZDoom region (ZZPICO_ARM_HEAP, 8 MB). In v0
 * PicoDrive aims for ~0 heap; this exists only so the link is clean and any
 * stray allocation fails cleanly (returns (void*)-1) rather than corrupting.
 *
 * ASCII only.
 */
#include <stddef.h>
#include "zzpico_shared.h"

typedef unsigned int  u32;
typedef unsigned char u8;

/* ---- logging: silenced ---- */
void lprintf(const char *fmt, ...) { (void)fmt; }

/* ---- mmap surface ----
 * The minimal MD path uses PicoCartInsert with a caller-provided pointer, so
 * these should not be hit. If they are, fail cleanly (never fake a pointer). */
void *plat_mmap(unsigned long addr, size_t size, int need_exec, int is_fixed)
{ (void)addr; (void)size; (void)need_exec; (void)is_fixed; return (void*)0; }
void *plat_mremap(void *ptr, size_t oldsize, size_t newsize)
{ (void)ptr; (void)oldsize; (void)newsize; return (void*)0; }
void  plat_munmap(void *ptr, size_t size)
{ (void)ptr; (void)size; }

/* ---- mem* (freestanding, byte-wise; MMU+Normal handles unaligned) ---- */
void *memset(void *d, int c, size_t n)
{ u8 *p=(u8*)d; while(n--) *p++=(u8)c; return d; }

void *memcpy(void *d, const void *s, size_t n)
{ u8 *pd=(u8*)d; const u8 *ps=(const u8*)s; while(n--) *pd++=*ps++; return d; }

void *memmove(void *d, const void *s, size_t n)
{
    u8 *pd=(u8*)d; const u8 *ps=(const u8*)s;
    if(pd==ps || n==0) return d;
    if(pd<ps){ while(n--) *pd++=*ps++; }
    else { pd+=n; ps+=n; while(n--) *--pd=*--ps; }
    return d;
}

/* ---- heap (validated ZZDoom region) ---- */
static char *heap_ptr = 0;
void *_sbrk(int incr)
{
    char *old;
    if(heap_ptr==0) heap_ptr=(char*)ZZPICO_ARM_HEAP;
    old=heap_ptr;
    if((u32)(heap_ptr+incr) > (u32)(ZZPICO_ARM_HEAP+ZZPICO_HEAP_SIZE))
        return (void*)-1;          /* clean failure, no sentinel corruption */
    heap_ptr+=incr;
    return old;
}

/* ---- minimal newlib syscall stubs (no filesystem in v0) ---- */
int  _close(int f){ (void)f; return -1; }
int  _read(int f, char *b, int l){ (void)f; (void)b; (void)l; return 0; }
int  _write(int f, char *b, int l){ (void)f; (void)b; return l; }
int  _lseek(int f, int o, int w){ (void)f; (void)o; (void)w; return 0; }
int  _fstat(int f, void *s){ (void)f; (void)s; return 0; }
int  _isatty(int f){ (void)f; return 1; }
int  _open(const char *p, int flags, ...){ (void)p; (void)flags; return -1; }
int  _kill(int p, int s){ (void)p; (void)s; return -1; }
int  _getpid(void){ return 1; }
void _exit(int c){ (void)c; for(;;){} }
