/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
/* ZZOpenLaraRIX A2: the engine's stdio goes to the OpenLara file layer (ol_fs.c).
 *
 * Force-included (-include) into the Game translation unit, after the system headers it names.
 * Same technique as runtime/zzquake_stdio.h (the Quake engine on Core1): the names are macros, the
 * functions below are the only ones that ever see the FILE pointers they return.
 *
 *   fopen "rb"  : a file of the preloaded container (PAK directory, basename, case-insensitive), or,
 *                 for the few names the 68k serves (settings, savegame.dat), a read through the
 *                 release file service; anything else: NULL at once, no 68k round trip.
 *   fopen "wb"  : the 68k file service only (PROGDIR:data/<basename>, 512 KiB at most).
 *   printf      : the release console trace (zzq_printf).
 *
 * ASCII only.
 */
#ifndef OL_STDIO_H
#define OL_STDIO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef getc
#undef putc
#undef feof
#undef ferror
#undef getchar
#undef putchar

#define fopen    ol_fopen
#define fclose   ol_fclose
#define fread    ol_fread
#define fwrite   ol_fwrite
#define fseek    ol_fseek
#define ftell    ol_ftell
#define fgetc    ol_fgetc
#define getc     ol_fgetc
#define feof     ol_feof
#define fflush   ol_fflush
#define remove   ol_remove
#define printf   zzq_printf
/* sprintf / vsprintf / snprintf stay newlib's: pure string formatting */

#ifdef __cplusplus
extern "C" {
#endif

FILE  *ol_fopen (const char *path, const char *mode);
int    ol_fclose(FILE *f);
size_t ol_fread (void *dst, size_t sz, size_t n, FILE *f);
size_t ol_fwrite(const void *src, size_t sz, size_t n, FILE *f);
int    ol_fseek (FILE *f, long off, int whence);
long   ol_ftell (FILE *f);
int    ol_fgetc (FILE *f);
int    ol_feof  (FILE *f);
int    ol_fflush(FILE *f);
int    ol_remove(const char *path);
int    zzq_printf(const char *fmt, ...);

/* the file layer itself */
int    ol_fs_init(const void *pak, unsigned size);   /* 0 = bad container */
int    ol_fs_count(void);
const char *ol_fs_name(int i, unsigned *size);
const void *ol_fs_find(const char *path, unsigned *size);   /* NULL = not in the container */

#ifdef __cplusplus
}
#endif

#endif /* OL_STDIO_H */
