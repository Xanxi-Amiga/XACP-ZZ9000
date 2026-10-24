/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2tgl_tgl.c - ZZQuake2TGL: the TinyGL side of the adapter.
 *
 * This file sees TinyGL's internal headers (zgl.h, zbuffer.h) and
 * nothing of Quake II. It owns the TinyGL context and the z-buffer
 * frame, and gives the GL binding (zzq2tgl_qgl.c) a few plain C
 * functions for the state TinyGL keeps in its ZBuffer:
 *     GL_MODULATE / GL_REPLACE        zb->zz_modulate
 *     GL_ALPHA_TEST and glAlphaFunc   zb->zz_alpha_limit
 *     GL_LINEAR of the bound texture  zb->zz_linear (1 bilinear,
 *                                     2 lightmap: span interpolation)
 *
 * It also provides what TinyGL expects from its platform: gl_malloc,
 * gl_zalloc, gl_free (on the Quake II heap: zzq2_malloc / zzq2_free,
 * zzq2_mem.c) and the message hooks (msghandling.c is not linked).
 *
 * ASCII only.
 */

#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include "zgl.h"
#include "zbuffer.h"

/* zzq2_mem.c */
extern void *zzq2_malloc(size_t size);
extern void  zzq2_free(void *p);
/* release zzquake_platform.c */
extern void  Sys_Error(char *error, ...);

/* ------------------------------------------------------------------
   Memory: TinyGL allocates its context, its z-buffer and frame, and
   one 256 KiB texel array per texture. Large blocks go to the arena
   (zzq2_malloc), like the engine's.
   ------------------------------------------------------------------ */
static unsigned g_tgl_bytes, g_tgl_allocs;

void *gl_malloc(GLint size)
{
    void *p;
    if (size <= 0)
        return NULL;
    p = zzq2_malloc((size_t) size);
    if (!p)
        Sys_Error("ZZQ2TGL: TinyGL out of memory (%d bytes)", (int) size);
    g_tgl_bytes += (unsigned) size;
    g_tgl_allocs++;
    return p;
}

void *gl_zalloc(GLint size)
{
    void *p = gl_malloc(size);
    memset(p, 0, (size_t) size);
    return p;
}

void gl_free(void *p)
{
    if (p)
        zzq2_free(p);
}

/* ------------------------------------------------------------------
   Messages. A TinyGL fatal error ends the run through the release
   Sys_Error (message shown by the launcher). Warnings are counted.
   ------------------------------------------------------------------ */
static unsigned g_tgl_warnings;

void tgl_warning(const char *text, ...) { (void) text; g_tgl_warnings++; }
void tgl_trace(const char *text, ...)   { (void) text; }
void tgl_fixme(const char *text, ...)   { (void) text; g_tgl_warnings++; }

void gl_fatal_error(char *format, ...)
{
    Sys_Error("TinyGL: %s", format ? format : "?");
#ifdef __arm__
    for (;;) { __asm__ volatile("wfe"); }
#else
    for (;;) { }    /* test bench on the PC only */
#endif
}

/* ------------------------------------------------------------------
   Context and frame
   ------------------------------------------------------------------ */
static ZBuffer *g_zb;
static int g_w, g_h;

/* Opens TinyGL at w x h the first time, resizes it afterwards.
   Returns 1 on success. */
int zz_tgl_open(int w, int h)
{
    if (!g_zb) {
        g_zb = ZB_open(w, h, ZB_MODE_RGBA, NULL);
        if (!g_zb)
            return 0;
        glInit(g_zb);
        g_zb->zz_modulate = 1;     /* OpenGL default: GL_MODULATE */
        g_zb->zz_alpha = 255;
        g_zb->zz_alpha_limit = 255; /* GL_ALPHA_TEST off           */
        g_zb->zz_linear = 0;        /* set by the GL binding        */
        g_zb->zz_qpersp = 1;        /* ZZOpenLaraRIX A2.18 */
        g_zb->zz_subpix = 1;
    } else if (w != g_w || h != g_h) {
        /* the GL state (textures, environment) is kept */
        if (ZB_resize(g_zb, NULL, w, h) != 0)
            return 0;
    }
    g_w = g_zb->xsize;
    g_h = g_zb->ysize;
    g_zb->zz_cx0 = 0; g_zb->zz_cy0 = 0;     /* ZZOpenLaraRIX A2.18: no scissor */
    g_zb->zz_cx1 = g_w; g_zb->zz_cy1 = g_h;
    return 1;
}

const unsigned *zz_tgl_frame(int *w, int *h)
{
    if (!g_zb)
        return NULL;
    if (w) *w = g_w;
    if (h) *h = g_h;
    return (const unsigned *) g_zb->pbuf;
}

void zz_tgl_set_modulate(int on)
{
    if (g_zb)
        g_zb->zz_modulate = on ? 1u : 0u;
}

void zz_tgl_set_linear(int on)
{
    if (g_zb)
        g_zb->zz_linear = (on == 1 || on == 2) ? (GLuint) on : 0u; /* 2: lightmap */
}

/* limit = largest (255 - alpha) that is still drawn; 255 = no test */
void zz_tgl_set_alpha_limit(unsigned limit)
{
    if (g_zb)
        g_zb->zz_alpha_limit = limit > 255u ? 255u : limit;
}

/* ZZOpenLaraRIX A2.18: 1 = textures divided by 1/w (Core1/tgl/ol_patch_tinygl_persp.py), 0 = by the depth */
void zz_tgl_set_qpersp(int on)
{
    if (g_zb)
        g_zb->zz_qpersp = on ? 1u : 0u;
}

/* ZZOpenLaraRIX A2.18: 1 = triangle vertices to the sub-pixel (Core1/tgl/ol_ztriangle.h), 0 = whole pixels */
void zz_tgl_set_subpix(int on)
{
    if (g_zb)
        g_zb->zz_subpix = on ? 1u : 0u;
}

/* ZZOpenLaraRIX A2.18: pixels drawn by the triangles, x0 <= x < x1, y0 <= y < y1 (origin top left) */
void zz_tgl_set_scissor(int x0, int y0, int x1, int y1)
{
    if (!g_zb)
        return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g_zb->xsize) x1 = g_zb->xsize;
    if (y1 > g_zb->ysize) y1 = g_zb->ysize;
    g_zb->zz_cx0 = x0; g_zb->zz_cy0 = y0;
    g_zb->zz_cx1 = x1; g_zb->zz_cy1 = y1;
}

unsigned zz_tgl_warnings(void)    { return g_tgl_warnings; }
unsigned zz_tgl_alloc_bytes(void) { return g_tgl_bytes; }

/* glClear restricted to the scissor box (ref_gl: the grey background
   of the player model in the player setup menu, RDF_NOWORLDMODEL).
   x, y_top, w, h in frame pixels, y from the top. Depth is cleared to
   0, the value TinyGL's glClear uses ("farthest"). */
void zz_tgl_clear_rect(int x, int y, int w, int h, int color, unsigned rgb, int depth)
{
    int i, j;
    if (!g_zb)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_w) w = g_w - x;
    if (y + h > g_h) h = g_h - y;
    if (w <= 0 || h <= 0)
        return;
    for (j = y; j < y + h; j++) {
        PIXEL *pp = g_zb->pbuf + j * g_w + x;
        GLushort *pz = g_zb->zbuf + j * g_w + x;
        for (i = 0; i < w; i++) {
            if (color) pp[i] = (PIXEL) rgb;
            if (depth) pz[i] = 0;
        }
    }
}
