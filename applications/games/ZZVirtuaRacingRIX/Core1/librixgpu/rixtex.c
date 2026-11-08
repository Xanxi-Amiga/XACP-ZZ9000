/* rixtex.c - librixgpu 1.0 - texture pool (see rixtex.h)
 * SPDX-License-Identifier: 0BSD */
#include "rixtex.h"
#include "rixgpu.h"

static u32 g_pool_base, g_pool_pages;
static int g_pool_cacheable;
static u32 g_pool_map[RIX_TEX_MAX_PAGES / 32u];   /* bit = page used */

int rix_texpool_init(u32 base_arm, u32 pages, int cacheable)
{
    u32 i;
    if ((base_arm & (RIX_TEX_PAGE_BYTES - 1u)) || pages == 0u || pages > RIX_TEX_MAX_PAGES) return (int)RIXE_TEX_POOL;
    g_pool_base = base_arm; g_pool_pages = pages; g_pool_cacheable = cacheable;
    for (i = 0; i < RIX_TEX_MAX_PAGES / 32u; i++) g_pool_map[i] = 0u;
    return 0;
}

u32 rix_texpool_free_pages(void)
{
    u32 i, n = 0u;
    for (i = 0; i < g_pool_pages; i++) if (!(g_pool_map[i >> 5] & (1u << (i & 31u)))) n++;
    return n;
}

static u32 log2u(u32 v) { u32 l = 0u; while ((1u << l) < v) l++; return l; }

/* first fit of `pages` consecutive free pages */
static int pool_find(u32 pages, u32 *first)
{
    u32 i, run = 0u;
    for (i = 0; i < g_pool_pages; i++) {
        if (g_pool_map[i >> 5] & (1u << (i & 31u))) { run = 0u; continue; }
        run++;
        if (run == pages) { *first = i + 1u - pages; return 1; }
    }
    return 0;
}

int rix_tex_alloc(rix_texture *t, u32 w, u32 h, int mag_linear)
{
    u32 bytes, pages, first, i;
    if (w < 8u || h < 8u || w > 256u || h > 256u || (w & (w - 1u)) || (h & (h - 1u))) return (int)RIXE_TEX_SIZE;
    bytes = w * h * 2u;
    pages = (bytes + RIX_TEX_PAGE_BYTES - 1u) / RIX_TEX_PAGE_BYTES;
    if (!pool_find(pages, &first)) return (int)RIXE_TEX_POOL;
    for (i = first; i < first + pages; i++) g_pool_map[i >> 5] |= 1u << (i & 31u);
    t->base = g_pool_base + first * RIX_TEX_PAGE_BYTES;
    t->pages = pages;
    t->w = w; t->h = h; t->log2w = log2u(w); t->log2h = log2u(h);
    t->cfg = RIX_TMU_CFG_RGB565(t->log2w, t->log2h, mag_linear ? 1u : 0u);
    return 0;
}

void rix_tex_free(rix_texture *t)
{
    u32 first, i;
    if (!t->pages) return;
    first = (t->base - g_pool_base) / RIX_TEX_PAGE_BYTES;
    for (i = first; i < first + t->pages && i < g_pool_pages; i++) g_pool_map[i >> 5] &= ~(1u << (i & 31u));
    t->pages = 0u;
}

extern void rix_frame_texture_invalidate(u32 base_arm);   /* rixframe.c */
void rix_tex_touch(const rix_texture *t) { rix_frame_texture_invalidate(t->base); }

static void tex_done(const rix_texture *t)
{
    dsb();
    if (g_pool_cacheable) rix_mem_clean(t->base, t->pages * RIX_TEX_PAGE_BYTES);
    rix_tex_touch(t);
}

u32 rix_texpool_largest_free(void)
{
    u32 i, run = 0u, best = 0u;
    for (i = 0; i < g_pool_pages; i++) {
        if (g_pool_map[i >> 5] & (1u << (i & 31u))) { run = 0u; continue; }
        run++; if (run > best) best = run;
    }
    return best;
}

void rix_palette_rgb888_to_565(const unsigned char *rgb, u16 *out)
{
    u32 i;
    for (i = 0; i < 256u; i++)
        out[i] = (u16)(((u16)(rgb[i * 3u] >> 3) << 11) | ((u16)(rgb[i * 3u + 1u] >> 2) << 5) | (u16)(rgb[i * 3u + 2u] >> 3));
}

void rix_tex_write_pal8(const rix_texture *t, const unsigned char *idx, const u16 *pal, int byteswap)
{
    volatile u16 *dst = (volatile u16 *)rix_ptr(t->base);
    u32 i, n = t->w * t->h;
    if (byteswap) for (i = 0; i < n; i++) { u16 v = pal[idx[i]]; dst[i] = (u16)((v >> 8) | (v << 8)); }
    else for (i = 0; i < n; i++) dst[i] = pal[idx[i]];
    tex_done(t);
}

void rix_tex_write_rgb565(const rix_texture *t, const u16 *src, int byteswap)
{
    volatile u16 *dst = (volatile u16 *)rix_ptr(t->base);
    u32 i, n = t->w * t->h;
    if (byteswap) for (i = 0; i < n; i++) dst[i] = (u16)((src[i] >> 8) | (src[i] << 8));
    else for (i = 0; i < n; i++) dst[i] = src[i];
    tex_done(t);
}

void rix_tex_write_rgb888(const rix_texture *t, const unsigned char *src, int byteswap)
{
    volatile u16 *dst = (volatile u16 *)rix_ptr(t->base);
    u32 i, n = t->w * t->h;
    for (i = 0; i < n; i++) {
        u16 v = (u16)(((u16)(src[i * 3u] >> 3) << 11) | ((u16)(src[i * 3u + 1u] >> 2) << 5) | (u16)(src[i * 3u + 2u] >> 3));
        if (byteswap) v = (u16)((v >> 8) | (v << 8));
        dst[i] = v;
    }
    tex_done(t);
}

/* sub-rectangle (glTexSubImage2D): rows [y, y+h) x [x, x+w) of the texture; only the touched
   rows are cleaned from the cache */
void rix_tex_write_rect_rgb565(const rix_texture *t, u32 x, u32 y, u32 w, u32 h, const u16 *src, int byteswap)
{
    volatile u16 *dst = (volatile u16 *)rix_ptr(t->base);
    u32 i, j;
    if (x + w > t->w || y + h > t->h) return;
    for (j = 0; j < h; j++) {
        volatile u16 *row = dst + (y + j) * t->w + x;
        const u16 *s = src + j * w;
        if (byteswap) for (i = 0; i < w; i++) row[i] = (u16)((s[i] >> 8) | (s[i] << 8));
        else for (i = 0; i < w; i++) row[i] = s[i];
    }
    dsb();
    if (g_pool_cacheable) rix_mem_clean(t->base + y * t->w * 2u, h * t->w * 2u);
    rix_tex_touch(t);
}

u32 rix_tex_table_words(const rix_texture *t) { return 1u + t->pages; }
