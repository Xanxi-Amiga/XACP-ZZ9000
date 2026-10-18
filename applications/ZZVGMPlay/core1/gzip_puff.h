#ifndef ZZVGM_GZIP_PUFF_H
#define ZZVGM_GZIP_PUFF_H
#include <stdint.h>
int zz_gzip_inflate(const uint8_t *src,uint32_t srclen,uint8_t *dst,uint32_t dstcap,uint32_t *outlen);
#endif
