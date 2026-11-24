/* rixclip.h - librixgpu 1.1 - triangle clipping for RasterIX
 *
 * RasterIX rasterizes from a descriptor whose bounding box must lie inside
 * the buffer (rix_tri_desc() refuses the others): a renderer must clip.  Two
 * stages, both Sutherland-Hodgman on convex polygons (a triangle gives at
 * most 4 vertices after the near plane, 8 after the four screen edges):
 *
 *   1. near plane, in clip space (before the perspective divide): vertices
 *      with w < w_min are cut away; every attribute interpolates linearly in
 *      clip space, which is exact.
 *   2. screen rectangle, in screen space (after rix_project): x, y, z, the
 *      colours and the perspective quantities u/w, v/w, 1/w interpolate
 *      linearly in screen space, which is exact for z and 1/w and matches
 *      RasterIX's own linear colour interpolation.
 *
 * The clipped polygon is drawn as a fan: (v0, v1, v2), (v0, v2, v3), ...
 * with rix_tri_desc() + rix_frame_tri().  rix_clip_draw() does that.
 *
 * Culling: cull BEFORE clipping on the sign of rix_tri_area() of the three
 * projected vertices when all are in front of the near plane; a triangle that
 * crosses the near plane is culled after step 1 (its first three output
 * vertices keep the winding).
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef RIXCLIP_H
#define RIXCLIP_H
#include "rixdesc.h"

typedef struct { float x, y, z, w; float u, v; float r, g, b, a; } rix_clipvert;

/* viewport: sx = x/w * scale_x + trans_x, sy = y/w * scale_y + trans_y (RasterIX y-up:
   scale_y > 0; the 481-line buffers add +1 to trans_y), z = z/w * scale_z + trans_z
   (OpenGL window depth: scale_z = trans_z = 0.5) */
typedef struct { float scale_x, trans_x, scale_y, trans_y, scale_z, trans_z; } rix_viewport;

#define RIX_CLIP_MAX 12u

/* near plane: keeps w >= w_min; returns the vertex count of the output polygon (0 = all behind) */
u32  rix_clip_near(const rix_clipvert *in, u32 n, float w_min, rix_clipvert *out);
/* perspective divide + viewport, n vertices */
void rix_project(const rix_clipvert *in, u32 n, const rix_viewport *vp, rix_vertex *out);
/* screen rectangle [x0, x1) x [y0, y1) in pixels (the 640x481 buffers of the demos: 0, 1, 640, 481);
   returns the output count (0 = fully outside).  in and out must not alias. */
u32  rix_clip_screen(const rix_vertex *in, u32 n, float x0, float y0, float x1, float y1, rix_vertex *out);
/* whole pipeline for one triangle: near clip, project, screen clip, fan into descriptors
   through emit(ctx, desc39) (return 0 to continue, else the error is returned).
   Returns 0, or the emit error; *tris receives the number of descriptors emitted. */
typedef int (*rix_emit_fn)(void *ctx, const u32 *desc39);
int  rix_clip_draw(const rix_clipvert tri[3], float w_min, const rix_viewport *vp,
                   float x0, float y0, float x1, float y1, rix_emit_fn emit, void *ctx, u32 *tris);

#endif
