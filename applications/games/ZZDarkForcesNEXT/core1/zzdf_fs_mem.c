/* Flat in-memory filesystem used by the Core1 build. */
 



















#include "zzdf_config.h"

typedef unsigned int u32;
typedef unsigned char u8;

#define ZZDF_MAX_OPEN   16
#define ZZDF_MAX_RAMF   24
 



#define ZZDF_RAMF_CHUNK (2048u*1024u)    

static volatile u32 *shared_fs = (volatile u32 *)ZZDF_SHARED_ARM;

 







extern void zzdf_log_puts(const char *s);

static u32 g_fs_miss    = 0;    /* opens that found nothing          */
static u32 g_fs_dirrej  = 0;    /* subdirectory requests refused     */
static int g_fs_budget  = 96;

static void fs_trace(const char *what, const char *path, const char *verdict)
{
    if (g_fs_budget <= 0) return;
    if (--g_fs_budget == 0) {
        zzdf_log_puts("[FS] (trace budget exhausted)\n");
        return;
    }
    zzdf_log_puts("[FS] ");
    zzdf_log_puts(what);
    zzdf_log_puts(" ");
    zzdf_log_puts(path ? path : "(null)");
    zzdf_log_puts(" -> ");
    zzdf_log_puts(verdict);
    zzdf_log_puts("\n");
}

/* Called by the FileUtil layer for readDirectory/directoryExists. */
void zzdf_fs_trace_dir(const char *path, int supported)
{
    if (supported) return;               /* the flat root, expected */
    g_fs_dirrej++;
    shared_fs[SH_FS_DIRREJ] = g_fs_dirrej;
    fs_trace("DIR ", path, "NOT SUPPORTED (flat MemFS, no subdirectories)");
}

u32 zzdf_fs_miss_count(void)   { return g_fs_miss; }
u32 zzdf_fs_dirrej_count(void) { return g_fs_dirrej; }

/* ---- manifest ---- */
static const zzdf_manifest_entry *g_manifest;
static u32 g_manifest_count;

/* ---- RAM write files ---- */
typedef struct {
    char name[64];
    u8  *data;          /* heap alloc, ZZDF_RAMF_CHUNK */
    u32  size;          /* current logical size */
    int  used;
    int  dirty;          
     




    int  overflow;
     





    int  committed;
     



    u32  seq;
} ramfile;
static ramfile g_ramf[ZZDF_MAX_RAMF];

 


#define ZZDF_RAMF_SEQ_BASE 0xF0000000u
static u32 g_ramf_seq = 0;

/* ---- open handles ---- */
typedef struct {
    int   used;
    int   write;        /* 1 = RAM file opened for write */
    const u8 *base;     /* read: blob base */
    ramfile  *rf;       /* write or RAM-read target */
    u32   size;
    u32   pos;
} zzdf_fh;
static zzdf_fh g_fh[ZZDF_MAX_OPEN];

/* fd space: 64..64+ZZDF_MAX_OPEN-1 (0/1/2 = console in syscalls) */
#define ZZDF_FD_BASE 64

#ifndef ZZDF_TEST_BUILD
extern void *malloc(unsigned int);   /* newlib, heap at 0x07000000 */
#else
#include <stdlib.h>
#endif

static int zzdf_lower(int c){ return (c>='A'&&c<='Z')?(c-'A'+'a'):c; }

static const char *zzdf_basename(const char *p)
{
    const char *b = p;
    while (*p) { if (*p=='/'||*p=='\\') b = p+1; p++; }
    return b;
}

static int zzdf_nameeq(const char *a, const char *b)
{
    while (*a && *b) {
        if (zzdf_lower(*a) != zzdf_lower(*b)) return 0;
        a++; b++;
    }
    return *a==0 && *b==0;
}

/* Mount a manifest against an explicit asset limit (host tests, and
 * the real init below). asset_limit is SH_HEAP_BASE on the target:
 * the heap is live above it, so an entry reaching into it - even from
 * a corrupted manifest - must be refused here, not read later.
 * Returns the number of usable entries, 0 on a rejected manifest. */
u32 zzdf_fs_mount_limit(const void *manifest_base, u32 asset_limit)
{
    const zzdf_manifest_hdr *h = (const zzdf_manifest_hdr *)manifest_base;
    u32 i;

    g_manifest = 0;
    g_manifest_count = 0;
    for (i=0;i<ZZDF_MAX_OPEN;i++) g_fh[i].used = 0;
    for (i=0;i<ZZDF_MAX_RAMF;i++) g_ramf[i].used = 0;

    /* the limit itself must be sane */
    if (asset_limit < ZZDF_ASSET_ARENA_BASE) return 0;
    if (asset_limit > ZZDF_ASSET_ARENA_END)  return 0;

    if (!h) return 0;
    if (h->magic != ZZDF_MANIFEST_MAGIC) return 0;
    if (h->count == 0 || h->count > ZZDF_MANIFEST_MAX) return 0;

    g_manifest = (const zzdf_manifest_entry *)(h + 1);

    /* reject entries that are empty, outside the arena, or reaching
       into the live heap: a bad manifest must fail here, not with a
       data abort or a heap corruption later */
    for (i = 0; i < h->count; i++) {
        const zzdf_manifest_entry *e = &g_manifest[i];
        if (e->name[0] == 0)                      { g_manifest=0; return 0; }
        if (e->size == 0)                         { g_manifest=0; return 0; }
        if (e->base < ZZDF_ASSET_ARENA_BASE)      { g_manifest=0; return 0; }
        if (e->base >= asset_limit)               { g_manifest=0; return 0; }
        if (e->size > asset_limit - e->base)      { g_manifest=0; return 0; }
        if (e->base + e->size < e->base)          { g_manifest=0; return 0; }
    }

    g_manifest_count = h->count;
    return g_manifest_count;
}

/* Static-bound convenience (limit at the guard). */
u32 zzdf_fs_mount(const void *manifest_base)
{
    return zzdf_fs_mount_limit(manifest_base, ZZDF_ASSET_ARENA_END);
}

void zzdf_fs_init(void)
{
    /* the asset limit is the live heap base the launcher published */
    zzdf_fs_mount_limit((const void *)shared_fs[SH_MANIFEST_ADDR],
                        shared_fs[SH_HEAP_BASE]);
    shared_fs[SH_MANIFEST_COUNT] = g_manifest_count;
}

u32 zzdf_fs_count(void) { return g_manifest_count; }

/* entry lookup for the launcher-visible report / host tests */
const zzdf_manifest_entry *zzdf_fs_entry(u32 idx)
{
    if (idx >= g_manifest_count) return 0;
    return &g_manifest[idx];
}

 








int zzdf_fs_open(const char *path, int write, int trunc)
{
    const char *base = zzdf_basename(path);
    u32 i; int h = -1;

    for (i=0;i<ZZDF_MAX_OPEN;i++) if(!g_fh[i].used){ h=(int)i; break; }
    if (h < 0) return -1;

    if (write) {
        ramfile *rf = 0;
        int fresh = 0;
        for (i=0;i<ZZDF_MAX_RAMF;i++)
            if (g_ramf[i].used && zzdf_nameeq(g_ramf[i].name, base))
                { rf = &g_ramf[i]; break; }
        if (!rf) {
            for (i=0;i<ZZDF_MAX_RAMF;i++) if(!g_ramf[i].used){ rf=&g_ramf[i]; break; }
            if (!rf) return -1;
            {
                int k=0;
                while (base[k] && k<63){ rf->name[k]=base[k]; k++; }
                rf->name[k]=0;
            }
            rf->data = (u8*)malloc(ZZDF_RAMF_CHUNK);
            if (!rf->data) return -1;
            rf->used = 1;
            rf->size = 0;
            rf->dirty = 0;
            rf->overflow = 0;
            fresh = 1;
        }
         










        if (fresh && !trunc) {
            for (i=0;i<g_manifest_count;i++) {
                if (zzdf_nameeq(g_manifest[i].name, base)) {
                    u32 n = g_manifest[i].size;
                    if (n > ZZDF_RAMF_CHUNK) n = ZZDF_RAMF_CHUNK;
                    {
                        const u8 *s = (const u8*)g_manifest[i].base;
                        u32 k;
                        for (k = 0; k < n; k++) rf->data[k] = s[k];
                    }
                    rf->size = n;
                    break;
                }
            }
        }
         




        if (trunc) { rf->size = 0; rf->overflow = 0; }
         



        rf->committed = 0;
        rf->dirty = 1;
        g_fh[h].used = 1; g_fh[h].write = 1;
        g_fh[h].rf = rf; g_fh[h].base = rf->data;
        g_fh[h].size = rf->size; g_fh[h].pos = 0;
        return ZZDF_FD_BASE + h;
    }

     












    for (i=0;i<ZZDF_MAX_RAMF;i++) {
        if (g_ramf[i].used && g_ramf[i].committed &&
            zzdf_nameeq(g_ramf[i].name, base)) {
            g_fh[h].used = 1; g_fh[h].write = 0;
            g_fh[h].base = g_ramf[i].data;
            g_fh[h].rf   = &g_ramf[i];
            g_fh[h].size = g_ramf[i].size;
            g_fh[h].pos  = 0;
            return ZZDF_FD_BASE + h;
        }
    }
    /* then the manifest: everything the engine has never written */
    for (i=0;i<g_manifest_count;i++) {
        if (zzdf_nameeq(g_manifest[i].name, base)) {
            g_fh[h].used = 1; g_fh[h].write = 0;
            g_fh[h].base = (const u8*)g_manifest[i].base;
            g_fh[h].rf   = 0;
            g_fh[h].size = g_manifest[i].size;
            g_fh[h].pos  = 0;
            return ZZDF_FD_BASE + h;
        }
    }
    /* and finally a RAM file that is not committed yet: better to read
       what is there than to fail, when nothing was staged under that
       name (a log, a scratch file the engine reopens). */
    for (i=0;i<ZZDF_MAX_RAMF;i++) {
        if (g_ramf[i].used && zzdf_nameeq(g_ramf[i].name, base)) {
            g_fh[h].used = 1; g_fh[h].write = 0;
            g_fh[h].base = g_ramf[i].data;
            g_fh[h].rf   = &g_ramf[i];
            g_fh[h].size = g_ramf[i].size;
            g_fh[h].pos  = 0;
            return ZZDF_FD_BASE + h;
        }
    }
    /* Path probes may miss before TFE falls back to archive lookup;
       count them without flooding the shared log ring. */
    g_fs_miss++;
    shared_fs[SH_FS_MISS] = g_fs_miss;
    return -1;
}

int zzdf_fs_exists(const char *path)
{
    const char *base = zzdf_basename(path);
    u32 i;
    for (i=0;i<g_manifest_count;i++)
        if (zzdf_nameeq(g_manifest[i].name, base)) return 1;
    for (i=0;i<ZZDF_MAX_RAMF;i++)
        if (g_ramf[i].used && zzdf_nameeq(g_ramf[i].name, base)) return 1;
    return 0;
}

 





u32 zzdf_fs_mtime(const char *path)
{
    const char *base = zzdf_basename(path);
    u32 i;
    for (i=0;i<ZZDF_MAX_RAMF;i++)
        if (g_ramf[i].used && g_ramf[i].committed &&
            zzdf_nameeq(g_ramf[i].name, base)) return g_ramf[i].seq;
    for (i=0;i<g_manifest_count;i++)
        if (zzdf_nameeq(g_manifest[i].name, base)) return g_manifest[i].mtime;
    return 0;
}

static zzdf_fh *fh_of(int fd)
{
    int h = fd - ZZDF_FD_BASE;
    if (h < 0 || h >= ZZDF_MAX_OPEN) return 0;
    if (!g_fh[h].used) return 0;
    return &g_fh[h];
}

int zzdf_fs_is_ours(int fd) { return fh_of(fd) != 0; }

int zzdf_fs_read(int fd, void *buf, u32 n)
{
    zzdf_fh *f = fh_of(fd);
    u32 left, k; const u8 *s; u8 *d;
    u32 sz;
    if (!f) return -1;
    /* A write-capable handle on a RAM file is still readable: that is
       what O_RDWR means, and DARKPILO.CFG depends on it. */
    sz = f->write ? (f->rf ? f->rf->size : 0) : f->size;
    if (f->write && !f->rf) return -1;
    if (f->pos >= sz) return 0;
    left = sz - f->pos;
    if (n > left) n = left;
    s = (f->write ? f->rf->data : f->base) + f->pos;
    d = (u8*)buf;
    for (k=0;k<n;k++) d[k]=s[k];
    f->pos += n;
    return (int)n;
}

int zzdf_fs_write(int fd, const void *buf, u32 n)
{
    zzdf_fh *f = fh_of(fd);
    const u8 *s; u8 *d; u32 k;
    if (!f || !f->write) return -1;

     







    if (f->pos + n > ZZDF_RAMF_CHUNK) {
        volatile u32 *sh = (volatile u32 *)ZZDF_SHARED_ARM;
        if (f->rf && !f->rf->overflow) {
            f->rf->overflow = 1;
            sh[SH_SAVE_TRUNC] = sh[SH_SAVE_TRUNC] + 1;
            zzdf_log_puts("[FS] WRITE REFUSED, file exceeds RAMFS capacity: ");
            zzdf_log_puts(f->rf->name);
            zzdf_log_puts(" (raise ZZDF_RAMF_CHUNK)\n");
        }
        return -1;
    }
    s = (const u8*)buf; d = f->rf->data + f->pos;
    for (k=0;k<n;k++) d[k]=s[k];
    f->pos += n;
    if (f->pos > f->rf->size) f->rf->size = f->pos;
    return (int)n;
}

int zzdf_fs_lseek(int fd, int off, int whence)
{
    zzdf_fh *f = fh_of(fd);
    long np;
    u32 sz;
    if (!f) return -1;
    sz = f->write ? f->rf->size : f->size;
    switch (whence) {
        case 0: np = off; break;                    /* SEEK_SET */
        case 1: np = (long)f->pos + off; break;     /* SEEK_CUR */
        case 2: np = (long)sz + off; break;         /* SEEK_END */
        default: return -1;
    }
    if (np < 0) return -1;
    if ((u32)np > sz && !f->write) return -1;
    f->pos = (u32)np;
    if (f->write && f->pos > f->rf->size) f->rf->size = f->pos;
    return (int)f->pos;
}

u32 zzdf_fs_size(int fd)
{
    zzdf_fh *f = fh_of(fd);
    if (!f) return 0;
    return f->write ? f->rf->size : f->size;
}

int zzdf_fs_tell(int fd)
{
    zzdf_fh *f = fh_of(fd);
    if (!f) return -1;
    return (int)f->pos;
}

int zzdf_fs_eof(int fd)
{
    zzdf_fh *f = fh_of(fd);
    if (!f) return 1;
    return f->pos >= (f->write ? f->rf->size : f->size);
}

 

static int zzdf_is_tfe(const char *n)
{
    u32 i = 0, len = 0;
    const char *e;
    while (n[len]) len++;
    if (len < 4) return 0;
    e = n + len - 4;
    if (e[0] != '.') return 0;
    for (i = 1; i < 4; i++) {
        char c = e[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != "tfe"[i - 1]) return 0;
    }
    return 1;
}

int zzdf_fs_close(int fd)
{
    zzdf_fh *f = fh_of(fd);
    if (!f) return -1;
     





    if (f->write && f->rf && !f->rf->overflow) {
        f->rf->committed = 1;
        f->rf->seq = ZZDF_RAMF_SEQ_BASE + (++g_ramf_seq);
    }
     



    if (f->write && f->rf && zzdf_is_tfe(f->rf->name)) {
        volatile u32 *sh = (volatile u32 *)ZZDF_SHARED_ARM;
        char msg[96]; u32 v = f->rf->size, k = 0, d;
        sh[SH_SAVE_BYTES] = v;
        /* No sprintf here: this file is built for the test harness too
           and must not drag stdio in. */
        msg[k++] = '['; msg[k++] = 'F'; msg[k++] = 'S'; msg[k++] = ']';
        msg[k++] = ' '; msg[k++] = 'S'; msg[k++] = 'A'; msg[k++] = 'V';
        msg[k++] = 'E'; msg[k++] = ' ';
        {
            const char *p = f->rf->name;
            while (*p && k < 60) msg[k++] = *p++;
        }
        msg[k++] = ' '; msg[k++] = '=';  msg[k++] = ' ';
        if (!v) { msg[k++] = '0'; }
        else {
            char tmp[12]; u32 t = 0;
            while (v && t < 11) { tmp[t++] = (char)('0' + (v % 10u)); v /= 10u; }
            for (d = 0; d < t; d++) msg[k++] = tmp[t - 1 - d];
        }
        msg[k++] = ' '; msg[k++] = 'B'; msg[k++] = '\n'; msg[k] = 0;
        zzdf_log_puts(msg);
    }
    f->used = 0;
    return 0;
}

 



















 






#define ZZDF_WB_ELIGIBLE(r) \
    ((r).used && (r).dirty && (r).size && !(r).overflow && (r).committed)

u32 zzdf_fs_dirty_count(void)
{
    u32 i, n = 0;
    for (i = 0; i < ZZDF_MAX_RAMF; i++)
        if (ZZDF_WB_ELIGIBLE(g_ramf[i])) n++;
    return n;
}

/* idx counts only dirty, non-empty, non-overflowed files, in table
   order. Returns 0 on a bad index, 1 on success. */
int zzdf_fs_dirty_entry(u32 idx, const char **name, const void **data, u32 *size)
{
    u32 i, n = 0;
    for (i = 0; i < ZZDF_MAX_RAMF; i++) {
        if (!ZZDF_WB_ELIGIBLE(g_ramf[i])) continue;
        if (n == idx) {
            if (name) *name = g_ramf[i].name;
            if (data) *data = g_ramf[i].data;
            if (size) *size = g_ramf[i].size;
            return 1;
        }
        n++;
    }
    return 0;
}
