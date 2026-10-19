/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2rix_qgl.c - ZZQuake2RIX: the qgl* bindings of ref_gl, on ZZGL
 * (zzgl/zzgl.c: the OpenGL 1.1 subset of ref_gl on librixgpu, the
 * RasterIX GPU of the ZZ9000 G4C bitstream).
 *
 * Derived from zzq2tgl_qgl.c of ZZQuake2TGL 0.4: the same 52 pointers,
 * the same wrappers where ref_gl needs an adaptation, TinyGL replaced by
 * ZZGL.  Every pointer gets a wrapper with the exact OpenGL prototype
 * (the ZZGL entry points use plain C types): no call through a pointer
 * of another type.
 *
 *   - GL_POLYGON, glOrtho, glDepthRange, glScissor, glAlphaFunc,
 *     GL_ALPHA_TEST, glTexEnvf, glReadPixels: ZZGL has them;
 *   - colours are clamped to [0, 1] (Quake II lights models up to
 *     about 2; OpenGL clamps);
 *   - levels above 0 of glTexImage2D are counted and dropped (no
 *     mipmaps on the TMU);
 *   - vertex arrays and glPointSize: never used by ref_gl without the
 *     extensions (gl_vertex_arrays 0): counted, ignored.
 *
 * Filtering: ref_gl sets GL_TEXTURE_MAG_FILTER on every texture
 * (GL_LINEAR on the lightmaps, gl_texturemode on the others, GL_LINEAR
 * by default).  The TMU filters bilinearly for free, so the cvar
 * rix_filter (archived; 0.3: renamed from zz_filter, which ZZQuake2TGL
 * keeps in the SAME config.cfg with another meaning and default 1)
 * defaults to 2:
 *     0  nearest everywhere
 *     1  lightmaps GL_LINEAR, other textures nearest (the ZZQuake2TGL
 *        default, for comparisons)
 *     2  GL_LINEAR wherever ref_gl asks for it (DEFAULT on RasterIX)
 * and rix_filter2d (archived, 0.3) for what is drawn under glOrtho (HUD,
 * console, menus, pics):
 *     0  nearest (DEFAULT: sharp text, texel for pixel)
 *     1  as rix_filter
 * and rix_texbias2d (archived, 0.3): 1 (default) = 2D samples a quarter
 * texel past the texel centre, 0 = exact centre
 *
 * Extensions: none is advertised.  ref_gl then uses its single texture
 * unit path (lightmaps in a second blended pass), no compiled vertex
 * arrays, no point parameters, no paletted textures.  The extension
 * pointers stay NULL.
 *
 * ASCII only.
 */

#include "ref_gl/gl_local.h"
#include "zzq2rix_version.h"
#include "zzgl.h"

/* ------------------------------------------------------------------
   Counters (end-of-run report, zzq2rix_gpu.c)
   ------------------------------------------------------------------ */
unsigned zzgl_n_teximage, zzgl_n_teximage_mip, zzgl_n_texsub;
unsigned zzgl_n_polygon, zzgl_n_scissor, zzgl_n_depthfunc_other;
unsigned zzgl_n_texenv_other, zzgl_n_arrays, zzgl_n_pointsize;

/* ------------------------------------------------------------------
   Wrappers
   ------------------------------------------------------------------ */
static GLfloat zz_c01(GLfloat v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

/* 0.8.2: category of each batch for the performance reports (zzq2rix_gpu.c).  zzq2_rix_phase is
   set by zzq2_glue.c around the ref_gl functions it interposes (1 R_DrawWorld, 2 R_DrawBrushModel,
   5 R_DrawAliasModel, 4 R_DrawAlphaSurfaces, 0 elsewhere); the lightmap pass is recognised by its
   blend (GL_ZERO, GL_SRC_COLOR: R_BlendLightmaps), 2D by glOrtho.  Measurement only. */
extern volatile int zzq2_rix_phase;
extern void zz_rix_cat_begin(void);
extern void zz_rix_cat_end(int category);
static int zz_2d;
static int zz_blend_on;
static GLenum zz_blend_s = GL_ONE, zz_blend_d = GL_ZERO;
static int zz_category(void)
{
    if (zz_2d) return 6;
    if (zz_blend_on && zz_blend_s == GL_ZERO && zz_blend_d == GL_SRC_COLOR) return 1;
    switch (zzq2_rix_phase) {
    case 1: return 0;
    case 2: return 2;
    case 5: return 3;
    case 4: return 4;
    default: return 5;
    }
}

static void APIENTRY zz_AlphaFunc(GLenum func, GLclampf ref) { zzglAlphaFunc(func, ref); }
static void APIENTRY zz_ArrayElement(GLint i) { (void)i; zzgl_n_arrays++; }
static void APIENTRY zz_Begin(GLenum mode)
{
    if (mode == GL_POLYGON) zzgl_n_polygon++;
    zz_rix_cat_begin();
    zzglBegin(mode);
}
static void APIENTRY zz_BlendFunc(GLenum s, GLenum d) { zz_blend_s = s; zz_blend_d = d; zzglBlendFunc(s, d); }
static void APIENTRY zz_Clear(GLbitfield mask) { zzglClear(mask); }
static void APIENTRY zz_ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { zzglClearColor(r, g, b, a); }
static void APIENTRY zz_Color3f(GLfloat r, GLfloat g, GLfloat b) { zzglColor4f(zz_c01(r), zz_c01(g), zz_c01(b), 1.0f); }
static void APIENTRY zz_Color3fv(const GLfloat *v) { zzglColor4f(zz_c01(v[0]), zz_c01(v[1]), zz_c01(v[2]), 1.0f); }
static void APIENTRY zz_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { zzglColor4f(zz_c01(r), zz_c01(g), zz_c01(b), zz_c01(a)); }
static void APIENTRY zz_Color4fv(const GLfloat *v) { zzglColor4f(zz_c01(v[0]), zz_c01(v[1]), zz_c01(v[2]), zz_c01(v[3])); }
static void APIENTRY zz_Color4ubv(const GLubyte *v) { zzglColor4ubv(v); }
static void APIENTRY zz_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{ (void)size; (void)type; (void)stride; (void)p; zzgl_n_arrays++; }
static void APIENTRY zz_CullFace(GLenum mode) { zzglCullFace(mode); }
static void APIENTRY zz_DeleteTextures(GLsizei n, const GLuint *t) { zzglDeleteTextures(n, t); }
static void APIENTRY zz_DepthFunc(GLenum func)
{
    if (func != GL_LEQUAL && func != GL_LESS && func != GL_GEQUAL) zzgl_n_depthfunc_other++;
    zzglDepthFunc(func);
}
static void APIENTRY zz_DepthMask(GLboolean flag) { zzglDepthMask(flag ? 1u : 0u); }
static void APIENTRY zz_DepthRange(GLclampd n, GLclampd f) { zzglDepthRange(n, f); }
static void APIENTRY zz_Disable(GLenum cap) { if (cap == GL_POINT_SMOOTH) return; if (cap == GL_BLEND) zz_blend_on = 0; zzglDisable(cap); }
static void APIENTRY zz_DrawBuffer(GLenum mode) { zzglDrawBuffer(mode); }
static void APIENTRY zz_Enable(GLenum cap) { if (cap == GL_POINT_SMOOTH) return; if (cap == GL_BLEND) zz_blend_on = 1; zzglEnable(cap); }
static void APIENTRY zz_EnableClientState(GLenum a) { (void)a; zzgl_n_arrays++; }
static void APIENTRY zz_End(void) { zzglEnd(); zz_rix_cat_end(zz_category()); }
static void APIENTRY zz_Finish(void) { zzglFinish(); }
/* 0.3: 2D (glOrtho: HUD, console, menus, pics) vs 3D (glFrustum), for rix_filter2d (zz_2d above) */
static void zz_apply_filter(void);
static cvar_t *zz_texbias2d;
static void zz_set_mode_2d(int on)
{
    zz_2d = on;
    if (!zz_texbias2d)
        zz_texbias2d = ri.Cvar_Get("rix_texbias2d", "1", CVAR_ARCHIVE);
    /* 0.3: 2D samples a quarter texel past the texel centre (see zzgl_set_texel_bias) */
    zzgl_set_texel_bias((on && zz_texbias2d->value != 0.0f) ? 0.25f : 0.0f);
    zz_apply_filter();
}
static void APIENTRY zz_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{ zzglFrustum(l, r, b, t, n, f); zz_set_mode_2d(0); }
static GLenum APIENTRY zz_GetError(void) { return zzglGetError(); }
static void APIENTRY zz_GetFloatv(GLenum pname, GLfloat *params) { zzglGetFloatv(pname, params); }
static const GLubyte *APIENTRY zz_GetString(GLenum name)
{
    switch (name) {
    case GL_VENDOR:     return (const GLubyte *)"ZZ9000 Core1";
    case GL_RENDERER:   return (const GLubyte *)"RasterIX G4C (ZZGL 0.1, librixgpu 1.2.1)";
    case GL_VERSION:    return (const GLubyte *)"1.1 ZZQuake2RIX " ZZQ2RIX_VERSION;
    case GL_EXTENSIONS: return (const GLubyte *)"";
    default:            return (const GLubyte *)"";
    }
}
static void APIENTRY zz_LoadIdentity(void) { zzglLoadIdentity(); }
static void APIENTRY zz_LoadMatrixf(const GLfloat *m) { zzglLoadMatrixf(m); }
static void APIENTRY zz_MatrixMode(GLenum m) { zzglMatrixMode(m); }
static void APIENTRY zz_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{ zzglOrtho(l, r, b, t, n, f); zz_set_mode_2d(1); }
static void APIENTRY zz_PointSize(GLfloat s) { (void)s; zzgl_n_pointsize++; }
static void APIENTRY zz_PolygonMode(GLenum face, GLenum mode) { zzglPolygonMode(face, mode); }
static void APIENTRY zz_PopMatrix(void) { zzglPopMatrix(); }
static void APIENTRY zz_PushMatrix(void) { zzglPushMatrix(); }
static void APIENTRY zz_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels)
{ zzglReadPixels(x, y, w, h, format, type, pixels); }
static void APIENTRY zz_Rotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z) { zzglRotatef(a, x, y, z); }
static void APIENTRY zz_Scalef(GLfloat x, GLfloat y, GLfloat z) { zzglScalef(x, y, z); }
static void APIENTRY zz_Scissor(GLint x, GLint y, GLsizei w, GLsizei h) { zzgl_n_scissor++; zzglScissor(x, y, w, h); }
static void APIENTRY zz_ShadeModel(GLenum mode) { zzglShadeModel(mode); }
static void APIENTRY zz_TexCoord2f(GLfloat s, GLfloat t) { zzglTexCoord2f(s, t); }
static void APIENTRY zz_TexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE) return;
    if ((GLenum)param != GL_MODULATE && (GLenum)param != GL_REPLACE) zzgl_n_texenv_other++;
    zzglTexEnvf(target, pname, param);
}
static void APIENTRY zz_TexImage2D(GLenum target, GLint level, GLint internalformat,
                                   GLsizei w, GLsizei h, GLint border, GLenum format,
                                   GLenum type, const GLvoid *pixels)
{
    if (level != 0) { zzgl_n_teximage_mip++; return; }
    zzgl_n_teximage++;
    zzglTexImage2D(target, level, internalformat, w, h, border, format, type, pixels);
}
static void APIENTRY zz_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y,
                                      GLsizei w, GLsizei h, GLenum format,
                                      GLenum type, const GLvoid *pixels)
{
    if (level != 0) { zzgl_n_teximage_mip++; return; }
    zzgl_n_texsub++;
    zzglTexSubImage2D(target, level, x, y, w, h, format, type, pixels);
}
static void APIENTRY zz_Translatef(GLfloat x, GLfloat y, GLfloat z) { zzglTranslatef(x, y, z); }
static void APIENTRY zz_Vertex2f(GLfloat x, GLfloat y) { zzglVertex2f(x, y); }
static void APIENTRY zz_Vertex3f(GLfloat x, GLfloat y, GLfloat z) { zzglVertex3f(x, y, z); }
static void APIENTRY zz_Vertex3fv(const GLfloat *v) { zzglVertex3fv(v); }
static void APIENTRY zz_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{ (void)size; (void)type; (void)stride; (void)p; zzgl_n_arrays++; }
static void APIENTRY zz_Viewport(GLint x, GLint y, GLsizei w, GLsizei h) { zzglViewport(x, y, w, h); }

/* ------------------------------------------------------------------
   Texture filtering (rix_filter, see the header)
   ------------------------------------------------------------------ */
#define ZZ_MAXTEX ZZGL_MAX_TEXTURES
static unsigned char zz_tex_linear[ZZ_MAXTEX];   /* MAG filter asked: GL_LINEAR */
static unsigned char zz_tex_known[ZZ_MAXTEX];    /* MAG filter was set           */
static unsigned char zz_tex_applied[ZZ_MAXTEX];  /* 0 unknown, 1 nearest, 2 linear: what ZZGL holds */
static GLuint  zz_bound_tex;
static cvar_t *rix_filter;
static int     rix_filter_shown = -1;
static cvar_t *rix_filter2d;
static int     rix_filter2d_shown = -1;

static void zz_apply_filter(void)
{
    int mode, lin;
    unsigned char want;
    GLuint t = zz_bound_tex;
    if (!rix_filter)
        rix_filter = ri.Cvar_Get("rix_filter", "2", CVAR_ARCHIVE);
    mode = (int)rix_filter->value;
    if (mode < 0) mode = 0;
    if (mode > 2) mode = 2;
    if (mode != rix_filter_shown) {
        rix_filter_shown = mode;
        ri.Con_Printf(PRINT_ALL, "ZZQuake2RIX %s: rix_filter %d (%s)\n", ZZQ2RIX_VERSION, mode,
                      mode == 0 ? "nearest everywhere" :
                      mode == 1 ? "lightmaps GL_LINEAR, textures nearest" :
                                  "GL_LINEAR where ref_gl asks (TMU bilinear)");
    }
    if (!rix_filter2d)
        rix_filter2d = ri.Cvar_Get("rix_filter2d", "0", CVAR_ARCHIVE);
    if ((int)rix_filter2d->value != rix_filter2d_shown) {
        rix_filter2d_shown = (int)rix_filter2d->value;
        ri.Con_Printf(PRINT_ALL, "ZZQuake2RIX %s: rix_filter2d %d (%s)\n", ZZQ2RIX_VERSION, rix_filter2d_shown,
                      rix_filter2d_shown ? "2D follows rix_filter" : "2D nearest: HUD, console, menus");
    }
    if (t >= ZZ_MAXTEX) return;
    /* OpenGL default MAG filter: GL_LINEAR */
    lin = zz_tex_known[t] ? zz_tex_linear[t] : 1;
    if (mode == 0) lin = 0;
    else if (mode == 1 && !(t >= TEXNUM_LIGHTMAPS && t < TEXNUM_SCRAPS)) lin = 0;
    /* 0.3: 2D drawn texel for pixel (HUD, console font, menus): nearest by default, the TMU's
       bilinear sampling would blend each texel with its neighbours (rix_filter2d 1: as 3D) */
    if (zz_2d && rix_filter2d->value == 0.0f) lin = 0;
    want = lin ? 2u : 1u;
    if (zz_tex_applied[t] != want) {
        /* ZZGL reloads the TMU after a filter change: only when it really changes */
        zzglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, lin ? (float)GL_LINEAR : (float)GL_NEAREST);
        zz_tex_applied[t] = want;
    }
}

static void APIENTRY zz_BindTexture(GLenum target, GLuint tex)
{
    zzglBindTexture(target, tex);
    zz_bound_tex = tex;
    zz_apply_filter();
}

/* Wrap modes: the TMU repeats; ref_gl never asks for anything else.
   MAG filter: recorded for the bound texture, applied through rix_filter. */
static void APIENTRY zz_TexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    if (target != GL_TEXTURE_2D || pname != GL_TEXTURE_MAG_FILTER)
        return;
    if (zz_bound_tex < ZZ_MAXTEX) {
        zz_tex_known[zz_bound_tex] = 1;
        zz_tex_linear[zz_bound_tex] = ((GLenum)param == GL_LINEAR);
    }
    zz_apply_filter();
}

/* ------------------------------------------------------------------
   The pointers ref_gl uses (and nothing else)
   ------------------------------------------------------------------ */
void ( APIENTRY * qglAlphaFunc )(GLenum func, GLclampf ref);
void ( APIENTRY * qglArrayElement )(GLint i);
void ( APIENTRY * qglBegin )(GLenum mode);
void ( APIENTRY * qglBindTexture )(GLenum target, GLuint texture);
void ( APIENTRY * qglBlendFunc )(GLenum sfactor, GLenum dfactor);
void ( APIENTRY * qglClear )(GLbitfield mask);
void ( APIENTRY * qglClearColor )(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void ( APIENTRY * qglColor3f )(GLfloat red, GLfloat green, GLfloat blue);
void ( APIENTRY * qglColor3fv )(const GLfloat *v);
void ( APIENTRY * qglColor4f )(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void ( APIENTRY * qglColor4fv )(const GLfloat *v);
void ( APIENTRY * qglColor4ubv )(const GLubyte *v);
void ( APIENTRY * qglColorPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void ( APIENTRY * qglCullFace )(GLenum mode);
void ( APIENTRY * qglDeleteTextures )(GLsizei n, const GLuint *textures);
void ( APIENTRY * qglDepthFunc )(GLenum func);
void ( APIENTRY * qglDepthMask )(GLboolean flag);
void ( APIENTRY * qglDepthRange )(GLclampd zNear, GLclampd zFar);
void ( APIENTRY * qglDisable )(GLenum cap);
void ( APIENTRY * qglDrawBuffer )(GLenum mode);
void ( APIENTRY * qglEnable )(GLenum cap);
void ( APIENTRY * qglEnableClientState )(GLenum array);
void ( APIENTRY * qglEnd )(void);
void ( APIENTRY * qglFinish )(void);
void ( APIENTRY * qglFrustum )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
GLenum ( APIENTRY * qglGetError )(void);
void ( APIENTRY * qglGetFloatv )(GLenum pname, GLfloat *params);
const GLubyte * ( APIENTRY * qglGetString )(GLenum name);
void ( APIENTRY * qglLoadIdentity )(void);
void ( APIENTRY * qglLoadMatrixf )(const GLfloat *m);
void ( APIENTRY * qglMatrixMode )(GLenum mode);
void ( APIENTRY * qglOrtho )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void ( APIENTRY * qglPointSize )(GLfloat size);
void ( APIENTRY * qglPolygonMode )(GLenum face, GLenum mode);
void ( APIENTRY * qglPopMatrix )(void);
void ( APIENTRY * qglPushMatrix )(void);
void ( APIENTRY * qglReadPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
void ( APIENTRY * qglRotatef )(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void ( APIENTRY * qglScalef )(GLfloat x, GLfloat y, GLfloat z);
void ( APIENTRY * qglScissor )(GLint x, GLint y, GLsizei width, GLsizei height);
void ( APIENTRY * qglShadeModel )(GLenum mode);
void ( APIENTRY * qglTexCoord2f )(GLfloat s, GLfloat t);
void ( APIENTRY * qglTexEnvf )(GLenum target, GLenum pname, GLfloat param);
void ( APIENTRY * qglTexImage2D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void ( APIENTRY * qglTexParameterf )(GLenum target, GLenum pname, GLfloat param);
void ( APIENTRY * qglTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void ( APIENTRY * qglTranslatef )(GLfloat x, GLfloat y, GLfloat z);
void ( APIENTRY * qglVertex2f )(GLfloat x, GLfloat y);
void ( APIENTRY * qglVertex3f )(GLfloat x, GLfloat y, GLfloat z);
void ( APIENTRY * qglVertex3fv )(const GLfloat *v);
void ( APIENTRY * qglVertexPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void ( APIENTRY * qglViewport )(GLint x, GLint y, GLsizei width, GLsizei height);

/* extensions: never advertised, never bound */
void ( APIENTRY * qglLockArraysEXT) (int, int);
void ( APIENTRY * qglUnlockArraysEXT) (void);
void ( APIENTRY * qglPointParameterfEXT)( GLenum param, GLfloat value );
void ( APIENTRY * qglPointParameterfvEXT)( GLenum param, const GLfloat *value );
void ( APIENTRY * qglColorTableEXT)( int, int, int, int, int, const void * );
void ( APIENTRY * qglMTexCoord2fSGIS)( GLenum, GLfloat, GLfloat );
void ( APIENTRY * qglSelectTextureSGIS)( GLenum );

qboolean QGL_Init(const char *dllname)
{
    (void)dllname;
    qglAlphaFunc         = zz_AlphaFunc;
    qglArrayElement      = zz_ArrayElement;
    qglBegin             = zz_Begin;
    qglBindTexture       = zz_BindTexture;
    qglBlendFunc         = zz_BlendFunc;
    qglClear             = zz_Clear;
    qglClearColor        = zz_ClearColor;
    qglColor3f           = zz_Color3f;
    qglColor3fv          = zz_Color3fv;
    qglColor4f           = zz_Color4f;
    qglColor4fv          = zz_Color4fv;
    qglColor4ubv         = zz_Color4ubv;
    qglColorPointer      = zz_ColorPointer;
    qglCullFace          = zz_CullFace;
    qglDeleteTextures    = zz_DeleteTextures;
    qglDepthFunc         = zz_DepthFunc;
    qglDepthMask         = zz_DepthMask;
    qglDepthRange        = zz_DepthRange;
    qglDisable           = zz_Disable;
    qglDrawBuffer        = zz_DrawBuffer;
    qglEnable            = zz_Enable;
    qglEnableClientState = zz_EnableClientState;
    qglEnd               = zz_End;
    qglFinish            = zz_Finish;
    qglFrustum           = zz_Frustum;
    qglGetError          = zz_GetError;
    qglGetFloatv         = zz_GetFloatv;
    qglGetString         = zz_GetString;
    qglLoadIdentity      = zz_LoadIdentity;
    qglLoadMatrixf       = zz_LoadMatrixf;
    qglMatrixMode        = zz_MatrixMode;
    qglOrtho             = zz_Ortho;
    qglPointSize         = zz_PointSize;
    qglPolygonMode       = zz_PolygonMode;
    qglPopMatrix         = zz_PopMatrix;
    qglPushMatrix        = zz_PushMatrix;
    qglReadPixels        = zz_ReadPixels;
    qglRotatef           = zz_Rotatef;
    qglScalef            = zz_Scalef;
    qglScissor           = zz_Scissor;
    qglShadeModel        = zz_ShadeModel;
    qglTexCoord2f        = zz_TexCoord2f;
    qglTexEnvf           = zz_TexEnvf;
    qglTexImage2D        = zz_TexImage2D;
    qglTexParameterf     = zz_TexParameterf;
    qglTexSubImage2D     = zz_TexSubImage2D;
    qglTranslatef        = zz_Translatef;
    qglVertex2f          = zz_Vertex2f;
    qglVertex3f          = zz_Vertex3f;
    qglVertex3fv         = zz_Vertex3fv;
    qglVertexPointer     = zz_VertexPointer;
    qglViewport          = zz_Viewport;

    qglLockArraysEXT = 0; qglUnlockArraysEXT = 0;
    qglPointParameterfEXT = 0; qglPointParameterfvEXT = 0;
    qglColorTableEXT = 0; qglMTexCoord2fSGIS = 0; qglSelectTextureSGIS = 0;
    return true;
}

void QGL_Shutdown(void)
{
}

/* gl_log: no log file on Core1 */
void GLimp_EnableLogging(qboolean enable) { (void)enable; }
void GLimp_LogNewFrame(void) { }
