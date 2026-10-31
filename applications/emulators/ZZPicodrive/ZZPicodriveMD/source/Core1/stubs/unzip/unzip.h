#ifndef ZZPICO_UNZIP_STUB_H
#define ZZPICO_UNZIP_STUB_H
/* Stub: zip loader gc-dropped. Fields match cart.c accesses. */
#include <stdio.h>
typedef struct ZIP { FILE *fp; } ZIP;
struct zipent {
    char *name;
    unsigned long uncompressed_size;
    unsigned long compressed_size;
    unsigned long offset;
    unsigned short compression_method;
    unsigned long crc32;
};
#endif
