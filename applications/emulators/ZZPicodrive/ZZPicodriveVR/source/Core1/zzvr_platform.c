/*
 * zzvr_platform.c - bare-metal support for PicoDrive on Core1.
 * Provides bounded memory allocation, memory operations, recompiler
 * cache allocation and the minimal system-call interface.
 * See zzvr_mem.h for the Core1 memory layout.
 */
#include <stddef.h>
#include "zzvr_shared.h"
#include "zzvr_mem.h"

typedef unsigned int  u32;
typedef unsigned char u8;

/* ---- logging: silenced ---- */
void lprintf(const char *fmt, ...) { (void)fmt; }

/* ---- statistics published by zzvr_core1.c ---- */
u32 zzvr_mmap_used  = 0;   /* bytes handed out by plat_mmap */
u32 zzvr_mmap_calls = 0;
u32 zzvr_mmap_fail  = 0;
u32 zzvr_sbrk_used  = 0;

/* ---- mmap surface: bump allocator, 4 KiB aligned, zero-filled, bounded ---- */
static u32 mmap_next = ZZVR_ARENA_MMAP;

void *plat_mmap(unsigned long addr, size_t size, int need_exec, int is_fixed)
{
    u32 base, sz;
    (void)addr; (void)need_exec; (void)is_fixed;   /* the address is only a hint in PicoDrive */
    if(size == 0) return (void*)0;
    sz = ((u32)size + 4095u) & ~4095u;
    base = (mmap_next + 4095u) & ~4095u;
    zzvr_mmap_calls++;
    if(base + sz > ZZVR_ARENA_MMAP_END || base + sz < base){ zzvr_mmap_fail++; return (void*)0; }
    mmap_next = base + sz;
    zzvr_mmap_used = mmap_next - ZZVR_ARENA_MMAP;
    { u32 *p = (u32*)base, *e = (u32*)(base + sz); while(p < e) *p++ = 0u; }
    return (void*)base;
}

/* VR edition: the SVP code cache must be within +-32 MiB of the blob, whose
   C helpers and stub_arm.S handlers the generated code calls and patches with
   single direct branches (the far-call sequence of the SH-2 recompiler cannot
   be patched by ssp_drc_next_patch).  The tail of the ROM staging window
   (WB/WA, 11-14 MiB from the blob) is free when the ROM + SVP data are small:
   Virtua Racing is 2 MiB, the SVP data 0.25 MiB. */
#define ZZVR_SVP_TCACHE      0x04E00000UL                /* .. 0x05200000 = ZZPICO_ARM_ROM_END */
#define ZZVR_SVP_TCACHE_SIZE (ZZPICO_ARM_ROM_END - ZZVR_SVP_TCACHE)
u32 zzvr_svp_tcache_near = 0;   /* diag: 1 = SVP code cache near the blob */

void *plat_mremap(void *ptr, size_t oldsize, size_t newsize)
{
    void *n;
    /* VR edition: the cartridge sits in the ROM staging window (inserted from
       memory, so cart.c's rom_alloc_size is 0): PicoSVPStartup() grows it by the
       SVP data.  Grow in place while it fits below the SVP code cache. */
    { u32 p=(u32)ptr;
      if(p >= ZZPICO_ARM_ROM && p < ZZPICO_ARM_ROM_END && p + (u32)newsize <= ZZVR_SVP_TCACHE)
          return ptr; }
    if(ptr == 0) return plat_mmap(0, newsize, 0, 0);
    if(newsize <= oldsize) return ptr;             /* shrink: keep the block */
    n = plat_mmap(0, newsize, 0, 0);
    if(n == 0) return (void*)0;
    { const u8 *s=(const u8*)ptr; u8 *d=(u8*)n; size_t i; for(i=0;i<oldsize;i++) d[i]=s[i]; }
    return n;                                       /* the old block is not reclaimed (bump) */
}

void plat_munmap(void *ptr, size_t size)
{ (void)ptr; (void)size; }                          /* bump allocator: no free */

/* ---- SH-2 recompiler code cache (A1b) ---- */
extern unsigned int zzvr_pico_ahw(void);     /* zzvr_core1.c: PicoIn.AHW */
void *plat_mem_get_for_drc(size_t size)
{
    if(zzvr_pico_ahw() & (1u<<2)){            /* PAHW_SVP: VR edition, see above */
        zzvr_svp_tcache_near = 0;
        if(size <= ZZVR_SVP_TCACHE_SIZE){ zzvr_svp_tcache_near = 1; return (void*)ZZVR_SVP_TCACHE; }
        return (void*)0;
    }
    if(size > ZZVR_ARENA_DRC_SIZE) return (void*)0;   /* cmn.c falls back to its static tcache */
    return (void*)ZZVR_ARENA_DRC;
}
int plat_mem_set_exec(void *ptr, size_t size)
{ (void)ptr; (void)size; return 0; }                /* our sections carry no XN bit */

/* ---- mem* (freestanding; word path when both sides are 4-aligned) ----
 * The loops must not be turned back into memcpy/memset calls by the compiler. */
#define ZZVR_NOPATTERN __attribute__((optimize("no-tree-loop-distribute-patterns")))
ZZVR_NOPATTERN void *memset(void *d, int c, size_t n)
{
    u8 *p=(u8*)d;
    if(n >= 16 && (((u32)(size_t)p) & 3u) == 0){
        u32 v = (u32)(u8)c; v |= v<<8; v |= v<<16;
        u32 *w=(u32*)p; size_t nw=n>>2;
        while(nw >= 4){ w[0]=v; w[1]=v; w[2]=v; w[3]=v; w+=4; nw-=4; }
        while(nw--) *w++=v;
        p=(u8*)w; n&=3u;
    }
    while(n--) *p++=(u8)c;
    return d;
}

ZZVR_NOPATTERN void *memcpy(void *d, const void *s, size_t n)
{
    u8 *pd=(u8*)d; const u8 *ps=(const u8*)s;
    if(n >= 16 && ((((u32)(size_t)pd) | ((u32)(size_t)ps)) & 3u) == 0){
        u32 *wd=(u32*)pd; const u32 *ws=(const u32*)ps; size_t nw=n>>2;
        while(nw >= 4){ wd[0]=ws[0]; wd[1]=ws[1]; wd[2]=ws[2]; wd[3]=ws[3]; wd+=4; ws+=4; nw-=4; }
        while(nw--) *wd++=*ws++;
        pd=(u8*)wd; ps=(const u8*)ws; n&=3u;
    }
    while(n--) *pd++=*ps++;
    return d;
}

ZZVR_NOPATTERN void *memmove(void *d, const void *s, size_t n)
{
    u8 *pd=(u8*)d; const u8 *ps=(const u8*)s;
    if(pd==ps || n==0) return d;
    if(pd < ps || pd >= ps + n) return memcpy(d, s, n);   /* no forward overlap */
    pd+=n; ps+=n; while(n--) *--pd=*--ps;
    return d;
}

/* ---- heap (arena SBRK region, bounded) ---- */
static char *heap_ptr = 0;
void *_sbrk(int incr)
{
    char *old;
    if(heap_ptr==0) heap_ptr=(char*)ZZVR_ARENA_SBRK;
    old=heap_ptr;
    if((u32)(size_t)(heap_ptr+incr) > (u32)ZZVR_ARENA_SBRK_END || (u32)(size_t)(heap_ptr+incr) < (u32)ZZVR_ARENA_SBRK)
        return (void*)-1;          /* clean failure, no sentinel corruption */
    heap_ptr+=incr;
    zzvr_sbrk_used = (u32)(size_t)heap_ptr - (u32)ZZVR_ARENA_SBRK;
    return old;
}

/* ---- minimal newlib syscall stubs (no filesystem) ---- */
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
