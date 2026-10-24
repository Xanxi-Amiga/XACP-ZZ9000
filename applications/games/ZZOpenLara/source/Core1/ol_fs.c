/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
/* ZZOpenLaraRIX A2: OpenLara file layer on Core1.
 *
 * Two sources, one stdio-like API (ol_stdio.h):
 *   1. the container the launcher preloads at 0x30000000 (a Quake PAK: "PACK", directory of 64-byte
 *      entries, 56-byte name + offset + size).  Files are served from memory, no copy.  Lookup by
 *      basename, case-insensitive, then by full path (the launcher stores basenames).
 *   2. the release file service of the 68k launcher (zzq_fopen of runtime/zzquake_fs_mem.c) for the
 *      files the game writes and reads back: "settings" and "savegame.dat" (PROGDIR:data/, 512 KiB
 *      at most each).
 *   3. A2.20: any other file the game reads (the levels, cutscenes, title picture...) from PROGDIR:data/,
 *      whole, into memory, through the part-file read of the ZZOpenLaraRIX launcher (Core1/ol_fsat.c,
 *      FS_CMD_READAT): the game goes from level to level without a container of every level. Freed at
 *      fclose. Existence checks read 1 byte.
 *   4. A2.21: videos (.RPL, .STR, .CPK, .FMV: 1 to 32 MB) are streamed: two blocks of 256 KiB in memory, the
 *      next block asked to the 68k in the background (Core1/ol_fsat.c, ol_fs_read_at_async) while the
 *      current one is read; a block not there yet is waited for. A2.21.2: 32 KiB blocks, 1 MB ahead,
 *      pumped every game frame (ol_fs_stream_pump).
 *
 * The runtime's own stdio (zzq_*) is not modified; the engine is compiled with ol_stdio.h.
 * ASCII only.
 */
#include <stdio.h>
#include <string.h>

typedef unsigned int u32;
typedef unsigned char u8;

extern FILE  *zzq_fopen (const char *path, const char *mode);
extern int    zzq_fclose(FILE *f);
extern size_t zzq_fread (void *dst, size_t sz, size_t n, FILE *f);
extern size_t zzq_fwrite(const void *src, size_t sz, size_t n, FILE *f);
extern int    zzq_fseek (FILE *f, long off, int whence);
extern long   zzq_ftell (FILE *f);
extern int    zzq_fgetc (FILE *f);
extern int    zzq_feof  (FILE *f);
extern int    zzq_printf(const char *fmt, ...);

#define OL_FS_MAX_ENTRIES 64
#define OL_FS_MAX_OPEN    8
#define OL_FS_NAME        57

typedef struct { char name[OL_FS_NAME]; u32 off, size; } ol_entry;

static const u8 *g_base;
static u32 g_size;
static ol_entry g_dir[OL_FS_MAX_ENTRIES];
static int g_count;

typedef struct {
    int used;
    int is_mem;          /* 1 = container (memory), 0 = 68k service (zzq_*) */
    const u8 *data;
    u32 size, pos;
    FILE *sys;
    u8 *own;             /* A2.20: loaded from PROGDIR:data/, freed at fclose */
    /* A2.21: streamed (videos) */
    int is_stream;
    char sname[64];
    u8 *ring;            /* A2.21.2: OL_FS_NB blocks of OL_FS_BLK, block b always in slot b % OL_FS_NB */
    int bidx[32];        /* block held by slot i, -1 = none */
    u32 blen[32];
    int pend;            /* block asked in the background (into its slot), -1 = none */
    int ahead;           /* last block read by the decoder: the read-ahead window starts after it */
} ol_fd;

/* A2.21.2: small blocks, a deep read-ahead. A2.21 had 2 blocks of 256 KiB: a TR1 film reads one chunk of
   418 KB per second (15 images + 1 s of sound), so every chunk needed a block nobody had asked for and
   was read while the game waited (~200 ms), and every 256 KiB request held the launcher's main loop
   (the one that shows Core1's images) as long. Now: 32 KiB per request (~25 ms of 68k), 32 blocks = 1 MB
   ahead (2.4 s of film), and the read-ahead is pumped every game frame (ol_fs_stream_pump), not only
   when the decoder reads. */
#define OL_FS_BLK (32u * 1024u)
#define OL_FS_NB  32
extern int  ol_fs_read_at_async(const char *name, unsigned off, unsigned len);
extern int  ol_fs_read_at_poll(void *dst, unsigned *got);
extern void ol_fs_read_at_quiesce(void);
static u32 g_stream_opens, g_stream_waits, g_stream_sync, g_stream_bg;
static u32 g_stream_retry, g_stream_bad, g_stream_bgbad, g_stream_bgfail, g_stream_short;   /* A2.22.2 */

/* A2.20: files of PROGDIR:data/ read whole through the launcher (Core1/ol_fsat.c) */
extern int   ol_fs_read_at(const char *name, unsigned off, void *dst, unsigned len, unsigned *total);
extern void *zzq2_malloc(size_t size);
extern void  zzq2_free(void *p);
#define OL_FS_LOOSE_MAX (64u * 1024u * 1024u)
static u32 g_loose_opens, g_loose_bytes, g_loose_misses, g_loose_us;
extern u32 zzq_us_now(void);

static ol_fd g_fd[OL_FS_MAX_OPEN];
static u32 g_opens_mem, g_opens_sys, g_misses;

static u32 le32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static const char *basename_of(const char *p)
{
    const char *b = p, *q;
    for (q = p; *q; q++) if (*q == '/' || *q == '\\' || *q == ':') b = q + 1;
    return b;
}

static int same_ci(const char *a, const char *b)
{
    while (*a && *b) { if (lower(*a) != lower(*b)) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

int ol_fs_init(const void *pak, unsigned size)
{
    const u8 *p = (const u8 *)pak;
    u32 dir, len, i;
    g_base = p; g_size = size; g_count = 0;
    if (size < 12 || memcmp(p, "PACK", 4)) return 0;
    dir = le32(p + 4); len = le32(p + 8);
    if (dir < 12 || dir > size || len % 64 || len > size - dir) return 0;
    for (i = 0; i < len / 64 && g_count < OL_FS_MAX_ENTRIES; i++) {
        const u8 *e = p + dir + i * 64;
        ol_entry *d = &g_dir[g_count];
        u32 off = le32(e + 56), sz = le32(e + 60);
        if (off > size || sz > size - off) return 0;
        memcpy(d->name, e, 56); d->name[56] = 0;
        d->off = off; d->size = sz;
        g_count++;
    }
    return 1;
}

int ol_fs_count(void) { return g_count; }

const char *ol_fs_name(int i, unsigned *size)
{
    if (i < 0 || i >= g_count) return 0;
    if (size) *size = g_dir[i].size;
    return g_dir[i].name;
}

const void *ol_fs_find(const char *path, unsigned *size)
{
    int i;
    const char *b;
    if (!path || !path[0]) return 0;
    b = basename_of(path);
    for (i = 0; i < g_count; i++) {
        if (same_ci(g_dir[i].name, path) || same_ci(basename_of(g_dir[i].name), b)) {
            if (size) *size = g_dir[i].size;
            return g_base + g_dir[i].off;
        }
    }
    return 0;
}

/* names the 68k launcher serves from PROGDIR:data (reads and writes) */
static int served_by_68k(const char *path)
{
    const char *b = basename_of(path);
    return same_ci(b, "settings") || same_ci(b, "savegame.dat");
}

/* A2.20 / A2.21: videos are streamed, never read whole */
static int is_video(const char *path)
{
    const char *b = basename_of(path), *e = 0, *q;
    for (q = b; *q; q++) if (*q == '.') e = q;
    if (!e) return 0;
    return same_ci(e, ".rpl") || same_ci(e, ".str") || same_ci(e, ".cpk") || same_ci(e, ".fmv");
}

static ol_fd *fd_alloc(void)
{
    int i;
    for (i = 0; i < OL_FS_MAX_OPEN; i++) if (!g_fd[i].used) { memset(&g_fd[i], 0, sizeof g_fd[i]); g_fd[i].used = 1; return &g_fd[i]; }
    return 0;
}

/* A2.21: the background block, if it arrived */
static void stream_poll(ol_fd *d)
{
    unsigned got = 0;
    int r, sl;
    if (d->pend < 0) return;
    sl = d->pend % OL_FS_NB;
    r = ol_fs_read_at_poll(d->ring + (u32)sl * OL_FS_BLK, &got);
    if (r == 0) return;
    if (r > 0) {
        u32 want = d->size - (u32)d->pend * OL_FS_BLK;
        if (want > OL_FS_BLK) want = OL_FS_BLK;
        if (got == want) { d->bidx[sl] = d->pend; d->blen[sl] = got; g_stream_bg++; }
        else g_stream_bgbad++;   /* A2.22.2: short background block: dropped, read again when needed */
    } else g_stream_bgfail++;
    d->pend = -1;
}

/* A2.21.2: the next block of the window (ahead+1 .. ahead+NB-1) not yet in memory, asked in the background */
static void stream_prefetch(ol_fd *d)
{
    int nb, last = d->ahead + OL_FS_NB - 1;
    if (d->pend >= 0) return;
    for (nb = d->ahead + 1; nb <= last; nb++) {
        u32 off = (u32)nb * OL_FS_BLK, len;
        int sl = nb % OL_FS_NB;
        if (off >= d->size) return;
        if (d->bidx[sl] == nb) continue;
        len = d->size - off;
        if (len > OL_FS_BLK) len = OL_FS_BLK;
        d->bidx[sl] = -1;
        if (ol_fs_read_at_async(d->sname, off, len) == 0) d->pend = nb;
        return;
    }
}

/* the slot holding block b, loaded if needed (waits for it when asked in the background) */
static int stream_block(ol_fd *d, int b)
{
    int sl = b % OL_FS_NB;
    stream_poll(d);
    if (d->bidx[sl] == b) return sl;
    if (d->pend == b) {
        g_stream_waits++;
        while (d->pend == b) {
            volatile u32 k;
            for (k = 0; k < 1000u; k++) { __asm__ volatile("nop"); }
            stream_poll(d);
        }
        if (d->bidx[sl] == b) return sl;
    }
    if (d->pend >= 0) {   /* another block in flight: let it land, it is probably still useful */
        while (d->pend >= 0) {
            volatile u32 k;
            for (k = 0; k < 1000u; k++) { __asm__ volatile("nop"); }
            stream_poll(d);
        }
        if (d->bidx[sl] == b) return sl;
    }
    {
        u32 off = (u32)b * OL_FS_BLK, len = d->size - off;
        int got;
        if (len > OL_FS_BLK) len = OL_FS_BLK;
        int tries;
        d->bidx[sl] = -1;
        got = -1;
        for (tries = 0; tries < 3 && got != (int)len; tries++) {   /* A2.22.2: a failed or short read is retried */
            if (tries) g_stream_retry++;
            got = ol_fs_read_at(d->sname, off, d->ring + (u32)sl * OL_FS_BLK, len, 0);
        }
        if (got != (int)len) g_stream_bad++;
        if (got < 0) return -1;
        d->bidx[sl] = b; d->blen[sl] = (u32)got;
        g_stream_sync++;
    }
    return sl;
}

static u32 stream_read(ol_fd *d, void *dst, u32 want)
{
    u8 *out = (u8 *)dst;
    u32 done = 0;
    if (d->pos >= d->size) return 0;
    if (want > d->size - d->pos) want = d->size - d->pos;
    while (done < want) {
        int b = (int)(d->pos / OL_FS_BLK), s = stream_block(d, b);
        u32 in, n;
        if (s < 0) { g_stream_short++; break; }
        in = d->pos - (u32)b * OL_FS_BLK;
        if (in >= d->blen[s]) { g_stream_short++; break; }
        n = d->blen[s] - in;
        if (n > want - done) n = want - done;
        memcpy(out + done, d->ring + (u32)s * OL_FS_BLK + in, n);
        done += n; d->pos += n;
        d->ahead = b;
        stream_prefetch(d);
    }
    return done;
}

/* A2.21.2: called once per game frame: lands the block in flight and asks the next one */
void ol_fs_stream_pump(void)
{
    int i;
    for (i = 0; i < OL_FS_MAX_OPEN; i++) {
        ol_fd *d = &g_fd[i];
        if (!d->used || !d->is_stream) continue;
        stream_poll(d);
        stream_prefetch(d);
    }
}

void ol_fs_stream_errors(unsigned *retry, unsigned *bad, unsigned *bgbad, unsigned *bgfail, unsigned *shortr)
{
    *retry = g_stream_retry; *bad = g_stream_bad; *bgbad = g_stream_bgbad; *bgfail = g_stream_bgfail; *shortr = g_stream_short;
}

void ol_fs_stream_stats(unsigned *opens, unsigned *sync, unsigned *bg, unsigned *waits)
{
    if (opens) *opens = g_stream_opens;
    if (sync) *sync = g_stream_sync;
    if (bg) *bg = g_stream_bg;
    if (waits) *waits = g_stream_waits;
}

FILE *ol_fopen(const char *path, const char *mode)
{
    ol_fd *d;
    const void *data;
    unsigned size;
    if (!path || !mode) return 0;
    if (mode[0] == 'r') {
        data = ol_fs_find(path, &size);
        if (data) {
            d = fd_alloc();
            if (!d) return 0;
            d->is_mem = 1; d->data = (const u8 *)data; d->size = size; d->pos = 0;
            g_opens_mem++;
            return (FILE *)d;
        }
        if (served_by_68k(path)) {
            FILE *f;
            ol_fs_read_at_quiesce();   /* A2.21: no background request under a release one */
            f = zzq_fopen(basename_of(path), "rb");
            if (!f) { g_misses++; return 0; }
            d = fd_alloc();
            if (!d) { zzq_fclose(f); return 0; }
            d->is_mem = 0; d->sys = f;
            g_opens_sys++;
            return (FILE *)d;
        }
        if (!is_video(path)) {   /* A2.20: PROGDIR:data/<basename>, read whole */
            u8 head[1];
            unsigned total = 0;
            u8 *buf;
            u32 t0 = zzq_us_now();
            if (ol_fs_read_at(basename_of(path), 0, head, 1, &total) < 0 || total == 0 || total > OL_FS_LOOSE_MAX) {
                g_loose_misses++; g_misses++; return 0;
            }
            buf = (u8 *)zzq2_malloc(total);
            if (!buf) { g_misses++; return 0; }
            if (ol_fs_read_at(basename_of(path), 0, buf, total, 0) != (int)total) {
                zzq2_free(buf); g_misses++; return 0;
            }
            d = fd_alloc();
            if (!d) { zzq2_free(buf); return 0; }
            d->is_mem = 1; d->data = buf; d->own = buf; d->size = total; d->pos = 0;
            g_loose_opens++; g_loose_bytes += total; g_loose_us += zzq_us_now() - t0;
            return (FILE *)d;
        }
        {   /* A2.21: a video, streamed */
            u8 head[1];
            unsigned total = 0;
            const char *b = basename_of(path);
            if (strlen(b) >= sizeof(d->sname) || ol_fs_read_at(b, 0, head, 1, &total) < 0 || total == 0) {
                g_misses++; return 0;
            }
            d = fd_alloc();
            if (!d) return 0;
            d->ring = (u8 *)zzq2_malloc(OL_FS_BLK * OL_FS_NB);
            if (!d->ring) { d->used = 0; g_misses++; return 0; }
            strcpy(d->sname, b);
            d->is_stream = 1; d->is_mem = 1; d->size = total; d->pos = 0;
            { int k; for (k = 0; k < OL_FS_NB; k++) d->bidx[k] = -1; }
            d->pend = -1; d->ahead = -1;
            g_stream_opens++;
            return (FILE *)d;
        }
    }
    if (mode[0] == 'w' || mode[0] == 'a') {
        FILE *f;
        if (!served_by_68k(path)) { g_misses++; return 0; }
        ol_fs_read_at_quiesce();
        f = zzq_fopen(basename_of(path), "wb");
        if (!f) return 0;
        d = fd_alloc();
        if (!d) { zzq_fclose(f); return 0; }
        d->is_mem = 0; d->sys = f;
        g_opens_sys++;
        return (FILE *)d;
    }
    return 0;
}

int ol_fclose(FILE *f)
{
    ol_fd *d = (ol_fd *)f;
    int rc = 0;
    if (!d || !d->used) return -1;
    if (!d->is_mem && d->sys) rc = zzq_fclose(d->sys);
    if (d->own) { zzq2_free(d->own); d->own = 0; }
    if (d->is_stream) {   /* A2.21 */
        if (d->pend >= 0) ol_fs_read_at_quiesce();
        if (d->ring) zzq2_free(d->ring);
        d->ring = 0;
        d->is_stream = 0;
    }
    d->used = 0;
    return rc;
}

size_t ol_fread(void *dst, size_t sz, size_t n, FILE *f)
{
    ol_fd *d = (ol_fd *)f;
    u32 want, left;
    if (!d || !d->used || sz == 0) return 0;
    if (!d->is_mem) return zzq_fread(dst, sz, n, d->sys);
    if (d->is_stream) return stream_read(d, dst, (u32)(sz * n)) / sz;
    want = (u32)(sz * n);
    left = d->size - d->pos;
    if (want > left) want = left;
    if (want) memcpy(dst, d->data + d->pos, want);
    d->pos += want;
    return want / sz;
}

size_t ol_fwrite(const void *src, size_t sz, size_t n, FILE *f)
{
    ol_fd *d = (ol_fd *)f;
    if (!d || !d->used || d->is_mem) return 0;
    return zzq_fwrite(src, sz, n, d->sys);
}

int ol_fseek(FILE *f, long off, int whence)
{
    ol_fd *d = (ol_fd *)f;
    long np;
    if (!d || !d->used) return -1;
    if (!d->is_mem) return zzq_fseek(d->sys, off, whence);
    if (whence == SEEK_SET) np = off;
    else if (whence == SEEK_CUR) np = (long)d->pos + off;
    else np = (long)d->size + off;
    if (np < 0) np = 0;
    if (np > (long)d->size) np = (long)d->size;
    d->pos = (u32)np;
    return 0;
}

long ol_ftell(FILE *f)
{
    ol_fd *d = (ol_fd *)f;
    if (!d || !d->used) return -1;
    if (!d->is_mem) return zzq_ftell(d->sys);
    return (long)d->pos;
}

int ol_fgetc(FILE *f)
{
    ol_fd *d = (ol_fd *)f;
    if (!d || !d->used) return -1;
    if (!d->is_mem) return zzq_fgetc(d->sys);
    if (d->pos >= d->size) return -1;
    if (d->is_stream) { u8 c; return stream_read(d, &c, 1) == 1 ? c : -1; }
    return d->data[d->pos++];
}

int ol_feof(FILE *f)
{
    ol_fd *d = (ol_fd *)f;
    if (!d || !d->used) return 1;
    if (!d->is_mem) return zzq_feof(d->sys);
    return d->pos >= d->size;
}

int ol_fflush(FILE *f) { (void)f; return 0; }
int ol_remove(const char *path) { (void)path; return -1; }

void ol_fs_stats(u32 *opens_mem, u32 *opens_sys, u32 *misses)
{
    if (opens_mem) *opens_mem = g_opens_mem;
    if (opens_sys) *opens_sys = g_opens_sys;
    if (misses) *misses = g_misses;
}

/* A2.20: existence without reading the file: the container, else 1 byte of PROGDIR:data/<basename>
   (settings and savegame.dat live there too) */
int ol_fs_exists(const char *path)
{
    u8 head[1];
    unsigned total = 0;
    if (!path || !path[0]) return 0;
    if (ol_fs_find(path, 0)) return 1;
    return ol_fs_read_at(basename_of(path), 0, head, 1, &total) >= 0 && total > 0;
}

void ol_fs_loose_stats(unsigned *opens, unsigned *bytes, unsigned *misses, unsigned *ms)
{
    if (ms) *ms = g_loose_us / 1000u;
    if (opens) *opens = g_loose_opens;
    if (bytes) *bytes = g_loose_bytes;
    if (misses) *misses = g_loose_misses;
}

