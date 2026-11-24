/* ZZScreensaverR: MatrixView's GL calls drawn through ZZGL on Core1.
 * Copyright (C) 2026 Xanxi. GPL-2.0-or-later. See COPYING. No warranty.
 *
 * What differs from a desktop OpenGL, and is handled here so that
 * matrixview.c stays as it was written:
 *  - ZZGL takes at most ZZGL_MAX_VERTS (64) vertices between Begin and End:
 *    the scene's single GL_QUADS run goes out 16 quads at a time;
 *  - the font is a luminance image; ZZGL takes RGB: each texel becomes a grey;
 *  - the scene loads one matrix and never selects a matrix mode: it is loaded
 *    as the projection, with an identity modelview;
 *  - with the scene's additive blending (ONE, ONE) a black quad adds nothing:
 *    such quads are not sent.
 *
 * Direct path (0.2).  On a grid whose cells are a whole number of pixels, every
 * character cell is the same pair of triangles moved by whole pixels: its two
 * RasterIX descriptors differ from a reference cell's only by the bounding box,
 * the colour words and the texture words of its glyph.  Those words are taken
 * from tables that librixgpu's own functions filled once (the same calls ZZGL's
 * draw_triangle() makes), so the descriptors sent are the ones ZZGL would have
 * built, word for word, without recomputing them 6000 times a frame.  Each cell
 * is checked once against the general computation before it may use the tables;
 * flares, clipped cells, and any quad that is not exactly a grid cell keep going
 * through ZZGL, in the scene's order.  Build with -DMV_NO_DIRECT for the 0.1
 * behaviour (every quad through ZZGL).
 *
 * Pixel path (0.3).  A character cell of a whole-pixel grid is a small picture
 * pasted at a fixed place: two textured, blended triangles per cell cost
 * RasterIX more than the pixels are worth (A4000, 0.2: 18.5 ms for 64 x 48).
 * On the presets that allow it, Core1 writes the cells itself into the frame
 * buffer, the way the presenter of ZZScreensaverS writes its previous frame
 * there: one small bitmap per glyph, sampled from the font as the bilinear
 * texture would be at this cell size, times the brightness of the cell.  Every
 * cell is written, black ones too, so the frame needs no clear.  What is not
 * exactly a grid cell (the flares) is kept and drawn by RasterIX afterwards,
 * on top, blended as before.  The picture is close to the triangles' one, not
 * identical: other rounding.  Build with -DMV_NO_PIXELS to leave it out.
 */
#include "zzgl.h"
#include "gl_common.h"
#include "matrix_scene.h"

#define MV_TEXTURE_ID 1u
#define MV_FONT_TEXELS (128u * 64u)
#define MV_BATCH_QUADS (ZZGL_MAX_VERTS / 4u)

typedef struct { float x, y, z, s, t; } mv_vertex;

static unsigned char font_rgb[MV_FONT_TEXELS * 3u];
static mv_vertex quad[4];
static float quad_rgb[3];
static float cur_s, cur_t, cur_rgb[3];
static unsigned char cur_rgb8[3], quad_rgb8[3];
static unsigned cur_rgb8_ok, quad_rgb8_ok;
static unsigned quad_n, batch_n, in_run, additive, failed;
static unsigned quads_in, quads_drawn, last_in, last_drawn;
static unsigned quads_direct, last_direct;
static unsigned char font_lum[MV_FONT_TEXELS];
static int font_w, font_h;

/* ---- direct path ---- */
#define DW RIX_TRI_DESC_WORDS
#define DW_BOX0 1u            /* descriptor layout (rixdesc.c): bounding box, */
#define DW_BOX1 2u
#define DW_COLOR 12u          /* colour r,g,b,a at the box origin, then per x, then per y, */
#define DW_TEX 30u            /* texture s/w,t/w,1/w at the box origin, then per x, then per y */
#define MV_CELLS_MAX ((unsigned)(MX_MAX_COLS) * (unsigned)(MX_MAX_ROWS))
#define MV_GLYPH_COLS 10u
#define MV_GLYPH_ROWS 6u

static zzgl_direct dctx;
static int direct_state;                 /* 0 = tables not built, 1 = in use, -1 = not for this grid */
static unsigned frame_direct;            /* this frame: 0 = not asked yet, 1 = may send descriptors itself, 2 = may not */
static int grid_cols, grid_rows;
static float z_ref;
static u32 tmpl[2][DW];                  /* the two triangles of the reference cell */
static u32 col_tab[2][256][3];           /* one colour channel: value, per x, per y */
static u32 s_tab[2][MV_GLYPH_COLS][3];   /* s/w of a glyph column: value, per x, per y */
static u32 t_tab[2][MV_GLYPH_ROWS][3];   /* t/w of a glyph row */
static float glyph_s0[MV_GLYPH_COLS], glyph_s1[MV_GLYPH_COLS], glyph_t0[MV_GLYPH_ROWS], glyph_t1[MV_GLYPH_ROWS];
static u32 cell_box[MV_CELLS_MAX][2][2]; /* per cell and triangle: the two bounding-box words */
static unsigned char cell_fast[MV_CELLS_MAX];
static unsigned cells_fast, cells_total;

/* one triangle as ZZGL's add_vertex() + draw_triangle() compute it, for the state
   zzgl_direct_begin() accepts; 0 when it is clipped or has no area */
static int general_tri(const mv_vertex *a, const mv_vertex *b, const mv_vertex *c, const float *rgb, u32 *d)
{
    rix_clipvert in[3];
    rix_vertex pv[3], sc[RIX_CLIP_MAX];
    const mv_vertex *src[3];
    const float *m = dctx.mvp;
    float mu, mv, fu, fv;
    u32 k;
    src[0] = a; src[1] = b; src[2] = c;
    for (k = 0; k < 3u; k++) {
        float x = src[k]->x, y = src[k]->y, z = src[k]->z;
        in[k].x = m[0] * x + m[4] * y + m[8] * z + m[12];
        in[k].y = m[1] * x + m[5] * y + m[9] * z + m[13];
        in[k].z = m[2] * x + m[6] * y + m[10] * z + m[14];
        in[k].w = m[3] * x + m[7] * y + m[11] * z + m[15];
        in[k].u = src[k]->s; in[k].v = src[k]->t;
        in[k].r = rgb[0]; in[k].g = rgb[1]; in[k].b = rgb[2]; in[k].a = 1.0f;
        if (in[k].r > 1.0f) in[k].r = 1.0f;
        if (in[k].g > 1.0f) in[k].g = 1.0f;
        if (in[k].b > 1.0f) in[k].b = 1.0f;
        if (in[k].r < 0.0f) in[k].r = 0.0f;
        if (in[k].g < 0.0f) in[k].g = 0.0f;
        if (in[k].b < 0.0f) in[k].b = 0.0f;
    }
    mu = in[0].u; mv = in[0].v;
    if (in[1].u < mu) mu = in[1].u;
    if (in[2].u < mu) mu = in[2].u;
    if (in[1].v < mv) mv = in[1].v;
    if (in[2].v < mv) mv = in[2].v;
    fu = (float)(int)mu;
    if (fu > mu) fu -= 1.0f;
    fv = (float)(int)mv;
    if (fv > mv) fv -= 1.0f;
    if (fu != 0.0f || fv != 0.0f) for (k = 0; k < 3u; k++) { in[k].u -= fu; in[k].v -= fv; }
    if (!(in[0].w >= dctx.w_min && in[1].w >= dctx.w_min && in[2].w >= dctx.w_min)) return 0;
    rix_project(in, 3u, dctx.vp, pv);
    for (k = 0; k < 3u; k++) { if (pv[k].z < 0.0f) pv[k].z = 0.0f; if (pv[k].z > 1.0f) pv[k].z = 1.0f; }
    if (rix_clip_screen(pv, 3u, dctx.clip_x0, dctx.clip_y0, dctx.clip_x1, dctx.clip_y1, sc) != 3u) return 0;
    return rix_tri_desc(&sc[0], &sc[1], &sc[2], d) ? 1 : 0;
}

/* the quad of grid cell (col, row) with glyph (k, j), as the scene issues it */
static void cell_quad(int col, int row, unsigned k, unsigned j, mv_vertex *q)
{
    float x0 = (float)(col - grid_cols / 2), y0 = (float)(grid_rows / 2 - row);
    q[0].x = x0;        q[0].y = y0;        q[0].s = glyph_s0[k]; q[0].t = glyph_t0[j];
    q[1].x = x0 + 1.0f; q[1].y = y0;        q[1].s = glyph_s1[k]; q[1].t = glyph_t0[j];
    q[2].x = x0 + 1.0f; q[2].y = y0 - 1.0f; q[2].s = glyph_s1[k]; q[2].t = glyph_t1[j];
    q[3].x = x0;        q[3].y = y0 - 1.0f; q[3].s = glyph_s0[k]; q[3].t = glyph_t1[j];
    q[0].z = q[1].z = q[2].z = q[3].z = z_ref;
}

/* the two descriptors of a quad through the general computation (ZZGL's GL_QUADS split) */
static int general_quad(const mv_vertex *q, const float *rgb, u32 d[2][DW])
{
    return general_tri(&q[0], &q[1], &q[2], rgb, d[0]) && general_tri(&q[0], &q[2], &q[3], rgb, d[1]);
}

static void compose(unsigned tri, const u32 *box, const unsigned char *rgb8, unsigned k, unsigned j, u32 *d)
{
    const u32 *t = tmpl[tri], *c;
    u32 i;
    for (i = 0; i < DW; i++) d[i] = t[i];
    d[DW_BOX0] = box[0]; d[DW_BOX1] = box[1];
    for (i = 0; i < 3u; i++) {
        c = col_tab[tri][rgb8[i]];
        d[DW_COLOR + i] = c[0]; d[DW_COLOR + 4u + i] = c[1]; d[DW_COLOR + 8u + i] = c[2];
    }
    c = s_tab[tri][k]; d[DW_TEX] = c[0]; d[DW_TEX + 3u] = c[1]; d[DW_TEX + 6u] = c[2];
    c = t_tab[tri][j]; d[DW_TEX + 1u] = c[0]; d[DW_TEX + 4u] = c[1]; d[DW_TEX + 7u] = c[2];
}

/* 1 when the two descriptors are equal outside the words listed in skip[0..n) */
static int same_but(const u32 *a, const u32 *b, const u32 *skip, u32 n)
{
    u32 i, k;
    for (i = 0; i < DW; i++) {
        for (k = 0; k < n; k++) if (skip[k] == i) break;
        if (k == n && a[i] != b[i]) return 0;
    }
    return 1;
}

static void byte_rgb(unsigned r, unsigned g, unsigned b, unsigned char *rgb8, float *rgb)
{
    rgb8[0] = (unsigned char)r; rgb8[1] = (unsigned char)g; rgb8[2] = (unsigned char)b;
    rgb[0] = (float)(int)r * (1.0f / 255.0f); rgb[1] = (float)(int)g * (1.0f / 255.0f); rgb[2] = (float)(int)b * (1.0f / 255.0f);
}

/* the depth of the grid and the texture coordinates of the glyphs, as the scene computes them */
static void grid_tables(float z)
{
    unsigned k, j;
    z_ref = z;
    for (k = 0; k < MV_GLYPH_COLS; k++) { float tx = (float)(int)k / 10; glyph_s0[k] = tx; glyph_s1[k] = tx + 0.1; }
    for (j = 0; j < MV_GLYPH_ROWS; j++) { float ty = (float)(int)j / 6; glyph_t0[j] = ty; glyph_t1[j] = ty + 0.166; }
}

/* Fills the tables from the reference cell, then lets a cell use them only if, for a glyph and
   a colour that change from cell to cell, the composed descriptors equal the general ones. */
static void direct_prepare(float z)
{
    static const u32 skip_col[9] = { 12u, 13u, 14u, 16u, 17u, 18u, 20u, 21u, 22u };
    static const u32 skip_s[3] = { 30u, 33u, 36u }, skip_t[3] = { 31u, 34u, 37u };
    mv_vertex q[4];
    u32 d[2][DW], e[DW];
    unsigned char rgb8[3];
    float rgb[3];
    unsigned k, j, v, tri;
    int col, row;
    direct_state = -1; cells_fast = 0u; cells_total = 0u;
    if (grid_cols < 2 || grid_rows < 2 || grid_cols > MX_MAX_COLS || grid_rows > MX_MAX_ROWS) return;
    grid_tables(z);
    byte_rgb(255u, 255u, 255u, rgb8, rgb);
    cell_quad(grid_cols / 2, grid_rows / 2, 0u, 0u, q);
    if (!general_quad(q, rgb, tmpl)) return;
    for (v = 0; v < 256u; v++) {
        byte_rgb(v, v, v, rgb8, rgb);
        if (!general_quad(q, rgb, d)) return;
        for (tri = 0; tri < 2u; tri++) {
            const u32 *x = d[tri];
            if (!same_but(x, tmpl[tri], skip_col, 9u)) return;
            if (x[12] != x[13] || x[12] != x[14] || x[16] != x[17] || x[16] != x[18] || x[20] != x[21] || x[20] != x[22]) return;
            col_tab[tri][v][0] = x[12]; col_tab[tri][v][1] = x[16]; col_tab[tri][v][2] = x[20];
        }
    }
    byte_rgb(255u, 255u, 255u, rgb8, rgb);
    for (k = 0; k < MV_GLYPH_COLS; k++) {
        cell_quad(grid_cols / 2, grid_rows / 2, k, 0u, q);
        if (!general_quad(q, rgb, d)) return;
        for (tri = 0; tri < 2u; tri++) {
            if (!same_but(d[tri], tmpl[tri], skip_s, 3u)) return;
            s_tab[tri][k][0] = d[tri][30]; s_tab[tri][k][1] = d[tri][33]; s_tab[tri][k][2] = d[tri][36];
        }
    }
    for (j = 0; j < MV_GLYPH_ROWS; j++) {
        cell_quad(grid_cols / 2, grid_rows / 2, 0u, j, q);
        if (!general_quad(q, rgb, d)) return;
        for (tri = 0; tri < 2u; tri++) {
            if (!same_but(d[tri], tmpl[tri], skip_t, 3u)) return;
            t_tab[tri][j][0] = d[tri][31]; t_tab[tri][j][1] = d[tri][34]; t_tab[tri][j][2] = d[tri][37];
        }
    }
    for (row = 0; row < grid_rows; row++) for (col = 0; col < grid_cols; col++) {
        unsigned cell = (unsigned)row * (unsigned)grid_cols + (unsigned)col, ok = 0u;
        k = (unsigned)(col * 7 + row * 3) % MV_GLYPH_COLS; j = (unsigned)(col + row * 5) % MV_GLYPH_ROWS;
        byte_rgb((cell & 1u) ? 128u : 0u, (cell * 37u + 11u) & 255u, (cell & 2u) ? 128u : 0u, rgb8, rgb);
        cell_quad(col, row, k, j, q);
        cells_total++;
        if (general_quad(q, rgb, d)) {
            ok = 1u;
            for (tri = 0; tri < 2u; tri++) {
                u32 i;
                compose(tri, &d[tri][DW_BOX0], rgb8, k, j, e);
                for (i = 0; i < DW; i++) if (e[i] != d[tri][i]) ok = 0u;
                cell_box[cell][tri][0] = d[tri][DW_BOX0]; cell_box[cell][tri][1] = d[tri][DW_BOX1];
            }
        }
        cell_fast[cell] = (unsigned char)ok;
        cells_fast += ok;
    }
    /* a grid where few cells qualify would only break ZZGL's batches: leave it to ZZGL whole.
       On a whole-pixel grid every cell qualifies except the outer ring, which the screen clip cuts. */
    if (cells_fast * 2u >= cells_total) direct_state = 1;
}

/* 1 when the quad just completed is exactly a grid cell (the tables of grid_tables() are set):
   its column, row and glyph */
static int exact_cell(int *col_out, int *row_out, unsigned *k_out, unsigned *j_out)
{
    const mv_vertex *q = quad;
    float x0 = q[0].x, y0 = q[0].y, z = q[0].z, sk, tj;
    int ix = (int)x0, iy = (int)y0, col, row, k, j;
    if ((float)ix != x0 || (float)iy != y0 || z != z_ref) return 0;
    col = ix + grid_cols / 2; row = grid_rows / 2 - iy;
    if (col < 0 || col >= grid_cols || row < 0 || row >= grid_rows) return 0;
    if (q[1].x != x0 + 1.0f || q[1].y != y0 || q[2].x != x0 + 1.0f || q[2].y != y0 - 1.0f ||
        q[3].x != x0 || q[3].y != y0 - 1.0f || q[1].z != z || q[2].z != z || q[3].z != z) return 0;
    sk = q[0].s * 10.0f + 0.5f; tj = q[0].t * 6.0f + 0.5f;
    if (!(sk >= 0.0f && sk < (float)MV_GLYPH_COLS && tj >= 0.0f && tj < (float)MV_GLYPH_ROWS)) return 0;
    k = (int)sk; j = (int)tj;
    if (q[0].s != glyph_s0[k] || q[1].s != glyph_s1[k] || q[2].s != glyph_s1[k] || q[3].s != glyph_s0[k] ||
        q[0].t != glyph_t0[j] || q[1].t != glyph_t0[j] || q[2].t != glyph_t1[j] || q[3].t != glyph_t1[j]) return 0;
    *col_out = col; *row_out = row; *k_out = (unsigned)k; *j_out = (unsigned)j;
    return 1;
}

/* the grid cell and glyph of the quad just completed, for the direct path; -1 when it is not
   exactly a cell or when that cell did not pass its check */
static int direct_cell(unsigned *k_out, unsigned *j_out)
{
    int col, row;
    if (direct_state == 0) {
        float x0 = quad[0].x, y0 = quad[0].y;
        if ((float)(int)x0 != x0 || (float)(int)y0 != y0) return -1;
        direct_prepare(quad[0].z);
    }
    if (direct_state != 1 || !exact_cell(&col, &row, k_out, j_out)) return -1;
    if (!cell_fast[(unsigned)row * (unsigned)grid_cols + (unsigned)col]) return -1;
    return row * grid_cols + col;
}

/* ---- pixel path ---- */
#if defined(__arm__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#ifdef RIX_HOST_SIM
void sim_check_fb_write(u32 dst);       /* the PC bench checks that the buffer is free for Core1 */
#endif

#define PX_MAX_CELL 32                  /* largest cell side in pixels */
#define PX_GLYPHS (MV_GLYPH_COLS * MV_GLYPH_ROWS)
#define PX_DEFER_MAX 1024u              /* quads left to RasterIX in a frame */
#define PX_FB_ROWS (MX_HEIGHT + 1)      /* the buffers have one more line, RasterIX line 0: the fences */

typedef struct { mv_vertex v[4]; float rgb[3]; } px_quad;

static int px_allowed;                  /* the preset asks for it */
static int px_state;                    /* 0 = not prepared, 1 = usable, -1 = not for this grid */
static unsigned px_frame;               /* this frame is written in pixels */
static int px_cw, px_ch;                /* cell size */
static u16 *px_fb;
static unsigned char px_glyph[PX_GLYPHS][PX_MAX_CELL * PX_MAX_CELL];   /* coverage 0..63 of each glyph at this cell size */
static u16 px_lut_g[64][64], px_lut_r[32][64], px_lut_b[32][64];       /* channel level x coverage -> its RGB565 field */
static u16 px_band[PX_MAX_CELL][MX_WIDTH];                              /* one row of cells, composed in cached memory */
static int px_band_row;
static unsigned char px_row_done[MX_MAX_ROWS];
static px_quad px_defer[PX_DEFER_MAX];
static unsigned px_defer_n, px_defer_lost;
static unsigned quads_px, last_px;

static int px_floor(float v) { int i = (int)v; return ((float)i > v) ? i - 1 : i; }
static int px_ceil(float v) { int i = (int)v; return ((float)i < v) ? i + 1 : i; }

/* where RasterIX puts a point of the grid plane: its screen coordinates (y upwards) */
static int px_project(float x, float y, float *sx, float *sy)
{
    rix_clipvert in;
    rix_vertex out;
    const float *m = dctx.mvp;
    in.x = m[0] * x + m[4] * y + m[8] * z_ref + m[12];
    in.y = m[1] * x + m[5] * y + m[9] * z_ref + m[13];
    in.z = m[2] * x + m[6] * y + m[10] * z_ref + m[14];
    in.w = m[3] * x + m[7] * y + m[11] * z_ref + m[15];
    in.u = 0.0f; in.v = 0.0f; in.r = 0.0f; in.g = 0.0f; in.b = 0.0f; in.a = 0.0f;
    if (!(in.w >= dctx.w_min)) return 0;
    rix_project(&in, 1u, dctx.vp, &out);
    *sx = out.sx; *sy = out.sy;
    return 1;
}

/* the font as GL_LINEAR reads it, texture coordinates wrapping: 0..255 */
static float px_texel(float s, float t)
{
    float x = s * (float)font_w - 0.5f, y = t * (float)font_h - 0.5f, acc = 0.0f;
    int x0 = px_floor(x), y0 = px_floor(y), i;
    float fx = x - (float)x0, fy = y - (float)y0;
    for (i = 0; i < 4; i++) {
        int xx = x0 + (i & 1), yy = y0 + (i >> 1);
        float wgt = ((i & 1) ? fx : 1.0f - fx) * ((i >> 1) ? fy : 1.0f - fy);
        xx = ((xx % font_w) + font_w) % font_w; yy = ((yy % font_h) + font_h) % font_h;
        acc += wgt * (float)font_lum[yy * font_w + xx];
    }
    return acc;
}

/* The pixel path is used only where the grid is what it assumes, checked here against the same
   projection RasterIX gets: cells of a whole number of pixels, column c at x = c * cw, row r on
   memory lines r * ch .. (RasterIX samples a pixel at its integer corner and counts y upwards,
   line 0 being the fence line, memory line 480), the whole screen covered. */
static void px_prepare(float z)
{
    float sx, sy, sx_l, sx_r, sy_t, sy_b;
    int c, r, i, m, cref, rref;
    unsigned k, j, a, v;
    px_state = -1;
    if (!px_allowed || font_w <= 0 || font_h <= 0) return;
    if (grid_cols < 2 || grid_rows < 2 || grid_cols > MX_MAX_COLS || grid_rows > MX_MAX_ROWS) return;
    if (MX_WIDTH % grid_cols || MX_HEIGHT % grid_rows) return;
    px_cw = MX_WIDTH / grid_cols; px_ch = MX_HEIGHT / grid_rows;
    if (px_cw < 2 || px_ch < 2 || px_cw > PX_MAX_CELL || px_ch > PX_MAX_CELL) return;
    grid_tables(z);
    for (c = 0; c <= grid_cols; c++) {
        if (!px_project((float)(c - grid_cols / 2), 0.0f, &sx, &sy)) return;
        if (px_ceil(sx) != c * px_cw) return;
    }
    for (r = 0; r <= grid_rows; r++) {
        if (!px_project(0.0f, (float)(grid_rows / 2 - r), &sx, &sy)) return;
        if (PX_FB_ROWS - px_ceil(sy) != r * px_ch) return;
    }
    /* where the direct path knows the bounding box RasterIX would get, it must start at the same place */
    if (direct_state == 1) for (r = 0; r < grid_rows; r++) for (c = 0; c < grid_cols; c++) {
        unsigned cell = (unsigned)r * (unsigned)grid_cols + (unsigned)c;
        u32 box = cell_box[cell][0][0];
        if (!cell_fast[cell]) continue;
        if ((int)(box & 0xFFFFu) != c * px_cw || (int)(box >> 16) != MX_HEIGHT - (r + 1) * px_ch + 1) return;
    }
    cref = grid_cols / 2; rref = grid_rows / 2;
    if (!px_project((float)(cref - grid_cols / 2), (float)(grid_rows / 2 - rref), &sx_l, &sy_t)) return;
    if (!px_project((float)(cref + 1 - grid_cols / 2), (float)(grid_rows / 2 - rref - 1), &sx_r, &sy_b)) return;
    if (!(sx_r > sx_l) || !(sy_t > sy_b)) return;
    for (j = 0; j < MV_GLYPH_ROWS; j++) for (k = 0; k < MV_GLYPH_COLS; k++) {
        unsigned char *g = px_glyph[j * MV_GLYPH_COLS + k];
        for (m = 0; m < px_ch; m++) for (i = 0; i < px_cw; i++) {
            /* the sample point of this pixel: x at its integer corner, y on RasterIX line 480 - memory line */
            float fx = ((float)(cref * px_cw + i) - sx_l) / (sx_r - sx_l);
            float ft = (sy_t - (float)(MX_HEIGHT - (rref * px_ch + m))) / (sy_t - sy_b);
            float cov = px_texel(glyph_s0[k] + fx * (glyph_s1[k] - glyph_s0[k]), glyph_t0[j] + ft * (glyph_t1[j] - glyph_t0[j]));
            int q = (int)(cov * (63.0f / 255.0f) + 0.5f);
            g[m * px_cw + i] = (unsigned char)(q < 0 ? 0 : q > 63 ? 63 : q);
        }
    }
    for (a = 0; a < 64u; a++) for (v = 0; v < 64u; v++) px_lut_g[a][v] = (u16)(((a * v * 2u + 63u) / 126u) << 5);
    for (a = 0; a < 32u; a++) for (v = 0; v < 64u; v++) {
        u16 q = (u16)((a * v * 2u + 63u) / 126u);
        px_lut_r[a][v] = (u16)(q << 11); px_lut_b[a][v] = q;
    }
    px_state = 1;
}

/* 64 bytes at a time: the frame buffers are mapped non-cacheable, wide stores stream */
static void px_store(void *dst, const void *src, u32 bytes)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    u32 i = 0u;
#if defined(__arm__) && defined(__ARM_NEON)
    for (; i + 64u <= bytes; i += 64u) {
        uint8x16_t a = vld1q_u8(s + i), b = vld1q_u8(s + i + 16u), c = vld1q_u8(s + i + 32u), e = vld1q_u8(s + i + 48u);
        vst1q_u8(d + i, a); vst1q_u8(d + i + 16u, b); vst1q_u8(d + i + 32u, c); vst1q_u8(d + i + 48u, e);
    }
#endif
    for (; i < bytes; i++) d[i] = s[i];
}

static void px_band_clear(void)
{
    int m, i;
    for (m = 0; m < px_ch; m++) for (i = 0; i < MX_WIDTH; i++) px_band[m][i] = 0u;
}

static void px_band_flush(void)
{
    int m;
    if (px_band_row < 0) return;
    for (m = 0; m < px_ch; m++) px_store(px_fb + (unsigned)(px_band_row * px_ch + m) * MX_WIDTH, px_band[m], MX_WIDTH * 2u);
    px_row_done[px_band_row] = 1u;
    px_band_row = -1;
}

static void px_put(int col, int row, unsigned k, unsigned j, const unsigned char *rgb8)
{
    const unsigned char *g = px_glyph[j * MV_GLYPH_COLS + k];
    const u16 *lg = px_lut_g[rgb8[1] >> 2];
    int m, i;
    if (row != px_band_row) { px_band_flush(); px_band_clear(); px_band_row = row; }
    if (!(rgb8[0] | rgb8[1] | rgb8[2])) return;
    if (!(rgb8[0] | rgb8[2])) {
        for (m = 0; m < px_ch; m++) {
            u16 *d = px_band[m] + col * px_cw;
            for (i = 0; i < px_cw; i++) d[i] = lg[g[m * px_cw + i]];
        }
    } else {
        const u16 *lr = px_lut_r[rgb8[0] >> 3], *lb = px_lut_b[rgb8[2] >> 3];
        for (m = 0; m < px_ch; m++) {
            u16 *d = px_band[m] + col * px_cw;
            for (i = 0; i < px_cw; i++) { unsigned v = g[m * px_cw + i]; d[i] = (u16)(lg[v] | lr[v] | lb[v]); }
        }
    }
}

/* end of a pixel frame: the rows the scene did not issue are written black, then RasterIX gets
   what was kept for it, over pixels that are all in memory by then */
static void px_finish(void)
{
    unsigned n, i;
    int r;
    px_band_flush();
    for (r = 0; r < grid_rows; r++) if (!px_row_done[r]) { px_band_clear(); px_band_row = r; px_band_flush(); }
#if defined(__arm__)
    __asm__ volatile("dsb" ::: "memory");
#endif
    for (n = 0; n < px_defer_n; n++) {
        const px_quad *q = &px_defer[n];
        if (batch_n == 0u) zzglBegin(ZGL_QUADS);
        zzglColor4f(q->rgb[0], q->rgb[1], q->rgb[2], 1.0f);
        for (i = 0u; i < 4u; i++) {
            zzglTexCoord2f(q->v[i].s, q->v[i].t);
            zzglVertex3f(q->v[i].x, q->v[i].y, q->v[i].z);
        }
        if (++batch_n == MV_BATCH_QUADS) { zzglEnd(); batch_n = 0u; }
    }
    if (batch_n) { zzglEnd(); batch_n = 0u; }
    px_defer_n = 0u;
}

void mv_gl_grid(int cols, int rows, int pixels)
{
    grid_cols = cols; grid_rows = rows; direct_state = 0;
    px_allowed = pixels ? 1 : 0; px_state = 0;
}
unsigned mv_gl_quads_direct(void) { return last_direct; }
unsigned mv_gl_cells_direct(void) { return direct_state == 1 ? cells_fast : 0u; }
unsigned mv_gl_quads_pixels(void) { return last_px; }
unsigned mv_gl_quads_lost(void) { return px_defer_lost; }
int mv_gl_pixels_on(void) { return px_state == 1; }

void mv_gl_reset(void)
{
    quad_n = 0u; batch_n = 0u; in_run = 0u; additive = 0u; failed = 0u;
    quads_in = 0u; quads_drawn = 0u; last_in = 0u; last_drawn = 0u;
    quads_direct = 0u; last_direct = 0u; frame_direct = 0u; direct_state = 0;
    px_state = 0; px_frame = 0u; px_band_row = -1; px_defer_n = 0u; px_defer_lost = 0u; quads_px = 0u; last_px = 0u;
    cur_s = 0.0f; cur_t = 0.0f;
    cur_rgb[0] = 1.0f; cur_rgb[1] = 1.0f; cur_rgb[2] = 1.0f;
    cur_rgb8[0] = 255u; cur_rgb8[1] = 255u; cur_rgb8[2] = 255u; cur_rgb8_ok = 1u;
    /* MatrixView relies on OpenGL's initial state for what it never sets */
    zzglDisable(ZGL_DEPTH_TEST);
    zzglDisable(ZGL_CULL_FACE);
    zzglDisable(ZGL_ALPHA_TEST);
}

int mv_gl_failed(void) { return failed ? 1 : 0; }
unsigned mv_gl_quads_in(void) { return last_in; }
unsigned mv_gl_quads_drawn(void) { return last_drawn; }

void mpglBegin(unsigned mode)
{
    if (mode != GL_QUADS) failed = 1u;
    in_run = 1u; quad_n = 0u; batch_n = 0u;
    quads_in = 0u; quads_drawn = 0u; quads_direct = 0u; quads_px = 0u;
    frame_direct = 0u;
}

void mpglEnd(void)
{
    if (px_frame) px_finish();
    if (batch_n) { zzglEnd(); batch_n = 0u; }
    if (quad_n) failed = 1u;
    in_run = 0u; quad_n = 0u; frame_direct = 0u; px_frame = 0u;
    last_in = quads_in; last_drawn = quads_drawn; last_direct = quads_direct; last_px = quads_px;
}

void mpglColor4ub(int r, int g, int b, int a)
{
    (void)a;
    cur_rgb8_ok = (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) ? 1u : 0u;
    cur_rgb8[0] = (unsigned char)r; cur_rgb8[1] = (unsigned char)g; cur_rgb8[2] = (unsigned char)b;
    cur_rgb[0] = (float)r * (1.0f / 255.0f);
    cur_rgb[1] = (float)g * (1.0f / 255.0f);
    cur_rgb[2] = (float)b * (1.0f / 255.0f);
}

void mpglTexCoord2f(float s, float t) { cur_s = s; cur_t = t; }

void mpglVertex3f(float x, float y, float z)
{
    unsigned i;
    mv_vertex *v;
    if (!in_run) { failed = 1u; return; }
    if (quad_n == 0u) {
        quad_rgb[0] = cur_rgb[0]; quad_rgb[1] = cur_rgb[1]; quad_rgb[2] = cur_rgb[2];
        quad_rgb8[0] = cur_rgb8[0]; quad_rgb8[1] = cur_rgb8[1]; quad_rgb8[2] = cur_rgb8[2]; quad_rgb8_ok = cur_rgb8_ok;
    }
    v = &quad[quad_n++];
    v->x = x; v->y = y; v->z = z; v->s = cur_s; v->t = cur_t;
    if (quad_n < 4u) return;
    quad_n = 0u;
    quads_in++;
    if (px_frame) {
        unsigned k = 0u, j = 0u;
        int col = 0, row = 0;
        int black = (quad_rgb[0] <= 0.0f && quad_rgb[1] <= 0.0f && quad_rgb[2] <= 0.0f);
        if (quad_rgb8_ok && exact_cell(&col, &row, &k, &j) && !px_row_done[row]) {
            px_put(col, row, k, j, quad_rgb8);
            if (!black) { quads_drawn++; quads_px++; }
            return;
        }
        if (black) return;
        if (px_defer_n < PX_DEFER_MAX) {
            px_quad *q = &px_defer[px_defer_n++];
            for (i = 0u; i < 4u; i++) q->v[i] = quad[i];
            q->rgb[0] = quad_rgb[0]; q->rgb[1] = quad_rgb[1]; q->rgb[2] = quad_rgb[2];
            quads_drawn++;
        } else px_defer_lost++;
        return;
    }
    if (additive && quad_rgb[0] <= 0.0f && quad_rgb[1] <= 0.0f && quad_rgb[2] <= 0.0f) return;
#ifndef MV_NO_DIRECT
    /* asked at the first quad that is drawn, where ZZGL's own Begin would settle the state:
       a frame with nothing to draw sends nothing more than before */
    if (frame_direct == 0u) frame_direct = (additive && (direct_state >= 0 || px_state == 0) && zzgl_direct_begin(&dctx)) ? 1u : 2u;
#endif
#ifndef MV_NO_PIXELS
    /* prepared during a frame that still goes to RasterIX whole; the pixel frames start with the next clear */
    if (frame_direct == 1u && px_state == 0) px_prepare(quad[0].z);
#endif
    if (frame_direct == 1u && quad_rgb8_ok && direct_state >= 0) {
        unsigned k = 0u, j = 0u;
        int cell = direct_cell(&k, &j);
        if (cell >= 0) {
            u32 d[DW];
            unsigned tri;
            if (batch_n) { zzglEnd(); batch_n = 0u; }      /* the scene's order is kept */
            for (tri = 0; tri < 2u; tri++) {
                compose(tri, cell_box[cell][tri], quad_rgb8, k, j, d);
                if (zzgl_direct_tri(d)) failed = 1u;
            }
            quads_drawn++; quads_direct++;
            return;
        }
    }
    if (batch_n == 0u) zzglBegin(ZGL_QUADS);
    zzglColor4f(quad_rgb[0], quad_rgb[1], quad_rgb[2], 1.0f);
    for (i = 0u; i < 4u; i++) {
        zzglTexCoord2f(quad[i].s, quad[i].t);
        zzglVertex3f(quad[i].x, quad[i].y, quad[i].z);
    }
    quads_drawn++;
    if (++batch_n == MV_BATCH_QUADS) { zzglEnd(); batch_n = 0u; }
}

void mpglTexImage2D(unsigned target, int level, int internalformat, int width, int height,
                    int border, unsigned format, unsigned type, const void *pixels)
{
    const unsigned char *src = (const unsigned char *)pixels;
    unsigned i, n;
    (void)internalformat; (void)border;
    if (target != GL_TEXTURE_2D || level != 0 || format != GL_LUMINANCE || type != GL_UNSIGNED_BYTE ||
        !src || width <= 0 || height <= 0 || (unsigned)width * (unsigned)height > MV_FONT_TEXELS) {
        failed = 1u;
        return;
    }
    n = (unsigned)width * (unsigned)height;
    for (i = 0u; i < n; i++) {
        font_rgb[3u * i] = src[i]; font_rgb[3u * i + 1u] = src[i]; font_rgb[3u * i + 2u] = src[i];
        font_lum[i] = src[i];
    }
    font_w = width; font_h = height; px_state = 0;
    zzglBindTexture(ZGL_TEXTURE_2D, MV_TEXTURE_ID);
    zzglTexParameterf(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, (float)ZGL_LINEAR);
    zzglTexParameterf(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, (float)ZGL_LINEAR);
    zzglTexImage2D(ZGL_TEXTURE_2D, 0, 3, width, height, 0, ZGL_RGB, ZGL_UNSIGNED_BYTE, font_rgb);
}

void mpglTexParameterf(unsigned target, unsigned pname, float param)
{
    (void)target;
    if (pname == GL_TEXTURE_MAG_FILTER) zzglTexParameterf(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, param);
    else if (pname == GL_TEXTURE_MIN_FILTER) zzglTexParameterf(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, param);
    else failed = 1u;
}

void mpglTexEnvi(unsigned target, unsigned pname, int param)
{
    if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE || param != GL_MODULATE) { failed = 1u; return; }
    zzglTexEnvf(ZGL_TEXTURE_ENV, ZGL_TEXTURE_ENV_MODE, (float)ZGL_MODULATE);
}

void mpglClearColor(float r, float g, float b, float a) { zzglClearColor(r, g, b, a); }

void mpglClear(unsigned mask)
{
    if (mask != GL_COLOR_BUFFER_BIT) { failed = 1u; return; }
    px_frame = 0u;
    if (px_state == 1 && additive && !in_run) {
        /* a pixel frame: every line is written by px_put() / px_finish(), RasterIX is given nothing
           before that (no clear, and ZZGL is not called until the pixels are in memory) */
        u32 target = zzgl_target_color();
        int r;
#ifdef RIX_HOST_SIM
        sim_check_fb_write(target);
#endif
        px_fb = (u16 *)rix_ptr(target);
        px_band_row = -1; px_defer_n = 0u;
        for (r = 0; r < grid_rows; r++) px_row_done[r] = 0u;
        px_frame = 1u;
        return;
    }
    zzglClear(ZGL_COLOR_BUFFER_BIT);
}

void mpglEnable(unsigned cap)
{
    if (cap == GL_BLEND) zzglEnable(ZGL_BLEND);
    else if (cap == GL_TEXTURE_2D) zzglEnable(ZGL_TEXTURE_2D);
    else failed = 1u;
}

void mpglBlendFunc(unsigned sfactor, unsigned dfactor)
{
    additive = (sfactor == GL_ONE && dfactor == GL_ONE) ? 1u : 0u;
    if (!additive) failed = 1u;
    zzglBlendFunc(ZGL_ONE, ZGL_ONE);
}

void mpglViewport(int x, int y, int width, int height) { zzglViewport(x, y, width, height); direct_state = 0; px_state = 0; }

void mpglLoadMatrixf(const float *m)
{
    zzglMatrixMode(ZGL_PROJECTION);
    zzglLoadMatrixf(m);
    zzglMatrixMode(ZGL_MODELVIEW);
    zzglLoadIdentity();
    direct_state = 0; px_state = 0;
}
