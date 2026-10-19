/* rixtex.h - librixgpu 1.1 - texture pool and texture objects
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
#define RIX_TEX_MAX_PAGES  8192u   /* pool bitmap capacity: 8192 pages = 16 MiB (the bitmap lives in the library, 1 KiB) */

typedef struct {
    u32 base;            /* ARM address of page 0 */
    u32 pages;           /* page count */
    u32 w, h;            /* texels */
    u32 log2w, log2h;
    u32 cfg;             /* OP_RENDER_CONFIG TMU0_CONFIG value (RGB565, filters as requested) */
} rix_texture;

/* pool: base 2 KiB-aligned, pages <= RIX_TEX_MAX_PAGES; cacheable = 1 when
   Core1 maps it write-back (writes are then cleaned to DDR after each
   upload).  The pool is the application's reservation: the library never
   assumes DDR is free (MEMORY_MAP.md). */
int  rix_texpool_init(u32 base_arm, u32 pages, int cacheable);
u32  rix_texpool_free_pages(void);
u32  rix_texpool_largest_free(void);   /* pages of the largest free run (fragmentation) */
/* w, h powers of two in 8..256; mag_linear = bilinear magnification (GL_LINEAR);
   returns 0 or RIXE_TEX_* */
int  rix_tex_alloc(rix_texture *t, u32 w, u32 h, int mag_linear);
void rix_tex_free(rix_texture *t);
/* uploads: src row-major, w*h texels; byteswap = 1 writes big-endian halves
   (the PRESWAP path of the demos: the scanout swap is then skipped) */
void rix_tex_write_rgb565(const rix_texture *t, const u16 *src, int byteswap);
void rix_tex_write_rgb888(const rix_texture *t, const unsigned char *src, int byteswap);
/* 8-bit indexed texels (Quake II .wal / .pcx) through a 256-entry RGB565 palette;
   palette565[] is in TMU order (little-endian values), byteswap as above */
void rix_tex_write_pal8(const rix_texture *t, const unsigned char *idx, const u16 *palette565, int byteswap);
/* sub-rectangle upload (glTexSubImage2D: dynamic lightmap blocks); src is w*h texels row-major */
void rix_tex_write_rect_rgb565(const rix_texture *t, u32 x, u32 y, u32 w, u32 h, const u16 *src, int byteswap);
/* Quake II palette (768 bytes RGB) -> 256 RGB565 entries */
void rix_palette_rgb888_to_565(const unsigned char *rgb768, u16 *out256);
/* an upload invalidates the TMU-bound tracking of rixframe when the texture
   is the one currently loaded: the next bind reloads it */
void rix_tex_touch(const rix_texture *t);
/* the two list words the frame API emits to bind the texture: TMU config +
   page table (pages words).  Needed only by callers building raw lists. */
u32  rix_tex_table_words(const rix_texture *t);

#endif
