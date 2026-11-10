/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzqgl_gl.c - ZZQuakeGL: the gl* functions GLQuake calls, on ZZGL.
 *
 * GLQuake (id Software, WinQuake/gl_*.c, commit bf4ac424) calls the
 * OpenGL 1.1 functions directly.  This file defines each of them, with
 * the exact OpenGL prototype (Core1/rix/shim/GL/gl.h), on the zzgl*
 * entry points of ZZGL 0.7.7 (zzgl/zzgl.c: ref_gl's OpenGL subset on
 * librixgpu, the RasterIX GPU of the ZZ9000 G4C bitstream).  It is the
 * counterpart of zzq2rix_qgl.c of ZZQuake2RIX 0.10.3, whose wrappers it
 * repeats, without the qgl* pointer table ref_gl needed.
 *
 * The adaptations, all of them already made for Quake II:
 *   - colours are clamped to [0, 1] (GLQuake lights models above 1;
 *     OpenGL clamps);
 *   - levels above 0 of glTexImage2D are counted and dropped (no mipmaps
 *     on the TMU); the TMU filters bilinearly when a texture asks for
 *     GL_LINEAR;
 *   - 2D (under glOrtho: console, HUD, menus, pics) is sampled nearest
 *     and a quarter texel past the texel centre (rix_filter2d 0,
 *     rix_texbias2d 1 of Q2RIX); 3D keeps what GLQuake asks for
 *     (rix_filter 2);
 *   - textures narrower or lower than 8 texels (ZZGL's minimum) are
 *     enlarged to 8 by texel replication before the upload;
 *   - a glBegin/glEnd batch longer than ZZGL's 64 vertices is split into
 *     several ZZGL batches (GLQuake draws ALL its particles in one
 *     GL_TRIANGLES batch, and the strips of its alias models can be long):
 *     every 63 vertices for triangles, 64 for quads, and for strips and
 *     fans a new batch restarted with the vertices the primitive
 *     continues from (strip: the last two, parity kept by the even split;
 *     fan, polygon: the first and the last);
 *   - glDrawBuffer(GL_FRONT): the draws that follow, up to
 *     glDrawBuffer(GL_BACK), are dropped (GLQuake's loading disc, drawn
 *     straight into the front buffer between frames: ZZGL has one frame
 *     at a time and such a draw would open a stray frame; R_TimeRefresh
 *     draws nothing either); the call itself, made at every file GLQuake
 *     loads, feeds the keepalive of zzqgl_present.c (launcher watchdog
 *     during long map loads);
 *   - glReadBuffer / glFinish / glHint / glFog*: no effect;
 *   - glGetString: ZZQuakeGL's own strings, GL_EXTENSIONS empty, so
 *     GLQuake takes its single-texture, no-palette, no-array paths.
 *
 * No Quake header here: this file sees ZZGL only.  Categories for the
 * performance reports of zzq2rix_gpu.c (zz_rix_cat_begin/end) come from
 * the link-time interposers of zzqgl_qgmain.c (zzq2_rix_phase).
 *
 * ASCII only.
 */

#include "GL/gl.h"
#include "zzgl.h"
#include "zzqgl_version.h"

/* zzq2rix_gpu.c (vendored, game independent) */
extern void zz_rix_cat_begin(void);
extern void zz_rix_cat_end(int category);
/* zzqgl_qgmain.c: 1 R_DrawWorld, 2 R_DrawBrushModel, 4 R_DrawWaterSurfaces, 5 R_DrawAliasModel, 0 elsewhere */
extern volatile int zzq2_rix_phase;

/* ------------------------------------------------------------------
   Counters (end-of-run report, gl_vid_zz.c)
   ------------------------------------------------------------------ */
unsigned zzqgl_n_teximage, zzqgl_n_teximage_mip, zzqgl_n_texsub, zzqgl_n_texsmall;
unsigned zzqgl_n_polygon, zzqgl_n_depthfunc_other, zzqgl_n_texenv_other;
unsigned zzqgl_n_drawbuffer_front, zzqgl_n_fog;

/* ------------------------------------------------------------------
   Settings (gl_vid_zz.c reads the cvars and calls these every frame)
   ------------------------------------------------------------------ */
static int zz_filter = 2;      /* 0 nearest everywhere, 1 lightmaps linear only, 2 as GLQuake asks */
static int zz_filter2d = 0;    /* 0 2D nearest, 1 2D as rix_filter */
static int zz_texbias2d = 1;   /* 1: 2D sample point +0.25 texel */
static int zz_2d;              /* 1 between glOrtho and the next glFrustum */
static GLuint zz_bound_tex;
static int zz_blend_on;
static GLenum zz_blend_s = GL_ONE, zz_blend_d = GL_ZERO;
/* lightmap texture numbers: told by gl_vid_zz.c (GLQuake allocates them at the first map load) */
static GLuint zz_lm_first, zz_lm_count;

void zzqgl_gl_set_filter(int mode) { zz_filter = mode < 0 ? 0 : mode > 2 ? 2 : mode; }
void zzqgl_gl_set_filter2d(int on) { zz_filter2d = on ? 1 : 0; }
void zzqgl_gl_set_texbias2d(int on) { zz_texbias2d = on ? 1 : 0; }
void zzqgl_gl_set_lightmaps(unsigned first, unsigned count) { zz_lm_first = first; zz_lm_count = count; }
int  zzqgl_tex_resident(int texnum) { return texnum >= 0 ? zzgl_tex_valid((u32)texnum) : 0; }

static GLfloat zz_c01(GLfloat v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

/* ---- long batches (see the header) ---------------------------------------- */
typedef struct { float x, y, z, s, t, r, g, b, a; } zz_vert;
static zz_vert zz_first, zz_prev2, zz_prev1;
static unsigned zz_n;              /* vertices given to ZZGL in the current ZZGL batch */
static GLenum zz_mode;
static int zz_in;                  /* between glBegin and glEnd */
static int zz_front;               /* glDrawBuffer(GL_FRONT): draws dropped */
static int zz_dropped;             /* the current batch is being dropped */
static float cur_s, cur_t, cur_r = 1.0f, cur_g = 1.0f, cur_b = 1.0f, cur_a = 1.0f;
unsigned zzqgl_n_split;

static void zz_emit(const zz_vert *v)
{
    zzglColor4f(v->r, v->g, v->b, v->a);
    zzglTexCoord2f(v->s, v->t);
    zzglVertex3f(v->x, v->y, v->z);
}

static void zz_vertex(float x, float y, float z)
{
    zz_vert v;
    unsigned limit;
    if (zz_dropped) return;
    if (!zz_in) { zzglVertex3f(x, y, z); return; }
    limit = (zz_mode == GL_TRIANGLES) ? 63u : 64u;
    if (zz_n >= limit) {
        zzglEnd();
        zzglBegin(zz_mode);
        zz_n = 0;
        if (zz_mode == GL_TRIANGLE_STRIP) { zz_emit(&zz_prev2); zz_emit(&zz_prev1); zz_n = 2; }
        else if (zz_mode == GL_TRIANGLE_FAN || zz_mode == GL_POLYGON) { zz_emit(&zz_first); zz_emit(&zz_prev1); zz_n = 2; }
        if (zz_n) { zzglColor4f(cur_r, cur_g, cur_b, cur_a); zzglTexCoord2f(cur_s, cur_t); }
        zzqgl_n_split++;
    }
    v.x = x; v.y = y; v.z = z; v.s = cur_s; v.t = cur_t; v.r = cur_r; v.g = cur_g; v.b = cur_b; v.a = cur_a;
    if (zz_n == 0) zz_first = v;
    zz_prev2 = zz_prev1; zz_prev1 = v;
    zz_n++;
    zzglVertex3f(x, y, z);
}

static int zz_is_lightmap(GLuint t) { return zz_lm_count && t >= zz_lm_first && t < zz_lm_first + zz_lm_count; }

static int zz_category(void)
{
    if (zz_2d) return 6;
    if (zz_blend_on && zz_blend_s == GL_ZERO && (zz_blend_d == GL_SRC_COLOR || zz_blend_d == GL_ONE_MINUS_SRC_COLOR)) return 1;
    switch (zzq2_rix_phase) {
    case 1: return 0;
    case 2: return 2;
    case 5: return 3;
    case 4: return 4;
    default: return 5;
    }
}

/* ------------------------------------------------------------------
   Texture filtering (rix_filter / rix_filter2d of Q2RIX)
   ------------------------------------------------------------------ */
#define ZZ_MAXTEX ZZGL_MAX_TEXTURES
static unsigned char zz_tex_linear[ZZ_MAXTEX];   /* MAG filter asked: GL_LINEAR */
static unsigned char zz_tex_known[ZZ_MAXTEX];    /* MAG filter was set           */
static unsigned char zz_tex_applied[ZZ_MAXTEX];  /* 0 unknown, 1 nearest, 2 linear: what ZZGL holds */

static void zz_apply_filter(void)
{
    int lin;
    unsigned char want;
    GLuint t = zz_bound_tex;
    if (t >= ZZ_MAXTEX) return;
    /* OpenGL default MAG filter: GL_LINEAR */
    lin = zz_tex_known[t] ? zz_tex_linear[t] : 1;
    if (zz_filter == 0) lin = 0;
    else if (zz_filter == 1 && !zz_is_lightmap(t)) lin = 0;
    /* 2D drawn texel for pixel (HUD, console font, menus): nearest by default */
    if (zz_2d && !zz_filter2d) lin = 0;
    want = lin ? 2u : 1u;
    if (zz_tex_applied[t] != want) {
        zzglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, lin ? (float)GL_LINEAR : (float)GL_NEAREST);
        zz_tex_applied[t] = want;
    }
}

static void zz_set_mode_2d(int on)
{
    zz_2d = on;
    zzgl_set_texel_bias((on && zz_texbias2d) ? 0.25f : 0.0f);
    zz_apply_filter();
}

/* ------------------------------------------------------------------
   The functions
   ------------------------------------------------------------------ */
void glAlphaFunc(GLenum func, GLclampf ref) { zzglAlphaFunc(func, ref); }
void glBegin(GLenum mode)
{
    if (mode == GL_POLYGON) zzqgl_n_polygon++;
    if (zz_front) { zz_dropped = 1; return; }
    zz_rix_cat_begin();
    zzglBegin(mode);
    zz_mode = mode; zz_n = 0; zz_in = 1;
}
void glBindTexture(GLenum target, GLuint texture)
{
    zzglBindTexture(target, texture);
    zz_bound_tex = texture;
    zz_apply_filter();
}
void glBlendFunc(GLenum s, GLenum d) { zz_blend_s = s; zz_blend_d = d; zzglBlendFunc(s, d); }
void glClear(GLbitfield mask) { if (zz_front) return; zzglClear(mask); }
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { zzglClearColor(r, g, b, a); }
void glColor3f(GLfloat r, GLfloat g, GLfloat b) { cur_r = zz_c01(r); cur_g = zz_c01(g); cur_b = zz_c01(b); cur_a = 1.0f; zzglColor4f(cur_r, cur_g, cur_b, cur_a); }
void glColor3ubv(const GLubyte *v) { cur_r = v[0] / 255.0f; cur_g = v[1] / 255.0f; cur_b = v[2] / 255.0f; cur_a = 1.0f; zzglColor4f(cur_r, cur_g, cur_b, cur_a); }
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { cur_r = zz_c01(r); cur_g = zz_c01(g); cur_b = zz_c01(b); cur_a = zz_c01(a); zzglColor4f(cur_r, cur_g, cur_b, cur_a); }
void glColor4fv(const GLfloat *v) { glColor4f(v[0], v[1], v[2], v[3]); }
void glCullFace(GLenum mode) { zzglCullFace(mode); }
void glDepthFunc(GLenum func)
{
    if (func != GL_LEQUAL && func != GL_LESS && func != GL_GEQUAL) zzqgl_n_depthfunc_other++;
    zzglDepthFunc(func);
}
void glDepthMask(GLboolean flag) { zzglDepthMask(flag ? 1u : 0u); }
void glDepthRange(GLclampd n, GLclampd f) { zzglDepthRange(n, f); }
void glDisable(GLenum cap) { if (cap == GL_BLEND) zz_blend_on = 0; if (cap == GL_FOG) return; zzglDisable(cap); }
extern void zzqgl_present_keepalive(void);   /* zzqgl_present.c (host bench: stub) */
void glDrawBuffer(GLenum mode)
{
    if (mode == GL_FRONT) { zzqgl_n_drawbuffer_front++; zzqgl_present_keepalive(); }
    zz_front = (mode == GL_FRONT);
    zzglDrawBuffer(mode);
}
void glEnable(GLenum cap) { if (cap == GL_BLEND) zz_blend_on = 1; if (cap == GL_FOG) return; zzglEnable(cap); }
void glEnd(void) { if (zz_dropped) { zz_dropped = 0; return; } zz_in = 0; zzglEnd(); zz_rix_cat_end(zz_category()); }
void glFinish(void) { zzglFinish(); }
void glFlush(void) { zzglFlush(); }
void glFogf(GLenum pname, GLfloat param) { (void)pname; (void)param; zzqgl_n_fog++; }
void glFogfv(GLenum pname, const GLfloat *params) { (void)pname; (void)params; zzqgl_n_fog++; }
void glFogi(GLenum pname, GLint param) { (void)pname; (void)param; zzqgl_n_fog++; }
void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{ zzglFrustum(l, r, b, t, n, f); zz_set_mode_2d(0); }
GLenum glGetError(void) { return zzglGetError(); }
void glGetFloatv(GLenum pname, GLfloat *params) { zzglGetFloatv(pname, params); }
const GLubyte *glGetString(GLenum name)
{
    switch (name) {
    case GL_VENDOR:     return (const GLubyte *)"ZZ9000 Core1 (Xanxi)";
    case GL_RENDERER:   return (const GLubyte *)"RasterIX G4C, ZZGL " ZZQGL_ZZGL_VERSION ", librixgpu " ZZQGL_RIX_VERSION;
    case GL_VERSION:    return (const GLubyte *)"1.1 ZZQuakeGL " ZZQGL_VERSION;
    case GL_EXTENSIONS: return (const GLubyte *)"";
    default:            return (const GLubyte *)"";
    }
}
void glHint(GLenum target, GLenum mode) { zzglHint(target, mode); }
void glLoadIdentity(void) { zzglLoadIdentity(); }
void glLoadMatrixf(const GLfloat *m) { zzglLoadMatrixf(m); }
void glMatrixMode(GLenum mode) { zzglMatrixMode(mode); }
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{ zzglOrtho(l, r, b, t, n, f); zz_set_mode_2d(1); }
void glPolygonMode(GLenum face, GLenum mode) { zzglPolygonMode(face, mode); }
void glPopMatrix(void) { zzglPopMatrix(); }
void glPushMatrix(void) { zzglPushMatrix(); }
void glReadBuffer(GLenum mode) { zzglReadBuffer(mode); }
void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels)
{ zzglReadPixels(x, y, w, h, format, type, pixels); }
void glRotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z) { zzglRotatef(a, x, y, z); }
void glScalef(GLfloat x, GLfloat y, GLfloat z) { zzglScalef(x, y, z); }
void glShadeModel(GLenum mode) { zzglShadeModel(mode); }
void glTexCoord2f(GLfloat s, GLfloat t) { cur_s = s; cur_t = t; zzglTexCoord2f(s, t); }
void glTexCoord2fv(const GLfloat *v) { cur_s = v[0]; cur_t = v[1]; zzglTexCoord2f(v[0], v[1]); }
void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE) return;
    if ((GLenum)param != GL_MODULATE && (GLenum)param != GL_REPLACE) zzqgl_n_texenv_other++;
    zzglTexEnvf(target, pname, param);
}

/* textures under 8 texels in a dimension: enlarged to 8 by replication (ZZGL's minimum) */
#define ZZ_SMALL_MAX 8
static unsigned char zz_small[ZZ_SMALL_MAX * ZZ_SMALL_MAX * 4];
static const GLvoid *zz_enlarge(GLsizei *w, GLsizei *h, GLenum format, const GLvoid *pixels)
{
    GLsizei bpp = (format == GL_RGBA) ? 4 : (format == GL_RGB) ? 3 : 1;
    GLsizei nw = *w < 8 ? 8 : *w, nh = *h < 8 ? 8 : *h, x, y, k;
    const unsigned char *src = (const unsigned char *)pixels;
    if (!pixels || *w <= 0 || *h <= 0 || nw > ZZ_SMALL_MAX || nh > ZZ_SMALL_MAX) return pixels;
    for (y = 0; y < nh; y++) {
        GLsizei sy = (y * *h) / nh;
        for (x = 0; x < nw; x++) {
            GLsizei sx = (x * *w) / nw;
            for (k = 0; k < bpp; k++)
                zz_small[(y * nw + x) * bpp + k] = src[(sy * *w + sx) * bpp + k];
        }
    }
    *w = nw; *h = nh;
    zzqgl_n_texsmall++;
    return zz_small;
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels)
{
    if (level != 0) { zzqgl_n_teximage_mip++; return; }
    if (w < 8 || h < 8) pixels = zz_enlarge(&w, &h, format, pixels);
    zzqgl_n_teximage++;
    zzglTexImage2D(target, level, internalformat, w, h, border, format, type, pixels);
}
void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    /* wrap: the TMU repeats; MIN filter: one level, the MAG filter decides (ZZGL);
       MAG: recorded for the bound texture, applied through the filter policy above */
    if (target != GL_TEXTURE_2D || pname != GL_TEXTURE_MAG_FILTER) return;
    if (zz_bound_tex < ZZ_MAXTEX) {
        zz_tex_known[zz_bound_tex] = 1;
        zz_tex_linear[zz_bound_tex] = ((GLenum)param == GL_LINEAR);
    }
    zz_apply_filter();
}
void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                     GLenum format, GLenum type, const GLvoid *pixels)
{
    if (level != 0) { zzqgl_n_teximage_mip++; return; }
    zzqgl_n_texsub++;
    zzglTexSubImage2D(target, level, x, y, w, h, format, type, pixels);
}
void glTranslatef(GLfloat x, GLfloat y, GLfloat z) { zzglTranslatef(x, y, z); }
void glVertex2f(GLfloat x, GLfloat y) { zz_vertex(x, y, 0.0f); }
void glVertex3f(GLfloat x, GLfloat y, GLfloat z) { zz_vertex(x, y, z); }
void glVertex3fv(const GLfloat *v) { zz_vertex(v[0], v[1], v[2]); }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { zzglViewport(x, y, w, h); }
