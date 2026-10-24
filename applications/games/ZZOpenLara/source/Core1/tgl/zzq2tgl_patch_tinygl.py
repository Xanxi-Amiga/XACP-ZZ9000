#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
zzq2tgl_patch_tinygl.py - ZZQuake2TGL: the changes Quake II needs in
TinyGL, applied to a COPY of the vendored tree (tinygl/ is never
modified).

  usage: zzq2tgl_patch_tinygl.py <tinygl tree> <output tree>

Strict patcher: every replacement must match exactly the number of
times stated, otherwise nothing is written and the build stops.

What changes, and why (see README_TGL.txt, "TinyGL"):

  zfeatures.h  LIT_TEXTURES 1 (GL_MODULATE needs the vertex colour),
               BLEND 1, NO_DRAW_COLOR 0, the multithreaded features,
               dirty rectangles, stipple and the runtime test off.
  zbuffer.h    1. the ZZGear bilinear sampler of the cube build is
                  removed: Quake II asks for GL_LINEAR, but nearest
                  sampling is what the software renderer shows and a
                  bilinear fetch costs four texels per pixel.
               2. three fields in ZBuffer: the texture environment
                  (modulate or replace), the constant alpha of the
                  primitive and the alpha test limit.
               3. RGB_MIX_FUNC: GL_MODULATE or GL_REPLACE at run time,
                  and the texel alpha byte is kept for the blender.
               4. blending: GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA,
                  GL_ZERO / GL_SRC_COLOR (lightmap pass) and the
                  factors TinyGL already had. TinyGL had no alpha at
                  all.
  zgl.h        GLImage keeps the size the texture was uploaded with
               (TinyGL stores every texture as 256x256).
  ztriangle.c  alpha test in the textured spans (the texel alpha is in
               the top byte of the texel, stored as 255 - alpha so
               that RGB uploads stay opaque).
  clip.c       the constant alpha of a triangle is taken from its
               first vertex before it is filled.
  texture.c    glTexImage2D accepts GL_RGBA, resamples directly into
               the 256x256 texel array (no temporary allocation),
               ignores mip levels above 0; glTexSubImage2D added.
  misc.c       gl_eval_viewport honours glDepthRange (new function).
  ztriangle.h  fill rule: a pixel shared by two triangles is drawn by
  ztriangle.c  one of them only (dark seams in the lightmap pass);
  misc.c,      screen coordinates span [0, size] and are clamped;
  clip.c,      points and lines kept inside the frame.
  vertex.c,
  zline.c
  zfeatures.h  t keeps 10 fraction bits like s (t overflowed beyond 16
  clip.c       texture repeats); each textured triangle is moved by
               whole repeats; no half-texel offset (OpenGL nearest).
  clip.c       vertices made by clipping keep their alpha.
  zbuffer.h    GL_LINEAR (0.3): a bilinear sampler that reads the
  zgl.h        texture at the size it was uploaded (the 256x256 store
  texture.c    replicates its texels), three ZBuffer fields (linear on
  clip.c       or off, log2 of the size); each textured rasterizer
  ztriangle.c  calls a linear copy of itself when zz_linear is set.
               Which textures are linear is decided by the GL binding
               (zzq2tgl_qgl.c, cvar zz_filter).
               0.4: a third copy for the lightmaps (zz_linear == 2):
               the bilinear value is taken once every 8 pixels and the
               colour is stepped in between (span interpolation).
  list.c       exit() replaced by gl_fatal_error() (no exit on Core1).
  gl.h         prototypes of glTexSubImage2D and glDepthRange.

ASCII only.
"""
import os
import re
import shutil
import sys

src, dst = sys.argv[1], sys.argv[2]
done = []


def fail(msg):
    sys.stderr.write("zzq2tgl_patch_tinygl: %s\n" % msg)
    sys.exit(1)


def edit(rel, pairs):
    path = os.path.join(dst, rel)
    text = open(path, encoding="ascii").read()
    for old, new, count in pairs:
        n = text.count(old)
        if n != count:
            fail("%s: expected %d match(es), found %d for:\n%s" % (rel, count, n, old[:200]))
        text = text.replace(old, new)
    open(path, "w", encoding="ascii", newline="\n").write(text)
    done.append(rel)


def regex(rel, pattern, repl, count, flags=0):
    path = os.path.join(dst, rel)
    text = open(path, encoding="ascii").read()
    text, n = re.subn(pattern, repl, text, flags=flags)
    if n != count:
        fail("%s: regex expected %d, found %d: %s" % (rel, count, n, pattern))
    open(path, "w", encoding="ascii", newline="\n").write(text)


if os.path.exists(dst):
    shutil.rmtree(dst)
shutil.copytree(src, dst)

# ---------------------------------------------------------------- zfeatures.h
features = {
    "ERROR_CHECK": 0, "STRICT_OOM_CHECKS": 0, "CUSTOM_MALLOC": 0,
    "FISR": 0, "ARRAYS": 1, "DISPLAYLISTS": 1, "LIT_TEXTURES": 1,
    "POLYGON_STIPPLE": 0, "ALT_RENDERMODES": 0, "GL_POLYGON": 0,
    "BLEND": 1, "BLEND_DRAW_PIXELS": 0, "TEXTURE_POW2": 8,
    "SPECULAR_BUFFERS": 0, "NO_COPY_COLOR": 0, "NO_DRAW_COLOR": 0,
    "FORCE_CLEAR_NO_COPY_COLOR": 0, "MULTITHREADED_DRAWPIXELS": 0,
    "MULTITHREADED_COPY_TEXIMAGE_2D": 0, "MULTITHREADED_ZB_COPYBUFFER": 0,
    "DIRTY_RECTANGLE": 0, "16_BITS": 0, "32_BITS": 1,
    "TINYGL_RUNTIME_COMPAT_TEST": 0,
}
for k, v in features.items():
    regex("include/zfeatures.h", r"(#define TGL_FEATURE_%s )\d+\n" % re.escape(k),
          r"\g<1>%d\n" % v, 1)

# ---------------------------------------------------------------- zbuffer.h
zb = open(os.path.join(dst, "include/zbuffer.h"), encoding="ascii").read()
m0 = zb.find("/* ZZGEAR_BILINEAR_SAMPLER")
if m0 >= 0:
    tail = "#define TEXTURE_SAMPLE(texture,s,t) zzgear_texture_bilinear((texture),(s),(t))\n\n"
    m1 = zb.find(tail, m0)
    if m1 < 0:
        fail("zbuffer.h: end of the ZZGear sampler not found")
    zb = zb[:m0] + zb[m1 + len(tail):]
    if "zzgear" in zb.lower():
        fail("zbuffer.h: ZZGear sampler only partly removed")
open(os.path.join(dst, "include/zbuffer.h"), "w", encoding="ascii", newline="\n").write(zb)

MIX_OLD = """#if TGL_HAS(LIT_TEXTURES)
#define RGB_MIX_FUNC(rr, gg, bb, tpix)                                       \\
    RGB_TO_PIXEL(((rr * GET_RED(tpix)) >> 8), ((gg * GET_GREEN(tpix)) >> 8), \\
                 ((bb * GET_BLUE(tpix)) >> 8))
#else
#define RGB_MIX_FUNC(rr, gg, bb, tpix) (tpix)
#endif
"""
MIX_NEW = """/* ZZQ2TGL: GL_MODULATE when zzmod is set, GL_REPLACE otherwise. The
 * top byte of a texel (255 - alpha) is kept for the blender. zzmod is a
 * local of every rasterizer function (TGL_STIPPLEVARS, ztriangle.c). */
#if TGL_HAS(LIT_TEXTURES)
#define RGB_MIX_FUNC(rr, gg, bb, tpix)                                    \\
    (zzmod ? (RGB_TO_PIXEL(((rr * GET_RED(tpix)) >> 8),                   \\
                           ((gg * GET_GREEN(tpix)) >> 8),                 \\
                           ((bb * GET_BLUE(tpix)) >> 8)) |                \\
              ((tpix) & 0xFF000000u))                                     \\
           : (tpix))
#else
#error "ZZQuake2TGL needs TGL_FEATURE_LIT_TEXTURES"
#endif
"""

BLEND_START = "#if TGL_HAS(BLEND)\n#define TGL_BLEND_VARS              \\\n"
BLEND_END = """#else
#define TGL_BLEND_VARS
#define TGL_BLEND_FUNC(source, dest) \\
    {                                \\
        dest = source;               \\
    }
#define TGL_BLEND_FUNC_RGB(rr, gg, bb, dest) \\
    {                                        \\
        dest = RGB_TO_PIXEL(rr, gg, bb);     \\
    }
#endif
"""
BLEND_NEW = """/* ZZQ2TGL: GL_LINEAR. TinyGL stores every texture as 256x256; a
 * texture uploaded at 2^lw x 2^lh (lw, lh <= 8) is stored with each texel
 * replicated, so the four texels around (u - 1/2, v - 1/2) are read at
 * the uploaded size, as OpenGL does, and weighted with 8-bit fractions.
 * All four bytes (the alpha byte too) are blended, two at a time in the
 * two 16-bit lanes of a word. GL_REPEAT. s and t: one repeat = 2^19. */
static inline GLuint zz_tgl_lerp(GLuint a, GLuint b, GLuint f)
{
    GLuint g = 256u - f;
    GLuint rb = ((a & 0x00FF00FFu) * g + (b & 0x00FF00FFu) * f) >> 8;
    GLuint ag = ((a >> 8) & 0x00FF00FFu) * g + ((b >> 8) & 0x00FF00FFu) * f;
    return (rb & 0x00FF00FFu) | (ag & 0xFF00FF00u);
}
#define ZZ_TGL_ST_BITS (1 + TGL_FEATURE_TEXTURE_POW2 + ZB_POINT_S_FRAC_BITS)
static inline PIXEL zz_tgl_sample_linear(const PIXEL *tex, GLuint s, GLuint t,
                                         GLuint lw, GLuint lh)
{
    GLuint shx = ZZ_TGL_ST_BITS - lw, shy = ZZ_TGL_ST_BITS - lh;
    GLuint u = s - (1u << (shx - 1)), v = t - (1u << (shy - 1));
    GLuint mx = (1u << lw) - 1u, my = (1u << lh) - 1u;
    GLuint ex = TGL_FEATURE_TEXTURE_POW2 - lw, ey = TGL_FEATURE_TEXTURE_POW2 - lh;
    GLuint x0 = (u >> shx) & mx, y0 = (v >> shy) & my;
    GLuint x1 = ((x0 + 1u) & mx) << ex, y1 = ((y0 + 1u) & my) << ey;
    GLuint fx = (u >> (shx - 8)) & 255u, fy = (v >> (shy - 8)) & 255u;
    const PIXEL *r0 = tex + ((y0 << ey) << TGL_FEATURE_TEXTURE_POW2);
    const PIXEL *r1 = tex + (y1 << TGL_FEATURE_TEXTURE_POW2);
    x0 <<= ex;
    return (PIXEL) zz_tgl_lerp(zz_tgl_lerp(r0[x0], r0[x1], fx),
                               zz_tgl_lerp(r1[x0], r1[x1], fx), fy);
}

/* ZZQ2TGL 0.4: the same for the 128x128 lightmap pages of Quake II
 * (BLOCK_WIDTH, BLOCK_HEIGHT): constant shifts. */
#define ZZ_TGL_LM_LOG2 7
static inline PIXEL zz_tgl_sample_lightmap(const PIXEL *tex, GLuint s, GLuint t)
{
    return zz_tgl_sample_linear(tex, s, t, ZZ_TGL_LM_LOG2, ZZ_TGL_LM_LOG2);
}

/* ZZQ2TGL blender. Channels are 8 bits. The alpha of the source is the
 * constant alpha of the primitive (zb->zz_alpha, 0..255, set per
 * triangle in clip.c) times the texel alpha (stored as 255 - alpha in
 * the top byte; 0 for untextured pixels). GL_FUNC_ADD only: Quake II
 * never changes the blend equation. Factors handled: GL_ZERO, GL_ONE,
 * GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR,
 * GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA. */
static inline GLuint zz_tgl_bf(GLuint f, GLuint c, GLuint s, GLuint d, GLuint a)
{
    switch (f) {
    case GL_ZERO:                return 0;
    case GL_SRC_COLOR:           return (c * s + 127u) / 255u;
    case GL_ONE_MINUS_SRC_COLOR: return (c * (255u - s) + 127u) / 255u;
    case GL_DST_COLOR:           return (c * d + 127u) / 255u;
    case GL_ONE_MINUS_DST_COLOR: return (c * (255u - d) + 127u) / 255u;
    case GL_SRC_ALPHA:           return (c * a + 127u) / 255u;
    case GL_ONE_MINUS_SRC_ALPHA: return (c * (255u - a) + 127u) / 255u;
    case GL_ONE:
    default:                     return c;
    }
}
static inline PIXEL zz_tgl_blend(PIXEL src, PIXEL dst, GLuint sf, GLuint df, GLuint ca)
{
    GLuint a  = (ca * (255u - (src >> 24)) + 127u) / 255u;
    GLuint sr = GET_RED(src), sg = GET_GREEN(src), sb = GET_BLUE(src);
    GLuint dr = GET_RED(dst), dg = GET_GREEN(dst), db = GET_BLUE(dst);
    GLuint r, g, b;
    if (sf == GL_ZERO && df == GL_SRC_COLOR) {          /* lightmap pass */
        r = (dr * sr + 127u) / 255u;
        g = (dg * sg + 127u) / 255u;
        b = (db * sb + 127u) / 255u;
    } else if (sf == GL_SRC_ALPHA && df == GL_ONE_MINUS_SRC_ALPHA) {
        r = (sr * a + dr * (255u - a) + 127u) / 255u;
        g = (sg * a + dg * (255u - a) + 127u) / 255u;
        b = (sb * a + db * (255u - a) + 127u) / 255u;
    } else {
        r = zz_tgl_bf(sf, sr, sr, dr, a) + zz_tgl_bf(df, dr, sr, dr, a);
        g = zz_tgl_bf(sf, sg, sg, dg, a) + zz_tgl_bf(df, dg, sg, dg, a);
        b = zz_tgl_bf(sf, sb, sb, db, a) + zz_tgl_bf(df, db, sb, db, a);
        if (r > 255u) r = 255u;
        if (g > 255u) g = 255u;
        if (b > 255u) b = 255u;
    }
    return (PIXEL)((r << 16) | (g << 8) | b);
}
#if TGL_HAS(BLEND)
#define TGL_BLEND_VARS              \\
    GLuint sfactor = zb->sfactor;   \\
    GLuint dfactor = zb->dfactor;   \\
    GLuint zzca = zb->zz_alpha;
#define TGL_BLEND_FUNC(source, dest)                               \\
    {                                                              \\
        dest = zz_tgl_blend((source), (dest), sfactor, dfactor, zzca); \\
    }
#define TGL_BLEND_FUNC_RGB(rr, gg, bb, dest)                       \\
    {                                                              \\
        dest = zz_tgl_blend(RGB_TO_PIXEL(rr, gg, bb), (dest),      \\
                            sfactor, dfactor, zzca);               \\
    }
#else
#error "ZZQuake2TGL needs TGL_FEATURE_BLEND"
#endif
"""
zb = open(os.path.join(dst, "include/zbuffer.h"), encoding="ascii").read()
if zb.count(MIX_OLD) != 1:
    fail("zbuffer.h: RGB_MIX_FUNC block not found exactly once")
zb = zb.replace(MIX_OLD, MIX_NEW)
a0 = zb.find(BLEND_START)
if a0 < 0 or zb.count(BLEND_START) != 1:
    fail("zbuffer.h: blend block start not found exactly once")
a1 = zb.find(BLEND_END, a0)
if a1 < 0:
    fail("zbuffer.h: blend block end not found")
zb = zb[:a0] + BLEND_NEW + zb[a1 + len(BLEND_END):]
FIELDS_OLD = "    GLenum blendeq, sfactor, dfactor;\n    GLint enable_blend;\n"
FIELDS_NEW = FIELDS_OLD + (
    "    /* ZZQ2TGL */\n"
    "    GLuint zz_modulate;    /* 1: GL_MODULATE, 0: GL_REPLACE          */\n"
    "    GLuint zz_alpha;       /* constant alpha of the primitive, 0..255 */\n"
    "    GLuint zz_alpha_limit; /* texels whose (255 - alpha) is above    */\n"
    "                           /* this are not drawn; 255 = no test     */\n"
    "    GLuint zz_linear;      /* 1: GL_LINEAR, 2: lightmap (0.4)        */\n"
    "    GLuint zz_lw, zz_lh;   /* log2 of its size as uploaded, <= 8     */\n")
if zb.count(FIELDS_OLD) != 1:
    fail("zbuffer.h: ZBuffer blend fields not found exactly once")
zb = zb.replace(FIELDS_OLD, FIELDS_NEW)
open(os.path.join(dst, "include/zbuffer.h"), "w", encoding="ascii", newline="\n").write(zb)
done.append("include/zbuffer.h")

# ---------------------------------------------------------------- zgl.h
edit("src/zgl.h", [(
    "typedef struct GLImage {\n"
    "    PIXEL pixmap[TGL_FEATURE_TEXTURE_DIM * TGL_FEATURE_TEXTURE_DIM];\n"
    "    GLint xsize, ysize;\n"
    "} GLImage;\n",
    "typedef struct GLImage {\n"
    "    PIXEL pixmap[TGL_FEATURE_TEXTURE_DIM * TGL_FEATURE_TEXTURE_DIM];\n"
    "    GLint xsize, ysize;\n"
    "    GLint zz_w, zz_h; /* ZZQ2TGL: size as uploaded, 0 = never */\n"
    "    GLuint zz_lw, zz_lh; /* log2 of it for GL_LINEAR, 8 if not */\n"
    "                         /* a power of two up to 256          */\n"
    "} GLImage;\n", 1)])

# ---------------------------------------------------------------- ztriangle.c
edit("src/ztriangle.c", [
    ("#else\n#define TGL_STIPPLEVARS\n#define STIPTEST(_a)\n#endif\n",
     "#else\n"
     "/* ZZQ2TGL: every rasterizer function gets the texture environment\n"
     " * and the alpha test limit in two locals. */\n"
     "#define TGL_STIPPLEVARS                    \\\n"
     "    GLuint zzmod = zb->zz_modulate;        \\\n"
     "    GLuint zzlim = zb->zz_alpha_limit;     \\\n"
     "    (void) zzmod;                          \\\n"
     "    (void) zzlim;\n"
     "#define STIPTEST(_a)\n"
     "#endif\n", 1),
    ("/*\n * ============================================================================\n"
     " * Texture mapped triangle - common macros\n",
     "/* ZZQ2TGL: alpha test in the textured spans only. The texel is read\n"
     " * again below; the compiler shares the two loads. */\n"
     "#undef STIPTEST\n"
     "#define STIPTEST(_a) &&((TEXTURE_SAMPLE(texture, s, t) >> 24) <= zzlim)\n\n"
     "/*\n * ============================================================================\n"
     " * Texture mapped triangle - common macros\n", 1),
])

# ---------------------------------------------------------------- clip.c
edit("src/clip.c", [(
    "    GLint dw = zb->depth_write;\n    ZB_fillTriangleFunc func;\n",
    "    GLint dw = zb->depth_write;\n    ZB_fillTriangleFunc func;\n"
    "    /* ZZQ2TGL: constant alpha of the triangle (first vertex). */\n"
    "    {\n"
    "        GLfloat fa = p0->color.v[3];\n"
    "        zb->zz_alpha = (fa <= 0.0f) ? 0u : (fa >= 1.0f) ? 255u\n"
    "                     : (GLuint) (fa * 255.0f + 0.5f);\n"
    "    }\n", 1),
    ("    func(zb, &p0->zp, &p1->zp, &p2->zp);\n}\n",
     "#ifdef ZZQ2TGL_RASTER_PROFILE\n"
     "    /* ZZQ2TGL: rasterization time, release profiler step TEX */\n"
     "    {\n"
     "        extern void zzq2tgl_raster_begin(void);\n"
     "        extern void zzq2tgl_raster_end(void);\n"
     "        zzq2tgl_raster_begin();\n"
     "        func(zb, &p0->zp, &p1->zp, &p2->zp);\n"
     "        zzq2tgl_raster_end();\n"
     "    }\n"
     "#else\n"
     "    func(zb, &p0->zp, &p1->zp, &p2->zp);\n"
     "#endif\n"
     "}\n", 1)])

# ---------------------------------------------------------------- texture.c
TEX_OLD_START = "void glopTexImage2D(GLParam *p)\n{\n"
tx = open(os.path.join(dst, "src/texture.c"), encoding="ascii").read()
t0 = tx.find(TEX_OLD_START)
if t0 < 0 or tx.count(TEX_OLD_START) != 1:
    fail("texture.c: glopTexImage2D not found exactly once")
TEX_OLD_END = "    if (do_free)\n        gl_free(pixels1);\n}\n"
t1 = tx.find(TEX_OLD_END, t0)
if t1 < 0:
    fail("texture.c: end of glopTexImage2D not found")
TEX_NEW = r'''/* ZZQ2TGL: texel of an uploaded RGB or RGBA image. The top byte holds
 * 255 - alpha, so that an RGB texel (alpha 255) keeps a zero top byte,
 * exactly as TinyGL stored it before. */
static PIXEL zz_tgl_texel(const GLubyte *p, GLint comps)
{
    GLuint a = (comps == 4) ? p[3] : 255u;
    return (PIXEL) (((255u - a) << 24) | ((GLuint) p[0] << 16) |
                    ((GLuint) p[1] << 8) | (GLuint) p[2]);
}

/* ZZQ2TGL: log2 of a power of two up to the store size, else the store
 * size (GL_LINEAR then reads the store itself). */
static GLuint zz_tgl_log2(GLint n)
{
    GLuint l;
    for (l = 0; l < TGL_FEATURE_TEXTURE_POW2; l++)
        if (n == (1 << l))
            return l;
    return TGL_FEATURE_TEXTURE_POW2;
}

void glopTexImage2D(GLParam *p)
{
    GLint target = p[1].i;
    GLint level = p[2].i;
    GLint width = p[4].i;
    GLint height = p[5].i;
    GLint format = p[7].i;
    GLint type = p[8].i;
    const GLubyte *pixels = (const GLubyte *) p[9].p;
    GLContext *c = gl_get_context();
    GLint comps, x, y;
    GLImage *im;

    /* Mip levels above 0 cannot be stored (MAX_TEXTURE_LEVELS 1) and
       are not needed: TinyGL samples level 0 only. */
    if (level != 0)
        return;
    if (c->current_texture == NULL || target != GL_TEXTURE_2D ||
        type != GL_UNSIGNED_BYTE || width <= 0 || height <= 0 || !pixels)
        gl_fatal_error("glTexImage2D: combination of parameters not handled");
    if (format == GL_RGBA)
        comps = 4;
    else if (format == GL_RGB)
        comps = 3;
    else
        gl_fatal_error("glTexImage2D: format not handled");

    im = &c->current_texture->images[0];
    im->xsize = TGL_FEATURE_TEXTURE_DIM;
    im->ysize = TGL_FEATURE_TEXTURE_DIM;
    im->zz_w = width;
    im->zz_h = height;
    im->zz_lw = zz_tgl_log2(width);
    im->zz_lh = zz_tgl_log2(height);
    /* nearest resampling, straight into the texel array */
    for (y = 0; y < TGL_FEATURE_TEXTURE_DIM; y++) {
        const GLubyte *row =
            pixels + (GLuint) ((y * height) >> TGL_FEATURE_TEXTURE_POW2) *
                         (GLuint) width * (GLuint) comps;
        PIXEL *out = im->pixmap + (y << TGL_FEATURE_TEXTURE_POW2);
        for (x = 0; x < TGL_FEATURE_TEXTURE_DIM; x++)
            out[x] = zz_tgl_texel(
                row + (GLuint) ((x * width) >> TGL_FEATURE_TEXTURE_POW2) *
                          (GLuint) comps,
                comps);
    }
}

/* ZZQ2TGL: glTexSubImage2D on the current texture, level 0, RGB or
 * RGBA unsigned bytes. The region is given in the coordinates of the
 * image as it was uploaded; it is scaled to the 256x256 store with
 * the same nearest rule as glTexImage2D. Not recorded in display
 * lists (Quake II does not use them). */
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                     GLint yoffset, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const GLvoid *data)
{
    GLContext *c = gl_get_context();
    const GLubyte *pixels = (const GLubyte *) data;
    GLImage *im;
    GLint comps, W, H, x0, x1, y0, y1, x, y;

    if (level != 0)
        return;
    if (c->current_texture == NULL || target != GL_TEXTURE_2D ||
        type != GL_UNSIGNED_BYTE || !pixels)
        gl_fatal_error("glTexSubImage2D: combination of parameters not handled");
    comps = (format == GL_RGBA) ? 4 : (format == GL_RGB) ? 3 : 0;
    if (!comps)
        gl_fatal_error("glTexSubImage2D: format not handled");
    im = &c->current_texture->images[0];
    W = im->zz_w;
    H = im->zz_h;
    if (W <= 0 || H <= 0 || xoffset < 0 || yoffset < 0 || width <= 0 ||
        height <= 0 || xoffset + width > W || yoffset + height > H)
        gl_fatal_error("glTexSubImage2D: region outside the texture");
    /* store rows/columns whose nearest source falls in the region */
    x0 = (xoffset * TGL_FEATURE_TEXTURE_DIM + W - 1) / W;
    x1 = ((xoffset + width) * TGL_FEATURE_TEXTURE_DIM + W - 1) / W;
    y0 = (yoffset * TGL_FEATURE_TEXTURE_DIM + H - 1) / H;
    y1 = ((yoffset + height) * TGL_FEATURE_TEXTURE_DIM + H - 1) / H;
    for (y = y0; y < y1; y++) {
        GLint sy = ((y * H) >> TGL_FEATURE_TEXTURE_POW2) - yoffset;
        const GLubyte *row = pixels + (GLuint) sy * (GLuint) width * (GLuint) comps;
        PIXEL *out = im->pixmap + (y << TGL_FEATURE_TEXTURE_POW2);
        for (x = x0; x < x1; x++) {
            GLint sx = ((x * W) >> TGL_FEATURE_TEXTURE_POW2) - xoffset;
            out[x] = zz_tgl_texel(row + (GLuint) sx * (GLuint) comps, comps);
        }
    }
}
'''
tx = tx[:t0] + TEX_NEW + tx[t1 + len(TEX_OLD_END):]
open(os.path.join(dst, "src/texture.c"), "w", encoding="ascii", newline="\n").write(tx)
done.append("src/texture.c")

# ---------------------------------------------------------------- misc.c
edit("src/misc.c", [
    ("void gl_eval_viewport()\n{\n",
     "/* ZZQ2TGL: glDepthRange. Window depth n + (f - n) * (ndc + 1) / 2,\n"
     " * mapped on the TinyGL z-buffer where larger is nearer. */\n"
     "static GLfloat zz_tgl_depth_n = 0.0f, zz_tgl_depth_f = 1.0f;\n\n"
     "void glDepthRange(GLclampd n, GLclampd f)\n"
     "{\n"
     "    if (n < 0) n = 0;\n"
     "    if (n > 1) n = 1;\n"
     "    if (f < 0) f = 0;\n"
     "    if (f > 1) f = 1;\n"
     "    zz_tgl_depth_n = (GLfloat) n;\n"
     "    zz_tgl_depth_f = (GLfloat) f;\n"
     "    gl_eval_viewport();\n"
     "}\n\n"
     "void gl_eval_viewport()\n{\n", 1),
    ("    v->trans.Z = ((zsize - 0.5) / 2.0) + ((1 << ZB_POINT_Z_FRAC_BITS)) / 2;\n",
     "    v->trans.Z = (zsize - 0.5) *\n"
     "                     (1.0 - zz_tgl_depth_n -\n"
     "                      (zz_tgl_depth_f - zz_tgl_depth_n) / 2.0) +\n"
     "                 ((1 << ZB_POINT_Z_FRAC_BITS)) / 2;\n", 1),
    ("    v->scale.Z = -((zsize - 0.5) / 2.0);\n",
     "    v->scale.Z = -((zsize - 0.5) / 2.0) * (zz_tgl_depth_f - zz_tgl_depth_n);\n", 1),
])

# ---------------------------------------------------------------- fill rule
# TinyGL drew both ends of every span and both the first and the last
# row of every triangle: two triangles sharing an edge both drew the
# pixels of that edge. In a single pass this is invisible; with the
# multi-pass lightmaps of Quake II (second pass blended, depth test
# "nearer or equal") those pixels were multiplied twice, a dark line
# along every polygon edge. The rule becomes the usual one: a pixel is
# drawn when ceil(left edge) <= x < ceil(right edge) and
# top row <= y < bottom row, so a pixel belongs to exactly one of two
# triangles that share an edge. Screen coordinates now span [0, size]
# (not [0, size - 0.5]) so that the last column and row are still
# covered, and are clamped to that range.
edit("src/ztriangle.h", [
    ("                n = (x2 >> 16) - x1;\n",
     "                n = ((x2 + 0xFFFF) >> 16) - x1 - 1; /* ZZQ2TGL fill rule */\n", 1),
    ("                nb_lines = p2->y - p1->y + 1;\n",
     "                nb_lines = p2->y - p1->y; /* ZZQ2TGL fill rule */\n", 1),
])
edit("src/ztriangle.c", [
    ("        n = (x2 >> 16) - x1;                                      \\\n",
     "        n = ((x2 + 0xFFFF) >> 16) - x1 - 1; /* ZZQ2TGL */        \\\n", 1),
])
edit("src/zgl.h", [(
    "static inline GLint clamp_viewport_coord(GLfloat result)\n",
    "/* ZZQ2TGL: screen x or y of a vertex, clamped to [0, max]. */\n"
    "static inline GLint zz_viewport_xy(GLfloat r, GLint max)\n"
    "{\n"
    "    if (!(r > 0.0f))\n"
    "        return 0;\n"
    "    if (r >= (GLfloat) max)\n"
    "        return max;\n"
    "    return (GLint) r;\n"
    "}\n\n"
    "static inline GLint clamp_viewport_coord(GLfloat result)\n", 1)])
for f in ("src/clip.c", "src/vertex.c"):
    regex(f, r"v->zp\.x = clamp_viewport_coord\((v->pc\.X \* winv \* c->viewport\.scale\.X \+\s+c->viewport\.trans\.X)\);",
          r"v->zp.x = zz_viewport_xy(\1, c->zb->xsize);", 1)
    regex(f, r"v->zp\.y = clamp_viewport_coord\((v->pc\.Y \* winv \* c->viewport\.scale\.Y \+\s+c->viewport\.trans\.Y)\);",
          r"v->zp.y = zz_viewport_xy(\1, c->zb->ysize);", 1)
edit("src/misc.c", [
    ("    v->trans.X = ((v->xsize - 0.5) / 2.0) + v->xmin;\n"
     "    v->trans.Y = ((v->ysize - 0.5) / 2.0) + v->ymin;\n",
     "    v->trans.X = (v->xsize / 2.0) + v->xmin; /* ZZQ2TGL: [0, size] */\n"
     "    v->trans.Y = (v->ysize / 2.0) + v->ymin;\n", 1),
    ("    v->scale.X = (v->xsize - 0.5) / 2.0;\n"
     "    v->scale.Y = -(v->ysize - 0.5) / 2.0;\n",
     "    v->scale.X = v->xsize / 2.0;\n"
     "    v->scale.Y = -v->ysize / 2.0;\n", 1),
])
# points and lines may now receive x == xsize or y == ysize: clamp.
edit("src/zline.c", [
    ("void ZB_plot(ZBuffer *zb, ZBufferPoint *p)\n{\n",
     "/* ZZQ2TGL: keep points and line ends inside the frame */\n"
     "static ZBufferPoint *zz_inside(ZBuffer *zb, ZBufferPoint *p, ZBufferPoint *q)\n"
     "{\n"
     "    *q = *p;\n"
     "    if (q->x >= zb->xsize) q->x = zb->xsize - 1;\n"
     "    if (q->y >= zb->ysize) q->y = zb->ysize - 1;\n"
     "    if (q->x < 0) q->x = 0;\n"
     "    if (q->y < 0) q->y = 0;\n"
     "    return q;\n"
     "}\n\n"
     "void ZB_plot(ZBuffer *zb, ZBufferPoint *p0)\n{\n"
     "    ZBufferPoint zzq0, *p = zz_inside(zb, p0, &zzq0);\n", 1),
    ("void ZB_line_z(ZBuffer *zb, ZBufferPoint *p1, ZBufferPoint *p2)\n{\n",
     "void ZB_line_z(ZBuffer *zb, ZBufferPoint *q1, ZBufferPoint *q2)\n{\n"
     "    ZBufferPoint zzq1, zzq2, *p1 = zz_inside(zb, q1, &zzq1), *p2 = zz_inside(zb, q2, &zzq2);\n", 1),
    ("void ZB_line(ZBuffer *zb, ZBufferPoint *p1, ZBufferPoint *p2)\n{\n",
     "void ZB_line(ZBuffer *zb, ZBufferPoint *q1, ZBufferPoint *q2)\n{\n"
     "    ZBufferPoint zzq1, zzq2, *p1 = zz_inside(zb, q1, &zzq1), *p2 = zz_inside(zb, q2, &zzq2);\n", 1),
])

# ---------------------------------------------------------------- texture coordinates
# TinyGL kept t with 18 fraction bits (so that s and t combine into a
# texel offset with one OR): one texture repeat = 2^27, and an int only
# holds |t| < 16 repeats. Quake II walls, floors and ceilings go far
# beyond (world coordinates / texture size): t wrapped around, which
# showed as streaks on floors and ceilings and noise on tall walls.
# Now t uses the same 10 fraction bits as s (one repeat = 2^19, the
# texel offset takes one more shift), and each textured triangle is
# moved by a whole number of repeats so that its smallest s and t are
# in [0, 1): same texels (GL_REPEAT), far from the int limit. The half
# texel TinyGL added to s and t is dropped: nearest sampling now takes
# floor(u * size) like OpenGL.
regex("include/zfeatures.h",
      r"#define ZB_POINT_T_FRAC_BITS \(ZB_POINT_S_FRAC_BITS \+ TGL_FEATURE_TEXTURE_POW2\)\n",
      "#define ZB_POINT_T_FRAC_BITS ZB_POINT_S_FRAC_BITS /* ZZQ2TGL: was S + POW2 */\n", 1)
edit("src/clip.c", [(
    "    if (c->texture_2d_enabled) {\n#if TGL_HAS(LIT_TEXTURES)\n        if (c->current_shade_model != GL_SMOOTH) {\n",
    "    if (c->texture_2d_enabled) {\n"
    "        /* ZZQ2TGL: texture coordinates of the triangle, moved by whole\n"
    "           repeats into [0, ...), one repeat = 2^19 for s and t. */\n"
    "        {\n"
    "            GLfloat ms = p0->tex_coord.X, mt = p0->tex_coord.Y;\n"
    "            const GLfloat one = (GLfloat) (1 << (1 + TGL_FEATURE_TEXTURE_POW2 +\n"
    "                                                 ZB_POINT_S_FRAC_BITS));\n"
    "            if (p1->tex_coord.X < ms) ms = p1->tex_coord.X;\n"
    "            if (p2->tex_coord.X < ms) ms = p2->tex_coord.X;\n"
    "            if (p1->tex_coord.Y < mt) mt = p1->tex_coord.Y;\n"
    "            if (p2->tex_coord.Y < mt) mt = p2->tex_coord.Y;\n"
    "            ms = floorf(ms);\n"
    "            mt = floorf(mt);\n"
    "            p0->zp.s = (GLint) ((p0->tex_coord.X - ms) * one);\n"
    "            p1->zp.s = (GLint) ((p1->tex_coord.X - ms) * one);\n"
    "            p2->zp.s = (GLint) ((p2->tex_coord.X - ms) * one);\n"
    "            p0->zp.t = (GLint) ((p0->tex_coord.Y - mt) * one);\n"
    "            p1->zp.t = (GLint) ((p1->tex_coord.Y - mt) * one);\n"
    "            p2->zp.t = (GLint) ((p2->tex_coord.Y - mt) * one);\n"
    "        }\n"
    "#if TGL_HAS(LIT_TEXTURES)\n        if (c->current_shade_model != GL_SMOOTH) {\n", 1)])

# ---------------------------------------------------------------- clipping keeps alpha
# The vertices created by clipping got r, g, b interpolated but not the
# alpha: whatever was on the stack. A blended polygon crossing the
# screen edge (Quake II full screen flashes: damage, pickup, water)
# came out opaque or invisible depending on that garbage.
edit("src/clip.c", [
    ("    for (i = 0; i < 3; i++)\n"
     "        q->color.v[i] = p0->color.v[i] + (p1->color.v[i] - p0->color.v[i]) * t;\n",
     "    for (i = 0; i < 4; i++) /* ZZQ2TGL: alpha too */\n"
     "        q->color.v[i] = p0->color.v[i] + (p1->color.v[i] - p0->color.v[i]) * t;\n", 1),
    ("        q->color.v[2] = p0->color.v[2] + (p1->color.v[2] - p0->color.v[2]) * t;\n    }\n",
     "        q->color.v[2] = p0->color.v[2] + (p1->color.v[2] - p0->color.v[2]) * t;\n"
     "        q->color.v[3] = p0->color.v[3] + (p1->color.v[3] - p0->color.v[3]) * t; /* ZZQ2TGL */\n"
     "    }\n", 1),
])

# ---------------------------------------------------------------- list.c
# two "never called" opcodes end the program with exit(): Core1 has no
# exit, the release Sys_Error path is used instead.
edit("src/list.c", [("    exit(1);\n", "    gl_fatal_error(\"TinyGL: unexpected display list opcode\");\n", 2)])

# ---------------------------------------------------------------- gl.h
edit("include/TGL/gl.h", [(
    "void glPolygonStipple(void *a);\n",
    "void glPolygonStipple(void *a);\n"
    "/* ZZQ2TGL additions */\n"
    "typedef double GLclampd_zz;\n"
    "void glDepthRange(GLclampd_zz n, GLclampd_zz f);\n"
    "void glTexSubImage2D(GLenum target, GLint level, GLint xoffset,\n"
    "                     GLint yoffset, GLsizei width, GLsizei height,\n"
    "                     GLenum format, GLenum type, const GLvoid *data);\n", 1)])
edit("src/misc.c", [("void glDepthRange(GLclampd n, GLclampd f)\n",
                     "void glDepthRange(GLclampd_zz n, GLclampd_zz f)\n", 1)])

# ---------------------------------------------------------------- GL_LINEAR (0.3)
# Quake II asks for GL_LINEAR on its lightmaps (and on its textures,
# through gl_texturemode). Nearest lightmaps show the light as squares
# of 16 world units on floors and walls, where the software renderer
# and OpenGL show smooth light. The 8 textured rasterizers are copied
# (zz_lin_ prefix) with the bilinear sampler of zbuffer.h for the
# colour; the alpha test keeps the nearest texel. Each original
# rasterizer calls its copy when zb->zz_linear is set, so the dispatch
# table and the nearest path are unchanged.
edit("src/ztriangle.c", [(
    "    GLuint zzlim = zb->zz_alpha_limit;     \\\n"
    "    (void) zzmod;                          \\\n"
    "    (void) zzlim;\n",
    "    GLuint zzlim = zb->zz_alpha_limit;     \\\n"
    "    GLuint zzlw = zb->zz_lw;               \\\n"
    "    GLuint zzlh = zb->zz_lh;               \\\n"
    "    (void) zzmod;                          \\\n"
    "    (void) zzlim;                          \\\n"
    "    (void) zzlw;                           \\\n"
    "    (void) zzlh;\n", 1)])
LIN_A = ("/*\n * ============================================================================\n"
         " * Texture mapped triangle - with blending\n")
LIN_B = ("/*\n * ============================================================================\n"
         " * Legacy compatibility functions\n")
zt = open(os.path.join(dst, "src/ztriangle.c"), encoding="ascii").read()
if zt.count(LIN_A) != 1 or zt.count(LIN_B) != 1:
    fail("ztriangle.c: textured block markers not found exactly once")
la, lb = zt.find(LIN_A), zt.find(LIN_B)
orig = zt[la:lb]
if orig.count("void ZB_fillTriangleMappingPerspective") != 8:
    fail("ztriangle.c: 8 textured rasterizers expected")
lin = orig.replace("void ZB_fillTriangleMappingPerspective",
                   "static void zz_lin_ZB_fillTriangleMappingPerspective")
for old in ("RGB_MIX_FUNC(or1, og1, ob1, (TEXTURE_SAMPLE(texture, s, t)))",
            "RGB_MIX_FUNC(or1, og1, ob1, TEXTURE_SAMPLE(texture, s, t))"):
    if lin.count(old) != 4:
        fail("ztriangle.c: texel fetch expected 4 times: " + old)
lin = lin.replace("RGB_MIX_FUNC(or1, og1, ob1, (TEXTURE_SAMPLE(texture, s, t)))",
                  "RGB_MIX_FUNC(or1, og1, ob1, (zz_tgl_sample_linear(texture, s, t, zzlw, zzlh)))")
lin = lin.replace("RGB_MIX_FUNC(or1, og1, ob1, TEXTURE_SAMPLE(texture, s, t))",
                  "RGB_MIX_FUNC(or1, og1, ob1, zz_tgl_sample_linear(texture, s, t, zzlw, zzlh))")
lin = lin.replace(" * Texture mapped triangle - with blending\n",
                  " * ZZQ2TGL: GL_LINEAR copies of the textured rasterizers below\n", 1)
# 0.4: lightmap copies. A lightmap texel covers many screen pixels: the
# bilinear value is computed at the start of every 8-pixel step of the
# perspective loop (the value at the end of a step is the start of the
# next) and r, g, b are stepped linearly in between, as the software
# renderer does with its light. One bilinear fetch per 8 pixels instead
# of one per pixel, no texel read per pixel. Lightmaps are opaque and
# never alpha tested: no alpha test in these copies.
lms = orig.replace("void ZB_fillTriangleMappingPerspective",
                   "static void zz_lms_ZB_fillTriangleMappingPerspective")
if lms.count("DRAW_LINE_TRI_TEXTURED(") != 8:
    fail("ztriangle.c: 8 DRAW_LINE_TRI_TEXTURED uses expected")
lms = lms.replace("DRAW_LINE_TRI_TEXTURED(", "ZZ_DRAW_LINE_LMS(")
for old in ("RGB_MIX_FUNC(or1, og1, ob1, (TEXTURE_SAMPLE(texture, s, t)))",
            "RGB_MIX_FUNC(or1, og1, ob1, TEXTURE_SAMPLE(texture, s, t))"):
    if lms.count(old) != 4:
        fail("ztriangle.c: texel fetch expected 4 times (lms): " + old)
lms = lms.replace("RGB_MIX_FUNC(or1, og1, ob1, (TEXTURE_SAMPLE(texture, s, t)))",
                  "RGB_MIX_FUNC(or1, og1, ob1, ((PIXEL) RGB_TO_PIXEL(lr, lg, lb)))")
lms = lms.replace("RGB_MIX_FUNC(or1, og1, ob1, TEXTURE_SAMPLE(texture, s, t))",
                  "RGB_MIX_FUNC(or1, og1, ob1, (PIXEL) RGB_TO_PIXEL(lr, lg, lb))")
lms = lms.replace(" * Texture mapped triangle - with blending\n",
                  " * ZZQ2TGL: lightmap copies (GL_LINEAR, span interpolation)\n", 1)
LMS_HEAD = r"""/* ZZQ2TGL 0.4: light colour of the step, 8.16 fixed point per channel */
#define ZZ_LMS_SET(c0, c1)                                            \
    {                                                                 \
        GLint r0_ = (GLint) (((c0) >> 16) & 255u);                     \
        GLint g0_ = (GLint) (((c0) >> 8) & 255u);                      \
        GLint b0_ = (GLint) ((c0) & 255u);                             \
        lr = r0_ << 16;                                               \
        lg = g0_ << 16;                                               \
        lb = b0_ << 16;                                               \
        ldr = ((GLint) (((c1) >> 16) & 255u) - r0_) << (16 - 3);       \
        ldg = ((GLint) (((c1) >> 8) & 255u) - g0_) << (16 - 3);        \
        ldb = ((GLint) ((c1) & 255u) - b0_) << (16 - 3);               \
    }
#define ZZ_LMS_STEP(do_reuse)                                          \
    {                                                                 \
        GLuint c0_ = (do_reuse) ? lnext                               \
                     : zz_tgl_sample_lightmap(texture, s, t);         \
        lnext = zz_tgl_sample_lightmap(texture, s + NB_INTERP * dsdx, \
                                       t + NB_INTERP * dtdx);         \
        ZZ_LMS_SET(c0_, lnext)                                        \
    }
/* last pixels of a span (fewer than 8): after full steps, one more
 * step toward the value 8 pixels further (only the first pixels are
 * drawn, the colour stays between two real values); a span shorter
 * than 8 pixels gets one value, taken at its middle. */
#define ZZ_LMS_TAIL(had_steps, cnt)                                   \
    {                                                                 \
        if (had_steps) {                                              \
            ZZ_LMS_STEP(1)                                            \
        } else {                                                      \
            GLuint cm_ = zz_tgl_sample_lightmap(                      \
                texture, s + (GLuint) (((cnt) * dsdx) >> 1),           \
                t + (GLuint) (((cnt) * dtdx) >> 1));                   \
            ZZ_LMS_SET(cm_, cm_)                                      \
        }                                                             \
    }
#define ZZ_DRAW_LINE_LMS(DEPTH_TEST, DEPTH_WRITE_OP)                  \
    {                                                                 \
        register GLushort *pz;                                        \
        register PIXEL *pp;                                           \
        register GLuint s, t, z;                                      \
        register GLint n;                                             \
        GLint lr, lg, lb, ldr, ldg, ldb, lhave = 0;                   \
        GLuint lnext = 0;                                             \
        OR1OG1OB1DECL                                                 \
        GLfloat sz, tz, fzl, zinv;                                    \
        n = ((x2 + 0xFFFF) >> 16) - x1 - 1; /* ZZQ2TGL */            \
        fzl = (GLfloat) z1;                                           \
        zinv = (GLfloat) (1.0 / fzl);                                 \
        pp = (PIXEL *) ((GLbyte *) pp1 + x1 * PSZB);                  \
        pz = pz1 + x1;                                                \
        z = z1;                                                       \
        sz = sz1;                                                     \
        tz = tz1;                                                     \
        while (n >= (NB_INTERP - 1)) {                                \
            register GLint dsdx, dtdx;                                \
            {                                                         \
                GLfloat ss, tt;                                       \
                ss = (sz * zinv);                                     \
                tt = (tz * zinv);                                     \
                s = (GLint) ss;                                       \
                t = (GLint) tt;                                       \
                dsdx = (GLint) ((dszdx - ss * fdzdx) * zinv);         \
                dtdx = (GLint) ((dtzdx - tt * fdzdx) * zinv);         \
            }                                                         \
            ZZ_LMS_STEP(lhave)                                        \
            lhave = 1;                                                \
            fzl += fndzdx;                                            \
            zinv = zinv * (2.0f - fzl * zinv);                        \
            zinv = zinv * (2.0f - fzl * zinv);                        \
            PUT_PIXEL_TEXTURED(0, DEPTH_TEST, DEPTH_WRITE_OP)         \
            PUT_PIXEL_TEXTURED(1, DEPTH_TEST, DEPTH_WRITE_OP)         \
            PUT_PIXEL_TEXTURED(2, DEPTH_TEST, DEPTH_WRITE_OP)         \
            PUT_PIXEL_TEXTURED(3, DEPTH_TEST, DEPTH_WRITE_OP)         \
            PUT_PIXEL_TEXTURED(4, DEPTH_TEST, DEPTH_WRITE_OP)         \
            PUT_PIXEL_TEXTURED(5, DEPTH_TEST, DEPTH_WRITE_OP)         \
            PUT_PIXEL_TEXTURED(6, DEPTH_TEST, DEPTH_WRITE_OP)         \
            PUT_PIXEL_TEXTURED(7, DEPTH_TEST, DEPTH_WRITE_OP)         \
            pz += NB_INTERP;                                          \
            pp += NB_INTERP;                                          \
            n -= NB_INTERP;                                           \
            sz += ndszdx;                                             \
            tz += ndtzdx;                                             \
        }                                                             \
        if (n >= 0) {                                                 \
            register GLint dsdx, dtdx;                                \
            {                                                         \
                GLfloat ss, tt;                                       \
                ss = (sz * zinv);                                     \
                tt = (tz * zinv);                                     \
                s = (GLint) ss;                                       \
                t = (GLint) tt;                                       \
                dsdx = (GLint) ((dszdx - ss * fdzdx) * zinv);         \
                dtdx = (GLint) ((dtzdx - tt * fdzdx) * zinv);         \
            }                                                         \
            ZZ_LMS_TAIL(lhave, n + 1)                                 \
            while (n >= 0) {                                          \
                PUT_PIXEL_TEXTURED(0, DEPTH_TEST, DEPTH_WRITE_OP)     \
                pz += 1;                                              \
                pp++;                                                 \
                n -= 1;                                               \
            }                                                         \
        }                                                             \
    }

/* in these copies: no alpha test, the light colour steps per pixel */
#undef STIPTEST
#define STIPTEST(_a)
#undef OR1G1B1INCR
#define OR1G1B1INCR \
    og1 += dgdx;    \
    or1 += drdx;    \
    ob1 += dbdx;    \
    lr += ldr;      \
    lg += ldg;      \
    lb += ldb;

"""
LMS_TAIL = r"""
/* end of the lightmap copies: macros of the other rasterizers back */
#undef STIPTEST
#define STIPTEST(_a) &&((TEXTURE_SAMPLE(texture, s, t) >> 24) <= zzlim)
#undef OR1G1B1INCR
#define OR1G1B1INCR \
    og1 += dgdx;    \
    or1 += drdx;    \
    ob1 += dbdx;

"""
lms = LMS_HEAD + lms + LMS_TAIL
orig, n = re.subn(
    r"void (ZB_fillTriangleMappingPerspective(?:NOBLEND)?_DT\d_DW\d)\(([^)]*)\)\n"
    r"\{\n    PIXEL \*texture;\n((?:    TGL_BLEND_VARS\n)?)    TGL_STIPPLEVARS\n",
    r"void \1(\2)\n{\n    PIXEL *texture;\n\3    TGL_STIPPLEVARS\n"
    r"    if (zb->zz_linear) { /* ZZQ2TGL: 1 bilinear, 2 lightmap */\n"
    r"        if (zb->zz_linear == 2)\n"
    r"            zz_lms_\1(zb, p0, p1, p2);\n"
    r"        else\n"
    r"            zz_lin_\1(zb, p0, p1, p2);\n"
    r"        return;\n"
    r"    }\n", orig)
if n != 8:
    fail("ztriangle.c: linear dispatch inserted %d times, 8 expected" % n)
zt = zt[:la] + lin + lms + orig + zt[lb:]
open(os.path.join(dst, "src/ztriangle.c"), "w", encoding="ascii", newline="\n").write(zt)
edit("src/clip.c", [(
    "        ZB_setTexture(zb, c->current_texture->images[0].pixmap);\n",
    "        ZB_setTexture(zb, c->current_texture->images[0].pixmap);\n"
    "        zb->zz_lw = c->current_texture->images[0].zz_lw; /* ZZQ2TGL */\n"
    "        zb->zz_lh = c->current_texture->images[0].zz_lh;\n"
    "        if (zb->zz_linear == 2 && (zb->zz_lw != 7 || zb->zz_lh != 7))\n"
    "            zb->zz_linear = 1; /* not a 128x128 lightmap page */\n", 1)])

print("TinyGL patched for ZZQuake2TGL: %d files" % len(set(done)))
