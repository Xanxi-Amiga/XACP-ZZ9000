/* 0BSD: data movement only; no pixel drawing or blending. */
#ifndef SW_HISTORY_H
#define SW_HISTORY_H
#include <stdint.h>
/* Source has already been byte-swapped for P96. Recover GPU RGB565 while
 * copying to a DIFFERENT, free framebuffer. Never copy the fence row. */
void sw_history_copy(void *dst,const void *src,uint32_t bytes);
#endif
