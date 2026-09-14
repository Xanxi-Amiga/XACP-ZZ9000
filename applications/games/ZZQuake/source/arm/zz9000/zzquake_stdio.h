/* Engine stdio is redirected to the in-memory filesystem. */




























#ifndef ZZQUAKE_STDIO_H
#define ZZQUAKE_STDIO_H

#include <stdio.h>   /* for FILE type name, size_t, EOF, SEEK_SET */

/* Kill newlib macro versions BEFORE redefining */
#undef getc
#undef putc
#undef feof
#undef ferror
#undef getchar
#undef putchar

/* ---- redirection map ---- */
#define fopen    zzq_fopen
#define fclose   zzq_fclose
#define fread    zzq_fread
#define fwrite   zzq_fwrite
#define fseek    zzq_fseek
#define ftell    zzq_ftell
#define fgetc    zzq_fgetc
#define getc     zzq_fgetc
#define feof     zzq_feof
#define fprintf  zzq_fprintf
#define fscanf   zzq_fscanf
#define fflush   zzq_fflush
#define remove   zzq_remove
#define printf   zzq_printf
/* sprintf / vsprintf intentionally NOT redirected: pure string
   formatting, no FILE, newlib versions are safe and needed. */

#ifdef __cplusplus
extern "C" {
#endif

/* FILE* here is an opaque token: the only values ever returned by
   zzq_fopen are pointers to zzq internal descriptors, and only the
   zzq_* functions below ever dereference them. newlib never sees
   these pointers. */
FILE  *zzq_fopen (const char *path, const char *mode);
int    zzq_fclose(FILE *f);
size_t zzq_fread (void *dst, size_t sz, size_t n, FILE *f);
size_t zzq_fwrite(const void *src, size_t sz, size_t n, FILE *f);
int    zzq_fseek (FILE *f, long off, int whence);
long   zzq_ftell (FILE *f);
int    zzq_fgetc (FILE *f);
int    zzq_feof  (FILE *f);
int    zzq_fprintf(FILE *f, const char *fmt, ...);
int    zzq_fscanf (FILE *f, const char *fmt, ...);
int    zzq_fflush (FILE *f);
int    zzq_remove (const char *path);
int    zzq_printf (const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* ZZQUAKE_STDIO_H */
