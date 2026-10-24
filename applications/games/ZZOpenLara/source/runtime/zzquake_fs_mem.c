/* Engine stdio is redirected to the in-memory filesystem. */






















#include <stddef.h>
#include <stdarg.h>   
#include "zzquake_config.h"

typedef unsigned int  u32;
typedef unsigned char u8;

extern volatile u32 *shared;          /* zzquake_main.c */
extern void zzq_record_file_req(const char *path);   /* main.c */

/* ------------------------------------------------------------------ */
/* Descriptor pool shared by both layers                              */
/* ------------------------------------------------------------------ */

#define ZZQ_MAX_FILES 12

typedef struct {
    int used;
    int is_pak;       /* 1 = pak virtuel                   */
    int is_write;     
    int slot;         /* Shared-memory protocol state. */
    u32 base;         /* ARM address of pak/loose data     */
    u32 size;
    u32 pos;
    u32 error;
    char name[ZZQ_FNAME_MAX]; 
} zzq_fd_t;

static zzq_fd_t g_fd[ZZQ_MAX_FILES];


static zzq_fd_t *zzq_open_read_common(const char *path);
static zzq_fd_t *zzq_open_write_common(const char *path);
static int fd_write(zzq_fd_t *d, const void *src, u32 count);
static int fd_flush_write(zzq_fd_t *d);

/* Engine stdio is redirected to the in-memory filesystem. */

#define TRACE(op) do { shared[SH_LAST_STDIO_OP] = (op); \
                       shared[SH_STDIO_OP_COUNT]++; } while (0)

/* case-insensitive ".dem" suffix test, for the A2-demoOFF build */
static int is_dem_path(const char *p)
{
    int l = 0;
    if (!p) return 0;
    while (p[l]) l++;
    if (l < 4) return 0;
    { char a = p[l-4], b = p[l-3], c = p[l-2], d = p[l-1];
      if (a >= 'A' && a <= 'Z') a += 32;
      if (b >= 'A' && b <= 'Z') b += 32;
      if (c >= 'A' && c <= 'Z') c += 32;
      if (d >= 'A' && d <= 'Z') d += 32;
      return (a == '.' && b == 'd' && c == 'e' && d == 'm'); }
}

static zzq_fd_t *fd_alloc(void)
{
    int i, k;
    zzq_fd_t *d;
    for (i = 0; i < ZZQ_MAX_FILES; i++) {
        if (g_fd[i].used) continue;
        d = &g_fd[i];

        /* A descriptor is recycled. Leaving flags from its previous
           life makes the next open order-dependent. Reset everything
           here instead of relying on each caller. */
        d->used = 1;
        d->is_pak = 0;
        d->is_write = 0;
        d->slot = -1;
        d->base = 0;
        d->size = 0;
        d->pos = 0;
        d->error = ZZFS_OK;
        for (k = 0; k < ZZQ_FNAME_MAX; k++) d->name[k] = 0;
        return d;
    }
    return 0;
}

static void fd_free(zzq_fd_t *d)
{
    int k;
    if (!d) return;
    d->used = 0;
    d->is_pak = 0;
    d->is_write = 0;
    d->slot = -1;
    d->base = 0;
    d->size = 0;
    d->pos = 0;
    d->error = ZZFS_OK;
    for (k = 0; k < ZZQ_FNAME_MAX; k++) d->name[k] = 0;
}

/* case-insensitive "ends with pak0.pak" */
/* PAK filesystem handling. */





static int path_has(const char *p, const char *dir)
{
    int i, j;
    for (i = 0; p[i]; i++) {
        for (j = 0; dir[j]; j++) {
            char a = p[i + j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (a != dir[j]) break;
        }
        if (!dir[j]) return 1;
    }
    return 0;
}

/* PAK filesystem handling. */






static const char *mod_name(void)
{
    static char nm[68];
    static int  done = 0;
    int i;
    if (!done) {
        for (i = 0; i < 16; i++) {
            u32 w = shared[SH_MOD_NAME + i];
            nm[i*4+0] = (char)(w & 0xFF);
            nm[i*4+1] = (char)((w >> 8) & 0xFF);
            nm[i*4+2] = (char)((w >> 16) & 0xFF);
            nm[i*4+3] = (char)((w >> 24) & 0xFF);
        }
        nm[63] = 0;
        done = 1;
    }
    return nm;
}

/* PAK filesystem handling. */







static int pak_index(const char *p)
{
    int lp = 0, i, digit = -1;
    if (!p) return -1;
    while (p[lp]) lp++;
    if (lp < 8) return -1;
    /* forme pakN.pak ? */
    {   const char *s = p + lp - 8;
        char c;
        c = s[0]; if (c >= 'A' && c <= 'Z') c += 32; if (c != 'p') return -1;
        c = s[1]; if (c >= 'A' && c <= 'Z') c += 32; if (c != 'a') return -1;
        c = s[2]; if (c >= 'A' && c <= 'Z') c += 32; if (c != 'k') return -1;
        if (s[3] < '0' || s[3] > '9') return -1;
        digit = s[3] - '0';
        c = s[4]; if (c != '.') return -1;
        c = s[5]; if (c >= 'A' && c <= 'Z') c += 32; if (c != 'p') return -1;
        c = s[6]; if (c >= 'A' && c <= 'Z') c += 32; if (c != 'a') return -1;
        c = s[7]; if (c >= 'A' && c <= 'Z') c += 32; if (c != 'k') return -1;
    }

    /* mission pack officiel */
    if (path_has(p, "hipnotic") || path_has(p, "rogue")) {
        shared[SH_MP_PROBES]++;
        if (digit != 0) return -1;             /* PAK filesystem handling. */
        if (shared[SH_PACK_COUNT] > 2 && shared[SH_PACK2_SIZE]) {
            if ((shared[SH_GAME_MODE] == ZZQ_GAME_HIPNOTIC &&
                 path_has(p, "hipnotic")) ||
                (shared[SH_GAME_MODE] == ZZQ_GAME_ROGUE &&
                 path_has(p, "rogue"))) {
                shared[SH_MP_OPENS]++;
                return 2;
            }
        }
        return -1;
    }

    
    if (shared[SH_GAME_MODE] == ZZQ_GAME_MOD && shared[SH_MOD_COUNT]) {
        const char *m = mod_name();
        if (m[0] && path_has(p, m)) {
            shared[SH_MP_PROBES]++;
            if (digit < (int)shared[SH_MOD_COUNT] &&
                digit < ZZQ_MODPACK_MAX &&
                shared[SH_MOD_SIZE + digit]) {
                shared[SH_MP_OPENS]++;
                return 10 + digit;
            }
            return -1;                         /* PAK filesystem handling. */
        }
    }

    /* id1 */
    if (digit == 0) return 0;
    if (digit == 1 && shared[SH_PACK_COUNT] > 1 && shared[SH_PACK1_SIZE])
        return 1;
    return -1;
}

static int is_pak_path(const char *p)
{
    return pak_index(p) >= 0;
}



static zzq_fd_t *open_pak_n(int n)
{
    zzq_fd_t *d;
    u32 base, size;
    if      (n >= 10) { base = shared[SH_MOD_ADDR + (n - 10)];
                        size = shared[SH_MOD_SIZE + (n - 10)]; }
    else if (n == 2) { base = shared[SH_PACK2_ADDR]; size = shared[SH_PACK2_SIZE]; }
    else if (n == 1) { base = shared[SH_PACK1_ADDR]; size = shared[SH_PACK1_SIZE]; }
    else             { base = shared[SH_PACK0_ADDR]; size = shared[SH_PACK0_SIZE]; }
    if (!base || !size) return 0;
    d = fd_alloc();
    if (!d) return 0;
    d->is_pak = 1;
    d->is_write = 0;
    d->slot = -1;
    d->base = base;
    d->size = size;
    d->pos  = 0;
    return d;
}

extern void zzq_publish_engine_state(void);   /* zzquake_qgmain.c */

/* CRC32 over a short span, same polynomial as the A1 pak check. */
u32 zzq_crc32(const u8 *p, u32 len)
{
    u32 crc = 0xFFFFFFFFu, i;
    int b;
    for (i = 0; i < len; i++) {
        crc ^= p[i];
        for (b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

static u32 rd_le32_at(const u8 *p) {
    return ((u32)p[0]) | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u32 fd_read(zzq_fd_t *d, void *dst, u32 count)
{
    u32 remain, n, i;
    const u8 *src;
    u8 *out = (u8 *)dst;

    /* E FIX: fd_read is the common reader for BOTH PAK descriptors and
       68k-backed loose files (savegames/config.cfg).  Reject only an
       invalid/write descriptor, not every non-PAK descriptor. */
    if (!d || !d->used || d->is_write || d->base == 0) return 0;
    remain = (d->pos < d->size) ? (d->size - d->pos) : 0;
    n = (count < remain) ? count : remain;
    src = (const u8 *)(d->base + d->pos);

    



    /* Keep the LAST big read, not the first. The first one is
       COM_LoadPackFile reading the 21696-byte pak directory, whose
       bytes are filenames - useless here. The last big read before
       the failure is the model we care about. */
    if (d->is_pak && n >= 4096u) {
        shared[SH_BIGRD_COUNT]     = count;
        shared[SH_BIGRD_POS]       = d->pos;
        shared[SH_BIGRD_W0]        = rd_le32_at(src + 0);
        shared[SH_BIGRD_W1]        = rd_le32_at(src + 4);
        shared[SH_BIGRD_NUMSKINS]  = rd_le32_at(src + 48);
        shared[SH_BIGRD_NUMVERTS]  = rd_le32_at(src + 60);
        shared[SH_BIGRD_NUMTRIS]   = rd_le32_at(src + 64);
        shared[SH_BIGRD_NUMFRAMES] = rd_le32_at(src + 68);
        shared[SH_BIGRD_DONE]      = shared[SH_BIGRD_DONE] + 1;  /* count */

        
        {
            static const int off[8] = { 0, 4, 48, 52, 56, 60, 64, 68 };
            int k;
            shared[SH_MDL_FILEPOS] = d->pos;
            shared[SH_MDL_LEN]     = n;
            shared[SH_MDL_SRC]     = (u32)(unsigned long)src;
            shared[SH_MDL_DST]     = (u32)(unsigned long)out;
            for (k = 0; k < 8; k++)
                shared[SH_MDL_S_BASE + k] = rd_le32_at(src + off[k]);
            shared[SH_MDL_CRC_S] = zzq_crc32(src, 84u);
        }
        zzq_publish_engine_state();
    }

    for (i = 0; i < n; i++) out[i] = src[i];
    d->pos += n;

    /* ---- D0 : the same header AT THE DESTINATION, right after the
       copy. If S is right and D0 is wrong, the copy is the culprit. */
    if (d->is_pak && n >= 4096u) {
        static const int off[8] = { 0, 4, 48, 52, 56, 60, 64, 68 };
        int k;
        for (k = 0; k < 8; k++)
            shared[SH_MDL_D0_BASE + k] = rd_le32_at(out + off[k]);
        shared[SH_MDL_CRC_D0] = zzq_crc32(out, 84u);
        shared[SH_MDL_VALID]  = 1;
    }
    if (d->is_pak) {
        shared[SH_PAK_READ_COUNT]++;
        shared[SH_PAK_BYTES_READ] += n;
    }
    return n;
}

static void fd_seek(zzq_fd_t *d, long pos, int whence)
{
    long p;
    if (!d) return;
    if (whence == 1)      p = (long)d->pos + pos;   /* SEEK_CUR */
    else if (whence == 2) p = (long)d->size + pos;  /* SEEK_END */
    else                  p = pos;                  /* SEEK_SET */
    if (p < 0) p = 0;
    if ((u32)p > d->size) p = (long)d->size;
    d->pos = (u32)p;
    if (d->is_pak) shared[SH_PAK_SEEK_COUNT]++;
}

/* ------------------------------------------------------------------ */
/* Layer 1: Sys_File* handles (sys.h contract)                        */
/* handle = index into g_fd, 0 reserved as invalid                    */
/* ------------------------------------------------------------------ */

/* PAK filesystem handling. */










static zzq_fd_t *g_sys_fd = 0; /* only for dormant newlib _open path */
static u32   g_fs_last_err = ZZFS_OK;
extern void zzq_ftoa_prec(char *dst, int cap, float v, int prec);
extern u32  zzq_us_now(void);

/* Shared-memory protocol state. */




static unsigned char g_slot_used[ZZQ_FILEBUF_SLOTS];

/* Shared-memory protocol state. */

static void fs_slot_publish(void)
{
    int i; u32 m = 0, n = 0;
    for (i = 0; i < ZZQ_FILEBUF_SLOTS; i++)
        if (g_slot_used[i]) { m |= (1u << i); n++; }
    shared[SH_FS_SLOTMASK] = m;
    if (n > shared[SH_FS_SLOTPEAK]) shared[SH_FS_SLOTPEAK] = n;
}

static int fs_slot_alloc(void)
{
    int i;
    for (i = 0; i < ZZQ_FILEBUF_SLOTS; i++)
        if (!g_slot_used[i]) {
            g_slot_used[i] = 1;
            shared[SH_FS_OPENS]++;
            fs_slot_publish();
            return i;
        }
    return -1;
}

static void fs_slot_free(int s)
{
    if (s >= 0 && s < ZZQ_FILEBUF_SLOTS && g_slot_used[s]) {
        g_slot_used[s] = 0;
        shared[SH_FS_CLOSES]++;
        fs_slot_publish();
    }
}

static unsigned char *fs_buf_at(int s)
{
    return (unsigned char *)ZZQ_FILEBUF_AT((s >= 0 && s < ZZQ_FILEBUF_SLOTS) ? s : 0);
}

/* Copy only the basename, and always terminate. */
static void fs_copy_basename(char *out, const char *path)
{
    const char *b = path, *p;
    u32 i;
    if (!path) { out[0] = 0; return; }
    for (p = path; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':') b = p + 1;
    for (i = 0; i < ZZQ_FNAME_MAX - 1 && b[i]; i++) out[i] = b[i];
    out[i] = 0;
}

/* Publish a basename into the shared request block. */
static void fs_publish_name(const char *path)
{
    char name[ZZQ_FNAME_MAX];
    u32 i;
    fs_copy_basename(name, path);
    for (i = 0; i < ZZQ_FNAME_MAX / 4; i++) {
        u32 w = 0, k;
        for (k = 0; k < 4; k++) {
            u8 c = 0;
            u32 j = i * 4 + k;
            if (j < ZZQ_FNAME_MAX && name[j]) c = (u8)name[j];
            w |= ((u32)c) << (k * 8);
        }
        shared[SH_FS_NAME + i] = w;
    }
}

/* One synchronous request.  Name and slot are explicit parameters:
   no request is allowed to depend on a global "current file". */
static int fs_request(const char *path, int slot, u32 cmd, u32 size)
{
    u32 seq, start;
    fs_publish_name(path);
    shared[SH_FS_SIZE]   = size;
    shared[SH_FS_OFFSET] = 0;
    shared[SH_FS_CHUNK]  = size;
    shared[SH_FS_ERR]    = ZZFS_OK;
    shared[SH_FS_SLOT]   = (u32)((slot >= 0 && slot < ZZQ_FILEBUF_SLOTS) ? slot : 0);
    shared[SH_FS_CMD]    = cmd;
    __asm__ volatile("dsb sy" ::: "memory");
    seq = shared[SH_FS_SEQ] + 1;
    shared[SH_FS_SEQ] = seq;           /* trigger LAST */
    __asm__ volatile("dsb sy" ::: "memory");

    shared[SH_FSQ_TOTAL]++;
    start = zzq_us_now();
    while (shared[SH_FS_ACK] != seq) {
        volatile u32 k;
        for (k = 0; k < 1000u; k++) { __asm__ volatile("nop"); }
        if ((u32)(zzq_us_now() - start) > 2000000u) {
            


            int i;
            g_fs_last_err = ZZFS_ERR_TIMEOUT;
            shared[SH_FSQ_TIMEOUT]++;
            for (i = 0; i < ZZQ_FNAME_MAX / 4; i++)
                shared[SH_FSQ_LASTFAIL + i] = shared[SH_FS_NAME + i];
            return 0;
        }
    }
    __asm__ volatile("dmb sy" ::: "memory");
    {   
        u32 w = (u32)(zzq_us_now() - start);
        if (w > shared[SH_FSQ_WAIT_MAX]) shared[SH_FSQ_WAIT_MAX] = w;
        shared[SH_FSQ_WAIT_SUM_MS] += w / 1000u;
        if      (w <    1000u) shared[SH_FSQ_H0]++;
        else if (w <   10000u) shared[SH_FSQ_H1]++;
        else if (w <  100000u) shared[SH_FSQ_H2]++;
        else if (w < 1000000u) shared[SH_FSQ_H3]++;
        else                   shared[SH_FSQ_H4]++;
    }
    g_fs_last_err = shared[SH_FS_ERR];
    if (g_fs_last_err != ZZFS_OK) shared[SH_FSQ_ERR68K]++;
    return (g_fs_last_err == ZZFS_OK);
}

u32 zzq_fs_last_error(void) { return g_fs_last_err; }

int Sys_FileOpenRead(char *path, int *hndl)
{
    zzq_record_file_req(path);
    TRACE(ZZQ_OP_SYSOPEN);
    if (shared[SH_ENABLE_DEMO_OFF] && is_dem_path(path)) {
        shared[SH_DEMO_BLOCKED]++;
        shared[SH_NOTFOUND_BENIGN]++;
        *hndl = -1;
        return -1;
    }
    if (is_pak_path(path)) {
        zzq_fd_t *d = open_pak_n(pak_index(path));
        if (!d) { *hndl = -1; return -1; }
        *hndl = (int)(d - g_fd) + 1;
        return (int)d->size;                /* engine expects length */
    }
    /* Cache/MMU handling follows the validated Core1 memory contract. */



    {
        zzq_fd_t *d = zzq_open_read_common(path);
        if (!d) { shared[SH_NOTFOUND_BENIGN]++; *hndl = -1; return -1; }
        *hndl = (int)(d - g_fd) + 1;
        return (int)d->size;
    }
}

int Sys_FileTime(char *path)
{
    zzq_record_file_req(path);
    if (is_pak_path(path)) return 1;
    

    if (!fs_request(path, 0, FS_CMD_STAT, 0)) return -1;
    return shared[SH_FS_EXISTS] ? 1 : -1;
}

void Sys_mkdir(char *path) { (void)path; }

/* ------------------------------------------------------------------ */
/* Layer 2: zzq stdio (names installed by zzquake_stdio.h)            */
/* The FILE* values handed to the engine are zzq_fd_t* in disguise.   */
/* Only these functions ever dereference them. newlib never does.     */
/* ------------------------------------------------------------------ */

/* Prototypes must match zzquake_stdio.h exactly. We include real
   <stdio.h> for FILE/size_t but NOT the wrapper (real names here). */
#include <stdio.h>

FILE *zzq_fopen(const char *path, const char *mode)
{
    zzq_record_file_req(path);
    TRACE(ZZQ_OP_FOPEN);
    


    if (shared[SH_ENABLE_DEMO_OFF] && is_dem_path(path)) {
        shared[SH_DEMO_BLOCKED]++;
        shared[SH_NOTFOUND_BENIGN]++;
        return 0;
    }
    if (mode && mode[0] == 'r' && is_pak_path(path))
        return (FILE *)open_pak_n(pak_index(path));
    /* Engine stdio is redirected to the in-memory filesystem. */






    if (mode && (mode[0] == 'w' || mode[0] == 'a')) {
        zzq_fd_t *d = zzq_open_write_common(path);
        if (!d) return 0;
        return (FILE *)d;
    }
    /* PAK filesystem handling. */



    if (mode && mode[0] == 'r') {
        zzq_fd_t *d = zzq_open_read_common(path);
        if (!d) { shared[SH_NOTFOUND_BENIGN]++; return 0; }
        return (FILE *)d;
    }
    shared[SH_NOTFOUND_BENIGN]++;
    return 0;
}

int zzq_fclose(FILE *f)
{
    zzq_fd_t *d = (zzq_fd_t *)f;
    int ok = 1;
    TRACE(ZZQ_OP_FCLOSE);
    if (!d || !d->used) return -1;

    /* WRITE is flushed using THIS descriptor's name/slot/position.
       No global "current file" participates. */
    if (d->is_write)
        ok = fd_flush_write(d);

    if (d->slot >= 0) fs_slot_free(d->slot);
    fd_free(d);
    return ok ? 0 : -1;
}

size_t zzq_fread(void *dst, size_t sz, size_t n, FILE *f)
{
    u32 got;
    TRACE(ZZQ_OP_FREAD);
    if (!f || sz == 0) return 0;
    got = fd_read((zzq_fd_t *)f, dst, (u32)(sz * n));
    return got / sz;
}

size_t zzq_fwrite(const void *src, size_t sz, size_t n, FILE *f)
{
    zzq_fd_t *d = (zzq_fd_t *)f;
    int got;
    if (!d || !d->used || !d->is_write || sz == 0) return 0;
    got = fd_write(d, src, (u32)(sz * n));
    if (got < 0) return 0;
    return (size_t)got / sz;
}

int zzq_fseek(FILE *f, long off, int whence)
{
    TRACE(ZZQ_OP_FSEEK);
    if (!f) return -1;
    fd_seek((zzq_fd_t *)f, off, whence);
    return 0;
}

long zzq_ftell(FILE *f)
{
    TRACE(ZZQ_OP_FTELL);
    if (!f) return -1;
    return (long)((zzq_fd_t *)f)->pos;
}

int zzq_fgetc(FILE *f)
{
    u8 c;
    TRACE(ZZQ_OP_GETC);
    if (!f) return -1;                       /* EOF */
    if (fd_read((zzq_fd_t *)f, &c, 1) != 1) return -1;
    return (int)c;
}

int zzq_feof(FILE *f)
{
    zzq_fd_t *d = (zzq_fd_t *)f;
    if (!d) return 1;
    return (d->pos >= d->size) ? 1 : 0;
}

int zzq_fprintf(FILE *f, const char *fmt, ...)
{
    











    zzq_fd_t *d = (zzq_fd_t *)f;
    static char out[2048];
    va_list ap;
    int n = 0;
    const char *p;
    if (!d || !d->is_write) return 0;
    va_start(ap, fmt);
    for (p = fmt; *p && n < (int)sizeof(out) - 64; p++) {
        if (*p != '%') { out[n++] = *p; continue; }
        p++;
        if (*p == '%') { out[n++] = '%'; continue; }
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '0' ||
               (*p >= '1' && *p <= '9') || *p == '.') p++;
        if (*p == 'i' || *p == 'd') {
            int v = va_arg(ap, int);
            char t[16]; int k = 0, neg = 0;
            if (v < 0) { neg = 1; v = -v; }
            if (!v) t[k++] = '0';
            while (v) { t[k++] = (char)('0' + v % 10); v /= 10; }
            if (neg) out[n++] = '-';
            while (k) out[n++] = t[--k];
        } else if (*p == 'f' || *p == 'g') {
            















            union { double d; unsigned int w[2]; } u;
            u.w[0] = va_arg(ap, unsigned int);
            u.w[1] = va_arg(ap, unsigned int);
            zzq_ftoa_prec(out + n, (int)sizeof(out) - n - 1, (float)u.d, 6);
            while (out[n]) n++;
        } else if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && n < (int)sizeof(out) - 2) out[n++] = *s++;
        } else if (*p == 'c') {
            out[n++] = (char)va_arg(ap, int);
        }
    }
    va_end(ap);
    if (fd_write(d, out, (u32)n) < 0) return 0;
    return n;
}














static long zzq_atoi_simple(const char *s)
{
    long v = 0; int neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

static float zzq_atof_simple(const char *s)
{
    float v = 0.0f, frac = 0.0f, scale = 0.1f;
    int neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10.0f + (float)(*s - '0'); s++; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            frac += (float)(*s - '0') * scale;
            scale *= 0.1f; s++;
        }
    }
    v += frac;
    return neg ? -v : v;
}

static int fs_getc(zzq_fd_t *d)
{
    unsigned char c;
    /* One reader only. fscanf/getc/fread/Sys_FileRead all converge
       on fd_read(), so a future backend change cannot split semantics. */
    if (!d || fd_read(d, &c, 1) != 1) return -1;
    return (int)c;
}









static int fs_token(zzq_fd_t *d, char *out, int max)
{
    int c, n = 0;
    do { c = fs_getc(d); } while (c == ' ' || c == '\n' || c == '\r' ||
                                  c == '\t');
    if (c < 0) return 0;
    while (c >= 0 && c != ' ' && c != '\n' && c != '\r' && c != '\t') {
        if (n < max - 1) out[n++] = (char)c;
        c = fs_getc(d);
    }
    if (c >= 0 && d->pos > 0) d->pos--;   /* rendre le separateur */
    out[n] = 0;
    return n;
}

int zzq_fscanf(FILE *f, const char *fmt, ...)
{
    zzq_fd_t *d = (zzq_fd_t *)f;
    char tok[256];
    va_list ap;
    int got = 0;
    const char *p;
    if (!d) return -1;
    va_start(ap, fmt);
    for (p = fmt; *p; p++) {
        if (*p != '%') continue;
        p++;
        if (*p == 'i' || *p == 'd') {
            int *dst = va_arg(ap, int *);
            if (!fs_token(d, tok, sizeof(tok))) break;
            *dst = (int)zzq_atoi_simple(tok);
            got++;
        } else if (*p == 'f') {
            float *dst = va_arg(ap, float *);
            if (!fs_token(d, tok, sizeof(tok))) break;
            *dst = zzq_atof_simple(tok);
            got++;
        } else if (*p == 's' || (*p >= '0' && *p <= '9')) {
            





            char *dst;
            int w = 0;
            while (*p >= '0' && *p <= '9') { w = w * 10 + (*p - '0'); p++; }
            if (*p != 's') break;
            dst = va_arg(ap, char *);
            if (w == 0) w = 63;        

            if (!fs_token(d, dst, w + 1)) break;
            got++;
        }
    }
    va_end(ap);
    return got ? got : -1;
}

int zzq_fflush(FILE *f) { (void)f; return 0; }

int zzq_remove(const char *path) { (void)path; return 0; }

/* zzq_printf is implemented in zzquake_platform.c (routes to the
   shared last-printf slots like Sys_Printf) */


/* Engine stdio is redirected to the in-memory filesystem. */




void Sys_FileSeek(int handle, int position)
{
    TRACE(ZZQ_OP_SYSSEEK);
    if (handle >= 1 && handle <= ZZQ_MAX_FILES)
        fd_seek(&g_fd[handle - 1], position, 0);
}

int Sys_FileRead(int handle, void *dest, int count)
{
    TRACE(ZZQ_OP_SYSREAD);
    if (handle >= 1 && handle <= ZZQ_MAX_FILES)
        return (int)fd_read(&g_fd[handle - 1], dest, (u32)count);
    return 0;
}

int Sys_FileWrite(int handle, void *data, int count)
{
    zzq_fd_t *d = (handle >= 1 && handle <= ZZQ_MAX_FILES)
                ? &g_fd[handle - 1] : 0;
    if (!d || !d->used || !d->is_write || count <= 0) return 0;
    return fd_write(d, data, (u32)count);
}

void Sys_FileClose(int handle)
{
    zzq_fd_t *d = (handle >= 1 && handle <= ZZQ_MAX_FILES)
                ? &g_fd[handle - 1] : 0;
    if (!d || !d->used) return;

    if (d->is_write)
        (void)fd_flush_write(d);

    if (d->slot >= 0) fs_slot_free(d->slot);
    fd_free(d);
}

int Sys_FileOpenWrite(char *path)
{
    zzq_fd_t *d;
    zzq_record_file_req(path);
    d = zzq_open_write_common(path);
    if (!d) return -1;
    return (int)(d - g_fd) + 1;
}

static int is_cfg_path(const char *p)
{
    const char *b = p, *q;
    if (!p) return 0;
    for (q = p; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
    return (b[0]=='c'&&b[1]=='o'&&b[2]=='n'&&b[3]=='f'&&b[4]=='i'&&b[5]=='g' &&
            b[6]=='.'&&b[7]=='c'&&b[8]=='f'&&b[9]=='g'&&b[10]==0);
}

/* One write descriptor owns its basename, slot and position from open
   through close.  There is no global current filename/slot/position. */
static zzq_fd_t *zzq_open_write_common(const char *path)
{
    zzq_fd_t *d;
    int s;

    zzq_record_file_req(path);
    d = fd_alloc();
    if (!d) return 0;

    s = fs_slot_alloc();
    if (s < 0) {
        shared[SH_FS_OPENFAIL]++;
        fd_free(d);
        return 0;
    }

    d->is_pak   = 0;
    d->is_write = 1;
    d->slot     = s;
    d->base     = ZZQ_FILEBUF_AT(s);
    d->size     = ZZQ_FILEBUF_SIZE;
    d->pos      = 0;
    d->error    = ZZFS_OK;
    fs_copy_basename(d->name, path);
    return d;
}

/* One read descriptor owns one immutable full-file snapshot.  This is
   still the v99 4-slot strategy, but ownership is explicit and all
   read APIs share fd_read(). */
static zzq_fd_t *zzq_open_read_common(const char *path)
{
    zzq_fd_t *d;
    int s;

    zzq_record_file_req((char *)path);
    d = fd_alloc();
    if (!d) return 0;

    s = fs_slot_alloc();
    if (s < 0) {
        shared[SH_FS_OPENFAIL]++;
        fd_free(d);
        return 0;
    }

    if (!fs_request(path, s, FS_CMD_READ, 0)) {
        fs_slot_free(s);
        fd_free(d);
        return 0;
    }

    d->is_pak   = 0;
    d->is_write = 0;
    d->slot     = s;
    d->base     = ZZQ_FILEBUF_AT(s);
    d->size     = shared[SH_FS_SIZE];
    d->pos      = 0;
    d->error    = ZZFS_OK;
    fs_copy_basename(d->name, path);

    if (is_cfg_path(path)) {
        shared[SH_FS_CFG_READ]++;
        shared[SH_FS_CFG_BYTES] = d->size;
    }
    return d;
}

static int fd_write(zzq_fd_t *d, const void *srcv, u32 count)
{
    unsigned char *dst;
    const unsigned char *src = (const unsigned char *)srcv;
    u32 i;

    if (!d || !d->used || !d->is_write) return -1;
    if (count == 0) return 0;

    if (d->pos > ZZQ_FILEBUF_SIZE || count > ZZQ_FILEBUF_SIZE - d->pos) {
        d->error = ZZFS_ERR_TOO_LARGE;
        g_fs_last_err = d->error;
        return -1;
    }

    dst = fs_buf_at(d->slot) + d->pos;
    for (i = 0; i < count; i++) dst[i] = src[i];
    d->pos += count;
    if (d->pos > d->size) d->size = d->pos;
    return (int)count;
}

static int fd_flush_write(zzq_fd_t *d)
{
    int ok;
    if (!d || !d->used || !d->is_write) return 0;
    if (d->error != ZZFS_OK) {
        g_fs_last_err = d->error;
        return 0;
    }

    __asm__ volatile("dsb sy" ::: "memory");
    ok = fs_request(d->name, d->slot, FS_CMD_WRITE, d->pos);
    if (!ok) {
        d->error = g_fs_last_err;
        return 0;
    }
    return 1;
}






int zzq_fs_open_write(const char *path)
{
    if (g_sys_fd) return 0;
    g_sys_fd = zzq_open_write_common(path);
    return g_sys_fd ? 1 : 0;
}

int zzq_fs_open_read(const char *path, unsigned int *size)
{
    if (g_sys_fd) return 0;
    g_sys_fd = zzq_open_read_common(path);
    if (!g_sys_fd) return 0;
    if (size) *size = g_sys_fd->size;
    return 1;
}

int zzq_fs_write(const void *buf, unsigned int n)
{
    if (!g_sys_fd || !g_sys_fd->is_write) return -1;
    return fd_write(g_sys_fd, buf, (u32)n);
}

int zzq_fs_read(void *buf, unsigned int n)
{
    if (!g_sys_fd || g_sys_fd->is_write) return 0;
    return (int)fd_read(g_sys_fd, buf, (u32)n);
}

void zzq_fs_close(void)
{
    if (!g_sys_fd) return;
    if (g_sys_fd->is_write)
        (void)fd_flush_write(g_sys_fd);
    if (g_sys_fd->slot >= 0)
        fs_slot_free(g_sys_fd->slot);
    fd_free(g_sys_fd);
    g_sys_fd = 0;
}

int zzq_fs_lseek(int off, int whence)
{
    if (!g_sys_fd) return -1;
    fd_seek(g_sys_fd, (long)off, whence);
    return (int)g_sys_fd->pos;
}
