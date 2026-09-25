 














#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include "zzdf_config.h"

typedef unsigned int u32;

extern void zzdf_fatal_end(unsigned int code);   /* zzdf_main.cpp */

/* MemFS (zzdf_fs_mem.c) */
extern int  zzdf_fs_open(const char *path, int write, int trunc);
extern int  zzdf_fs_is_ours(int fd);
extern int  zzdf_fs_read(int fd, void *buf, u32 n);
extern int  zzdf_fs_write(int fd, const void *buf, u32 n);
extern int  zzdf_fs_lseek(int fd, int off, int whence);
extern u32  zzdf_fs_size(int fd);
extern int  zzdf_fs_close(int fd);

/* ---- console log ring (ARM -> 68k) ---- */
static volatile u32 *sh = (volatile u32 *)ZZDF_SHARED_ARM;

void zzdf_log_putc(char c)
{
    volatile char *ring = (volatile char *)(ZZDF_SHARED_ARM + ZZDF_LOGRING_OFF);
    u32 head = sh[SH_LOG_HEAD];
    ring[head % ZZDF_LOGRING_SIZE] = c;
    sh[SH_LOG_HEAD] = head + 1;
}

void zzdf_log_puts(const char *s)
{
    while (*s) zzdf_log_putc(*s++);
}

/* ---- process ---- */
void _exit(int status)
{
    (void)status;
    zzdf_fatal_end(ZZDF_FATAL_SYS_ERROR);
    while (1) { __asm__ volatile("wfe"); }
}

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    zzdf_fatal_end(ZZDF_FATAL_CPP_ABORT);
    while (1) { __asm__ volatile("wfe"); }
}

int _getpid(void) { return 1; }

/* C++ runtime hooks */
void __cxa_pure_virtual(void)
{
    zzdf_fatal_end(ZZDF_FATAL_PURECALL);
    while (1) { __asm__ volatile("wfe"); }
}

/* ---- files ---- */
int _open(const char *path, int flags, ...)
{
    /* newlib: O_WRONLY=1, O_RDWR=2, O_APPEND=8, O_CREAT=0x200,
       O_TRUNC=0x400. Truncation follows O_TRUNC ONLY - O_RDWR on an
       existing file must preserve it (DARKPILO.CFG is opened that way
       and its header read straight back). */
    int fd;
    int wr    = (flags & (1 | 2 | 0x200)) ? 1 : 0;
    int trunc = (flags & 0x400) ? 1 : 0;
    if (wr && (flags & 1) && !(flags & 2)) trunc = 1;   /* plain "wb" */
    fd = zzdf_fs_open(path, wr, trunc);
    if (fd < 0) { errno = ENOENT; return -1; }
    return fd;
}

int _close(int fd)
{
    if (zzdf_fs_is_ours(fd)) return zzdf_fs_close(fd);
    return 0;
}

_ssize_t _read(int fd, void *buf, size_t n)
{
    if (zzdf_fs_is_ours(fd)) return zzdf_fs_read(fd, buf, (u32)n);
    return 0;
}

_ssize_t _write(int fd, const void *buf, size_t n)
{
    if (zzdf_fs_is_ours(fd)) return zzdf_fs_write(fd, buf, (u32)n);
    if (fd == 1 || fd == 2) {
        const char *s = (const char *)buf;
        size_t i;
        for (i=0;i<n;i++) zzdf_log_putc(s[i]);
        return (_ssize_t)n;
    }
    return (_ssize_t)n;
}

_off_t _lseek(int fd, _off_t off, int whence)
{
    if (zzdf_fs_is_ours(fd)) return zzdf_fs_lseek(fd, (int)off, whence);
    return 0;
}

int _fstat(int fd, struct stat *st)
{
    if (zzdf_fs_is_ours(fd)) {
        st->st_mode = S_IFREG;
        st->st_size = (off_t)zzdf_fs_size(fd);
        return 0;
    }
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    return (fd == 0 || fd == 1 || fd == 2) ? 1 : 0;
}

int _stat(const char *path, struct stat *st)
{
    extern int zzdf_fs_exists(const char *path);
    if (zzdf_fs_exists(path)) {
        st->st_mode = S_IFREG;
        st->st_size = 0;
        return 0;
    }
    errno = ENOENT;
    return -1;
}

int _link(const char *o, const char *n) { (void)o;(void)n; errno=EMLINK; return -1; }
int _unlink(const char *p) { (void)p; return 0; }
int mkdir(const char *p, mode_t m) { (void)p;(void)m; return 0; }

/* ---- time: Zynq-7020 Global Timer, 333 MHz, READ-ONLY (never touch
 * GTIMER_CTRL: ZZMIDI realtime depends on it). ZZQuake-validated. ---- */
#define SCU_BASE    0xF8F00000UL
#define GTIMER_LO   (*(volatile u32*)(SCU_BASE+0x0200))
#define GTIMER_HI   (*(volatile u32*)(SCU_BASE+0x0204))

unsigned long long zzdf_gtimer_read(void)
{
    u32 hi1, lo, hi2;
    do { hi1 = GTIMER_HI; lo = GTIMER_LO; hi2 = GTIMER_HI; }
    while (hi1 != hi2);
    return ((unsigned long long)hi1 << 32) | lo;
}

u32 zzdf_us_now(void)
{
    return (u32)(zzdf_gtimer_read() / 333ULL);
}

int _gettimeofday(struct timeval *tv, void *tz)
{
    unsigned long long us;
    (void)tz;
    if (!tv) return -1;
    us = zzdf_gtimer_read() / 333ULL;
    tv->tv_sec  = (time_t)(us / 1000000ULL);
    tv->tv_usec = (suseconds_t)(us % 1000000ULL);
    return 0;
}

clock_t _times(void *buf) { (void)buf; return (clock_t)-1; }
