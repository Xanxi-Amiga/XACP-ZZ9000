/* rixdesc.h - librixgpu 1.0 - RasterIX triangle descriptor builder
 *
 * The 39-word TMU_COUNT=1 descriptor (RegisterAndDescriptorDefines.vh,
 * TRIANGLE_STREAM_*): bounding box, three edge functions with their x/y
 * increments (S.5 fixed point, 1/32 pixel), then per-attribute value at the
 * bbox origin + x increment + y increment for colour (r,g,b,a), depth (z,w)
 * and TMU0 texture (s/w, t/w, 1/w).  Bit-exact with the validated A07b/B1
 * cube_make_desc() for colour and texture (host_tests/test_rix.c proves it);
 * depth z/w are now filled (the cube left them 0: no Z-buffer then).
 *
 * Screen convention: RasterIX y grows upwards (Rasterizer.v stores the pixel
 * at ((yres-1)-y)*xres+x); the caller maps its y accordingly (gear_xform.c).
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef RIXDESC_H
#define RIXDESC_H
#include "rix_hw.h"

typedef struct {
    float sx, sy;        /* screen position, pixels, RasterIX y-up */
    float z;             /* window depth 0..1 (0 near) for the Z-buffer; ignored when depth test is off */
    float invw;          /* 1/w (perspective correction of s,t) */
    float u, v;          /* texture coordinates 0..1 */
    float r, g, b, a;    /* vertex colour 0..1 */
} rix_vertex;

/* Builds the descriptor; returns 1, or 0 when the triangle has no area or
   its bounding box leaves the 640x481 buffer (clip before calling). */
int rix_tri_desc(const rix_vertex *a, const rix_vertex *b, const rix_vertex *c, u32 *out39);

/* signed screen area * 2 in 1/32 units: < 0 for a counter-clockwise triangle in y-up window
   coordinates, > 0 clockwise (the RasterIX edge function; measured, host bench 14/10) */
s32 rix_tri_area(const rix_vertex *a, const rix_vertex *b, const rix_vertex *c);
/* 1.2.2: 1 = top-left fill rule (a pixel exactly on an edge shared by two triangles is drawn once);
   1.2.3: default 0 (garbled output on the G4C core with it on) */
extern int rix_fill_rule;
/* 1.2.3: 1 = texture planes with 1/w normalised per triangle (reference driver); default 0 */
extern int rix_tex_wnorm;

#endif
