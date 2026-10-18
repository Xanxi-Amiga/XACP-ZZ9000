#ifndef ZZVGM_OPLL2413_ALLOC_H
#define ZZVGM_OPLL2413_ALLOC_H
#include <stddef.h>
void zz_opll_pool_reset(void);
void *zz_opll_malloc(size_t n);
void *zz_opll_calloc(size_t n, size_t s);
void zz_opll_free(void *p);
#endif
