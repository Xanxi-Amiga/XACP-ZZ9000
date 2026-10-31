#ifndef ZZPICO_ZLIB_STUB_H
#define ZZPICO_ZLIB_STUB_H
/* Stub zlib pour build bare-metal ZZPicoDrive.
   crc32/inflate* sont DEFINIS dans zzpico_stubs.c ; ici juste type + declarations.
   gz* : chemin fichier de state.c jamais exerce (on passe par PicoStateFP callbacks). */
#include <stddef.h>
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef unsigned char Bytef;
typedef unsigned int uIntf;
typedef unsigned long uLongf;
typedef void *voidpf;
typedef void *voidp;

typedef struct z_stream_s {
    unsigned char *next_in;  unsigned int avail_in;  unsigned long total_in;
    unsigned char *next_out; unsigned int avail_out; unsigned long total_out;
    char *msg; void *state;
    void *zalloc; void *zfree; void *opaque;
    int data_type; unsigned long adler; unsigned long reserved;
} z_stream;
typedef z_stream *z_streamp;

#define Z_OK          0
#define Z_STREAM_END  1
#define Z_NO_FLUSH    0
#define Z_FINISH      4
#define MAX_WBITS     15

int inflateInit2(z_stream *strm, int windowBits);
int inflate(z_stream *strm, int flush);
int inflateEnd(z_stream *strm);
unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len);

typedef void *gzFile;
static inline gzFile gzopen(const char *path, const char *mode){ (void)path;(void)mode; return (gzFile)0; }
static inline int gzread(gzFile f, void *buf, unsigned len){ (void)f;(void)buf;(void)len; return 0; }
static inline int gzwrite(gzFile f, const void *buf, unsigned len){ (void)f;(void)buf;(void)len; return 0; }
static inline int gzclose(gzFile f){ (void)f; return 0; }

#define Z_DEFAULT_STRATEGY 0
#define Z_DEFAULT_COMPRESSION -1
static inline int    gzsetparams(gzFile f, int l, int st){ (void)f;(void)l;(void)st; return 0; }
static inline long   gzseek(gzFile f, long o, int w){ (void)f;(void)o;(void)w; return -1; }
static inline long   gztell(gzFile f){ (void)f; return 0; }
static inline int    gzeof(gzFile f){ (void)f; return 1; }
static inline int    gzgetc(gzFile f){ (void)f; return -1; }
static inline int    gzputc(gzFile f, int c){ (void)f; return c; }
static inline int    gzflush(gzFile f, int fl){ (void)f;(void)fl; return 0; }
static inline const char *gzerror(gzFile f, int *e){ (void)f; if(e)*e=0; return ""; }

#endif
