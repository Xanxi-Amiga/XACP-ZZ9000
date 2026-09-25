 

















#ifndef ZZDF_HOST_ALLOC_H
#define ZZDF_HOST_ALLOC_H

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MSYS__)
/* ---- native Windows (MinGW): VirtualAlloc ---- */
#include <windows.h>
static void *alloc32(unsigned long n)
{
    static unsigned long long next = 0x10000000ULL;
    int tries;
    for (tries = 0; tries < 12; tries++) {
        void *p = VirtualAlloc((void *)next, n,
                               MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        next += 0x08000000ULL;              /* 128 MiB stride */
        if (p && ((unsigned long long)p >> 32) == 0) return p;
        if (p) VirtualFree(p, 0, MEM_RELEASE);
    }
    {
        void *p = VirtualAlloc(NULL, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p && ((unsigned long long)p >> 32) == 0) return p;
    }
    printf("FATAL: could not get memory below 4 GiB (Windows)\n");
    exit(2);
}
#else
/* ---- POSIX: Linux, MSYS2 "MSYS" shell, Cygwin ---- */
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif
static void *alloc32(unsigned long n)
{
    void *p;
#ifdef MAP_32BIT
    /* Linux glibc: kernel guarantees the result is below 4 GiB */
    p = mmap(0, n, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (p != MAP_FAILED && ((unsigned long long)p >> 32) == 0) return p;
    if (p != MAP_FAILED) munmap(p, n);
#endif
    /* No MAP_32BIT (MSYS/Cygwin, or older systems): ask for a low
       fixed address. On MSYS/Cygwin the low 2 GiB is normally free. */
    {
        unsigned long long hint;
        for (hint = 0x10000000ULL; hint < 0x80000000ULL; hint += 0x08000000ULL) {
            p = mmap((void *)hint, n, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) continue;
            if (((unsigned long long)p >> 32) == 0) return p;
            munmap(p, n);
        }
    }
    /* Last resort: plain malloc, only usable if it lands below 4 GiB */
    p = malloc(n);
    if (p && ((unsigned long long)p >> 32) == 0) return p;
    if (p) free(p);
    printf("FATAL: could not get memory below 4 GiB on this host.\n"
           "       Run the host tests in the MSYS2 MINGW64 shell, or on\n"
           "       Linux. (They only sanity-check the code on the host;\n"
           "       they are not part of the Amiga build.)\n");
    exit(2);
}
#endif

#endif
