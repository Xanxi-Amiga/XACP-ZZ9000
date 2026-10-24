/* zzgl.h - ZZGL 0.7.6 - the immediate-mode OpenGL subset Quake II's ref_gl uses,
 * on librixgpu 1.2 (RasterIX G4C).
 *
 * Scope = the 52 qgl* entry points of ZZQuake2TGL's zzq2tgl_qgl.c (ref_gl of
 * quake2generic 84f34ed5, TinyGL build), reimplemented on the GPU:
 *   matrices        MatrixMode LoadIdentity LoadMatrixf MultMatrixf PushMatrix PopMatrix
 *                   Rotatef Translatef Scalef Ortho Frustum Viewport DepthRange GetFloatv
 *   primitives      Begin End Vertex2f Vertex3f Vertex3fv TexCoord2f TexCoord2fv
 *                   Color3f Color3fv Color4f Color4fv Color3ubv Color4ubv
 *   state           Enable Disable DepthFunc DepthMask BlendFunc AlphaFunc CullFace
 *                   ShadeModel PolygonMode TexEnvf TexParameterf ColorMask Scissor
 *                   Clear ClearColor ClearDepth DrawBuffer ReadBuffer Hint
 *   textures        BindTexture TexImage2D TexSubImage2D DeleteTextures GenTextures
 *   misc            Finish Flush GetError GetString GetIntegerv ReadPixels
 *
 * What the hardware gives and what ZZGL does with it (BILAN / API.md rules):
 *   - one TMU, no texture memory: a bind streams the texture's pages (64x64: 16 us) and costs a
 *     pipeline drain (rule 11: the bind travels outside the frame chunks, as a short DDR list
 *     or a FIFO packet, and the next chunk waits for its fence; about 136 us on ROOM B, D2f);
 *     ref_gl's texture chains keep the bind count where a one-TMU card would have it;
 *   - RGB565 textures; levels > 0 of TexImage2D are ignored (no mipmaps); textures above
 *     256x256 are refused (ref_gl rounds down with gl_round_down, default 1);
 *   - alpha-keyed textures (RGBA with a 0/255 alpha, sprites, grates, pics): kept as RGB565
 *     plus an alpha-test flag until the RGBA5551 probe passes (0.1: the texel alpha is lost,
 *     the alpha test then acts on the vertex alpha only);
 *   - two-pass lightmaps (strategy A, ROOM D2): the lightmap textures (texnums 1024..1151 in
 *     ref_gl, 128x128) are ordinary textures, TexSubImage2D updates them in place
 *     (rix_tex_write_rect_rgb565);
 *   - depth: 16-bit window z = (z_ndc + 1) / 2 mapped through DepthRange; LESS/LEQUAL/GEQUAL...
 *     as RasterIX functions; gl_ztrick works (alternating range + GEQUAL);
 *   - blending: the RasterIX factor table (ZERO, ONE, DST_COLOR, SRC_COLOR, ONE_MINUS_*,
 *     SRC_ALPHA, ONE_MINUS_SRC_ALPHA, DST_ALPHA...) - validated on the Amiga: ONE,ONE,
 *     ZERO,SRC_COLOR, SRC_ALPHA,ONE_MINUS_SRC_ALPHA; the others are mapped, not yet probed;
 *   - no stencil (gl_shadows stays 0), no fog, no point parameters, no multitexture, no
 *     vertex arrays, no polygon offset: ref_gl takes its default paths for all of them;
 *   - the frame: ZZGL opens a librixgpu frame lazily at the first draw or clear after the
 *     previous zzgl_swap(), on the colour buffer the presenter hands out; zzgl_swap() ends it
 *     (frame fence) and publishes it; the clear flags of glClear() become the frame head's
 *     memsets (colour clear of the frame, depth clear or ZTRICK).
 *
 * Host test: host_tests/test_zzgl.c (through sim_rix's GPU model, holes and coverage).
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
#define ZGL_RGB 0x1907
#define ZGL_RGBA 0x1908
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
#define ZZGL_MAX_TEXTURES   2304u   /* ref_gl: TEXNUM_LIGHTMAPS 1024 (+128 lightmaps), TEXNUM_SCRAPS 1152, TEXNUM_IMAGES 1153 + MAX_GLTEXTURES 1024 = 2177; texnums are used directly as indices */
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
/* 0.7.6 (ZZOpenLara A2.17): n vertices of the open Begin/End at once; for each one the same values as
   zzglColor4ubv(rgba) + zzglTexCoord2f(s, t) + zzglVertex3f(x, y, z) (same arithmetic, same order),
   without three calls per vertex. Not an OpenGL entry point. */
typedef struct { float x, y, z, s, t; unsigned char rgba[4]; } zzgl_vtx;
void zzglVertexArray(const zzgl_vtx *v, u32 n);
void zzglViewport(s32 x, s32 y, s32 w, s32 h);

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
/* A2.13 (ZZOpenLaraRIX): 1 = a full-screen colour + depth clear is sent as two chunks, each waited (SYNC
   mode only: the waits are the GPU times, totals in zzgl_clr_us[colour, depth] / zzgl_clr_n); hook = the
   colour clear is skipped when it answers 1 (the frame's buffer is already black, RIXPRECLEAR) */
void zzgl_set_clear_split(int on);
void zzgl_set_preclear_hook(int (*fn)(void));
extern u32 zzgl_clr_us[2], zzgl_clr_n[2], zzgl_preclear_hits;

#endif
