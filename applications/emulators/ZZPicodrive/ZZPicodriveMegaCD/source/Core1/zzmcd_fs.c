/*
 * zzmcd_fs.c - ZZPicoDriveMegaCD Core1 file access via AmigaOS.
 *
 * PicoDrive and libchdr use file operations forwarded to the 68k launcher
 * through a request/acknowledgement protocol in shared ZZ9000 memory.
 * An LRU block cache and sequential prefetch reduce waits during CD reads.
 * Supported image contents are supplied by the user; no BIOS or game data
 * are embedded here.
 */
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "zzvr_shared.h"

typedef uint32_t u32;
extern volatile u32 *zzmcd_shared(void);                    /* zzvr_core1.c */
extern void dcache_inval_range(void *addr, uint32_t len);   /* zzvr_mmu.c   */
extern u32 zzmcd_cyc(void);                                 /* zzvr_core1.c: PMU cycle counter */

#define FS_MAXH   8
#define FS_FD0    3
#define FS_BLK    ZZMCD_FS_BLOCK
#define FS_NBUF   ZZMCD_FS_NBUF

struct fsh { int used; int h68k; u32 size; u32 pos; };
struct fsb { int fd; u32 base; u32 len; u32 lru; int state; };   /* state 0 empty, 1 valid, 2 pending */
static struct fsh fh[FS_MAXH];
static struct fsb fb[FS_NBUF];
static u32 lru_clock;
static int pend_buf = -1;          /* buffer of the outstanding background request, -1 = none */
static u32 req_seq;

static volatile u32 *S(void){ return zzmcd_shared(); }
static unsigned char *buf_ptr(int b){ return (unsigned char *)(ZZMCD_FS_BUF_ARM + (u32)b * FS_BLK); }

static void fs_stat_wait(u32 c0)
{
    volatile u32 *s = S();
    u32 us = (zzmcd_cyc() - c0) / 666u;
    s[ZZMCD_SH_FS_WAITS]++;
    s[ZZMCD_SH_FS_WAIT_US] += us;
    if (us > s[ZZMCD_SH_FS_WAIT_MAX]) s[ZZMCD_SH_FS_WAIT_MAX] = us;
}

/* wait for the outstanding request (if any) */
static void fs_wait_ack(int count_wait)
{
    volatile u32 *s = S();
    u32 c0;
    if (s[ZZMCD_SH_FS_ACK_SEQ] == req_seq) return;
    c0 = zzmcd_cyc();
    s[ZZMCD_SH_FS_URGENT] = 1;      /* the 68k serves this one at once */
    while (s[ZZMCD_SH_FS_ACK_SEQ] != req_seq) { }
    s[ZZMCD_SH_FS_URGENT] = 0;
    if (count_wait) fs_stat_wait(c0);
}

/* finish the background request: its buffer becomes valid (or empty) */
static u32 byte_sum(const unsigned char *p, u32 n)
{
    u32 s = 0, i;
    for (i = 0; i < n; i++) s += p[i];
    return s;
}

static void fs_issue(u32 cmd, int h68k, u32 off, u32 len, int slot);

static void fs_complete_pending(int count_wait)
{
    volatile u32 *s = S();
    int b, tries;
    if (pend_buf < 0) return;
    b = pend_buf;
    for (tries = 0;; tries++) {
        fs_wait_ack(count_wait);
        pend_buf = -1;
        if ((int)s[ZZMCD_SH_FS_RESULT] <= 0) { fb[b].state = 0; return; }
        fb[b].len = s[ZZMCD_SH_FS_RESULT];
        dcache_inval_range(buf_ptr(b), FS_BLK);
        /* the data must be the one the 68k read (byte sum) */
        if (byte_sum(buf_ptr(b), fb[b].len) == s[ZZMCD_SH_FS_SUM]) break;
        s[ZZMCD_SH_FS_BADSUM]++;
        if (s[ZZMCD_SH_FS_BADSUM] == 1) s[ZZMCD_SH_FS_BADFIRST] = fb[b].base;
        if (tries >= 2) break;                  /* keep it, counted */
        s[ZZMCD_SH_FS_RETRIED]++;
        fs_issue(ZZMCD_FS_READ, fh[fb[b].fd - FS_FD0].h68k, fb[b].base, FS_BLK, b);
        pend_buf = b;
    }
    fb[b].state = 1;
}

static void fs_issue(u32 cmd, int h68k, u32 off, u32 len, int slot)
{
    volatile u32 *s = S();
    fs_complete_pending(1);         /* one request at a time */
    s[ZZMCD_SH_FS_CMD] = cmd;
    s[ZZMCD_SH_FS_HANDLE] = (u32)h68k;
    s[ZZMCD_SH_FS_OFFSET] = off;
    s[ZZMCD_SH_FS_LEN] = len;
    s[ZZMCD_SH_FS_SLOT] = (u32)slot;
    s[ZZMCD_SH_FS_RESULT] = 0;
    req_seq++;
    s[ZZMCD_SH_FS_REQ_SEQ] = req_seq;
    s[ZZMCD_SH_FS_REQUESTS]++;
}

static int find_buf(int fd, u32 base)
{
    int i;
    for (i = 0; i < FS_NBUF; i++)
        if (fb[i].state && fb[i].fd == fd && fb[i].base == base) return i;
    return -1;
}

static int victim(int keep)
{
    int i, v = -1; u32 best = 0xFFFFFFFFu;
    for (i = 0; i < FS_NBUF; i++) {
        if (i == keep || fb[i].state == 2) continue;
        if (fb[i].state == 0) return i;
        if (fb[i].lru < best) { best = fb[i].lru; v = i; }
    }
    return v;
}

/* make block 'base' of fd valid, return its buffer */
static int get_block(int fd, u32 base)
{
    struct fsh *h = &fh[fd - FS_FD0];
    int b = find_buf(fd, base);
    if (b >= 0 && fb[b].state == 2) { fs_complete_pending(1); b = find_buf(fd, base); }
    if (b >= 0 && fb[b].state == 1) { fb[b].lru = ++lru_clock; return b; }
    b = victim(-1);
    if (b < 0) { fs_complete_pending(1); b = victim(-1); if (b < 0) return -1; }
    fb[b].fd = fd; fb[b].base = base; fb[b].state = 2; fb[b].len = 0;
    fs_issue(ZZMCD_FS_READ, h->h68k, base, FS_BLK, b);
    pend_buf = b;
    fs_complete_pending(1);
    if (fb[b].state != 1) return -1;
    fb[b].lru = ++lru_clock;
    return b;
}

/* background read of the next blocks of the file (up to FS_AHEAD blocks
   ahead, a finished background read is collected at once so that the next one
   can start; a prefetched block counts as recently used, it is not evicted
   before it is read) */
#define FS_AHEAD 8         /* 8 x 16 KiB = 128 KiB ahead */
static void prefetch(int fd, u32 base, int keep)
{
    struct fsh *h = &fh[fd - FS_FD0];
    int b, k;
    if (pend_buf >= 0) {
        if (S()[ZZMCD_SH_FS_ACK_SEQ] != req_seq) return;   /* still running */
        fs_complete_pending(0);
    }
    if (S()[ZZMCD_SH_FS_ACK_SEQ] != req_seq) return;
    for (k = 1; k <= FS_AHEAD; k++) {
        u32 nb = base + (u32)k * FS_BLK;
        if (nb >= h->size) return;
        if (find_buf(fd, nb) >= 0) continue;
        b = victim(keep);
        if (b < 0) return;
        fb[b].fd = fd; fb[b].base = nb; fb[b].state = 2; fb[b].len = 0; fb[b].lru = ++lru_clock;
        fs_issue(ZZMCD_FS_READ, h->h68k, nb, FS_BLK, b);
        pend_buf = b;
        S()[ZZMCD_SH_FS_PREFETCH]++;
        return;
    }
}

int _open(const char *path, int flags, ...)
{
    volatile u32 *s = S();
    int i, fd = -1;
    char *dst = (char *)(s + ZZMCD_SH_FS_PATH);
    (void)flags;
    if (!path) { errno = ENOENT; return -1; }
    for (i = 0; i < FS_MAXH; i++) if (!fh[i].used) { fd = FS_FD0 + i; break; }
    if (fd < 0) { errno = EMFILE; return -1; }
    fs_complete_pending(0);
    for (i = 0; i < ZZMCD_FS_PATH_MAX - 1 && path[i]; i++) dst[i] = path[i];
    dst[i] = 0;
    fs_issue(ZZMCD_FS_OPEN, 0, 0, 0, 0);
    fs_wait_ack(0);
    if ((int)s[ZZMCD_SH_FS_RESULT] < 0) { errno = ENOENT; return -1; }
    fh[fd - FS_FD0].used = 1;
    fh[fd - FS_FD0].h68k = (int)s[ZZMCD_SH_FS_RESULT];
    fh[fd - FS_FD0].size = s[ZZMCD_SH_FS_SIZE];
    fh[fd - FS_FD0].pos = 0;
    s[ZZMCD_SH_FS_OPENS]++;
    return fd;
}

int _close(int fd)
{
    int i;
    if (fd < FS_FD0 || fd >= FS_FD0 + FS_MAXH || !fh[fd - FS_FD0].used) return -1;
    fs_complete_pending(0);
    for (i = 0; i < FS_NBUF; i++) if (fb[i].fd == fd) fb[i].state = 0;
    fs_issue(ZZMCD_FS_CLOSE, fh[fd - FS_FD0].h68k, 0, 0, 0);
    fs_wait_ack(0);
    fh[fd - FS_FD0].used = 0;
    return 0;
}

int _read(int fd, char *dst, int len)
{
    struct fsh *h;
    int done = 0;
    if (fd < FS_FD0 || fd >= FS_FD0 + FS_MAXH || !fh[fd - FS_FD0].used) return 0;
    h = &fh[fd - FS_FD0];
    while (len > 0 && h->pos < h->size) {
        u32 base = h->pos & ~(u32)(FS_BLK - 1), off = h->pos - base, n;
        int b = get_block(fd, base);
        if (b < 0) break;
        if (off >= fb[b].len) break;
        n = fb[b].len - off;
        if (n > (u32)len) n = (u32)len;
        memcpy(dst + done, buf_ptr(b) + off, n);
        done += (int)n; len -= (int)n; h->pos += n;
        prefetch(fd, base, b);
    }
    S()[ZZMCD_SH_FS_BYTES] += (u32)done;
    return done;
}

int _lseek(int fd, int off, int whence)
{
    struct fsh *h;
    long p;
    if (fd < FS_FD0 || fd >= FS_FD0 + FS_MAXH || !fh[fd - FS_FD0].used) return -1;
    h = &fh[fd - FS_FD0];
    p = (whence == 0) ? off : (whence == 1) ? (long)h->pos + off : (long)h->size + off;
    if (p < 0) { errno = EINVAL; return -1; }
    h->pos = (u32)p;
    return (int)p;
}

int _fstat(int fd, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    if (fd >= FS_FD0 && fd < FS_FD0 + FS_MAXH && fh[fd - FS_FD0].used) {
        st->st_mode = S_IFREG;
        st->st_size = (off_t)fh[fd - FS_FD0].size;
    } else
        st->st_mode = S_IFCHR;
    return 0;
}

/* called at cmd_init: no file open, no request outstanding */
void zzmcd_fs_reset(void)
{
    volatile u32 *s = S();
    memset(fh, 0, sizeof(fh)); memset(fb, 0, sizeof(fb));
    pend_buf = -1; lru_clock = 0;
    req_seq = s[ZZMCD_SH_FS_REQ_SEQ];
    s[ZZMCD_SH_FS_ACK_SEQ] = req_seq;
}

/* libchdr's error code for a CHD that does not open (0 = opens) */
#include <libchdr/chd.h>
int zzmcd_chd_open_err(const char *path)
{
    chd_file *cf = NULL;
    chd_error e = chd_open(path, CHD_OPEN_READ, NULL, &cf);
    if (cf) chd_close(cf);
    return (int)e;
}
