/*
 * zzquake_syscalls.c - newlib syscall stubs for the bare-metal link
 * (-lgcc -lc -lm without --specs=nosys.specs, real ZZDoom link order).
 *
 * The transferred ZZDoom sys_stub.c only carries mkdir/_unlink/_link;
 * the full syscall set its build used was not preserved. This file
 * provides the complete set. _exit routes to the fatal terminal state
 * (abort() -> _exit must still leave Core1 relaunchable).
 *
 * _sbrk intentionally NOT here: it lives in zzquake_main.c (publishes
 * SH_HEAP_USED and ZZQ_FATAL_OUT_OF_MEM).
 *
 * ASCII only.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include "zzquake_config.h"

extern void zzq_fatal_end(unsigned int code);   /* zzquake_main.c */

void _exit(int status)
{
    (void)status;
    



    zzq_fatal_end(ZZQ_FATAL_SYS_ERROR);   /* STATUS=0xFF puis WFE */
    while (1) { __asm__ volatile("wfe"); }
}

/* Engine stdio is redirected to the in-memory filesystem. */












extern int  zzq_fs_open_write(const char *path);
extern int  zzq_fs_open_read(const char *path, unsigned int *size);
extern int  zzq_fs_write(const void *buf, unsigned int n);
extern int  zzq_fs_read(void *buf, unsigned int n);
extern void zzq_fs_close(void);
extern int  zzq_fs_lseek(int off, int whence);

#define ZZQ_STDIO_FD 64          

int _open(const char *path, int flags, ...)
{
    unsigned int sz;
    /* O_WRONLY=1, O_RDWR=2, O_CREAT=0x200 selon la newlib */
    if (flags & (1 | 2 | 0x200)) {
        if (zzq_fs_open_write(path)) return ZZQ_STDIO_FD;
        return -1;
    }
    if (zzq_fs_open_read(path, &sz)) return ZZQ_STDIO_FD;
    return -1;
}

int _write(int fd, const void *buf, size_t n)
{
    if (fd == ZZQ_STDIO_FD) return zzq_fs_write(buf, (unsigned int)n);
    (void)buf; return (int)n;      /* fd 1 et 2 : console, ignores */
}

int _read(int fd, void *buf, size_t n)
{
    if (fd == ZZQ_STDIO_FD) return zzq_fs_read(buf, (unsigned int)n);
    (void)buf; (void)n; return 0;
}

int _close(int fd)
{
    if (fd == ZZQ_STDIO_FD) { zzq_fs_close(); return 0; }
    return -1;
}

off_t _lseek(int fd, off_t o, int w)
{
    if (fd == ZZQ_STDIO_FD) return (off_t)zzq_fs_lseek((int)o, w);
    (void)o; (void)w; return 0;
}

int   _fstat(int fd, struct stat *st)
                          { (void)fd; if (st) st->st_mode = S_IFCHR;
                            return 0; }
int   _isatty(int fd)     { (void)fd; return 1; }
int   _kill(int pid, int sig)
                          { (void)pid; (void)sig; return -1; }
int   _getpid(void)       { return 1; }

/* from the transferred ZZDoom sys_stub.c, kept verbatim */
int mkdir(const char *path, mode_t mode)
                          { (void)path; (void)mode; return 0; }
int _unlink(const char *path) { (void)path; return -1; }
int _link(const char *o, const char *n)
                          { (void)o; (void)n; return -1; }
