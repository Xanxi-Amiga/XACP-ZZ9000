/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2_stdio.h - forced include for the Quake II objects (ZZQuake2 A0).
 *
 * First the ZZQuake640 release redirection header, unchanged: every
 * stdio call of the engine lands in the validated memory filesystem
 * (zzquake_fs_mem.c). Then the few extra names Quake II uses and
 * Quake I did not.
 *
 * ASCII only.
 */
#ifndef ZZQ2_STDIO_H
#define ZZQ2_STDIO_H

#include "zzquake_stdio.h"      /* release file, used as it is */
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A0 file policy: pak0.pak only. zzq2_fopen refuses pak1..pak9 at
   once, gives the save files to zzq2_save.c (0.6), then hands
   everything else to the release zzq_fopen. */
FILE *zzq2_fopen(const char *path, const char *mode);

/* cl_parse.c renames a finished download: never reached in A0. */
int   zzq2_rename(const char *oldn, const char *newn);

/* 0.6, save games (zzq2_save.c): fclose reports a save file that could
   not be written; remove empties a save file (the release has no
   delete). Any other file: the release zzq_fclose / zzq_remove. */
int   zzq2_fclose(FILE *f);
int   zzq2_remove(const char *path);

/* 0.7, compressed save files (zzq2_save.c): a save file lives in
   Core1 memory while it is open; these serve it, and hand any other
   FILE to the release function. */
size_t zzq2_fread (void *dst, size_t sz, size_t n, FILE *f);
size_t zzq2_fwrite(const void *src, size_t sz, size_t n, FILE *f);
int    zzq2_fseek (FILE *f, long off, int whence);
long   zzq2_ftell (FILE *f);
int    zzq2_feof  (FILE *f);
int    zzq2_fgetc (FILE *f);

/* Allocations of 64 KiB and more go to the high arena, smaller ones
   stay on the release newlib heap (zzq2_mem.c). */
void *zzq2_malloc(size_t size);
void  zzq2_free(void *p);

#ifdef __cplusplus
}
#endif

#undef  fopen
#define fopen   zzq2_fopen
#undef  fclose
#define fclose  zzq2_fclose
#undef  remove
#define remove  zzq2_remove
#undef  fread
#define fread   zzq2_fread
#undef  fwrite
#define fwrite  zzq2_fwrite
#undef  fseek
#define fseek   zzq2_fseek
#undef  ftell
#define ftell   zzq2_ftell
#undef  feof
#define feof    zzq2_feof
#undef  fgetc
#define fgetc   zzq2_fgetc
#undef  getc
#define getc    zzq2_fgetc
#define rename  zzq2_rename
#define malloc  zzq2_malloc
#define free    zzq2_free

#endif
