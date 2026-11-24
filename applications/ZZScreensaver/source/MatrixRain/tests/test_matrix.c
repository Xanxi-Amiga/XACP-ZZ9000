/* ZZScreensaverR PC bench: is the ZZGL adapter transparent?
 * Copyright (C) 2026 Xanxi. GPL-2.0-or-later.
 *
 * The unmodified scene (matrix/matrixview.c) is drawn twice by the same small
 * software rasteriser:
 *   - MX_REFERENCE build: the scene's mpgl* calls are executed directly, every
 *     quad of the single GL_QUADS run, the luminance font as it is;
 *   - adapter build: the scene's calls go through matrix/mv_gl.c, and this file
 *     stands in for the few ZZGL functions the adapter calls, refusing what the
 *     real ZZGL refuses (more than 64 vertices in a Begin/End, a texture that is
 *     not RGB, not a power of two, smaller than 8 or larger than 256).
 * Both builds write the same frames when the adapter changes nothing visible.
 * This is not a model of RasterIX: no RGB565 texel, no hardware fill rule. With
 * "565" the adapter build quantises texels and frame buffer to RGB565 for a
 * closer preview.
 *
 * A third build, -DMX_PIXELS, lets the adapter use its pixel path: the stand-in
 * then gives it the matrices and the viewport ZZGL would, and a 640 x 481
 * RGB565 buffer laid out like a RasterIX colour buffer; the written frame is
 * that buffer plus what the adapter still sent as quads (the flares). It is
 * compared with the reference within the rounding of RGB565, not byte for byte.
 *
 * usage: test_matrix cols rows frames first_written out.rgb [565]
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "matrix_scene.h"
#include "matrixview.h"

#define W MX_WIDTH
#define H MX_HEIGHT

typedef struct { float x, y, w, s, t; } rvert;

static unsigned char fb[W * H * 3];
static unsigned char tex[256 * 256 * 3];
static int tex_w, tex_h, tex_ok, q565, raster_on;
static float proj[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
static unsigned long checks_failed, tris_drawn;

static void fail(const char *what) { if (checks_failed++ < 5) fprintf(stderr, "CHECK FAILED: %s\n", what); }

static void texel(float s, float t, float out[3])
{
    float x = s * (float)tex_w - 0.5f, y = t * (float)tex_h - 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y), i, c;
    float fx = x - (float)x0, fy = y - (float)y0, acc[3] = { 0, 0, 0 };
    for (i = 0; i < 4; i++) {
        int xx = x0 + (i & 1), yy = y0 + (i >> 1);
        float wgt = ((i & 1) ? fx : 1.0f - fx) * ((i >> 1) ? fy : 1.0f - fy);
        const unsigned char *p;
        xx = ((xx % tex_w) + tex_w) % tex_w; yy = ((yy % tex_h) + tex_h) % tex_h;
        p = tex + 3 * (yy * tex_w + xx);
        for (c = 0; c < 3; c++) acc[c] += wgt * (float)p[c];
    }
    for (c = 0; c < 3; c++) out[c] = acc[c] / 255.0f;
}

static void add_pixel(int x, int y, const float rgb[3])
{
    static const int bits[3] = { 5, 6, 5 };
    unsigned char *p = fb + 3 * (y * W + x);
    int c;
    for (c = 0; c < 3; c++) {
        float v = rgb[c] < 0.0f ? 0.0f : rgb[c] > 1.0f ? 1.0f : rgb[c];
        int add, sum;
        if (q565) {
            int levels = (1 << bits[c]) - 1, q = (int)(v * (float)levels);
            add = q * 255 / levels;
        } else add = (int)(v * 255.0f + 0.5f);
        sum = p[c] + add;
        p[c] = (unsigned char)(sum > 255 ? 255 : sum);
    }
}

/* edge function on 1/16 pixel integers; top-left rule so that the two triangles of a quad never
   both add the pixels of their common edge */
static long long edge(long long ax, long long ay, long long bx, long long by, long long px, long long py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}
static int top_left(long long ax, long long ay, long long bx, long long by)
{
    long long dx = bx - ax, dy = by - ay;
    return (dy < 0) || (dy == 0 && dx > 0);
}

static void triangle(const rvert *a, const rvert *b, const rvert *c, const float rgb[3])
{
    const rvert *v[3];
    long long X[3], Y[3], area, minx, maxx, miny, maxy, px, py;
    int i;
    v[0] = a; v[1] = b; v[2] = c;
    for (i = 0; i < 3; i++) {
        float sx = (v[i]->x / v[i]->w * 0.5f + 0.5f) * (float)W;
        float sy = (1.0f - (v[i]->y / v[i]->w * 0.5f + 0.5f)) * (float)H;
        X[i] = (long long)lrintf(sx * 16.0f); Y[i] = (long long)lrintf(sy * 16.0f);
    }
    area = edge(X[0], Y[0], X[1], Y[1], X[2], Y[2]);
    if (area == 0) return;
    if (area < 0) { const rvert *tv = v[1]; long long tq; v[1] = v[2]; v[2] = tv; tq = X[1]; X[1] = X[2]; X[2] = tq; tq = Y[1]; Y[1] = Y[2]; Y[2] = tq; area = -area; }
    minx = maxx = X[0]; miny = maxy = Y[0];
    for (i = 1; i < 3; i++) { if (X[i] < minx) minx = X[i]; if (X[i] > maxx) maxx = X[i]; if (Y[i] < miny) miny = Y[i]; if (Y[i] > maxy) maxy = Y[i]; }
    minx = minx / 16 - 1; maxx = maxx / 16 + 1; miny = miny / 16 - 1; maxy = maxy / 16 + 1;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > W - 1) maxx = W - 1;
    if (maxy > H - 1) maxy = H - 1;
    tris_drawn++;
    if (!raster_on) return;      /* frames that are not written: counted, not filled */
    for (py = miny; py <= maxy; py++) for (px = minx; px <= maxx; px++) {
        long long cx = px * 16 + 8, cy = py * 16 + 8;
        long long e0 = edge(X[1], Y[1], X[2], Y[2], cx, cy), e1 = edge(X[2], Y[2], X[0], Y[0], cx, cy), e2 = edge(X[0], Y[0], X[1], Y[1], cx, cy);
        float l0, l1, l2, tx[3], out[3];
        if (e0 < 0 || e1 < 0 || e2 < 0) continue;
        if (e0 == 0 && !top_left(X[1], Y[1], X[2], Y[2])) continue;
        if (e1 == 0 && !top_left(X[2], Y[2], X[0], Y[0])) continue;
        if (e2 == 0 && !top_left(X[0], Y[0], X[1], Y[1])) continue;
        l0 = (float)e0 / (float)area; l1 = (float)e1 / (float)area; l2 = (float)e2 / (float)area;
        if (tex_ok) texel(l0 * v[0]->s + l1 * v[1]->s + l2 * v[2]->s, l0 * v[0]->t + l1 * v[1]->t + l2 * v[2]->t, tx);
        else { tx[0] = tx[1] = tx[2] = 1.0f; }
        out[0] = tx[0] * rgb[0]; out[1] = tx[1] * rgb[1]; out[2] = tx[2] * rgb[2];
        add_pixel((int)px, (int)py, out);
    }
}

static void project(float x, float y, float z, float s, float t, rvert *o)
{
    o->x = proj[0] * x + proj[4] * y + proj[8] * z + proj[12];
    o->y = proj[1] * x + proj[5] * y + proj[9] * z + proj[13];
    o->w = proj[3] * x + proj[7] * y + proj[11] * z + proj[15];
    o->s = s; o->t = t;
}

static void store_texture(const unsigned char *src, int w, int h, int channels)
{
    int i, c;
    for (i = 0; i < w * h; i++) for (c = 0; c < 3; c++) {
        int v = channels == 1 ? src[i] : src[3 * i + c];
        if (q565) { int levels = (c == 1) ? 63 : 31; v = (v * levels / 255) * 255 / levels; }
        tex[3 * i + c] = (unsigned char)v;
    }
    tex_w = w; tex_h = h; tex_ok = 1;
}

#ifdef MX_REFERENCE
/* ---- the scene's calls executed directly ---- */
#include "gl_common.h"
static rvert rq[4]; static int rn; static float rs, rt, rrgb[3] = { 1, 1, 1 }, rq_rgb[3];
void mpglBegin(unsigned mode) { if (mode != GL_QUADS) fail("mode"); rn = 0; }
void mpglEnd(void) { if (rn) fail("incomplete quad"); }
void mpglColor4ub(int r, int g, int b, int a) { (void)a; rrgb[0] = (float)r * (1.0f / 255.0f); rrgb[1] = (float)g * (1.0f / 255.0f); rrgb[2] = (float)b * (1.0f / 255.0f); }
void mpglTexCoord2f(float s, float t) { rs = s; rt = t; }
void mpglVertex3f(float x, float y, float z)
{
    if (rn == 0) { rq_rgb[0] = rrgb[0]; rq_rgb[1] = rrgb[1]; rq_rgb[2] = rrgb[2]; }
    project(x, y, z, rs, rt, &rq[rn++]);
    if (rn < 4) return;
    rn = 0;
    triangle(&rq[0], &rq[1], &rq[2], rq_rgb);
    triangle(&rq[0], &rq[2], &rq[3], rq_rgb);
}
void mpglTexImage2D(unsigned target, int level, int internalformat, int width, int height, int border, unsigned format, unsigned type, const void *pixels)
{
    (void)target; (void)level; (void)internalformat; (void)border; (void)type;
    if (format != GL_LUMINANCE) fail("format");
    store_texture((const unsigned char *)pixels, width, height, 1);
}
void mpglTexParameterf(unsigned target, unsigned pname, float param) { (void)target; (void)pname; (void)param; }
void mpglTexEnvi(unsigned target, unsigned pname, int param) { (void)target; (void)pname; (void)param; }
void mpglClearColor(float r, float g, float b, float a) { (void)r; (void)g; (void)b; (void)a; }
void mpglClear(unsigned mask) { (void)mask; memset(fb, 0, sizeof fb); }
void mpglEnable(unsigned cap) { (void)cap; }
void mpglBlendFunc(unsigned sfactor, unsigned dfactor) { (void)sfactor; (void)dfactor; }
void mpglViewport(int x, int y, int width, int height) { (void)x; (void)y; (void)width; (void)height; }
void mpglLoadMatrixf(const float *m) { memcpy(proj, m, sizeof proj); }
void mv_gl_reset(void) {}
int mv_gl_failed(void) { return 0; }
unsigned mv_gl_quads_in(void) { return 0; }
unsigned mv_gl_quads_drawn(void) { return 0; }
#else
/* ---- stand-in for the ZZGL functions matrix/mv_gl.c calls ---- */
#include "zzgl.h"
static rvert zv[ZZGL_MAX_VERTS]; static u32 zn, z_in_begin, z_mode, z_matrix_mode = ZGL_MODELVIEW, z_bound, z_tex_enabled, z_blend_enabled, z_blend_ok, z_env_ok;
static float zs, zt, zrgb[3] = { 1, 1, 1 }, zv_rgb[ZZGL_MAX_VERTS][3], z_mv[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, z_pr[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
static void z_update(void)
{
    int i, j, k;
    for (i = 0; i < 4; i++) for (j = 0; j < 4; j++) { float acc = 0; for (k = 0; k < 4; k++) acc += z_pr[k * 4 + j] * z_mv[i * 4 + k]; proj[i * 4 + j] = acc; }
}
void zzglBegin(u32 mode) { if (z_in_begin) fail("Begin inside Begin"); if (mode != ZGL_QUADS) fail("ZZGL mode"); z_in_begin = 1; z_mode = mode; zn = 0; z_update(); }
void zzglEnd(void)
{
    u32 i;
    if (!z_in_begin) fail("End without Begin");
    z_in_begin = 0;
    if (zn & 3u) fail("vertices not a multiple of 4");
    if (!z_tex_enabled || !z_blend_enabled || !z_blend_ok || !z_env_ok || z_bound != 1u) fail("state at draw time");
    for (i = 0; i + 3u < zn; i += 4u) { triangle(&zv[i], &zv[i + 1u], &zv[i + 2u], zv_rgb[i]); triangle(&zv[i], &zv[i + 2u], &zv[i + 3u], zv_rgb[i]); }
}
void zzglColor4f(float r, float g, float b, float a) { (void)a; zrgb[0] = r; zrgb[1] = g; zrgb[2] = b; }
void zzglTexCoord2f(float s, float t) { zs = s; zt = t; }
void zzglVertex3f(float x, float y, float z)
{
    if (!z_in_begin) { fail("vertex outside Begin/End"); return; }
    if (zn >= ZZGL_MAX_VERTS) { fail("more than ZZGL_MAX_VERTS vertices in a Begin/End"); return; }
    zv_rgb[zn][0] = zrgb[0]; zv_rgb[zn][1] = zrgb[1]; zv_rgb[zn][2] = zrgb[2];
    project(x, y, z, zs, zt, &zv[zn++]);
}
void zzglBindTexture(u32 target, u32 texnum) { (void)target; z_bound = texnum; }
void zzglTexParameterf(u32 target, u32 pname, float param) { (void)target; (void)pname; (void)param; }
void zzglTexImage2D(u32 target, s32 level, s32 internalformat, s32 width, s32 height, s32 border, u32 format, u32 type, const void *pixels)
{
    u32 w = (u32)width, h = (u32)height;
    (void)target; (void)internalformat; (void)border;
    if (level != 0 || type != ZGL_UNSIGNED_BYTE || format != ZGL_RGB) { fail("texture format ZZGL refuses"); return; }
    if (w < 8u || h < 8u || w > 256u || h > 256u || (w & (w - 1u)) || (h & (h - 1u))) { fail("texture size ZZGL refuses"); return; }
    store_texture((const unsigned char *)pixels, width, height, 3);
}
void zzglTexEnvf(u32 target, u32 pname, float param) { (void)target; if (pname == ZGL_TEXTURE_ENV_MODE) z_env_ok = ((u32)param == ZGL_MODULATE); }
void zzglClearColor(float r, float g, float b, float a) { if (r != 0.0f || g != 0.0f || b != 0.0f) fail("clear colour"); (void)a; }
void zzglClear(u32 mask) { if (mask != ZGL_COLOR_BUFFER_BIT) fail("clear mask"); memset(fb, 0, sizeof fb); }
void zzglEnable(u32 cap) { if (cap == ZGL_TEXTURE_2D) z_tex_enabled = 1; else if (cap == ZGL_BLEND) z_blend_enabled = 1; else fail("Enable"); }
void zzglDisable(u32 cap) { if (cap != ZGL_DEPTH_TEST && cap != ZGL_CULL_FACE && cap != ZGL_ALPHA_TEST) fail("Disable"); }
void zzglBlendFunc(u32 sfactor, u32 dfactor) { z_blend_ok = (sfactor == ZGL_ONE && dfactor == ZGL_ONE); }
void zzglViewport(s32 x, s32 y, s32 width, s32 height) { if (x || y || width != W || height != H) fail("viewport"); }
void zzglMatrixMode(u32 mode) { z_matrix_mode = mode; }
void zzglLoadIdentity(void) { static const float id[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; memcpy(z_matrix_mode == ZGL_PROJECTION ? z_pr : z_mv, id, sizeof id); }
void zzglLoadMatrixf(const float *m) { memcpy(z_matrix_mode == ZGL_PROJECTION ? z_pr : z_mv, m, 16 * sizeof(float)); }
static u16 fb565[W * (H + 1)];          /* a RasterIX colour buffer: 480 displayed lines and the fence line */
void *sim_ptr(u32 a) { (void)a; return fb565; }
void sim_check_fb_write(u32 dst) { if (dst != 0x06600000u) fail("pixel path: not the target buffer"); }
u32 zzgl_target_color(void) { return 0x06600000u; }
#ifdef MX_PIXELS
/* what zzgl_direct_begin() hands out in ZZGL: the product of the matrices, and the viewport of
   viewport_update() for a 640 x 480 viewport at the origin (zzgl.c) */
int zzgl_direct_begin(zzgl_direct *d)
{
    static rix_viewport vp;
    if (z_in_begin) return 0;
    z_update();
    vp.scale_x = (float)W * 0.5f; vp.trans_x = (float)W * 0.5f - 0.5f + 0.03125f;
    vp.scale_y = (float)H * 0.5f; vp.trans_y = 1.0f + (float)H * 0.5f - 0.5f;
    vp.scale_z = 0.5f; vp.trans_z = 0.5f;
    d->mvp = proj; d->vp = &vp; d->w_min = 0.001f;
    d->clip_x0 = 0.0f; d->clip_x1 = (float)W - 0.53125f; d->clip_y0 = 1.0f; d->clip_y1 = (float)(H + 1) - 0.53125f;
    return 1;
}
/* only during the one frame in which the pixel path is prepared: not drawn here, that frame is not compared */
int zzgl_direct_tri(const u32 *desc39) { (void)desc39; return 0; }
#else
/* this stand-in has no RasterIX frame: the direct path is refused, every quad comes through the
   calls above (tools/host_stream.py checks the direct path against the real ZZGL and librixgpu) */
int zzgl_direct_begin(zzgl_direct *d) { (void)d; return 0; }
int zzgl_direct_tri(const u32 *desc39) { (void)desc39; fail("descriptor without a frame"); return 1; }
#endif
#endif

int main(int argc, char **argv)
{
    int cols = argc > 1 ? atoi(argv[1]) : 90, rows = argc > 2 ? atoi(argv[2]) : 70;
    int frames = argc > 3 ? atoi(argv[3]) : 600, first = argc > 4 ? atoi(argv[4]) : frames - 1;
    const char *out = argc > 5 ? argv[5] : "frames.rgb";
    FILE *f;
    int i;
    unsigned long in_sum = 0, drawn_sum = 0, written = 0;
    q565 = (argc > 6 && !strcmp(argv[6], "565"));
    f = fopen(out, "wb");
    if (!f || cols < 2 || rows < 2 || cols > MX_MAX_COLS || rows > MX_MAX_ROWS || frames < 1) return 2;
    mv_sys_reset(0x58414e58u);
    mv_gl_reset();
#ifdef MX_PIXELS
    mv_gl_grid(cols, rows, 1);
#endif
    matrixview_matrix_resize(cols, rows);
    matrixview_init(W, H);
    for (i = 0; i < frames; i++) {
        tris_drawn = 0;
        raster_on = (i >= first);
#ifdef MX_PIXELS
        /* a pixel frame has no clear: the displayed lines start as a leftover no frame contains,
           so that a pixel the adapter failed to write shows in the comparison */
        memset(fb, 0, sizeof fb);
        { int q; for (q = 0; q < W * H; q++) fb565[q] = 0xF81Fu; for (; q < W * (H + 1); q++) fb565[q] = 0u; }
#endif
        matrixview_draw((double)i / 60.0, NULL);
        in_sum += mv_gl_quads_in(); drawn_sum += mv_gl_quads_drawn();
#ifdef MX_PIXELS
        if (i >= first) {
            /* the displayed lines of the RGB565 buffer, under the quads the adapter left to the rasteriser */
            int x, y, c;
            if (!mv_gl_pixels_on()) { int q; for (q = 0; q < W * H; q++) fb565[q] = 0u; }    /* a frame RasterIX drew whole */
            if (!mv_gl_pixels_on() || !mv_gl_quads_pixels()) fail("pixel path not in use");
            if (mv_gl_quads_lost()) fail("pixel path dropped quads");
            for (x = 0; x < W; x++) if (fb565[H * W + x]) fail("pixel path wrote the fence line");
            for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
                unsigned v = fb565[y * W + x], rgb[3];
                unsigned char *px = fb + 3 * (y * W + x);
                rgb[0] = (v >> 11) * 255u / 31u; rgb[1] = ((v >> 5) & 63u) * 255u / 63u; rgb[2] = (v & 31u) * 255u / 31u;
                for (c = 0; c < 3; c++) { unsigned sum = px[c] + rgb[c]; px[c] = (unsigned char)(sum > 255u ? 255u : sum); }
            }
        }
#endif
        if (i >= first) { fwrite(fb, 1, sizeof fb, f); written++; }
    }
    fclose(f);
    if (mv_sys_failed()) fail("scene memory pool");
    if (mv_gl_failed()) fail("adapter refused a call of the scene");
    printf("grid %dx%d frames %d written %lu quads_in_per_frame %lu quads_sent_per_frame %lu last_frame_triangles %lu checks_failed %lu\n",
           cols, rows, frames, written, in_sum / (unsigned long)frames, drawn_sum / (unsigned long)frames, tris_drawn, checks_failed);
    return checks_failed ? 1 : 0;
}
