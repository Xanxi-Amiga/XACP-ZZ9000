/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2_save.c - ZZQuake2 0.6: Quake II save games on the file service
 * of the ZZQuake640 release.
 *
 * WHAT QUAKE II DOES
 *   Quake II keeps its saves in directories under the game directory:
 *     save/current/   the game being played. On every map change the
 *                     state of the map left is written there
 *                     (<map>.sav: entities, <map>.sv2: configstrings
 *                     and area portals), plus server.ssv (comment, map
 *                     command, latched cvars) and game.ssv (game and
 *                     player state). Coming back to a map of the same
 *                     unit reloads its .sav: this is how the hubs of
 *                     the full game keep their state.
 *     save/save0 ..   the save slots of the menu (save0 is also the
 *     save/save14     autosave at each map change). Saving copies
 *                     every file of save/current into the slot,
 *                     loading copies them back. To do it, Quake II
 *                     lists the *.sav of a directory (Sys_FindFirst)
 *                     and deletes files (remove).
 *
 * WHAT THE RELEASE FILE SERVICE OFFERS (unchanged, launcher unchanged)
 *   One drawer (PROGDIR:baseq2), base names only, at most 63
 *   characters, no separator; READ, WRITE (atomic, through a .tmp) and
 *   STAT; 512 KiB per file; no listing; no delete (zzq_remove does
 *   nothing).
 *
 * WHAT THIS FILE DOES (Core1 side only)
 *   1. Names. "<gamedir>/save/<dir>/<file>" becomes the flat name
 *      "sv_<dir>_<file>" in the drawer, for example
 *          ./baseq2/save/current/demo2.sav -> sv_current_demo2.sav
 *          ./baseq2/save/save3/server.ssv  -> sv_save3_server.ssv
 *      <dir> must be 1 to 16 letters or digits (the menu uses current
 *      and save0..save14), so the first '_' after "sv_" always ends it
 *      and two different paths never share a name. Anything else under
 *      save/ is refused, as before.
 *   2. Delete. remove() of a save file writes it EMPTY; an empty save
 *      file is treated as absent everywhere (fopen for reading fails,
 *      listings skip it). Quake II never writes an empty save file.
 *   3. Listing. Sys_FindFirst/Next of "<...>/save/<dir>/*.sav" (or
 *      *.sv2) asks, map by map, whether sv_<dir>_<map>.sav exists
 *      (STAT, release Sys_FileTime). The maps are those of pak0.pak
 *      (its directory is read in memory, where the launcher preloaded
 *      it), plus any map name of a save file written during the
 *      session.
 *      Every other pattern still finds nothing, as upstream.
 *   4. A table of what is known (absent or empty / exists / written
 *      with data) for current and save0..save15, so that a name is
 *      asked to the launcher once per session, not at every listing.
 *      Only this blob writes these files while it runs.
 *   5. A write that does not go through (more than 512 KiB, launcher
 *      error, no answer) is reported in the console and the file is
 *      emptied, so that an OLDER content can never be loaded in its
 *      place.
 *   6. (0.8) The version stamp of game.ssv is no longer made here:
 *      functions and monster moves are saved by name (patched copy of
 *      game/g_save.c, Core1/zzq2_svtab.c), the stamp only names the
 *      layout of the saved structures, so every blob built from the
 *      same game code reads the save. (0.6 - 0.7: the launcher
 *      checksum of the blob, SH_IMG_SUM_68K.)
 *   7. (0.6.1) A request the release file layer gave up on (its 2 s
 *      limit) is still carried out by the launcher, which serves one
 *      request at a time. Nothing else is sent before that late
 *      answer has come (30 s at most): a write that answered late is
 *      a write that succeeded, a STAT takes the late answer, a read
 *      is asked again (up to 3 times). Seen on the hardware: up to
 *      1 s per request with MUSIC=MP3 reading the same disk, and once
 *      a save slot that came back without its .sv2 (probably this).
 *   8. (0.7) Compression. A save file is built in Core1 memory as
 *      Quake II writes it, and compressed when it is closed
 *      (Core1/zzq2_lz.c, LZ4 block layout); the release file service
 *      only ever sees the compressed file:
 *          bytes 0-3   'Z' 'Q' '2' 1   (format 1)
 *          bytes 4-7   size of the data once decompressed
 *          bytes 8-11  size of the compressed data that follows
 *          bytes 12-15 CRC-32 of the decompressed data
 *      Reading loads the whole file, checks both sizes and the CRC,
 *      and serves fread/fseek/ftell/feof/fgetc from memory; a damaged
 *      file is refused with a console message, never loaded. Why: on
 *      the full game a .sav at the START of a map is 108 to 524 KB,
 *      eight maps over the 512 KiB of the release buffers; compressed,
 *      7 to 40 times smaller. The 68k copies that many fewer bytes:
 *      saves and map changes get faster. A file without the header
 *      (written before 0.7) is read as it is.
 *
 * ASCII only.
 */

#include "qcommon/qcommon.h"
#include "zzquake_config.h"

extern volatile unsigned int *shared;

/* release file layer (zzquake_fs_mem.c), called as it is */
extern FILE        *zzq_fopen(const char *path, const char *mode);
extern int          zzq_fclose(FILE *f);
extern long         zzq_ftell(FILE *f);
extern int          zzq_feof(FILE *f);
extern int          zzq_remove(const char *path);
extern int          Sys_FileTime(char *path);
extern unsigned int zzq_fs_last_error(void);
extern unsigned int zzq_us_now(void);
extern int          zzq_fseek(FILE *f, long off, int whence);
extern size_t       zzq_fread(void *dst, size_t sz, size_t n, FILE *f);
extern size_t       zzq_fwrite(const void *src, size_t sz, size_t n, FILE *f);
extern int          zzq_fgetc(FILE *f);

/* zzq2_lz.c (0.7) */
extern unsigned int zzq2_lz_bound(unsigned int n);
extern unsigned int zzq2_lz_compress(const unsigned char *src, unsigned int n,
                                     unsigned char *dst);
extern int          zzq2_lz_decompress(const unsigned char *src, unsigned int n,
                                       unsigned char *dst, unsigned int out_n);

#define SV_DIRS      17          /* current, save0 .. save15          */
#define SV_MAPS      128         /* map names known                   */
#define SV_MAPLEN    32          /* longest map name                  */
#define SV_DIRLEN    16          /* longest save directory name       */
#define SV_FILES     (2 + 2 * SV_MAPS)

/* what is known of one file */
#define ST_UNKNOWN   0           /* not asked yet                     */
#define ST_ABSENT    1           /* does not exist, or is empty       */
#define ST_EXISTS    2           /* STAT says it exists (maybe empty) */
#define ST_PRESENT   3           /* holds data                        */

static unsigned char g_st[SV_DIRS][SV_FILES];
static char g_map[SV_MAPS][SV_MAPLEN + 1];
static int  g_nmaps;
static int  g_maps_ready;

typedef struct {
    char flat[ZZQ_FNAME_MAX];    /* name in the drawer                */
    int  dir;                    /* 0..16, -1 = not in the table      */
    int  file;                   /* 0..SV_FILES-1, -1 = not in table  */
} svname_t;

/* ------------------------------------------------------------------
   Map names
   ------------------------------------------------------------------ */

static unsigned int sv_le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static char sv_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* Index of a map name (lower case), added when add is set and there is
   room. -1 when unknown or no room. */
static int sv_map_index(const char *name, int len, int add)
{
    int i, k;
    if (len <= 0 || len > SV_MAPLEN) return -1;
    for (i = 0; i < g_nmaps; i++) {
        for (k = 0; k < len; k++)
            if (g_map[i][k] != name[k]) break;
        if (k == len && g_map[i][len] == 0) return i;
    }
    if (!add || g_nmaps >= SV_MAPS) return -1;
    for (k = 0; k < len; k++) g_map[g_nmaps][k] = name[k];
    g_map[g_nmaps][len] = 0;
    return g_nmaps++;
}

/* The maps of pak0.pak: entries "maps/<name>.bsp" of the pack
   directory, read where the launcher placed the pak (the same memory
   Quake II reads it from). */
static void sv_maps_init(void)
{
    const unsigned char *pak;
    unsigned int size, dirofs, dirlen, i, n;

    if (g_maps_ready) return;
    g_maps_ready = 1;
    pak  = (const unsigned char *)(unsigned long)shared[SH_PACK0_ADDR];
    size = shared[SH_PACK0_SIZE];
    if (!pak || size < 12 || pak[0] != 'P' || pak[1] != 'A' ||
        pak[2] != 'C' || pak[3] != 'K')
        return;
    dirofs = sv_le32(pak + 4);
    dirlen = sv_le32(pak + 8);
    if (dirofs > size || dirlen > size - dirofs) return;
    n = dirlen / 64u;
    for (i = 0; i < n; i++) {
        const char *e = (const char *)(pak + dirofs + i * 64u);
        char nm[57];
        int l = 0, k;
        while (l < 56 && e[l]) { nm[l] = sv_lower(e[l]); l++; }
        nm[l] = 0;
        if (l < 10 || nm[0] != 'm' || nm[1] != 'a' || nm[2] != 'p' ||
            nm[3] != 's' || nm[4] != '/' || nm[l - 4] != '.' ||
            nm[l - 3] != 'b' || nm[l - 2] != 's' || nm[l - 1] != 'p')
            continue;
        for (k = 5; k < l - 4; k++)
            if (nm[k] == '/' || nm[k] == '\\') break;
        if (k != l - 4) continue;                  /* maps/sub/x.bsp */
        (void)sv_map_index(nm + 5, l - 9, 1);
    }
}

/* ------------------------------------------------------------------
   Path -> flat name
   ------------------------------------------------------------------ */

static int sv_isalnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

/* Start of the directory name after the "/save/" component, 0 if the
   path has none. */
static const char *sv_after_save(const char *p)
{
    static const char pat[6] = { '/', 's', 'a', 'v', 'e', '/' };
    int i, j;
    if (!p) return 0;
    for (i = 0; p[i]; i++) {
        for (j = 0; j < 6; j++) {
            char a = p[i + j];
            if (a == '\\') a = '/';
            if (sv_lower(a) != pat[j]) break;
        }
        if (j == 6) return p + i + 6;
    }
    return 0;
}

/* Directory index: current = 0, save0..save15 = 1..16, else -1. */
static int sv_dir_index(const char *d)
{
    if (!strcmp(d, "current")) return 0;
    if (d[0] == 's' && d[1] == 'a' && d[2] == 'v' && d[3] == 'e') {
        const char *n = d + 4;
        if (n[0] >= '0' && n[0] <= '9' && !n[1]) return 1 + (n[0] - '0');
        if (n[0] == '1' && n[1] >= '0' && n[1] <= '5' && !n[2])
            return 1 + 10 + (n[1] - '0');
    }
    return -1;
}

/* Builds the flat name of <dir>/<file>. file is lower case. */
static void sv_make(svname_t *n, const char *dir, const char *file)
{
    int o = 0, k;
    n->flat[o++] = 's'; n->flat[o++] = 'v'; n->flat[o++] = '_';
    for (k = 0; dir[k]; k++) n->flat[o++] = dir[k];
    n->flat[o++] = '_';
    for (k = 0; file[k] && o < ZZQ_FNAME_MAX - 1; k++) n->flat[o++] = file[k];
    n->flat[o] = 0;
}

/* File index in the table: server.ssv 0, game.ssv 1, <map>.sav
   2 + 2m, <map>.sv2 3 + 2m; -1 for anything else. */
static int sv_file_index(const char *f, int add)
{
    int l = (int)strlen(f), m, sv2;
    if (!strcmp(f, "server.ssv")) return 0;
    if (!strcmp(f, "game.ssv"))   return 1;
    if (l < 5) return -1;
    if (!strcmp(f + l - 4, ".sav"))      sv2 = 0;
    else if (!strcmp(f + l - 4, ".sv2")) sv2 = 1;
    else return -1;
    sv_maps_init();
    m = sv_map_index(f, l - 4, add);
    if (m < 0) return -1;
    return 2 + 2 * m + sv2;
}

/* 0: not a save path. 1: n filled. -1: a save path that cannot be
   given a name (the caller refuses it, as A0 did). The file part may
   be "*.sav" or "*.sv2" when pattern is set; *star then points to the
   '*' of path. A map name met for the first time joins the list only
   when add is set (a file written): the demo playback, for one, asks
   for "save/current/q2demo1.dm2.sav", which is no map. */
static int sv_parse(const char *path, svname_t *n, int pattern,
                    const char **star, char *dirout, int add)
{
    const char *d = sv_after_save(path), *f;
    char dir[SV_DIRLEN + 1], file[SV_MAPLEN + 5];
    int i;

    if (!d) return 0;
    for (i = 0; d[i] && d[i] != '/' && d[i] != '\\'; i++) {
        char c = sv_lower(d[i]);
        if (i >= SV_DIRLEN || !sv_isalnum(c)) return -1;
        dir[i] = c;
    }
    if (i == 0 || !d[i]) return -1;
    dir[i] = 0;
    f = d + i + 1;
    for (i = 0; f[i]; i++) {
        char c = sv_lower(f[i]);
        if (i >= SV_MAPLEN + 4) return -1;
        if (c == '/' || c == '\\' || c == ':') return -1;
        if (c == '.' && f[i + 1] == '.') return -1;
        file[i] = c;
    }
    if (i == 0) return -1;
    file[i] = 0;
    if (dirout) strcpy(dirout, dir);
    if (pattern) {
        if (strcmp(file, "*.sav") && strcmp(file, "*.sv2")) return -1;
        if (star) *star = f;
        n->dir = sv_dir_index(dir);
        n->file = (file[4] == '2') ? 1 : 0;        /* the extension */
        n->flat[0] = 0;
        return 1;
    }
    for (i = 0; file[i]; i++)
        if (file[i] == '*' || file[i] == '?') return -1;
    sv_make(n, dir, file);
    n->dir  = sv_dir_index(dir);
    n->file = sv_file_index(file, add);
    return 1;
}

/* ------------------------------------------------------------------
   What is known
   ------------------------------------------------------------------ */

static int sv_get(const svname_t *n)
{
    if (n->dir < 0 || n->file < 0) return ST_UNKNOWN;
    return g_st[n->dir][n->file];
}

static void sv_set(const svname_t *n, int st)
{
    if (n->dir < 0 || n->file < 0) return;
    g_st[n->dir][n->file] = (unsigned char)st;
}

/* ------------------------------------------------------------------
   Late answers (0.6.1)
   ------------------------------------------------------------------ */

#define SV_LATE_US 30000000u                    /* 30 s               */

/* Called right after a release request gave up (SH_FSQ_TIMEOUT moved).
   The launcher is still busy with that request and will answer it:
   wait for its ACK before anything else is sent. A request posted now
   would only be served after it, and the late SH_FS_SIZE of a READ
   would overwrite the size of the new request. 1 if the late answer
   came and says OK; its SH_FS_EXISTS (STAT) is then valid. */
static int sv_wait_late(const char *what)
{
    unsigned int t0 = zzq_us_now(), ms;
    while (shared[SH_FS_ACK] != shared[SH_FS_SEQ]) {
        if ((unsigned int)(zzq_us_now() - t0) > SV_LATE_US) {
            Com_Printf("ZZQuake2: %s: no answer from the launcher\n", what);
            return 0;
        }
    }
    __asm__ volatile("dmb sy" ::: "memory");
    ms = 2000u + (unsigned int)(zzq_us_now() - t0) / 1000u;
    Com_Printf("ZZQuake2: %s: launcher answered after about %u ms\n", what, ms);
    return shared[SH_FS_ERR] == ZZFS_OK;
}

/* 1 if the file may hold data (asks the launcher once). */
static int sv_exists(svname_t *n)
{
    int st = sv_get(n);
    if (st == ST_UNKNOWN) {
        unsigned int tb = shared[SH_FSQ_TIMEOUT];
        if (Sys_FileTime(n->flat) >= 0)
            st = ST_EXISTS;
        else if (shared[SH_FSQ_TIMEOUT] != tb) {
            if (!sv_wait_late(n->flat))
                return 0;                       /* no answer: unknown */
            st = shared[SH_FS_EXISTS] ? ST_EXISTS : ST_ABSENT;
        } else if (zzq_fs_last_error() == ZZFS_OK)
            st = ST_ABSENT;                     /* a real "no"        */
        else
            return 0;
        sv_set(n, st);
    }
    return st == ST_EXISTS || st == ST_PRESENT;
}

/* Empties the file (the release has no delete). 0 on success. */
static int sv_wait_late(const char *what);

static int sv_erase(svname_t *n)
{
    unsigned int tb = shared[SH_FSQ_TIMEOUT];
    FILE *f = zzq_fopen(n->flat, "wb");
    if (!f) { sv_set(n, ST_UNKNOWN); return -1; }
    if (zzq_fclose(f) != 0 &&
        !(shared[SH_FSQ_TIMEOUT] != tb && sv_wait_late(n->flat))) {
        sv_set(n, ST_UNKNOWN);
        return -1;
    }
    sv_set(n, ST_ABSENT);
    return 0;
}

/* ------------------------------------------------------------------
   Files being written
   ------------------------------------------------------------------ */

#define SV_OPEN 8
static FILE    *g_wf[SV_OPEN];
static svname_t g_wn[SV_OPEN];

static void sv_track(FILE *f, const svname_t *n)
{
    int i;
    for (i = 0; i < SV_OPEN; i++)
        if (!g_wf[i]) { g_wf[i] = f; g_wn[i] = *n; return; }
}

/* ------------------------------------------------------------------
   Save files in memory, compressed on disk (0.7)
   ------------------------------------------------------------------ */

#define SVZ_HDR     16u
#define SVZ_MAXRAW  (16u << 20)          /* refuse a header above 16 MiB  */
#define SVF_LIVE    0x53564631u
#define SVF_MAX     8

typedef struct {
    unsigned int   live;                 /* SVF_LIVE while open           */
    int            writing;
    unsigned char *buf;
    unsigned int   len, cap, pos;
    svname_t       n;
} svf_t;

static svf_t g_svf[SVF_MAX];

/* The svf_t behind a FILE *, or 0 for a release FILE. Only pointers
   into g_svf are ever looked at. */
static svf_t *svf_of(FILE *f)
{
    const char *p = (const char *)f;
    const char *a = (const char *)&g_svf[0], *b = (const char *)&g_svf[SVF_MAX];
    if (!f || p < a || p >= b || (unsigned int)(p - a) % sizeof(svf_t)) return 0;
    return ((svf_t *)f)->live == SVF_LIVE ? (svf_t *)f : 0;
}

static svf_t *svf_alloc(const svname_t *n, int writing)
{
    int i;
    for (i = 0; i < SVF_MAX; i++)
        if (g_svf[i].live != SVF_LIVE) {
            memset(&g_svf[i], 0, sizeof(g_svf[i]));
            g_svf[i].live = SVF_LIVE;
            g_svf[i].writing = writing;
            g_svf[i].n = *n;
            return &g_svf[i];
        }
    return 0;
}

static void svf_free(svf_t *s)
{
    if (s->buf) zzq2_free(s->buf);
    memset(s, 0, sizeof(*s));
}

/* Room for `need` bytes. 0 on success. */
static int svf_reserve(svf_t *s, unsigned int need)
{
    unsigned int cap;
    unsigned char *nb;
    if (need <= s->cap) return 0;
    if (need > SVZ_MAXRAW) return -1;
    cap = s->cap ? s->cap : 65536u;
    while (cap < need) cap *= 2u;
    if (cap > SVZ_MAXRAW) cap = SVZ_MAXRAW;
    nb = (unsigned char *)zzq2_malloc(cap);
    if (!nb) return -1;
    if (s->len) memcpy(nb, s->buf, s->len);
    if (s->buf) zzq2_free(s->buf);
    s->buf = nb;
    s->cap = cap;
    return 0;
}

static unsigned int g_crc_tab[256];

static unsigned int sv_crc32(const unsigned char *p, unsigned int n)
{
    unsigned int c = 0xFFFFFFFFu, i;
    if (!g_crc_tab[1]) {
        unsigned int k, b;
        for (k = 0; k < 256u; k++) {
            unsigned int v = k;
            for (b = 0; b < 8u; b++) v = (v >> 1) ^ (0xEDB88320u & (0u - (v & 1u)));
            g_crc_tab[k] = v;
        }
    }
    for (i = 0; i < n; i++) c = g_crc_tab[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void put32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static unsigned int get32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* The release file f (already open, not empty) is read whole, closed,
   decompressed and checked. Returns the FILE * of a svf_t, or 0. */
static FILE *svf_load(FILE *f, const svname_t *n)
{
    long sz;
    unsigned char *tmp;
    size_t got;
    svf_t *s;
    const char *why = 0;

    zzq_fseek(f, 0, 2);
    sz = zzq_ftell(f);
    zzq_fseek(f, 0, 0);
    if (sz <= 0) { zzq_fclose(f); return 0; }
    tmp = (unsigned char *)zzq2_malloc((size_t)sz);
    if (!tmp) { zzq_fclose(f); why = "no memory"; goto bad; }
    got = zzq_fread(tmp, 1, (size_t)sz, f);
    zzq_fclose(f);
    if (got != (size_t)sz) { zzq2_free(tmp); why = "short read"; goto bad; }
    s = svf_alloc(n, 0);
    if (!s) { zzq2_free(tmp); why = "too many save files open"; goto bad; }
    if (sz >= (long)SVZ_HDR && tmp[0] == 'Z' && tmp[1] == 'Q' && tmp[2] == '2' && tmp[3] == 1) {
        unsigned int raw = get32(tmp + 4), comp = get32(tmp + 8), crc = get32(tmp + 12);
        if (comp != (unsigned int)sz - SVZ_HDR) why = "size";
        else if (raw > SVZ_MAXRAW) why = "header";
        else if (svf_reserve(s, raw ? raw : 1u)) why = "no memory";
        else if (zzq2_lz_decompress(tmp + SVZ_HDR, comp, s->buf, raw)) why = "data";
        else if (sv_crc32(s->buf, raw) != crc) why = "CRC";
        zzq2_free(tmp);
        if (why) { svf_free(s); goto bad; }
        s->len = raw;
    } else {
        /* written before 0.7: used as it is */
        s->buf = tmp;
        s->len = s->cap = (unsigned int)sz;
    }
    return (FILE *)s;
bad:
    Com_Printf("ZZQuake2: %s is damaged (%s), not loaded\n", n->flat, why);
    return 0;
}

static int sv_release_close(FILE *f);

/* Close of a save file being written: compress, then hand the
   compressed file to the release file layer. */
static int svf_close_write(svf_t *s)
{
    svname_t n = s->n;
    unsigned int raw = s->len, crc, clen;
    unsigned char *z;
    FILE *f;

    if (raw == 0) {                              /* empty = deleted   */
        svf_free(s);
        return sv_erase(&n);
    }
    crc = sv_crc32(s->buf, raw);
    z = (unsigned char *)zzq2_malloc(SVZ_HDR + zzq2_lz_bound(raw));
    if (!z) {
        svf_free(s);
        Com_Printf("ZZQuake2: %s NOT written (no memory)\n", n.flat);
        sv_erase(&n);                   /* as a failed write */
        return -1;
    }
    clen = zzq2_lz_compress(s->buf, raw, z + SVZ_HDR);
    z[0] = 'Z'; z[1] = 'Q'; z[2] = '2'; z[3] = 1;
    put32(z + 4, raw);
    put32(z + 8, clen);
    put32(z + 12, crc);
    svf_free(s);
    Com_DPrintf("ZZQuake2: %s %u -> %u bytes\n", n.flat, raw, SVZ_HDR + clen);
    f = zzq_fopen(n.flat, "wb");
    if (!f) {
        zzq2_free(z);
        Com_Printf("ZZQuake2: %s NOT written (no file buffer free)\n", n.flat);
        sv_erase(&n);                   /* as a failed write */
        return -1;
    }
    zzq_fwrite(z, 1, SVZ_HDR + clen, f);
    zzq2_free(z);
    sv_track(f, &n);
    return sv_release_close(f);
}

/* The stdio calls of the Quake II objects (zzq2_stdio.h). A save file
   is served here; any other FILE goes to the release function. */
size_t zzq2_fread(void *dst, size_t sz, size_t cnt, FILE *f)
{
    svf_t *s = svf_of(f);
    unsigned int want, avail;
    if (!s) return zzq_fread(dst, sz, cnt, f);
    if (s->writing || sz == 0) return 0;
    want = (unsigned int)(sz * cnt);
    avail = s->len - s->pos;
    if (want > avail) want = avail;
    memcpy(dst, s->buf + s->pos, want);
    s->pos += want;
    return want / sz;
}

size_t zzq2_fwrite(const void *src, size_t sz, size_t cnt, FILE *f)
{
    svf_t *s = svf_of(f);
    unsigned int n;
    if (!s) return zzq_fwrite(src, sz, cnt, f);
    if (!s->writing || sz == 0) return 0;
    n = (unsigned int)(sz * cnt);
    if (svf_reserve(s, s->pos + n)) return 0;
    memcpy(s->buf + s->pos, src, n);
    s->pos += n;
    if (s->pos > s->len) s->len = s->pos;
    return cnt;
}

int zzq2_fseek(FILE *f, long off, int whence)
{
    svf_t *s = svf_of(f);
    long p;
    if (!s) return zzq_fseek(f, off, whence);
    p = whence == 1 ? (long)s->pos + off : whence == 2 ? (long)s->len + off : off;
    if (p < 0) p = 0;
    if (p > (long)s->len) p = (long)s->len;
    s->pos = (unsigned int)p;
    return 0;
}

long zzq2_ftell(FILE *f)
{
    svf_t *s = svf_of(f);
    if (!s) return zzq_ftell(f);
    return (long)s->pos;
}

int zzq2_feof(FILE *f)
{
    svf_t *s = svf_of(f);
    if (!s) return zzq_feof(f);
    return s->pos >= s->len;
}

int zzq2_fgetc(FILE *f)
{
    svf_t *s = svf_of(f);
    if (!s) return zzq_fgetc(f);
    if (s->writing || s->pos >= s->len) return -1;
    return s->buf[s->pos++];
}

/* ------------------------------------------------------------------
   Entry points
   ------------------------------------------------------------------ */

/* zzq2_fopen (zzq2_glue.c) for a path with a "save" component. */
FILE *zzq2_save_fopen(const char *path, const char *mode)
{
    svname_t n;
    FILE *f;

    if (sv_parse(path, &n, 0, 0, 0, mode && mode[0] == 'w') != 1) {
        Com_DPrintf("ZZQuake2: save path refused: %s\n", path);
        return 0;
    }
    if (mode && mode[0] == 'r') {
        int k;
        if (sv_get(&n) == ST_ABSENT) return 0;
        f = 0;
        for (k = 0; k < 3 && !f; k++) {
            unsigned int q = shared[SH_FSQ_TOTAL], tb = shared[SH_FSQ_TIMEOUT];
            f = zzq_fopen(n.flat, "rb");
            if (f) break;
            if (shared[SH_FSQ_TIMEOUT] != tb) {
                /* given up after 2 s: wait for the late answer, then
                   ask again (the file is in the disk cache by then) */
                if (!sv_wait_late(n.flat)) return 0;
                continue;
            }
            /* "absent" only on a real answer of the launcher (a request
               went out and it said "cannot open") */
            if (shared[SH_FSQ_TOTAL] != q &&
                zzq_fs_last_error() == ZZFS_ERR_OPEN)
                sv_set(&n, ST_ABSENT);
            return 0;
        }
        if (!f) {
            Com_Printf("ZZQuake2: %s could not be read\n", n.flat);
            return 0;
        }
        if (zzq_feof(f)) {                      /* empty: deleted     */
            zzq_fclose(f);
            sv_set(&n, ST_ABSENT);
            return 0;
        }
        sv_set(&n, ST_PRESENT);
        return svf_load(f, &n);                 /* 0.7: in memory     */
    }
    if (mode && mode[0] == 'w') {
        /* 0.7: built in memory, compressed and written at fclose */
        svf_t *s = svf_alloc(&n, 1);
        if (!s) Com_Printf("ZZQuake2: %s: too many save files open\n", n.flat);
        return (FILE *)s;
    }
    return 0;                                   /* no append on saves */
}

/* fclose of every Quake II object (zzq2_stdio.h). */
int zzq2_fclose(FILE *f)
{
    svf_t *s = svf_of(f);
    if (s) {
        if (s->writing) return svf_close_write(s);
        svf_free(s);
        return 0;
    }
    return sv_release_close(f);
}

/* Close of a release FILE; for a save file being written (tracked),
   the outcome is recorded and a failure is reported. */
static int sv_release_close(FILE *f)
{
    int i, r;
    long size;
    svname_t n;

    for (i = 0; i < SV_OPEN; i++)
        if (f && g_wf[i] == f) break;
    if (i == SV_OPEN) return zzq_fclose(f);

    n = g_wn[i];
    g_wf[i] = 0;
    size = zzq_ftell(f);
    {
        unsigned int tb = shared[SH_FSQ_TIMEOUT];
        r = zzq_fclose(f);
        /* given up after 2 s but written by the launcher all the same */
        if (r != 0 && shared[SH_FSQ_TIMEOUT] != tb && sv_wait_late(n.flat))
            r = 0;
    }
    if (r == 0) {
        sv_set(&n, size > 0 ? ST_PRESENT : ST_ABSENT);
        return 0;
    }
    {
        unsigned int e = zzq_fs_last_error();
        Com_Printf("ZZQuake2: %s NOT written (%s)\n", n.flat,
                   e == ZZFS_ERR_TOO_LARGE ? "over 512 KiB" :
                   e == ZZFS_ERR_TIMEOUT   ? "no answer from the launcher" :
                                             "launcher error");
    }
    if (sv_erase(&n) != 0)
        Com_Printf("ZZQuake2: %s could not be emptied either\n", n.flat);
    return r;
}

/* remove() of every Quake II object (zzq2_stdio.h). Only the save
   files are concerned; anything else keeps the release behaviour. */
int zzq2_remove(const char *path)
{
    svname_t n;
    int r = sv_parse(path, &n, 0, 0, 0, 0);
    if (r == 0) return zzq_remove(path);
    if (r < 0) return -1;
    if (!sv_exists(&n)) return 0;               /* nothing to delete  */
    return sv_erase(&n);
}

/* Sys_FindFirst / Sys_FindNext / Sys_FindClose (removed from the
   patched copy of other/q_system.c, where they found nothing). */
static struct {
    int  active;
    int  next;
    int  sv2;
    int  dir;
    char dname[SV_DIRLEN + 1];
    int  plen;                                  /* chars before '*'   */
    char out[MAX_OSPATH];
} g_find;

char *Sys_FindNext(unsigned musthave, unsigned canthave)
{
    (void)musthave; (void)canthave;
    if (!g_find.active) return 0;
    while (g_find.next < g_nmaps) {
        svname_t n;
        char file[SV_MAPLEN + 5];
        int m = g_find.next++, l = (int)strlen(g_map[m]);
        if (g_find.plen + l + 4 >= MAX_OSPATH) continue;
        strcpy(file, g_map[m]);
        strcpy(file + l, g_find.sv2 ? ".sv2" : ".sav");
        sv_make(&n, g_find.dname, file);
        n.dir  = g_find.dir;
        n.file = 2 + 2 * m + g_find.sv2;
        if (!sv_exists(&n)) continue;
        strcpy(g_find.out + g_find.plen, file);
        return g_find.out;
    }
    return 0;
}

char *Sys_FindFirst(char *path, unsigned musthave, unsigned canthave)
{
    svname_t n;
    const char *star = 0;

    g_find.active = 0;
    if (sv_parse(path, &n, 1, &star, g_find.dname, 0) != 1 || !star)
        return 0;                               /* upstream: nothing  */
    g_find.plen = (int)(star - path);
    if (g_find.plen + 1 >= MAX_OSPATH) return 0;
    memcpy(g_find.out, path, (size_t)g_find.plen);
    g_find.out[g_find.plen] = 0;
    g_find.dir  = n.dir;
    g_find.sv2  = n.file;
    g_find.next = 0;
    g_find.active = 1;
    sv_maps_init();
    return Sys_FindNext(musthave, canthave);
}

void Sys_FindClose(void)
{
    g_find.active = 0;
}
