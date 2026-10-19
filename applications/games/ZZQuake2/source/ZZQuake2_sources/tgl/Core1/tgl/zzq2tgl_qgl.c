/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2tgl_qgl.c - ZZQuake2TGL: the qgl* bindings of ref_gl, on TinyGL.
 *
 * Replaces ref_gl/qgl_system.c of quake2generic (not linked). Only the
 * pointers that ref_gl really uses are defined (see README_TGL.txt,
 * matrix). Most of them point straight at TinyGL; the others go
 * through a small wrapper, for one of these reasons:
 *
 *   - TinyGL has the call under another signature (glTexEnvi,
 *     glTexParameteri, glColor4f instead of glColor4ubv);
 *   - TinyGL has no such call and Quake II needs its effect
 *     (glAlphaFunc, GL_ALPHA_TEST, GL_MODULATE/GL_REPLACE, glOrtho,
 *     glDepthRange);
 *   - TinyGL has no GL_POLYGON in this configuration: a convex polygon
 *     is drawn as GL_TRIANGLE_FAN, the same triangles;
 *   - the call is not needed and is ignored, but counted (glScissor,
 *     mip levels above 0, glDepthFunc other than GL_LEQUAL/GL_LESS).
 *
 * Filtering (0.3): glTexParameterf(GL_TEXTURE_MAG_FILTER) is recorded
 * per texture number; the cvar zz_filter decides which textures that
 * asked for GL_LINEAR really get it (see zz_apply_filter).
 *
 * Extensions: none is advertised. ref_gl then uses its single texture
 * unit path (lightmaps in a second blended pass), no compiled vertex
 * arrays, no point parameters, no paletted textures. The extension
 * pointers stay NULL.
 *
 * ASCII only.
 */

#include "ref_gl/gl_local.h"
#include "zzq2tgl_version.h"

/* zzq2tgl_tgl.c */
extern void zz_tgl_set_modulate(int on);
extern void zz_tgl_set_alpha_limit(unsigned limit);
extern const unsigned *zz_tgl_frame(int *w, int *h);
extern void zz_tgl_clear_rect(int x, int y, int w, int h, int color, unsigned rgb, int depth);
extern void zz_tgl_set_linear(int on);

/* ------------------------------------------------------------------
   Counters (reported by "zzgl_stats" and at the end of the run)
   ------------------------------------------------------------------ */
unsigned zzgl_n_teximage, zzgl_n_teximage_mip, zzgl_n_texsub;
unsigned zzgl_n_polygon, zzgl_n_scissor, zzgl_n_depthfunc_other;
unsigned zzgl_n_texenv_other;

/* ------------------------------------------------------------------
   State kept here
   ------------------------------------------------------------------ */
static int      zz_alpha_test_on = 0;
static int      zz_scissor_on = 0;
static GLint    zz_sc_x, zz_sc_y, zz_sc_w, zz_sc_h;
static GLclampf zz_cc[4];
static GLenum   zz_alpha_func = GL_ALWAYS;
static GLclampf zz_alpha_ref = 0.0f;

/* top byte of a texel = 255 - alpha. Largest value still drawn. */
static void zz_apply_alpha(void)
{
    int lim = 255;
    if (zz_alpha_test_on) {
        int r = (int)(zz_alpha_ref * 255.0f);   /* floor, ref >= 0 */
        if (r < 0) r = 0;
        if (r > 255) r = 255;
        /* Quake II uses GL_GREATER 0.666 only. Other functions are
           approximated by the nearest of these two. */
        if (zz_alpha_func == GL_GREATER)
            lim = 254 - r;                       /* alpha >= r + 1 */
        else if (zz_alpha_func == GL_GEQUAL)
            lim = 255 - r;                       /* alpha >= r     */
        else
            lim = 255;                           /* GL_ALWAYS etc. */
        if (lim < 0) lim = 0;
    }
    zz_tgl_set_alpha_limit((unsigned)lim);
}

/* ------------------------------------------------------------------
   Wrappers
   ------------------------------------------------------------------ */
static void APIENTRY zz_Enable(GLenum cap)
{
    if (cap == GL_ALPHA_TEST) { zz_alpha_test_on = 1; zz_apply_alpha(); return; }
    if (cap == GL_SCISSOR_TEST) { zz_scissor_on = 1; return; }
    if (cap == GL_POINT_SMOOTH) return;
    glEnable(cap);
}

static void APIENTRY zz_Disable(GLenum cap)
{
    if (cap == GL_ALPHA_TEST) { zz_alpha_test_on = 0; zz_apply_alpha(); return; }
    if (cap == GL_SCISSOR_TEST) { zz_scissor_on = 0; return; }
    if (cap == GL_POINT_SMOOTH) return;
    glDisable(cap);
}

static void APIENTRY zz_AlphaFunc(GLenum func, GLclampf ref)
{
    zz_alpha_func = func;
    zz_alpha_ref = ref;
    zz_apply_alpha();
}

static void APIENTRY zz_Begin(GLenum mode)
{
    if (mode == GL_POLYGON) { zzgl_n_polygon++; mode = GL_TRIANGLE_FAN; }
    glBegin(mode);
}

/* OpenGL clamps colours to [0, 1]; TinyGL masks them, so a colour
   above 1 (Quake II lights models up to about 2) wrapped around into
   wrong hues. Clamped here, for every colour call. */
static GLfloat zz_c01(GLfloat v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
static void zz_color(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    glColor4f(zz_c01(r), zz_c01(g), zz_c01(b), zz_c01(a));
}

static void APIENTRY zz_End(void)               { glEnd(); }
static void APIENTRY zz_Vertex2f(GLfloat x, GLfloat y) { glVertex2f(x, y); }
static void APIENTRY zz_Vertex3f(GLfloat x, GLfloat y, GLfloat z) { glVertex3f(x, y, z); }
static void APIENTRY zz_Vertex3fv(const GLfloat *v) { glVertex3f(v[0], v[1], v[2]); }
static void APIENTRY zz_TexCoord2f(GLfloat s, GLfloat t) { glTexCoord2f(s, t); }
static void APIENTRY zz_Color3f(GLfloat r, GLfloat g, GLfloat b) { zz_color(r, g, b, 1.0f); }
static void APIENTRY zz_Color3fv(const GLfloat *v) { zz_color(v[0], v[1], v[2], 1.0f); }
static void APIENTRY zz_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { zz_color(r, g, b, a); }
static void APIENTRY zz_Color4fv(const GLfloat *v) { zz_color(v[0], v[1], v[2], v[3]); }
static void APIENTRY zz_Color4ubv(const GLubyte *v)
{
    glColor4f(v[0] * (1.0f / 255.0f), v[1] * (1.0f / 255.0f),
              v[2] * (1.0f / 255.0f), v[3] * (1.0f / 255.0f));
}

static void APIENTRY zz_MatrixMode(GLenum m)    { glMatrixMode(m); }
static void APIENTRY zz_LoadIdentity(void)      { glLoadIdentity(); }
static void APIENTRY zz_PushMatrix(void)        { glPushMatrix(); }
static void APIENTRY zz_PopMatrix(void)         { glPopMatrix(); }
static void APIENTRY zz_LoadMatrixf(const GLfloat *m) { glLoadMatrixf((GLfloat *)m); }
static void APIENTRY zz_Rotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z) { glRotatef(a, x, y, z); }
static void APIENTRY zz_Translatef(GLfloat x, GLfloat y, GLfloat z) { glTranslatef(x, y, z); }
static void APIENTRY zz_Scalef(GLfloat x, GLfloat y, GLfloat z) { glScalef(x, y, z); }
static void APIENTRY zz_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t,
                                GLdouble n, GLdouble f) { glFrustum(l, r, b, t, n, f); }

/* glOrtho is not in TinyGL: multiply by the OpenGL 1.1 matrix
   (column-major, as glMultMatrixf expects). */
static void APIENTRY zz_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t,
                              GLdouble n, GLdouble f)
{
    GLfloat m[16];
    memset(m, 0, sizeof(m));
    m[0]  = (GLfloat)(2.0 / (r - l));
    m[5]  = (GLfloat)(2.0 / (t - b));
    m[10] = (GLfloat)(-2.0 / (f - n));
    m[12] = (GLfloat)(-(r + l) / (r - l));
    m[13] = (GLfloat)(-(t + b) / (t - b));
    m[14] = (GLfloat)(-(f + n) / (f - n));
    m[15] = 1.0f;
    glMultMatrixf(m);
}

/* OpenGL counts the viewport y from the BOTTOM of the window, TinyGL
   from the top of its frame. */
static void APIENTRY zz_Viewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    int fh = 0;
    zz_tgl_frame(0, &fh);
    glViewport(x, fh - (y + h), w, h);
}
/* The scissor box only restricts glClear here: ref_gl uses it for
   nothing else (RDF_NOWORLDMODEL views). Drawing is not clipped to it. */
static void APIENTRY zz_Scissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    zz_sc_x = x; zz_sc_y = y; zz_sc_w = w; zz_sc_h = h;
    zzgl_n_scissor++;
}
static void APIENTRY zz_DepthRange(GLclampd n, GLclampd f) { glDepthRange(n, f); }
static void APIENTRY zz_DepthMask(GLboolean flag) { glDepthMask(flag); }
/* TinyGL tests "nearer or equal" (GL_LEQUAL), which is what Quake II
   asks for; GL_LESS differs only on equal depths. */
static void APIENTRY zz_DepthFunc(GLenum func)
{
    if (func != GL_LEQUAL && func != GL_LESS) zzgl_n_depthfunc_other++;
}
static void APIENTRY zz_CullFace(GLenum mode)   { glCullFace(mode); }
static void APIENTRY zz_ShadeModel(GLenum mode) { glShadeModel(mode); }
static void APIENTRY zz_PolygonMode(GLenum face, GLenum mode) { glPolygonMode(face, mode); }
static void APIENTRY zz_BlendFunc(GLenum s, GLenum d) { glBlendFunc(s, d); }
static void APIENTRY zz_DrawBuffer(GLenum mode) { (void)mode; }
static void APIENTRY zz_ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    zz_cc[0] = r; zz_cc[1] = g; zz_cc[2] = b; zz_cc[3] = a;
    glClearColor(r, g, b, a);
}

static unsigned zz_c8(GLclampf v)
{
    return v <= 0.0f ? 0u : v >= 1.0f ? 255u : (unsigned)(v * 255.0f + 0.5f);
}

static void APIENTRY zz_Clear(GLbitfield mask)
{
    if (zz_scissor_on) {
        int fh = 0;
        zz_tgl_frame(0, &fh);
        zz_tgl_clear_rect(zz_sc_x, fh - (zz_sc_y + zz_sc_h), zz_sc_w, zz_sc_h,
                          (mask & GL_COLOR_BUFFER_BIT) != 0,
                          (zz_c8(zz_cc[0]) << 16) | (zz_c8(zz_cc[1]) << 8) | zz_c8(zz_cc[2]),
                          (mask & GL_DEPTH_BUFFER_BIT) != 0);
        return;
    }
    glClear((GLint)mask);
}
static void APIENTRY zz_Finish(void)            { }
static GLenum APIENTRY zz_GetError(void)        { return GL_NO_ERROR; }
static void APIENTRY zz_GetFloatv(GLenum pname, GLfloat *params) { glGetFloatv(pname, params); }
static void APIENTRY zz_PointSize(GLfloat s)    { glPointSize(s); }
static void APIENTRY zz_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h,
                                   GLenum format, GLenum type, GLvoid *pixels)
{
    /* screenshots: not available, the buffer is cleared */
    (void)x; (void)y; (void)format; (void)type;
    if (pixels && w > 0 && h > 0) memset(pixels, 0, (size_t)w * (size_t)h * 3u);
}

static const GLubyte *APIENTRY zz_GetString(GLenum name)
{
    switch (name) {
    case GL_VENDOR:     return (const GLubyte *)"ZZ9000 Core1";
    case GL_RENDERER:   return (const GLubyte *)"TinyGL software (Cortex-A9)";
    case GL_VERSION:    return (const GLubyte *)"1.1 ZZQuake2TGL " ZZQ2TGL_VERSION;
    case GL_EXTENSIONS: return (const GLubyte *)"";
    default:            return (const GLubyte *)"";
    }
}

/* ------------------------------------------------------------------
   Texture filtering. ref_gl sets GL_TEXTURE_MAG_FILTER on every
   texture it uploads: GL_LINEAR on the lightmaps (texture numbers
   TEXNUM_LIGHTMAPS .. TEXNUM_SCRAPS - 1), the filter of gl_texturemode
   on the other textures (GL_LINEAR by default). The cvar zz_filter
   (archived in config.cfg) chooses what TinyGL really does:
     0  nearest everywhere (ZZQuake2TGL 0.2)
     1  smooth light (default): lightmaps bilinear once every 8
        pixels, colour stepped in between (0.4); textures nearest like
        the software renderer
     2  as 1, plus GL_LINEAR on every other texture that asks for it
        (textures, models, 2D), no mipmaps (TinyGL keeps level 0 only)
     3  lightmaps bilinear at every pixel, textures nearest (the 0.3
        default, kept for comparison)
   ------------------------------------------------------------------ */
#define ZZ_MAXTEX 4096
static unsigned char zz_tex_linear[ZZ_MAXTEX]; /* MAG filter is GL_LINEAR */
static unsigned char zz_tex_known[ZZ_MAXTEX];  /* MAG filter was set      */
static GLuint  zz_bound_tex;
static cvar_t *zz_filter;
static int     zz_filter_shown = -1;

static void zz_apply_filter(void)
{
    int mode, lin;
    GLuint t = zz_bound_tex;
    if (!zz_filter)
        zz_filter = ri.Cvar_Get("zz_filter", "1", CVAR_ARCHIVE);
    mode = (int)zz_filter->value;
    if (mode < 0) mode = 0;
    if (mode > 3) mode = 3;
    if (mode != zz_filter_shown) {
        zz_filter_shown = mode;
        ri.Con_Printf(PRINT_ALL, "ZZQuake2TGL %s: zz_filter %d (%s)\n", ZZQ2TGL_VERSION, mode,
                      mode == 0 ? "nearest everywhere" :
                      mode == 1 ? "smooth light" :
                      mode == 2 ? "smooth light, GL_LINEAR textures" :
                                  "lightmaps bilinear per pixel");
    }
    /* OpenGL default MAG filter: GL_LINEAR */
    lin = (t < ZZ_MAXTEX) ? (zz_tex_known[t] ? zz_tex_linear[t] : 1) : 1;
    if (t >= TEXNUM_LIGHTMAPS && t < TEXNUM_SCRAPS) {
        /* lightmap: 2 = span interpolation, 1 = bilinear per pixel */
        if (mode == 0 || !lin) lin = 0;
        else lin = (mode == 3) ? 1 : 2;
    } else if (mode != 2) {
        lin = 0;
    }
    zz_tgl_set_linear(lin);
}

static void APIENTRY zz_BindTexture(GLenum target, GLuint tex)
{
    glBindTexture(target, (GLint)tex);
    zz_bound_tex = tex;
    zz_apply_filter();
}
static void APIENTRY zz_DeleteTextures(GLsizei n, const GLuint *t) { glDeleteTextures(n, t); }

static void APIENTRY zz_TexImage2D(GLenum target, GLint level, GLint internalformat,
                                   GLsizei w, GLsizei h, GLint border, GLenum format,
                                   GLenum type, const GLvoid *pixels)
{
    if (level != 0) { zzgl_n_teximage_mip++; return; }
    zzgl_n_teximage++;
    glTexImage2D(target, level, internalformat, w, h, border, format, type,
                 (void *)pixels);
}

static void APIENTRY zz_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y,
                                      GLsizei w, GLsizei h, GLenum format,
                                      GLenum type, const GLvoid *pixels)
{
    if (level != 0) { zzgl_n_teximage_mip++; return; }
    zzgl_n_texsub++;
    glTexSubImage2D(target, level, x, y, w, h, format, type, pixels);
}

/* Wrap modes: TinyGL repeats; ref_gl never asks for anything else.
   MAG filter: recorded for the bound texture (see zz_apply_filter). */
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

static void APIENTRY zz_TexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE) return;
    if ((GLenum)param == GL_MODULATE)
        zz_tgl_set_modulate(1);
    else {
        if ((GLenum)param != GL_REPLACE) zzgl_n_texenv_other++;
        zz_tgl_set_modulate(0);
    }
}

/* vertex arrays: gl_vertex_arrays is 0 by default; bound anyway */
static void APIENTRY zz_EnableClientState(GLenum a) { glEnableClientState(a); }
static void APIENTRY zz_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{ glVertexPointer(size, type, stride, p); }
static void APIENTRY zz_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{ glColorPointer(size, type, stride, p); }
static void APIENTRY zz_ArrayElement(GLint i) { glArrayElement(i); }

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
