/* rixdesc.c - librixgpu 1.2.3 - triangle descriptor builder (see rixdesc.h)
 * Math identical to cube_make_desc() of ZZCube B1 (A07b reference); the
 * depth attribute is interpolated like the others.
 * SPDX-License-Identifier: 0BSD */
#include "rixdesc.h"

#define W 640
#define H 481
#define EDGE_SHIFT 5
#define EDGE_HALF (1 << 4)
#define EDGE_ONE  (1 << 5)

static u32 fbits(float f) { union { float f; u32 u; } v; v.f = f; return v.u; }
static s32 edge_func(const s32 a[2], const s32 b[2], const s32 c[2])
{
    return (s32)(((int64_t)(c[0] - a[0]) * (b[1] - a[1])) - ((int64_t)(c[1] - a[1]) * (b[0] - a[0])));
}
static s32 f_to_edge(float v) { return (s32)(v * (float)EDGE_ONE + 0.5f); }
static void interp4(float *o, const float *a, const float *b, const float *c, const float *w)
{ int i; for (i = 0; i < 4; i++) o[i] = a[i] * w[0] + b[i] * w[1] + c[i] * w[2]; }
static void interp3(float *o, const float *a, const float *b, const float *c, const float *w)
{ int i; for (i = 0; i < 3; i++) o[i] = a[i] * w[0] + b[i] * w[1] + c[i] * w[2]; }
static void interp2(float *o, const float *a, const float *b, const float *c, const float *w)
{ int i; for (i = 0; i < 2; i++) o[i] = a[i] * w[0] + b[i] * w[1] + c[i] * w[2]; }

s32 rix_tri_area(const rix_vertex *a, const rix_vertex *b, const rix_vertex *c)
{
    s32 v0[2], v1[2], v2[2];
    v0[0] = f_to_edge(a->sx); v0[1] = f_to_edge(a->sy);
    v1[0] = f_to_edge(b->sx); v1[1] = f_to_edge(b->sy);
    v2[0] = f_to_edge(c->sx); v2[1] = f_to_edge(c->sy);
    return edge_func(v0, v1, v2);
}

int rix_fill_rule = 0;   /* 1.2.2: 1 = top-left fill rule (see below); 1.2.3: OFF by default - with it on, the A4000
                            showed garbled 2D text, jittering models and flickering water (ZZQuake2RIX 0.4, 14/10):
                            the G4C core's edge walker does not take the biased start values as the model does */
int rix_tex_wnorm = 0;   /* 1.2.3: 1 = the texture planes use 1/w scaled by 1/max(1/w) of the triangle (the RasterIX
                            reference driver normalises w "to avoid that w gets too small"); s/q, t/q unchanged */

int rix_tri_desc(const rix_vertex *a, const rix_vertex *b, const rix_vertex *c, u32 *out)
{
    s32 v0[2], v1[2], v2[2], p[2], area, sign, bsx, bsy, bex, bey, wi[3], wx[3], wy[3];
    float ai, wn[3], wxn[3], wyn[3], q4[4], q3[3], q2[2];
    float c0[4], c1[4], c2[4], t0[3], t1[3], t2[3], d0[2], d1[2], d2[2];
    int i, k = 0;
    v0[0] = f_to_edge(a->sx); v0[1] = f_to_edge(a->sy);
    v1[0] = f_to_edge(b->sx); v1[1] = f_to_edge(b->sy);
    v2[0] = f_to_edge(c->sx); v2[1] = f_to_edge(c->sy);
    area = edge_func(v0, v1, v2); sign = (area <= 0) ? -1 : 1; area *= sign;
    if (area <= 0) return 0;
    bsx = v0[0]; if (v1[0] < bsx) bsx = v1[0]; if (v2[0] < bsx) bsx = v2[0];
    bsy = v0[1]; if (v1[1] < bsy) bsy = v1[1]; if (v2[1] < bsy) bsy = v2[1];
    bex = v0[0]; if (v1[0] > bex) bex = v1[0]; if (v2[0] > bex) bex = v2[0];
    bey = v0[1]; if (v1[1] > bey) bey = v1[1]; if (v2[1] > bey) bey = v2[1];
    bsx = (bsx + EDGE_HALF) >> EDGE_SHIFT; bsy = (bsy + EDGE_HALF) >> EDGE_SHIFT;
    bex = (bex + EDGE_ONE + EDGE_HALF) >> EDGE_SHIFT; bey = (bey + EDGE_ONE + EDGE_HALF) >> EDGE_SHIFT;
    if (bsx < 0 || bsy < 0 || bex <= bsx || bey <= bsy || bex > W + 1 || bey > H + 1) return 0;
    p[0] = bsx << EDGE_SHIFT; p[1] = bsy << EDGE_SHIFT;
    wi[0] = edge_func(v1, v2, p) * sign; wi[1] = edge_func(v2, v0, p) * sign; wi[2] = edge_func(v0, v1, p) * sign;
    wx[0] = (v2[1] - v1[1]) * EDGE_ONE * sign; wx[1] = (v0[1] - v2[1]) * EDGE_ONE * sign; wx[2] = (v1[1] - v0[1]) * EDGE_ONE * sign;
    wy[0] = (v1[0] - v2[0]) * EDGE_ONE * sign; wy[1] = (v2[0] - v0[0]) * EDGE_ONE * sign; wy[2] = (v0[0] - v1[0]) * EDGE_ONE * sign;
    ai = 1.0f / (float)area;
    for (i = 0; i < 3; i++) { wn[i] = (float)wi[i] * ai; wxn[i] = (float)wx[i] * ai; wyn[i] = (float)wy[i] * ai; }
    c0[0] = a->r; c0[1] = a->g; c0[2] = a->b; c0[3] = a->a;
    c1[0] = b->r; c1[1] = b->g; c1[2] = b->b; c1[3] = b->a;
    c2[0] = c->r; c2[1] = c->g; c2[2] = c->b; c2[3] = c->a;
    {
        float qa = a->invw, qb = b->invw, qc = c->invw;
        if (rix_tex_wnorm) {
            float m = qa; if (qb > m) m = qb; if (qc > m) m = qc;
            if (m > 0.0f) { float r = 1.0f / m; qa *= r; qb *= r; qc *= r; }
        }
        t0[0] = a->u * qa; t0[1] = a->v * qa; t0[2] = qa;
        t1[0] = b->u * qb; t1[1] = b->v * qb; t1[2] = qb;
        t2[0] = c->u * qc; t2[1] = c->v * qc; t2[2] = qc;
    }
    d0[0] = a->z; d0[1] = a->invw; d1[0] = b->z; d1[1] = b->invw; d2[0] = c->z; d2[1] = c->invw;
    out[k++] = 0u;
    out[k++] = ((u32)(u16)bsy << 16) | (u32)(u16)bsx;
    out[k++] = ((u32)(u16)bey << 16) | (u32)(u16)bex;
    /* fill rule (1.2.2): RasterIX draws a pixel when the three edge values are >= 0, so a pixel
       whose sample point lies exactly on an edge shared by two triangles was drawn by both -
       blended twice (ZZQuake2RIX 0.3: R_PolyBlend's full-screen quad showed its diagonal as a
       red or green line, 480 pixels).  An edge keeps such pixels only if its value grows along
       +x, or along +y when it is parallel to x; the neighbour sees the same edge with the
       opposite gradients and drops them.  The edge values are integers, so -1 removes exactly
       the zero ones.  The interpolants above keep the unbiased values. */
    for (i = 0; i < 3; i++) out[k++] = (u32)((!rix_fill_rule || wx[i] > 0 || (wx[i] == 0 && wy[i] > 0)) ? wi[i] : wi[i] - 1);
    for (i = 0; i < 3; i++) out[k++] = (u32)wx[i];
    for (i = 0; i < 3; i++) out[k++] = (u32)wy[i];
    interp4(q4, c0, c1, c2, wn);  for (i = 0; i < 4; i++) out[k++] = fbits(q4[i]);
    interp4(q4, c0, c1, c2, wxn); for (i = 0; i < 4; i++) out[k++] = fbits(q4[i]);
    interp4(q4, c0, c1, c2, wyn); for (i = 0; i < 4; i++) out[k++] = fbits(q4[i]);
    interp2(q2, d0, d1, d2, wn);  for (i = 0; i < 2; i++) out[k++] = fbits(q2[i]);
    interp2(q2, d0, d1, d2, wxn); for (i = 0; i < 2; i++) out[k++] = fbits(q2[i]);
    interp2(q2, d0, d1, d2, wyn); for (i = 0; i < 2; i++) out[k++] = fbits(q2[i]);
    interp3(q3, t0, t1, t2, wn);  for (i = 0; i < 3; i++) out[k++] = fbits(q3[i]);
    interp3(q3, t0, t1, t2, wxn); for (i = 0; i < 3; i++) out[k++] = fbits(q3[i]);
    interp3(q3, t0, t1, t2, wyn); for (i = 0; i < 3; i++) out[k++] = fbits(q3[i]);
    return k == (int)RIX_TRI_DESC_WORDS;
}
