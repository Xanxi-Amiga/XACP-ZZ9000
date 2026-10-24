/*
 * ZZOpenLaraRIX A2.18: TinyGL's triangle template (src/ztriangle.h), rewritten for sub-pixel vertices.
 * Installed over the patched copy by Core1/tgl/ol_patch_tinygl_persp.py.
 *
 * TinyGL placed every vertex on a whole pixel (ZBufferPoint.x/y are integers) and walked the edges
 * from there: when the camera moves, each vertex jumps by a pixel at a time and the faces shake.
 * Here the screen position is kept in floating point (ZBufferPoint.fx/fy, set with x/y):
 *   - rows: the pixel centres y + 0.5 from ceil(y0 - 0.5) to ceil(y2 - 0.5) - 1 (a row on a shared
 *     edge belongs to one triangle only);
 *   - spans: from ceil(xl - 0.5) to ceil(xr - 0.5) - 1 at the row's centre (same rule along x);
 *   - every interpolated value is evaluated at the centre of the first pixel of the span from the
 *     plane equations (no accumulated edge error), then stepped along the span by the macros of
 *     ztriangle.c, unchanged (DRAW_LINE: x1 = first pixel, x2 = (last + 1) << 16).
 * zb->zz_subpix = 0 rounds the positions down to whole pixels first: TinyGL's shaking, for comparison.
 * Rows and spans are cut to the scissor rectangle zb->zz_cx0..zz_cx1 x zz_cy0..zz_cy1 (pixels, origin top
 * left, end excluded): the game no longer narrows the viewport to the portal rectangle (that changed the
 * projection per room, and the same vertex landed a hundredth of a pixel apart in two rooms: thin black
 * cracks once the positions are not rounded any more).
 * Perspective (INTERP_STZ): s*q, t*q and q = 1/w (zb->zz_qpersp = 1) or the depth z (0, TinyGL's own).
 *
 * Interface kept: INTERP_Z / INTERP_RGB / INTERP_ST / INTERP_STZ, DRAW_INIT, DRAW_LINE or PUT_PIXEL,
 * the variables they use (x1, x2, z1, r1, g1, b1, s1, t1, sz1, tz1, q1, pp1, pz1, the_y and the
 * gradients dzdx, drdx, dgdx, dbdx, dsdx, dtdx, dszdx, dtzdx, dqdx, fdzdx, fndzdx, ndszdx, ndtzdx).
 * ASCII only.
 */

#ifndef ZZ_CEILI
/* smallest integer >= v (both signs), without the library (this file is included inside functions) */
#define ZZ_CEILI(v) ((GLint) (v) + (((GLfloat) (GLint) (v)) < (v) ? 1 : 0))
#endif

{
    ZBufferPoint *pa = p0, *pb = p1, *pc = p2;
    GLfloat ax, ay, bx, by, cx, cy;
    GLfloat fdx1, fdx2, fdy1, fdy2, fz;
    GLushort *pz1;
    PIXEL *pp1;
    GLint x1, x2;
#if TGL_HAS(POLYGON_STIPPLE)
    GLint the_y;
#endif

#ifdef INTERP_Z
    GLint z1, dzdx, dzdy;
    GLfloat zfx, zfy;
#endif
#ifdef INTERP_RGB
    GLint r1, drdx, drdy;
    GLint g1, dgdx, dgdy;
    GLint b1, dbdx, dbdy;
    GLfloat rfx, rfy, gfx, gfy, bfx, bfy;
#endif
#ifdef INTERP_ST
    GLint s1, dsdx, dsdy;
    GLint t1, dtdx, dtdy;
    GLfloat sfx, sfy, tfx, tfy;
#endif
#ifdef INTERP_STZ
    GLfloat sz1, dszdx, dszdy;
    GLfloat tz1, dtzdx, dtzdy;
    GLfloat q1, dqdx, dqdy;
    GLfloat fdzdx, fndzdx, ndszdx, ndtzdx;
#endif

    /* the vertices by increasing y */
    if (pb->fy < pa->fy) {
        ZBufferPoint *t = pa;
        pa = pb;
        pb = t;
    }
    if (pc->fy < pa->fy) {
        ZBufferPoint *t = pc;
        pc = pb;
        pb = pa;
        pa = t;
    } else if (pc->fy < pb->fy) {
        ZBufferPoint *t = pb;
        pb = pc;
        pc = t;
    }
    p0 = pa;
    p1 = pb;
    p2 = pc;
    if (zb->zz_subpix) {
        ax = p0->fx; ay = p0->fy;
        bx = p1->fx; by = p1->fy;
        cx = p2->fx; cy = p2->fy;
    } else {
        ax = (GLfloat) p0->x; ay = (GLfloat) p0->y;
        bx = (GLfloat) p1->x; by = (GLfloat) p1->y;
        cx = (GLfloat) p2->x; cy = (GLfloat) p2->y;
    }

    fdx1 = bx - ax;
    fdy1 = by - ay;
    fdx2 = cx - ax;
    fdy2 = cy - ay;
    fz = fdx1 * fdy2 - fdx2 * fdy1;

    if (fz != 0.0f) {
        GLfloat fzi = 1.0f / fz;
        GLfloat d1, d2;
        fdx1 *= fzi;
        fdy1 *= fzi;
        fdx2 *= fzi;
        fdy2 *= fzi;

#ifdef INTERP_Z
        d1 = (GLfloat) (p1->z - p0->z);
        d2 = (GLfloat) (p2->z - p0->z);
        zfx = fdy2 * d1 - fdy1 * d2;
        zfy = fdx1 * d2 - fdx2 * d1;
        dzdx = (GLint) zfx;
        dzdy = (GLint) zfy;
#endif
#ifdef INTERP_RGB
        d1 = (GLfloat) (p1->r - p0->r);
        d2 = (GLfloat) (p2->r - p0->r);
        rfx = fdy2 * d1 - fdy1 * d2;
        rfy = fdx1 * d2 - fdx2 * d1;
        drdx = (GLint) rfx;
        drdy = (GLint) rfy;
        d1 = (GLfloat) (p1->g - p0->g);
        d2 = (GLfloat) (p2->g - p0->g);
        gfx = fdy2 * d1 - fdy1 * d2;
        gfy = fdx1 * d2 - fdx2 * d1;
        dgdx = (GLint) gfx;
        dgdy = (GLint) gfy;
        d1 = (GLfloat) (p1->b - p0->b);
        d2 = (GLfloat) (p2->b - p0->b);
        bfx = fdy2 * d1 - fdy1 * d2;
        bfy = fdx1 * d2 - fdx2 * d1;
        dbdx = (GLint) bfx;
        dbdy = (GLint) bfy;
#endif
#ifdef INTERP_ST
        d1 = (GLfloat) (p1->s - p0->s);
        d2 = (GLfloat) (p2->s - p0->s);
        sfx = fdy2 * d1 - fdy1 * d2;
        sfy = fdx1 * d2 - fdx2 * d1;
        dsdx = (GLint) sfx;
        dsdy = (GLint) sfy;
        d1 = (GLfloat) (p1->t - p0->t);
        d2 = (GLfloat) (p2->t - p0->t);
        tfx = fdy2 * d1 - fdy1 * d2;
        tfy = fdx1 * d2 - fdx2 * d1;
        dtdx = (GLint) tfx;
        dtdy = (GLint) tfy;
#endif
#ifdef INTERP_STZ
        {
            GLfloat w0 = zb->zz_qpersp ? p0->q : (GLfloat) p0->z;
            GLfloat w1 = zb->zz_qpersp ? p1->q : (GLfloat) p1->z;
            GLfloat w2 = zb->zz_qpersp ? p2->q : (GLfloat) p2->z;
            p0->sz = (GLfloat) p0->s * w0;
            p0->tz = (GLfloat) p0->t * w0;
            p1->sz = (GLfloat) p1->s * w1;
            p1->tz = (GLfloat) p1->t * w1;
            p2->sz = (GLfloat) p2->s * w2;
            p2->tz = (GLfloat) p2->t * w2;
            d1 = p1->sz - p0->sz;
            d2 = p2->sz - p0->sz;
            dszdx = fdy2 * d1 - fdy1 * d2;
            dszdy = fdx1 * d2 - fdx2 * d1;
            d1 = p1->tz - p0->tz;
            d2 = p2->tz - p0->tz;
            dtzdx = fdy2 * d1 - fdy1 * d2;
            dtzdy = fdx1 * d2 - fdx2 * d1;
            d1 = w1 - w0;
            d2 = w2 - w0;
            dqdx = fdy2 * d1 - fdy1 * d2;
            dqdy = fdx1 * d2 - fdx2 * d1;
            (void) dqdy;
        }
#endif

        DRAW_INIT();

        {
            /* Coverage by the three edge functions f = A x + B y + C (>= 0 inside), each edge taken from its
               lower vertex (by y, then x) so that two triangles sharing an edge compute it alike.
               Opaque triangles with sub-pixel vertices are widened by half a pixel on every side (E: a pixel
               is drawn when its square touches the face, f >= -E (|A| + |B|)): the level geometry has faces
               that should meet but stop a fraction of a pixel apart (rooms, steps: the vertices are not
               shared); whole-pixel vertices used to close those slits by chance, the exact rule leaves them
               black. A pixel added that way is drawn alone, with the values of the nearest point of the face
               (the centre moved back onto the edge it is outside of, or the corner): no texel from outside
               the face, no colour from the neighbouring tiles of the texture page. Blended triangles keep the
               exact rule (each pixel blended once), except the alpha-tested ones (keyed tiles, drawn with
               premultiplied blending: their texels are opaque or dropped, a second pass changes nothing). */
            GLfloat E = (zb->zz_subpix && (!zb->enable_blend || zb->zz_alpha_limit < 255u)) ? 0.5f : 0.0f;
            GLfloat vx[3], vy[3], eA[3], eB[3], eC[3], eN[3], eI[3], bx0, bx1;
            GLint ya, yc, y, k;
            vx[0] = ax; vy[0] = ay; vx[1] = bx; vy[1] = by; vx[2] = cx; vy[2] = cy;
            /* A2.19: the three widened edges alone overshoot at a sharp corner (a thin triangle of Lara gave a
               spike across the whole row: horizontal streaks around her). The widened triangle is exactly
               these three half-planes AND its bounding box widened by E (the sum of a triangle and a square
               is bounded by the edge directions of both): the rows already are, the columns are cut here. */
            bx0 = vx[0]; bx1 = vx[0];
            for (k = 1; k < 3; k++) { if (vx[k] < bx0) bx0 = vx[k]; if (vx[k] > bx1) bx1 = vx[k]; }
            bx0 -= E; bx1 += E;
            for (k = 0; k < 3; k++) {
                int i = k, j = (k + 1) % 3, o = (k + 2) % 3;
                GLfloat f;
                if (vy[j] < vy[i] || (vy[j] == vy[i] && vx[j] < vx[i])) { i = j; j = k; }
                eA[k] = vy[j] - vy[i];
                eB[k] = vx[i] - vx[j];
                eC[k] = -(eA[k] * vx[i] + eB[k] * vy[i]);
                f = eA[k] * vx[o] + eB[k] * vy[o] + eC[k];
                if (f < 0.0f) { eA[k] = -eA[k]; eB[k] = -eB[k]; eC[k] = -eC[k]; }
                eN[k] = E * ((eA[k] < 0.0f ? -eA[k] : eA[k]) + (eB[k] < 0.0f ? -eB[k] : eB[k]));
                eI[k] = eA[k] * eA[k] + eB[k] * eB[k];
                eI[k] = eI[k] > 0.0f ? 1.0f / eI[k] : 0.0f;
            }
            ya = ZZ_CEILI(ay - 0.5f - E);
            yc = ZZ_CEILI(cy - 0.5f + E);
            if (ya < zb->zz_cy0) ya = zb->zz_cy0;   /* the scissor rectangle (zz_tgl_set_scissor) */
            if (yc > zb->zz_cy1) yc = zb->zz_cy1;
            if (ya < 0) ya = 0;
            if (yc > zb->ysize) yc = zb->ysize;
            pp1 = (PIXEL *) (zb->pbuf) + zb->xsize * ya;
            pz1 = zb->zbuf + ya * zb->xsize;
            for (y = ya; y < yc; y++, pp1 += zb->xsize, pz1 += zb->xsize) {
                GLfloat yy = (GLfloat) y + 0.5f;
                GLfloat lo = -1e30f, hi = 1e30f, lo1 = -1e30f, hi1 = 1e30f;
                GLint xs0, xe0, xs, xe, px;
                int empty0 = 0, empty1 = 0;
                for (k = 0; k < 3; k++) {
                    GLfloat kk = eB[k] * yy + eC[k];
                    if (eA[k] > 0.0f) {
                        GLfloat b0 = -kk / eA[k], b1 = (-kk - eN[k]) / eA[k];
                        if (b0 > lo) lo = b0;
                        if (b1 > lo1) lo1 = b1;
                    } else if (eA[k] < 0.0f) {
                        GLfloat b0 = -kk / eA[k], b1 = (-kk - eN[k]) / eA[k];
                        if (b0 < hi) hi = b0;
                        if (b1 < hi1) hi1 = b1;
                    } else {
                        if (kk < 0.0f) empty0 = 1;
                        if (kk < -eN[k]) empty1 = 1;
                    }
                }
                if (lo1 < bx0) lo1 = bx0;
                if (hi1 > bx1) hi1 = bx1;
                if (empty1 || hi1 <= lo1) continue;
                xs = ZZ_CEILI(lo1 - 0.5f);
                xe = ZZ_CEILI(hi1 - 0.5f);
                if (xs < zb->zz_cx0) xs = zb->zz_cx0;
                if (xe > zb->zz_cx1) xe = zb->zz_cx1;
                if (xe <= xs) continue;
                if (empty0 || hi <= lo) {
                    xs0 = xe0 = xe;                    /* no exact span in this row */
                } else {
                    xs0 = ZZ_CEILI(lo - 0.5f);
                    xe0 = ZZ_CEILI(hi - 0.5f);
                    if (xs0 < xs) xs0 = xs;
                    if (xe0 > xe) xe0 = xe;
                    if (xe0 <= xs0) xs0 = xe0 = xe;
                }
#if TGL_HAS(POLYGON_STIPPLE)
                the_y = y;
#endif
                for (px = xs; px < xe; ) {
                GLfloat ex, ey;
                if (px == xs0) {   /* the exact span: values at the pixel centres */
                    x1 = xs0;
                    x2 = xe0 << 16;
                    ex = (GLfloat) xs0 + 0.5f - ax;
                    ey = yy - ay;
                    px = xe0;
                } else {           /* one added pixel: values at the nearest point of the face */
                    GLfloat qx = (GLfloat) px + 0.5f, qy = yy, f[3];
                    int out = 0, last = 0;
                    for (k = 0; k < 3; k++) {
                        f[k] = eA[k] * qx + eB[k] * qy + eC[k];
                        if (f[k] < 0.0f) { out++; last = k; }
                    }
                    if (out == 1) {                    /* onto the edge it is outside of */
                        GLfloat s = f[last] * eI[last];
                        qx -= s * eA[last];
                        qy -= s * eB[last];
                    } else if (out >= 2) {             /* outside two edges: their common corner */
                        int k0 = -1, k1 = -1;
                        for (k = 0; k < 3; k++) if (f[k] < 0.0f) { if (k0 < 0) k0 = k; else k1 = k; }
                        /* edge k joins vertices k and k + 1: the shared vertex of k0 and k1 */
                        k = (k1 == k0 + 1) ? k1 : k0;
                        qx = vx[k];
                        qy = vy[k];
                    }
                    x1 = px;
                    x2 = (px + 1) << 16;
                    ex = qx - ax;
                    ey = qy - ay;
                    px++;
                }
#ifdef INTERP_Z
                {
                    GLfloat v = (GLfloat) p0->z + ex * zfx + ey * zfy;
                    z1 = v > 0.0f ? (GLint) v : 0;
                }
#endif
#ifdef INTERP_RGB
                {
                    GLfloat v;
                    v = (GLfloat) p0->r + ex * rfx + ey * rfy; r1 = v > 0.0f ? (GLint) v : 0;
                    v = (GLfloat) p0->g + ex * gfx + ey * gfy; g1 = v > 0.0f ? (GLint) v : 0;
                    v = (GLfloat) p0->b + ex * bfx + ey * bfy; b1 = v > 0.0f ? (GLint) v : 0;
                }
#endif
#ifdef INTERP_ST
                s1 = (GLint) ((GLfloat) p0->s + ex * sfx + ey * sfy);
                t1 = (GLint) ((GLfloat) p0->t + ex * tfx + ey * tfy);
#endif
#ifdef INTERP_STZ
                sz1 = p0->sz + ex * dszdx + ey * dszdy;
                tz1 = p0->tz + ex * dtzdx + ey * dtzdy;
                q1 = (zb->zz_qpersp ? p0->q : (GLfloat) p0->z) + ex * dqdx + ey * dqdy;
#endif
#ifndef DRAW_LINE
                {
                    register PIXEL *pp;
                    register GLint n;
#ifdef INTERP_Z
                    register GLushort *pz;
                    register GLuint z;
#endif
#ifdef INTERP_RGB
                    register GLint or1, og1, ob1;
#endif
#ifdef INTERP_ST
                    register GLuint s, t;
#endif
                    n = ((x2 + 0xFFFF) >> 16) - x1 - 1;
                    pp = (PIXEL *) pp1 + x1;
#ifdef INTERP_Z
                    pz = pz1 + x1;
                    z = z1;
#endif
#ifdef INTERP_RGB
                    or1 = r1;
                    og1 = g1;
                    ob1 = b1;
#endif
#ifdef INTERP_ST
                    s = s1;
                    t = t1;
#endif
                    while (n >= 3) {
                        PUT_PIXEL(0);
                        PUT_PIXEL(1);
                        PUT_PIXEL(2);
                        PUT_PIXEL(3);
#ifdef INTERP_Z
                        pz += 4;
#endif
                        pp += 4;
                        n -= 4;
                    }
                    while (n >= 0) {
                        PUT_PIXEL(0);
#ifdef INTERP_Z
                        pz++;
#endif
                        pp++;
                        n--;
                    }
                }
#else
                DRAW_LINE();
#endif
                }
            }
        }
    }
}

#undef INTERP_Z
#undef INTERP_RGB
#undef INTERP_ST
#undef INTERP_STZ

#undef DRAW_INIT
#undef DRAW_LINE
#undef PUT_PIXEL
