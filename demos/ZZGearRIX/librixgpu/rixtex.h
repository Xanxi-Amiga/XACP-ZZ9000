/* rixtex.h - librixgpu 1.0 - texture pool and texture objects
 *
 * G4B facts this module is built on (RasterIX_EF, no external texture
 * memory): the TMU holds ONE texture in FPGA memory; OP_TEXTURE streams the
 * texture's 2 KiB pages from DDR into it (measured: 8 KiB in ~16 us, 128 KiB
 * in ~233 us through the FIFO, i.e. ~70 us of MMIO for the 65-word table +
 * ~160 us of HP3 transfer).  Textures are power-of-two, <= 256x256, RGB565
 * little-endian (the TMU reads 16-bit words), row-major, 2 KiB pages in
 * order.  The page table must travel through the FIFO (a table inside a DDR
 * list deadlocks FSC/TMU on the AXI read channel: B1 run 5).
 *
 * The pool is any DDR range the GPU can read: inside the G4B window
 * (Non-Cacheable for Core1) or elsewhere (then the writer cleans the cache:
 * rix_tex_write_* calls rix_mem_clean() when the pool is marked cacheable).
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef RIXTEX_H
#define RIXTEX_H
#include "rix_hw.h"

#define RIX_TEX_PAGE_BYTES 2048u
#define RIX_TEX_MAX_PAGES  512u    /* pool bitmap size: 512 pages = 1 MiB */

typedef struct {
    u32 base;            /* ARM address of page 0 */
    u32 pages;           /* page count */
    u32 w, h;            /* texels */
    u32 log2w, log2h;
    u32 cfg;             /* OP_RENDER_CONFIG TMU0_CONFIG value (RGB565, filters as requested) */
} rix_texture;

/* pool: base 2 KiB-aligned; cacheable = 1 when Core1 maps it write-back
   (writes are then cleaned to DDR after each upload) */
int  rix_texpool_init(u32 base_arm, u32 pages, int cacheable);
u32  rix_texpool_free_pages(void);
/* w, h powers of two in 8..256; mag_linear = bilinear magnification (GL_LINEAR);
   returns 0 or RIXE_TEX_* */
int  rix_tex_alloc(rix_texture *t, u32 w, u32 h, int mag_linear);
void rix_tex_free(rix_texture *t);
/* uploads: src row-major, w*h texels; byteswap = 1 writes big-endian halves
   (the PRESWAP path of the demos: the scanout swap is then skipped) */
void rix_tex_write_rgb565(const rix_texture *t, const u16 *src, int byteswap);
void rix_tex_write_rgb888(const rix_texture *t, const unsigned char *src, int byteswap);
/* the two list words the frame API emits to bind the texture: TMU config +
   page table (pages words).  Needed only by callers building raw lists. */
u32  rix_tex_table_words(const rix_texture *t);

#endif
