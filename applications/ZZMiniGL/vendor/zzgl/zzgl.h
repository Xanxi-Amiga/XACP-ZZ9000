/* ZZGL 0.8.10-unified-dev1 + ZZMiniGL A6 sections: OpenGL subset for ZZ9000 RasterIX.
 * The ZZMiniGL sections (marked "ZZMiniGL") add what a MiniGL client needs on top of the unified
 * base: normals, texture coordinate generation (with the ZZ_texgen_frame extension), dithered
 * uploads, resident client arrays and indexed draws.  Nothing in the unified base was removed.
 * Backed by librixgpu; includes texture state, queue submission,
 * depth handling, clipping and indexed triangle support.
 * SPDX-License-Identifier: 0BSD
 */
#ifndef ZZGL_H
#define ZZGL_H
#include "rixframe.h"
#include "rixclip.h"

/* ---- GL enums used by ref_gl (values of the OpenGL ABI, so ref_gl's headers match) ---- */
#define ZGL_FALSE 0
#define ZGL_TRUE 1
#define ZGL_POINTS 0x0000
#define ZGL_LINES 0x0001
#define ZGL_LINE_LOOP 0x0002
#define ZGL_LINE_STRIP 0x0003
#define ZGL_TRIANGLES 0x0004
#define ZGL_TRIANGLE_STRIP 0x0005
#define ZGL_TRIANGLE_FAN 0x0006
#define ZGL_QUADS 0x0007
#define ZGL_QUAD_STRIP 0x0008
#define ZGL_POLYGON 0x0009
#define ZGL_NEVER 0x0200
#define ZGL_LESS 0x0201
#define ZGL_EQUAL 0x0202
#define ZGL_LEQUAL 0x0203
#define ZGL_GREATER 0x0204
#define ZGL_NOTEQUAL 0x0205
#define ZGL_GEQUAL 0x0206
#define ZGL_ALWAYS 0x0207
#define ZGL_ZERO 0
#define ZGL_ONE 1
#define ZGL_SRC_COLOR 0x0300
#define ZGL_ONE_MINUS_SRC_COLOR 0x0301
#define ZGL_SRC_ALPHA 0x0302
#define ZGL_ONE_MINUS_SRC_ALPHA 0x0303
#define ZGL_DST_ALPHA 0x0304
#define ZGL_ONE_MINUS_DST_ALPHA 0x0305
#define ZGL_DST_COLOR 0x0306
#define ZGL_ONE_MINUS_DST_COLOR 0x0307
#define ZGL_SRC_ALPHA_SATURATE 0x0308
#define ZGL_FRONT 0x0404
#define ZGL_BACK 0x0405
#define ZGL_FRONT_AND_BACK 0x0408
#define ZGL_CW 0x0900
#define ZGL_CCW 0x0901
#define ZGL_CULL_FACE 0x0B44
#define ZGL_DEPTH_TEST 0x0B71
#define ZGL_ALPHA_TEST 0x0BC0
#define ZGL_BLEND 0x0BE2
#define ZGL_SCISSOR_TEST 0x0C11
#define ZGL_TEXTURE_1D 0x0DE0   /* 0.8.3 (ZZHalfLife build 54): accepted as a 2D texture one line high */
#define ZGL_TEXTURE_2D 0x0DE1
#define ZGL_STENCIL_TEST 0x0B90
#define ZGL_FOG 0x0B60
#define ZGL_POLYGON_OFFSET_FILL 0x8037
#define ZGL_FLAT 0x1D00
#define ZGL_SMOOTH 0x1D01
#define ZGL_POINT 0x1B00
#define ZGL_LINE 0x1B01
#define ZGL_FILL 0x1B02
#define ZGL_MODELVIEW 0x1700
#define ZGL_PROJECTION 0x1701
#define ZGL_TEXTURE 0x1702
#define ZGL_MODELVIEW_MATRIX 0x0BA6
#define ZGL_PROJECTION_MATRIX 0x0BA7
#define ZGL_TEXTURE_ENV 0x2300
#define ZGL_TEXTURE_ENV_MODE 0x2200
#define ZGL_MODULATE 0x2100
#define ZGL_DECAL 0x2101
#define ZGL_REPLACE 0x1E01
#define ZGL_TEXTURE_MAG_FILTER 0x2800
#define ZGL_TEXTURE_MIN_FILTER 0x2801
#define ZGL_TEXTURE_WRAP_S 0x2802
#define ZGL_TEXTURE_WRAP_T 0x2803
#define ZGL_NEAREST 0x2600
#define ZGL_LINEAR 0x2601
#define ZGL_NEAREST_MIPMAP_NEAREST 0x2700
#define ZGL_LINEAR_MIPMAP_NEAREST 0x2701
#define ZGL_NEAREST_MIPMAP_LINEAR 0x2702
#define ZGL_LINEAR_MIPMAP_LINEAR 0x2703
#define ZGL_REPEAT 0x2901
#define ZGL_CLAMP 0x2900
#define ZGL_CLAMP_TO_EDGE 0x812F        /* ZZHalfLife 0.8.1 */
#define ZGL_CLAMP_TO_BORDER 0x812D      /* ZZHalfLife 0.8.1: taken as CLAMP_TO_EDGE (no border colour in the TMU) */
#define ZGL_RGB 0x1907
#define ZGL_RGBA 0x1908
#define ZGL_LUMINANCE 0x1909
#define ZGL_UNSIGNED_BYTE 0x1401
#define ZGL_COLOR_BUFFER_BIT 0x00004000
#define ZGL_DEPTH_BUFFER_BIT 0x00000100
#define ZGL_STENCIL_BUFFER_BIT 0x00000400
#define ZGL_VENDOR 0x1F00
#define ZGL_RENDERER 0x1F01
#define ZGL_VERSION 0x1F02
#define ZGL_EXTENSIONS 0x1F03
#define ZGL_MAX_TEXTURE_SIZE 0x0D33
#define ZGL_NO_ERROR 0
#define ZGL_INVALID_ENUM 0x0500
#define ZGL_INVALID_VALUE 0x0501
#define ZGL_INVALID_OPERATION 0x0502
#define ZGL_OUT_OF_MEMORY 0x0505
#define ZGL_FASTEST 0x1101
#define ZGL_NICEST 0x1102
#define ZGL_DONT_CARE 0x1100
#define ZGL_PERSPECTIVE_CORRECTION_HINT 0x0C50

/* ---- context ---- */
#ifndef ZZGL_MAX_TEXTURES   /* ZZMiniGL: overridable (the MiniGL Core1 build uses 1024 to keep its BSS in the cached section) */
#define ZZGL_MAX_TEXTURES   2304u   /* ref_gl: TEXNUM_LIGHTMAPS 1024 (+128 lightmaps), TEXNUM_SCRAPS 1152, TEXNUM_IMAGES 1153 + MAX_GLTEXTURES 1024 = 2177; texnums are used directly as indices */
#endif
#define ZZGL_MAX_VERTS      64u     /* vertices of one Begin/End polygon (ref_gl's biggest: warp polys ~ 32) */
#define ZZGL_MATRIX_STACK   8u

typedef struct {
    u32 pool_base, pool_pages;          /* texture pool (rix_texpool_init) */
    int pool_cacheable;                 /* 1 = write-back mapping: uploads are cleaned (arena outer-NC + L1 clean) */
    u32 zbuf;                           /* depth buffer, 640x481x16 (arena) */
    u32 width, height;                  /* 640, 480 */
    int ztrick;                         /* 1: no depth clear, alternating range + GEQUAL (gl_ztrick) */
} zzgl_config;

/* called by the presenter glue: the colour buffer the next frame draws into, and the
   fence value to carry (rix_frame_end) */
typedef struct {
    u32 color;           /* ARM address of the 640x481 RGB565 buffer */
    u32 fence;           /* 16-bit frame fence value */
} zzgl_target;

int  zzgl_init(const zzgl_config *cfg);            /* after rix_init + rix_ring_init */
int  zzgl_tex_valid(u32 texnum);                    /* 1 when the texture has resident pool pages */
void zzgl_set_evict(int on);                       /* ZZHalfLife 0.7.9: drop textures unused for 2 frames when the pool is full (Quake II behaviour); off by default */
void zzgl_tex_counts(u32 *uploads, u32 *downscaled, u32 *refused, u32 *evicted);   /* 0.7.9: since the start */
u32  zzgl_live_textures(void);                     /* 0.7.9: textures holding pool pages now */
u32  zzgl_sub_dropped(void);                       /* 0.7.9: sub-uploads of a reduced texture that were off its grid */
/* 0.8.5 (ZZHalfLife build 84): what is SUBMITTED to the GPU, by origin.  The caller names the origin
   of what it is about to draw (zzgl_origin_set: world, lightmap pass, models, sprites, HUD... the
   numbers are the caller's, ZZGL only uses them as an index) and reads the frame's counters back
   with zzgl_origin_take.  Counted per origin: Begin/End batches, triangles sent, binds really sent
   and the 2 KiB pages they stream into the TMU, and the PIXELS the triangles cover on the screen
   after the screen clip, blended and not blended apart.  The pixels are the load nobody measured:
   on 15/11 a frame of 2 065 triangles and 59 binds kept Core1 waiting for the GPU 107 ms and no
   counter could say whether that was fill.  Cost: one cross product per triangle sent.
   zzgl_set_origin_stats(0) turns the pixel sum off. */
#define ZZGL_ORIGINS 16u
typedef struct { u32 batches, tris, binds, pages; float px_opaque, px_blend; u32 tris_in; float px_box;   /* tris_in: handed to ZZGL, before culling and clipping (build 85); px_box: 0.8.6, see zzgl_set_box_stats */
                 u32 wk_desc, wk_clocks, wk_far, wk_end, wk_lines, wk_empty;                                /* 0.8.7, see zzgl_set_walk_stats */
                 u32 tris_rej; } zzgl_origin_stat;                                                          /* 0.8.7: of tris_in, those of batches rejected whole (zzgl_set_batch_reject) */
void zzgl_origin_set(u32 origin);
void zzgl_origin_take(zzgl_origin_stat *dst, u32 n);
/* 0.8.6 (ZZHalfLife build 86): the pixels of each triangle's BOUNDING BOX on the screen, per
   origin (px_box), beside the pixels of its surface.  Off by default: it adds a dozen compares
   to every triangle sent.  For one question only: does the GPU's time follow the surface or the
   box. */
void zzgl_set_box_stats(int on);
/* 0.8.6 (ZZHalfLife build 86): the depth buffer of each frame.  When set, the hook is called as
   a frame opens (its first draw, clear or swap), before anything of the frame is submitted.  It
   writes the address of the depth buffer this frame must use (640x481x16, as zzgl_config.zbuf)
   and returns 1 when that buffer already holds the far value (0xFFFF) in every word: the
   frame's depth clears are then NOT sent to the GPU, as long as the clear value is 1.0 and
   nothing has drawn with the depth mask on.  Returning 0 is always right (the GPU clears).
   Without a hook: zzgl_config.zbuf at every frame, every clear by the GPU. */
typedef int (*zzgl_zbuf_hook)(u32 *zbuf);
void zzgl_set_zbuf_hook(zzgl_zbuf_hook fn);
/* 0.8.7 (ZZHalfLife build 87): every triangle descriptor sent is also walked by the model of the
   rasteriser's edge walker (librixgpu/rix_walk_model.h, which reproduces the A4000's price
   list to 3 %), and its clocks are summed by origin: wk_desc descriptors, wk_clocks the walker's
   clocks, wk_far those spent on lines where it ran to the end of the box, wk_end those on the
   lines before the first and after the last line with a pixel, wk_lines / wk_empty the lines
   walked and those without a pixel.  The GPU's rasterising time is then 10.04 ns a clock plus
   1.46 us a descriptor.  Off by default, and COSTLY: Core1 walks every pixel the GPU will draw,
   so the frame takes about twice as long.  For a measurement run only. */
void zzgl_set_walk_stats(int on);                /* requires build flag ZZGL_ENABLE_WALK_MODEL=1 (GPL diagnostics) */
/* 0.8.7 (ZZHalfLife build 87): a batch (one Begin/End) whose vertices are ALL outside the same
   side of the view volume - left, right, bottom, top, or behind the near plane - is dropped at
   zzglEnd, before any of its triangles is copied, projected, culled and clipped one by one to
   find the same thing.  Exact: such a batch never drew a pixel (a triangle whose three vertices
   are on the outer side of a plane has no point on its inner side).  On the A4000 on 15/11 the
   decals of c1a1b were 4120 triangles a frame, of which 303 reached the screen, for 8,7 ms of
   Core1.  Off by default. */
void zzgl_set_batch_reject(int on);
/* the test behind it, checked where it runs: as compiled, against a second way of writing it,
   on 4000 batches made on the spot (*batches), of which *outside are outside.  Returns the
   batches on which the two disagree: must be 0. */
u32 zzgl_batch_reject_check(u32 *batches, u32 *outside);
/* 0.8.8 (ZZHalfLife build 92): the back faces of the batches whose origin is in origin_mask (bit n
   = origin n, zzgl_origin_set) are dropped on the screen position of their vertices, worked out
   once per vertex, before the triangle is copied, clamped and projected.  Same test, same
   arithmetic as the one draw_triangle makes afterwards; a triangle with a vertex behind the near
   plane is left to it.  What is sent to the GPU does not change.  Off by default. */
void zzgl_set_early_cull(int on, u32 origin_mask);
/* checked where it runs: positions and verdicts against the triangle path's, on *tris triangles
   made on the spot (of which *culled are back faces); returns those that differ: must be 0. */
u32 zzgl_early_cull_check(u32 *tris, u32 *culled);
u32 zzgl_early_cull_check_detail(u32 *nan_two_ways);   /* 0.8.9: after the check: tests that failed (1 near plane, 2 position, 4 area, 8 verdict); triangles where a NaN had two spellings (allowed) */
u32 zzgl_early_culled(void);                       /* triangles it dropped since the start */
void zzgl_set_origin_stats(int on);
void zzgl_set_clear_split(int on);              /* split full-screen colour/depth clears for synchronous timing */
void zzgl_set_preclear_hook(int (*fn)(void));  /* callback returns 1 if the first black colour clear is already done */
extern u32 zzgl_clr_us[2], zzgl_clr_n[2], zzgl_preclear_hits;
void zzgl_set_pipelined(int on);                   /* ZZHalfLife 0.8.0: deferred mode, frame N+1 begins while N is still queued (librixgpu rix_frame_set_pipelined); applied when a frame opens with nothing pending */
void zzgl_set_alpha4444(int on);                   /* ZZHalfLife 0.7.8: RGBA uploads with intermediate alpha levels stored as RGBA4444 (gradient decals, soft sprites); set once the init test passed */
u32  zzgl_tex4444_count(void);                     /* ZZHalfLife 0.7.8: uploads stored as RGBA4444 since the start */
void zzgl_set_alpha_textures(int on);              /* 0.2: alpha-keyed RGBA uploads stored as RGBA5551 (TMU alpha for the alpha test) instead of RGB565; set before the uploads (ZZQuake2RIX: zz_alpha, vid_restart re-uploads) */
void zzgl_set_texel_bias(float texels);           /* 0.3: shift of the sample point in texels (0 = none); ZZQuake2RIX sets +0.25 for 2D (rix_texbias2d) */
void zzgl_set_target(const zzgl_target *t);        /* before the first draw of a frame */
int  zzgl_swap(void);                              /* ends the frame (frame fence); returns the librixgpu error or 0 */
int  zzgl_frame_open(void);                        /* 1 while a frame is being built */
const rix_frame *zzgl_frame(void);                 /* statistics of the frame being built */
u32  zzgl_tris_frame(void);                        /* descriptors emitted in the current/last frame */

/* ---- the qgl* functions (ref_gl binds them by pointer in qgl.h: zzq2_qgl.c assigns these) ---- */
void zzglAlphaFunc(u32 func, float ref);
void zzglBegin(u32 mode);
void zzglBindTexture(u32 target, u32 texnum);
void zzglBlendFunc(u32 sfactor, u32 dfactor);
void zzglClear(u32 mask);
void zzglClearColor(float r, float g, float b, float a);
void zzglClearDepth(double d);
void zzglColor3f(float r, float g, float b);
void zzglColor3fv(const float *v);
void zzglColor3ubv(const unsigned char *v);
void zzglColor4f(float r, float g, float b, float a);
void zzglColor4fv(const float *v);
void zzglColor4ubv(const unsigned char *v);
void zzglColorMask(u32 r, u32 g, u32 b, u32 a);
void zzglCullFace(u32 mode);
void zzglDeleteTextures(s32 n, const u32 *textures);
void zzglDepthFunc(u32 func);
void zzglDepthMask(u32 flag);
void zzglDepthRange(double zNear, double zFar);
void zzglDisable(u32 cap);
void zzglDrawBuffer(u32 mode);
void zzglEnable(u32 cap);
void zzglEnd(void);
void zzglEndIndexed(const unsigned char *idx, u32 n);   /* 0.8.10: ends a Begin(ZGL_TRIANGLES) whose vertices were given once each; n indices, three a triangle */
void zzglFinish(void);
void zzglFlush(void);
void zzglFrontFace(u32 mode);
void zzglFrustum(double l, double r, double b, double t, double n, double f);
void zzglGenTextures(s32 n, u32 *textures);
u32  zzglGetError(void);
void zzglGetFloatv(u32 pname, float *params);
void zzglGetIntegerv(u32 pname, s32 *params);
const unsigned char *zzglGetString(u32 name);
void zzglHint(u32 target, u32 mode);
void zzglLoadIdentity(void);
void zzglLoadMatrixf(const float *m);
void zzglMatrixMode(u32 mode);
void zzglMultMatrixf(const float *m);
void zzglOrtho(double l, double r, double b, double t, double n, double f);
void zzglPolygonMode(u32 face, u32 mode);
void zzglPopMatrix(void);
void zzglPushMatrix(void);
void zzglReadBuffer(u32 mode);
void zzglReadPixels(s32 x, s32 y, s32 w, s32 h, u32 format, u32 type, void *pixels);
void zzglRotatef(float angle, float x, float y, float z);
void zzglScalef(float x, float y, float z);
void zzglScissor(s32 x, s32 y, s32 w, s32 h);
void zzglShadeModel(u32 mode);
void zzglPointSize(float size);                  /* ZZMiniGL A8.23: GL_POINTS are squares of this size in whole pixels (rounded down since A8.24, 1..16) */
void zzglTexCoord2f(float s, float t);
void zzglTexCoord2fv(const float *v);
void zzglTexEnvf(u32 target, u32 pname, float param);
void zzglTexImage2D(u32 target, s32 level, s32 internalformat, s32 width, s32 height, s32 border, u32 format, u32 type, const void *pixels);
void zzglTexParameterf(u32 target, u32 pname, float param);
void zzglTexSubImage2D(u32 target, s32 level, s32 xoffset, s32 yoffset, s32 width, s32 height, u32 format, u32 type, const void *pixels);
void zzglTranslatef(float x, float y, float z);
void zzglVertex2f(float x, float y);
void zzglVertex3f(float x, float y, float z);
void zzglVertex3fv(const float *v);
void zzglViewport(s32 x, s32 y, s32 w, s32 h);


/* ==== ZZMiniGL (A3 .. A6): texture coordinate generation, normals, dithering, client arrays ==== */
#define ZGL_DITHER 0x0BD0
#define ZGL_NORMALIZE 0x0BA1
#define ZGL_LIGHTING 0x0B50
#define ZGL_COLOR_MATERIAL 0x0B57
#define ZGL_LIGHT0 0x4000
#define ZGL_POINT_SMOOTH 0x0B10
#define ZGL_LINE_SMOOTH 0x0B20
#define ZGL_POLYGON_SMOOTH 0x0B41
#define ZGL_TEXTURE_GEN_S 0x0C60
#define ZGL_TEXTURE_GEN_T 0x0C61
#define ZGL_TEXTURE_GEN_R 0x0C62
#define ZGL_TEXTURE_GEN_Q 0x0C63
#define ZGL_S 0x2000
#define ZGL_T 0x2001
#define ZGL_R 0x2002
#define ZGL_Q 0x2003
#define ZGL_TEXTURE_GEN_MODE 0x2500
#define ZGL_OBJECT_PLANE 0x2501
#define ZGL_EYE_PLANE 0x2502
#define ZGL_EYE_LINEAR 0x2400
#define ZGL_OBJECT_LINEAR 0x2401
#define ZGL_SPHERE_MAP 0x2402
/* vendor extension ZZ_texgen_frame (A4R): glTexGeni(GL_S or GL_T, GL_TEXGEN_MODELVIEW_ZZ, 1) copies the
   CURRENT modelview and its normal matrix; SPHERE_MAP and EYE_LINEAR generation of BOTH coordinates
   then use that frame instead of the modelview in effect when vertices are specified, until
   glTexGeni(..., GL_TEXGEN_MODELVIEW_ZZ, 0). */
#define ZGL_TEXGEN_MODELVIEW_ZZ 0x5A5A
/* packed vertex format of the array entry points (same bits as the wire protocol's ZZ_VF_*) */
#define ZGL_VF_POS3   1u
#define ZGL_VF_NORMAL 2u
#define ZGL_VF_UV     4u
#define ZGL_VF_COLOR  8u
#define ZZGL_MAX_ARRAY_VERTS 3072u   /* resident client arrays (GLLockArrays); A4R: keeps the Core1 BSS in the cached section */
void zzglNormal3f(float x, float y, float z);
void zzglTexGeni(u32 coord, u32 pname, s32 param);              /* GL_TEXTURE_GEN_MODE: SPHERE_MAP, OBJECT_LINEAR, EYE_LINEAR; ZZ_texgen_frame */
void zzglTexGenfv(u32 coord, u32 pname, const float *v);        /* GL_OBJECT_PLANE / GL_EYE_PLANE (eye plane x inverse modelview, as the spec) */
/* resident client arrays: the packed vertices (fmt bits ZGL_VF_*, floats in that order) are kept until
   zzgl_arrays_unlock(); zzgl_draw_elements draws any primitive mode from 16-bit indices into them, each
   referenced vertex transformed once per call.  Returns 0 or a GL error. */
u32  zzgl_arrays_load(u32 fmt, u32 count, const float *packed);
void zzgl_arrays_unlock(void);
u32  zzgl_resident_count(void);
void zzgl_draw_elements(u32 mode, u32 count, const unsigned short *idx);
/* non-resident client arrays: the vertices travel with the call (any primitive mode, any length) */
void zzgl_draw_arrays(u32 mode, u32 fmt, u32 count, const float *packed);
/* diagnostic: FNV-1a of the last texture uploaded to DDR, little-endian RGB565 bytes */
u32 zzgl_texture_debug_fnv(void);
/* ==== end ZZMiniGL ==== */

/* statistics (reset by zzgl_swap) */
typedef struct {
    u32 begins, verts, tris_in, tris_out, binds, binds_skipped, tex_uploads, tex_subuploads, clipped, culled, state_words, errors;
    u32 upload_us;
    u32 end_ticks;       /* 0.7: global-timer ticks spent in zzglEnd (vertex batch -> descriptors in the chunk, binds included) */
    u32 bind_ticks;      /* 0.7: ticks spent in rix_frame_texture (binds, BINDLIST fence waits included) */
    u32 flush_ticks;     /* 0.7: ticks spent in zzglBegin's state flush (registers + binds) */
    u32 desc_ticks;      /* 0.7.1, profile on only: ticks in rix_tri_desc (descriptor computation) */
    u32 emit_ticks;      /* 0.7.1, profile on only: ticks in rix_frame_tri (descriptor written to the chunk, rollovers included) */
    u32 end_bwait_us;    /* 0.7.1: bind fence waits that happened inside zzglEnd (a full chunk closed behind a pending bind) */
    u32 tex_versions;    /* 0.7.4: texture updates served by a new version of the pages (deferred mode, versioning on) */
    u32 tex_version_us;  /* 0.7.4: time spent making those versions (allocation + copy + clean) */
    u32 tex_version_fail;/* 0.7.4: versions refused (pool or retired list full): a drain was made instead */
    u32 open_tick;       /* ZZHalfLife 0.8.0: global timer when the frame opened (its first draw or clear) */
    u32 zclear_skipped;  /* 0.8.6: depth clears not sent to the GPU: the frame's depth buffer was handed over clean */
    u32 zclear_gpu;      /* 0.8.6: depth clears the GPU made */
    /* ZZMiniGL */
    u32 xform_ticks;     /* ticks spent transforming array vertices (MVP, normal matrix, texgen) */
    u32 setup_ticks;     /* ticks spent in array triangle setup (clip, project, descriptor, list write) */
    u32 verts_xformed;   /* vertices transformed (resident ones once per draw call) */
    u32 texgen_fixed;    /* 1 while a frozen texgen frame is in use */
    u32 idx_tris;        /* triangles drawn by zzgl_draw_elements */
    u32 fast_clip_tris;  /* triangles already inside the screen (no screen clipper) */
    u32 screen_clip_tris;/* triangles passed to the screen clipper */
} zzgl_stats;
const zzgl_stats *zzgl_get_stats(void);
/* 0.7.1: 1 = split zzglEnd's time (two more timer reads per descriptor); 0 = off (default) */
void zzgl_set_profile(int on);
/* 0.7.2: 1 = librixgpu deferred submission (rix_frame_set_deferred) from the next frame on; 0 = direct (default) */
void zzgl_set_deferred(int on);
/* 0.7.4: 1 = in deferred mode, glTexSubImage2D of a texture whose last bind has not executed yet writes into a
   NEW copy of its pages (the old pages are freed once the frame's fence has passed) instead of draining the
   queue; 0 = drain (0.7.2 behaviour, default) */
void zzgl_set_tex_versioning(int on);
/* 0.7.5: 1 = the chunk holding a depth clear is closed (submitted) at once; zzgl_pump = rix_frame_pump of the
   open frame, for long Core1 work outside the draws */
void zzgl_set_clear_flush(int on);
void zzgl_pump(void);

/* 0.8.3 (ZZHalfLife build 54): called at most once per upload when the texture pool cannot fit it,
   before the image is halved or refused; the caller frees what it no longer needs and the allocation
   is retried.  NULL = nothing to call (default).  zzgl_oom_calls = how many times it was called. */
void zzgl_set_oom_cb(void (*cb)(void));
u32 zzgl_oom_calls(void);

/* 0.8.2: the i-th GL_INVALID_ENUM noted (function, value, times); 0 past the last */
int zzgl_bad_enum(u32 i, const char **fn, u32 *v, u32 *n);

#endif
