/* rixclip.c - librixgpu 1.1 - triangle clipping (see rixclip.h)
 * SPDX-License-Identifier: 0BSD */
#include "rixclip.h"

static void cv_lerp(rix_clipvert *o, const rix_clipvert *a, const rix_clipvert *b, float t)
{
    o->x = a->x + (b->x - a->x) * t; o->y = a->y + (b->y - a->y) * t;
    o->z = a->z + (b->z - a->z) * t; o->w = a->w + (b->w - a->w) * t;
    o->u = a->u + (b->u - a->u) * t; o->v = a->v + (b->v - a->v) * t;
    o->r = a->r + (b->r - a->r) * t; o->g = a->g + (b->g - a->g) * t;
    o->b = a->b + (b->b - a->b) * t; o->a = a->a + (b->a - a->a) * t;
}

u32 rix_clip_near(const rix_clipvert *in, u32 n, float w_min, rix_clipvert *out)
{
    u32 i, m = 0u;
    if (n == 0u) return 0u;
    for (i = 0; i < n; i++) {
        const rix_clipvert *a = &in[i], *b = &in[(i + 1u) % n];
        float da = a->w - w_min, db = b->w - w_min;
        if (da >= 0.0f) { if (m < RIX_CLIP_MAX) out[m++] = *a; }
        if ((da >= 0.0f) != (db >= 0.0f)) {
            /* canonical order: always from the inside vertex towards the outside one, so that the
               two polygons sharing this edge (which walk it in opposite directions) compute the
               bit-identical intersection: near the near plane a rounding difference of 1e-5 in
               world space is a 1 px crack on screen (found by the simulator's hole check) */
            const rix_clipvert *in = (da >= 0.0f) ? a : b, *outv = (da >= 0.0f) ? b : a;
            float din = (da >= 0.0f) ? da : db, dout = (da >= 0.0f) ? db : da;
            float t = din / (din - dout);
            if (m < RIX_CLIP_MAX) { cv_lerp(&out[m], in, outv, t); out[m].w = w_min; m++; }
        }
    }
    return m;
}

void rix_project(const rix_clipvert *in, u32 n, const rix_viewport *vp, rix_vertex *out)
{
    u32 i;
    for (i = 0; i < n; i++) {
        float iw = 1.0f / in[i].w;
        out[i].sx = in[i].x * iw * vp->scale_x + vp->trans_x;
        out[i].sy = in[i].y * iw * vp->scale_y + vp->trans_y;
        out[i].z = in[i].z * iw * vp->scale_z + vp->trans_z;
        out[i].invw = iw;
        out[i].u = in[i].u; out[i].v = in[i].v;
        out[i].r = in[i].r; out[i].g = in[i].g; out[i].b = in[i].b; out[i].a = in[i].a;
    }
}

/* screen-space lerp: u, v travel as u/w, v/w with 1/w */
static void sv_lerp(rix_vertex *o, const rix_vertex *a, const rix_vertex *b, float t)
{
    float uwa = a->u * a->invw, uwb = b->u * b->invw, vwa = a->v * a->invw, vwb = b->v * b->invw;
    float iw = a->invw + (b->invw - a->invw) * t;
    float uw = uwa + (uwb - uwa) * t, vw = vwa + (vwb - vwa) * t;
    o->sx = a->sx + (b->sx - a->sx) * t; o->sy = a->sy + (b->sy - a->sy) * t;
    o->z = a->z + (b->z - a->z) * t;
    o->invw = iw;
    o->u = (iw > 1.0e-30f) ? uw / iw : a->u; o->v = (iw > 1.0e-30f) ? vw / iw : a->v;
    o->r = a->r + (b->r - a->r) * t; o->g = a->g + (b->g - a->g) * t;
    o->b = a->b + (b->b - a->b) * t; o->a = a->a + (b->a - a->a) * t;
}

/* one edge: inside when sign * (coord - bound) >= 0 */
static u32 clip_edge(const rix_vertex *in, u32 n, int axis, float bound, float sign, rix_vertex *out)
{
    u32 i, m = 0u;
    for (i = 0; i < n; i++) {
        const rix_vertex *a = &in[i], *b = &in[(i + 1u) % n];
        float ca = axis ? a->sy : a->sx, cb = axis ? b->sy : b->sx;
        float da = sign * (ca - bound), db = sign * (cb - bound);
        if (da >= 0.0f) { if (m < RIX_CLIP_MAX) out[m++] = *a; }
        if ((da >= 0.0f) != (db >= 0.0f)) {
            const rix_vertex *in = (da >= 0.0f) ? a : b, *outv = (da >= 0.0f) ? b : a;   /* canonical order, as in rix_clip_near */
            float din = (da >= 0.0f) ? da : db, dout = (da >= 0.0f) ? db : da;
            float t = din / (din - dout);
            if (m < RIX_CLIP_MAX) { sv_lerp(&out[m], in, outv, t); if (axis) out[m].sy = bound; else out[m].sx = bound; m++; }
        }
    }
    return m;
}

u32 rix_clip_screen(const rix_vertex *in, u32 n, float x0, float y0, float x1, float y1, rix_vertex *out)
{
    rix_vertex tmp[RIX_CLIP_MAX];
    u32 i, m = n;
    int inside = 1;
    if (n == 0u) return 0u;
    for (i = 0; i < n; i++)
        if (in[i].sx < x0 || in[i].sx > x1 || in[i].sy < y0 || in[i].sy > y1) { inside = 0; break; }
    if (inside) { for (i = 0; i < n; i++) out[i] = in[i]; return n; }   /* common case: nothing to do */
    m = clip_edge(in, m, 0, x0, 1.0f, tmp);
    m = clip_edge(tmp, m, 0, x1, -1.0f, out);
    m = clip_edge(out, m, 1, y0, 1.0f, tmp);
    m = clip_edge(tmp, m, 1, y1, -1.0f, out);
    return m;
}

int rix_clip_draw(const rix_clipvert tri[3], float w_min, const rix_viewport *vp,
                  float x0, float y0, float x1, float y1, rix_emit_fn emit, void *ctx, u32 *tris)
{
    rix_clipvert cp[RIX_CLIP_MAX];
    rix_vertex sv[RIX_CLIP_MAX], cs[RIX_CLIP_MAX];
    u32 desc[RIX_TRI_DESC_WORDS];
    u32 n, i, count = 0u;
    int rc;
    n = rix_clip_near(tri, 3u, w_min, cp);
    if (n < 3u) { if (tris) *tris = 0u; return 0; }
    rix_project(cp, n, vp, sv);
    n = rix_clip_screen(sv, n, x0, y0, x1, y1, cs);
    for (i = 1; i + 1u < n; i++) {
        if (!rix_tri_desc(&cs[0], &cs[i], &cs[i + 1u], desc)) continue;   /* zero area after clipping */
        rc = emit(ctx, desc);
        if (rc) return rc;
        count++;
    }
    if (tris) *tris = count;
    return 0;
}
