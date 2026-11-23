/* rixdesc.c - librixgpu 1.4.0 - triangle descriptor builder (see rixdesc.h)
 * Math identical to cube_make_desc() of ZZCube B1 (A07b reference); the
 * depth attribute is interpolated like the others.
 * SPDX-License-Identifier: 0BSD */
#include "rixdesc.h"

/* librixgpu 1.4.1: the interpolation below in NEON when the compiler offers
   it (build_arm.sh compiles THIS FILE ONLY with -mfpu=neon; everything else stays on VFPv3-d16).
   Measured on the A4000 on 14/11 with RIXPROF=1: of zzglEnd's 6024 us a frame, rix_tri_desc took
   2166 and the descriptor writes only 901 - the cost is the arithmetic, and above all the 27
   interpolated floats that each had to cross from a VFP register to an ARM register to be stored
   as their bit pattern, which is the expensive move on a Cortex-A9.  NEON interpolates four at a
   time and stores the words from its own registers, so those 27 moves disappear.
   The arithmetic is UNCHANGED: ARMv7 NEON's VMLA is not fused, and the three products are summed
   in the same order, so every value that is not denormal gives the same bits as before.  NEON
   flushes denormals to zero; here that can only concern a gradient smaller than 1e-38 per pixel. */
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define RIXDESC_NEON 1
/* rix_neon = 0 runs the scalar path instead, in the same binary (NEON=0).
   Every "is it the NEON?" question is then one run and one option, at the same place in the map,
   instead of two builds and a message. */
/* the NEON path loads (z, invw), (u, v) and (r, g, b, a) as adjacent floats of rix_vertex.
   If the structure is ever reordered this stops compiling instead of drawing nonsense. */
typedef char rixdesc_layout_check[
    ( (int)((char *)&((rix_vertex *)0)->invw - (char *)&((rix_vertex *)0)->z) == 4
   && (int)((char *)&((rix_vertex *)0)->v    - (char *)&((rix_vertex *)0)->u) == 4
   && (int)((char *)&((rix_vertex *)0)->g    - (char *)&((rix_vertex *)0)->r) == 4
   && (int)((char *)&((rix_vertex *)0)->b    - (char *)&((rix_vertex *)0)->r) == 8
   && (int)((char *)&((rix_vertex *)0)->a    - (char *)&((rix_vertex *)0)->r) == 12 ) ? 1 : -1];
#endif

/* 1.4: the frame geometry (rix_set_geometry; 640 x 481 by default) */
extern u32 rix_geo_w, rix_geo_h_alloc;   /* rixgpu.c (rixgpu.h) */
#define W ((s32)rix_geo_w)
#define H ((s32)rix_geo_h_alloc)
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

/* 1 = the NEON paths (default), 0 = the scalar reference, chosen at run time
   by the NEON= option.  Shared by rixdesc.c, rixclip.c and zzgl.c. */
int rix_neon = 1;

int rix_fill_rule = 0;   /* 1.2.2: 1 = top-left fill rule (see below); 1.2.3: OFF by default - with it on, the A4000
                            showed garbled 2D text, jittering models and flickering water (ZZQuake2RIX 0.4, 14/10):
                            the G4C core's edge walker does not take the biased start values as the model does */
int rix_tex_wnorm = 0;   /* 1.2.3: 1 = the texture planes use 1/w scaled by 1/max(1/w) of the triangle (the RasterIX
                            reference driver normalises w "to avoid that w gets too small"); s/q, t/q unchanged */

/* librixgpu 1.4.2: rix_box_fit, the bounding box cut to the lines that hold
   a pixel.  Why: the rasteriser's edge walker (Rasterizer.v; librixgpu/rix_walk_model.h is its
   clock-by-clock model, which reproduces the A4000's price list of 15/11 to 3 %) starts each line
   where the previous line's left edge was, and on a line that holds NO pixel it runs to the end
   of the box, back to its start, and searches again on the next line: two widths of the box and
   more, for nothing.  The box built below is the triangle's rounded outwards, so its first and
   last lines hold no pixel more often than not, and the thin end of a triangle several.
     1: the empty lines are taken off the END of the box (bbEnd.y comes down).  Nothing else in
        the descriptor changes: the GPU draws the same pixels with the same values, bit for bit.
     2: and off its START (bbStart.y goes up).  The edge values at the new start are the old
        ones plus whole y increments, exact integers: the same pixels are inside.  The colour,
        depth and texture planes are evaluated at the new start by the same formula as before,
        so a pixel's value may differ from mode 0 in the last bit of a float - and the two
        passes of a world polygon cut their boxes alike, so their depths stay equal.
   A triangle whose box holds no pixel at all is not sent (it draws nothing): rix_tri_desc
   returns 0, as for a triangle without area.
   The test is exact, in the integers the walker itself tests (edge value >= 0 on the three
   edges, column inside the box).  Not applied with rix_fill_rule (its biased edge values). */
int rix_box_fit = 0;
u32 rix_box_fit_lines, rix_box_fit_dropped;
#define RIX_BOX_FIT_MAX 16
/* 1 when no column 0..n-1 of the line has its three edge values >= 0; w[]: their values at column 0.
   Exact, and one division at most (this CPU divides in software): an edge whose value grows with
   the column gives a first column, one whose value falls a last column; the columns that pass the
   three edges are an interval, so it is enough to compute ONE of its ends and to try the other
   edges there - at the first column of the only "growing" edge that fails at column 0, or at the
   last column of the only "falling" edge when two growing edges fail at column 0.  No division
   at all when column 0 itself is inside, or when an edge fails for every column. */
static int line_empty(const s32 w[3], const s32 wx[3], s32 n)
{
    int lf[3], uf[3], nl = 0, nu = 0, i;
    s32 hi = n - 1;
    for (i = 0; i < 3; i++) {
        if (w[i] < 0) { if (wx[i] <= 0) return 1; lf[nl++] = i; }   /* fails at column 0 and does not grow: never passes */
        else if (wx[i] < 0) uf[nu++] = i;                             /* passes at column 0, falls from there */
    }
    if (!nl) return 0;                                                /* column 0 is inside */
    if (nl == 1) {
        const s32 v = w[lf[0]], s = wx[lf[0]];
        const u32 lo = ((0u - (u32)v) + (u32)(s - 1)) / (u32)s;       /* the first column with v + k*s >= 0 */
        if (lo > (u32)hi) return 1;
        for (i = 0; i < nu; i++) if (w[uf[i]] + (s32)lo * wx[uf[i]] < 0) return 1;
        return 0;
    }
    if (nu == 1) {                                                    /* the last column the falling edge passes */
        const u32 k = (u32)w[uf[0]] / (0u - (u32)wx[uf[0]]);
        if (k < (u32)hi) hi = (s32)k;
    }
    for (i = 0; i < nl; i++) if (w[lf[i]] + hi * wx[lf[i]] < 0) return 1;
    return 0;
}

int rix_tri_desc(const rix_vertex *a, const rix_vertex *b, const rix_vertex *c, u32 *out)
{
    s32 v0[2], v1[2], v2[2], p[2], area, sign, bsx, bsy, bex, bey, wi[3], wx[3], wy[3];
    float ai, wn[3], wxn[3], wyn[3], q4[4], q3[3], q2[2];
    float c0[4], c1[4], c2[4], t0[3], t1[3], t2[3], d0[2], d1[2], d2[2];
    float qa, qb, qc;
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
    if (rix_box_fit && !rix_fill_rule) {              /* see rix_box_fit above */
        const s32 n = bex - bsx;
        s32 e[3], cut = 0;
        {   /* first what costs nothing: the box above was rounded to the NEAREST line, so it starts
               a line before the triangle half of the time, and ends a line after it as often.  A
               line is sampled at its integer y: none can hold a pixel outside ymin..ymax. */
            s32 t = v0[1]; if (v1[1] > t) t = v1[1]; if (v2[1] > t) t = v2[1];
            t = (t >> EDGE_SHIFT) + 1;
            if (t < bey) {
                if (t <= bsy) { rix_box_fit_dropped++; return 0; }
                rix_box_fit_lines += (u32)(bey - t); bey = t;
            }
            if (rix_box_fit > 1) {
                t = v0[1]; if (v1[1] < t) t = v1[1]; if (v2[1] < t) t = v2[1];
                t = (t + EDGE_ONE - 1) >> EDGE_SHIFT;
                if (t >= bey) { rix_box_fit_dropped++; return 0; }
                while (bsy < t) { bsy++; wi[0] += wy[0]; wi[1] += wy[1]; wi[2] += wy[2]; rix_box_fit_lines++; }
            }
        }
        for (;;) {                                    /* the last lines of the box */
            const s32 dy = bey - 1 - bsy;
            e[0] = wi[0] + dy * wy[0]; e[1] = wi[1] + dy * wy[1]; e[2] = wi[2] + dy * wy[2];
            if (!line_empty(e, wx, n)) break;
            if (dy == 0) { rix_box_fit_dropped++; return 0; }      /* not one pixel in the box */
            bey--; rix_box_fit_lines++;
            if (++cut >= RIX_BOX_FIT_MAX) break;
        }
        if (rix_box_fit > 1) {                        /* its first lines */
            cut = 0;
            while (bsy + 1 < bey && cut < RIX_BOX_FIT_MAX && line_empty(wi, wx, n)) {
                bsy++; wi[0] += wy[0]; wi[1] += wy[1]; wi[2] += wy[2];
                rix_box_fit_lines++; cut++;
            }
        }
    }
    ai = 1.0f / (float)area;
    for (i = 0; i < 3; i++) { wn[i] = (float)wi[i] * ai; wxn[i] = (float)wx[i] * ai; wyn[i] = (float)wy[i] * ai; }
    qa = a->invw; qb = b->invw; qc = c->invw;
    if (rix_tex_wnorm) {
        float m = qa; if (qb > m) m = qb; if (qc > m) m = qc;
        if (m > 0.0f) { float r = 1.0f / m; qa *= r; qb *= r; qc *= r; }
    }
    /* the staging arrays are filled in the scalar branch only.  Build 60 took them out of the NEON
       path: written one float at a time by the scalar FPU and read back by NEON, they made the data
       cross the stack between two pipelines, which a Cortex-A9 pays for (a NEON load that hits a
       pending VFP store stalls).  The NEON path loads straight from the vertices, whose layout puts
       (z, invw), (u, v) and (r, g, b, a) next to each other - checked at compile time above. */
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
#ifdef RIXDESC_NEON
    if (rix_neon) {
    /* same values, same order of operations, written straight from the NEON registers.
       C = (r,g,b,a); D = (z, invw, u*q, v*q); Q = q broadcast.  The descriptor wants the three
       colour quadruples, then the three depth pairs, then the three texture triples. */
#define RIX_I3(R, A, B, C_, W) do { \
        R = vmulq_n_f32((A), (W)[0]); \
        R = vmlaq_n_f32(R, (B), (W)[1]); \
        R = vmlaq_n_f32(R, (C_), (W)[2]); } while (0)
    {
        /* straight from the vertices, no staging through the stack.
           (z, invw) and (u, v) are adjacent pairs, (r, g, b, a) a quadruple; t = (u*q, v*q). */
        const float32x4_t C0 = vld1q_f32(&a->r), C1 = vld1q_f32(&b->r), C2 = vld1q_f32(&c->r);
        const float32x4_t D0 = vcombine_f32(vld1_f32(&a->z), vmul_n_f32(vld1_f32(&a->u), qa));
        const float32x4_t D1 = vcombine_f32(vld1_f32(&b->z), vmul_n_f32(vld1_f32(&b->u), qb));
        const float32x4_t D2 = vcombine_f32(vld1_f32(&c->z), vmul_n_f32(vld1_f32(&c->u), qc));
        const float32x4_t Q0 = vdupq_n_f32(qa), Q1 = vdupq_n_f32(qb), Q2 = vdupq_n_f32(qc);
        float32x4_t CW, CX, CY, DW, DX, DY, QW, QX, QY;
        RIX_I3(CW, C0, C1, C2, wn); RIX_I3(CX, C0, C1, C2, wxn); RIX_I3(CY, C0, C1, C2, wyn);
        RIX_I3(DW, D0, D1, D2, wn); RIX_I3(DX, D0, D1, D2, wxn); RIX_I3(DY, D0, D1, D2, wyn);
        RIX_I3(QW, Q0, Q1, Q2, wn); RIX_I3(QX, Q0, Q1, Q2, wxn); RIX_I3(QY, Q0, Q1, Q2, wyn);
        vst1q_u32(&out[k], vreinterpretq_u32_f32(CW)); k += 4;
        vst1q_u32(&out[k], vreinterpretq_u32_f32(CX)); k += 4;
        vst1q_u32(&out[k], vreinterpretq_u32_f32(CY)); k += 4;
        vst1_u32(&out[k], vreinterpret_u32_f32(vget_low_f32(DW))); k += 2;
        vst1_u32(&out[k], vreinterpret_u32_f32(vget_low_f32(DX))); k += 2;
        vst1_u32(&out[k], vreinterpret_u32_f32(vget_low_f32(DY))); k += 2;
        vst1_u32(&out[k], vreinterpret_u32_f32(vget_high_f32(DW))); k += 2;
        vst1q_lane_u32(&out[k], vreinterpretq_u32_f32(QW), 0); k += 1;
        vst1_u32(&out[k], vreinterpret_u32_f32(vget_high_f32(DX))); k += 2;
        vst1q_lane_u32(&out[k], vreinterpretq_u32_f32(QX), 0); k += 1;
        vst1_u32(&out[k], vreinterpret_u32_f32(vget_high_f32(DY))); k += 2;
        vst1q_lane_u32(&out[k], vreinterpretq_u32_f32(QY), 0); k += 1;
        (void)q4; (void)q3; (void)q2; (void)i; (void)c0; (void)c1; (void)c2;
        (void)t0; (void)t1; (void)t2; (void)d0; (void)d1; (void)d2;
    }
#undef RIX_I3
    } else {
    /* NEON=0: the scalar reference, from the staging arrays filled just below for this case */
    c0[0] = a->r; c0[1] = a->g; c0[2] = a->b; c0[3] = a->a;
    c1[0] = b->r; c1[1] = b->g; c1[2] = b->b; c1[3] = b->a;
    c2[0] = c->r; c2[1] = c->g; c2[2] = c->b; c2[3] = c->a;
    t0[0] = a->u * qa; t0[1] = a->v * qa; t0[2] = qa;
    t1[0] = b->u * qb; t1[1] = b->v * qb; t1[2] = qb;
    t2[0] = c->u * qc; t2[1] = c->v * qc; t2[2] = qc;
    d0[0] = a->z; d0[1] = a->invw; d1[0] = b->z; d1[1] = b->invw; d2[0] = c->z; d2[1] = c->invw;
    interp4(q4, c0, c1, c2, wn);  for (i = 0; i < 4; i++) out[k++] = fbits(q4[i]);
    interp4(q4, c0, c1, c2, wxn); for (i = 0; i < 4; i++) out[k++] = fbits(q4[i]);
    interp4(q4, c0, c1, c2, wyn); for (i = 0; i < 4; i++) out[k++] = fbits(q4[i]);
    interp2(q2, d0, d1, d2, wn);  for (i = 0; i < 2; i++) out[k++] = fbits(q2[i]);
    interp2(q2, d0, d1, d2, wxn); for (i = 0; i < 2; i++) out[k++] = fbits(q2[i]);
    interp2(q2, d0, d1, d2, wyn); for (i = 0; i < 2; i++) out[k++] = fbits(q2[i]);
    interp3(q3, t0, t1, t2, wn);  for (i = 0; i < 3; i++) out[k++] = fbits(q3[i]);
    interp3(q3, t0, t1, t2, wxn); for (i = 0; i < 3; i++) out[k++] = fbits(q3[i]);
    interp3(q3, t0, t1, t2, wyn); for (i = 0; i < 3; i++) out[k++] = fbits(q3[i]);
    }
#else
    c0[0] = a->r; c0[1] = a->g; c0[2] = a->b; c0[3] = a->a;
    c1[0] = b->r; c1[1] = b->g; c1[2] = b->b; c1[3] = b->a;
    c2[0] = c->r; c2[1] = c->g; c2[2] = c->b; c2[3] = c->a;
    t0[0] = a->u * qa; t0[1] = a->v * qa; t0[2] = qa;
    t1[0] = b->u * qb; t1[1] = b->v * qb; t1[2] = qb;
    t2[0] = c->u * qc; t2[1] = c->v * qc; t2[2] = qc;
    d0[0] = a->z; d0[1] = a->invw; d1[0] = b->z; d1[1] = b->invw; d2[0] = c->z; d2[1] = c->invw;
    interp4(q4, c0, c1, c2, wn);  for (i = 0; i < 4; i++) out[k++] = fbits(q4[i]);
    interp4(q4, c0, c1, c2, wxn); for (i = 0; i < 4; i++) out[k++] = fbits(q4[i]);
    interp4(q4, c0, c1, c2, wyn); for (i = 0; i < 4; i++) out[k++] = fbits(q4[i]);
    interp2(q2, d0, d1, d2, wn);  for (i = 0; i < 2; i++) out[k++] = fbits(q2[i]);
    interp2(q2, d0, d1, d2, wxn); for (i = 0; i < 2; i++) out[k++] = fbits(q2[i]);
    interp2(q2, d0, d1, d2, wyn); for (i = 0; i < 2; i++) out[k++] = fbits(q2[i]);
    interp3(q3, t0, t1, t2, wn);  for (i = 0; i < 3; i++) out[k++] = fbits(q3[i]);
    interp3(q3, t0, t1, t2, wxn); for (i = 0; i < 3; i++) out[k++] = fbits(q3[i]);
    interp3(q3, t0, t1, t2, wyn); for (i = 0; i < 3; i++) out[k++] = fbits(q3[i]);
#endif
    return k == (int)RIX_TRI_DESC_WORDS;
}
