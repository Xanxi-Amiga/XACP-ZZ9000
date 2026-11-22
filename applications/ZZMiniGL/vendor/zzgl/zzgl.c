/* ZZGL 0.8.10-unified-dev1: Half-Life renderer with QuakeGL and OpenLara APIs,
 * + ZZMiniGL A6 sections (normals, texgen, ZZ_texgen_frame, dithered uploads, client arrays).
 * SPDX-License-Identifier: 0BSD
 */
#include "zzgl.h"
/* GPL-derived walker diagnostics are optional and excluded from normal builds. */
#ifndef ZZGL_ENABLE_WALK_MODEL
#define ZZGL_ENABLE_WALK_MODEL 0
#endif
#if ZZGL_ENABLE_WALK_MODEL
#include "rix_walk_model.h"
#endif
#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    rix_texture t;
    u32 w, h;
    int valid, alpha, mag_linear, is_lightmap;
    int named;           /* ZZHalfLife 0.7.7: name handed out by zzglGenTextures and not deleted yet (valid or not) */
    int fmt5551;         /* 0.2: stored as RGBA5551 (alpha-keyed texture, zzgl_set_alpha_textures on) */
    int fmt4444;         /* ZZHalfLife 0.7.8: stored as RGBA4444 (alpha with intermediate levels, zzgl_set_alpha4444 on) */
    int shift;           /* ZZHalfLife 0.7.9: stored at 1/2^shift of the size ref_gl gave (the pool was full) */
    int clamp_s, clamp_t;   /* ZZHalfLife 0.8.1: GL_TEXTURE_WRAP_S/T CLAMP(_TO_EDGE/_BORDER) -> TMU clamp bits 8 / 9 */
    u32 last_frame;
    u32 bind_seq, bind_frame;   /* 0.7.4: last bind of these pages (librixgpu sequence) and the frame it was in */
} zgl_tex;

/* NEON where it is worth it (build_arm.sh compiles this file with
   -mfpu=neon; the scalar code below stays the reference and is what the host tests run). */
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define ZZGL_NEON 1
extern int rix_neon;   /* NEON=0 runs the scalar path */
#endif

typedef struct { float x, y, z, w; float s, t; float r, g, b, a; } zgl_vert;

/* ZZMiniGL A9: with ZZGL_STATE_ARENA the state (texture table included) lives in the NOLOAD section
   .zz_arena that the application maps (cleared by zzgl_init, as before) */
#if defined(ZZGL_STATE_ARENA)
#define ZZGL_STATE_ATTR __attribute__((section(".zz_arena"), aligned(32)))
#else
#define ZZGL_STATE_ATTR
#endif
static struct {
    zzgl_config cfg;
    rix_frame frame;
    zzgl_target target;
    int frame_open;
    u32 frame_no;
    /* matrices, column-major like OpenGL */
    float mv[ZZGL_MATRIX_STACK][16], pr[ZZGL_MATRIX_STACK][16];
    u32 mv_top, pr_top;
    u32 matrix_mode;
    float mvp[16];
    int mvp_dirty;
    float w_min;                         /* near plane in clip space (perspective: the frustum's near) */
    float texel_bias;                    /* 0.3: sample point shift, in texels (zzgl_set_texel_bias) */
    /* viewport / depth range / scissor */
    s32 vp_x, vp_y, vp_w, vp_h;
    float dr_near, dr_far;
    rix_viewport vp;
    float clip_x0, clip_y0, clip_x1, clip_y1;
    int scissor_on; s32 sc_x, sc_y, sc_w, sc_h;
    /* raster state */
    int en_texture, en_depth, en_blend, en_alpha, en_cull;
    u32 depth_func, blend_s, blend_d, alpha_func; float alpha_ref;
    int depth_mask, color_mask;
    u32 cull_mode, front_face, shade_model, polygon_mode, tex_env;
    u32 features_sent, fragment_sent, env_sent;
    int state_dirty, env_dirty, scissor_dirty;
    /* clear */
    u32 clear_rgba; float clear_depth;
    /* current vertex attributes and the Begin/End buffer */
    float cur_r, cur_g, cur_b, cur_a, cur_s, cur_t;
    int in_begin; u32 begin_mode;
    zgl_vert vb[ZZGL_MAX_VERTS]; u32 vb_n;
    /* ZZMiniGL: current normal, texture coordinate generation (S and T), dithering */
    float cur_nx, cur_ny, cur_nz;         /* zzglNormal3f, (0,0,1) at init as the spec */
    float point_size;                     /* ZZMiniGL A8.23: zzglPointSize, 1 at init */
    int texgen_on[2];                     /* glEnable(GL_TEXTURE_GEN_S / _T) */
    u32 texgen_mode[2];                   /* ZGL_SPHERE_MAP, ZGL_OBJECT_LINEAR, ZGL_EYE_LINEAR */
    float texgen_obj[2][4], texgen_eye[2][4];   /* object plane; eye plane already multiplied by the inverse modelview */
    int texgen_any;                       /* cached: any of texgen_on */
    float nm[9];                          /* normal matrix (inverse transpose of the modelview's 3x3), row-major */
    int nm_dirty;
    int texgen_fixed;                     /* ZZ_texgen_frame: texgen uses texgen_mv / texgen_nm instead of the modelview */
    float texgen_mv[16], texgen_nm[9];
    int dither;                           /* glEnable(GL_DITHER): ordered dither at texture conversion time */
    /* textures */
    zgl_tex tex[ZZGL_MAX_TEXTURES];
    u32 bound, bound_sent;
    u32 tex_next_id;
    u32 error;
    zzgl_stats st;
    u16 convert[256u * 256u];
} G ZZGL_STATE_ATTR;

static const float IDENTITY[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
#ifdef ZZQ2RIX_HOST
unsigned zzgl_dbg_cull_flip;   /* host bench: triangles whose s32 area sign disagreed with the float one */
#endif
/* ZZMiniGL: FNV-1a of the last uploaded texture as it is in DDR (once per upload, never per frame) */
static u32 g_texture_debug_fnv;
u32 zzgl_texture_debug_fnv(void) { return g_texture_debug_fnv; }
static void texture_debug_hash_actual(const rix_texture *t) {
    const volatile u16 *pixels = (const volatile u16 *)rix_ptr(t->base);
    u32 k, hash = 2166136261u;
    for (k = 0; k < t->w * t->h; k++) {
        u16 v = pixels[k];
        hash = (hash ^ (u32)(v & 255u)) * 16777619u;
        hash = (hash ^ (u32)(v >> 8)) * 16777619u;
    }
    g_texture_debug_fnv = hash;
}
static int g_alpha_textures;   /* 0.2: alpha-keyed uploads as RGBA5551 (zzgl_set_alpha_textures) */
void zzgl_set_alpha_textures(int on) { g_alpha_textures = on ? 1 : 0; }
/* ZZHalfLife 0.7.8: textures whose alpha has intermediate levels (Half-Life's gradient decals: blood,
   scorch marks; soft sprites and particles) as RGBA4444, format 0 of the TMU (RasterIX
   RegisterAndDescriptorDefines.vh: 0 RGBA4444, 1 RGBA5551, 2 RGB565).  RGBA5551 kept one bit of
   that alpha: hard-edged solid blobs instead of soft marks (A4000, 06/11, c1a0c / c1a1).  Off until
   the init texel test has seen the format work on the hardware (Core1/zzhl_gpu.c). */
#ifndef RIX_TMU_CFG_RGBA4444
#define RIX_TMU_CFG_RGBA4444(log2w, log2h, mag) (((u32)(log2w) & 15u) | (((u32)(log2h) & 15u) << 4) | ((mag) ? 0x400u : 0u))
#endif
static int g_alpha4444;
static u32 g_tex4444_count;
void zzgl_set_alpha4444(int on) { g_alpha4444 = on ? 1 : 0; }
u32 zzgl_tex4444_count(void) { return g_tex4444_count; }
/* ZZHalfLife 0.8.1: the wrap mode.  RasterIX's TMU texture config (XX19d-RIX
   RasterIX_G1/rtl/RasterIX/RegisterAndDescriptorDefines.vh): width [3:0], height [7:4], clamp s [8],
   clamp t [9], mag filter [10], min filter [11], pixel format [15:12]; TextureSampler.v clamps both
   texels of the bilinear pair to the edge (CLAMP_TO_EDGE).  Up to 0.8.0 every texture repeated:
   the bilinear filter at the border of a sky box face mixed in the opposite border (the seams Hakim
   saw between the sky's faces, c1a3 outside, 11/11). */
#define ZZGL_TMU_CLAMP_S 0x100u
#define ZZGL_TMU_CLAMP_T 0x200u
static u32 tex_cfg_of(const zgl_tex *t, int lin)
{
    u32 c;
    if (t->fmt4444) c = RIX_TMU_CFG_RGBA4444(t->t.log2w, t->t.log2h, lin);
    else if (t->fmt5551) c = RIX_TMU_CFG_RGBA5551(t->t.log2w, t->t.log2h, lin);
    else c = RIX_TMU_CFG_RGB565(t->t.log2w, t->t.log2h, lin);
    if (t->clamp_s) c |= ZZGL_TMU_CLAMP_S;
    if (t->clamp_t) c |= ZZGL_TMU_CLAMP_T;
    return c;
}
void zzgl_set_texel_bias(float texels) { G.texel_bias = texels; }

static void set_error(u32 e) { if (G.error == ZGL_NO_ERROR) G.error = e; G.st.errors++; }
/* 0.8.5 (ZZHalfLife build 84): the submission counters by origin (zzgl.h) */
static u32 g_org;
static int g_org_px = 1;
static int g_org_box;                                  /* 0.8.6: zzgl_set_box_stats */
void zzgl_set_box_stats(int on) { g_org_box = on ? 1 : 0; }
/* 0.8.6: the frame's depth buffer and whether it was handed over clean (zzgl.h) */
static zzgl_zbuf_hook g_zbuf_hook;
static int g_z_clean;
void zzgl_set_zbuf_hook(zzgl_zbuf_hook fn) { g_zbuf_hook = fn; }
static zzgl_origin_stat g_os[ZZGL_ORIGINS];
void zzgl_origin_set(u32 origin) { g_org = origin < ZZGL_ORIGINS ? origin : 0u; }
void zzgl_set_origin_stats(int on) { g_org_px = on ? 1 : 0; }
void zzgl_origin_take(zzgl_origin_stat *dst, u32 n)
{
    u32 i;
    if (n > ZZGL_ORIGINS) n = ZZGL_ORIGINS;
    for (i = 0; i < n; i++) dst[i] = g_os[i];
    memset(g_os, 0, sizeof g_os);
}
/* 0.8.2 (ZZHalfLife build 53): each GL_INVALID_ENUM noted with its function and value, counted, for the
   exit report (zzgl_bad_enum): ref_gl only says "GL_INVALID_ENUM at gl_rmain.c:806" (157 times in the
   Hazard Course on the A4000, 12/11), not which call nor which enum is missing here */
#define ZGL_BADENUM_MAX 16u
static struct { const char *fn; u32 v, n; } g_badenum[ZGL_BADENUM_MAX];
static void bad_enum(const char *fn, u32 v)
{
    u32 i;
    for (i = 0; i < ZGL_BADENUM_MAX && g_badenum[i].fn; i++)
        if (g_badenum[i].fn == fn && g_badenum[i].v == v) { g_badenum[i].n++; break; }
    if (i < ZGL_BADENUM_MAX && !g_badenum[i].fn) { g_badenum[i].fn = fn; g_badenum[i].v = v; g_badenum[i].n = 1u; }
    set_error(ZGL_INVALID_ENUM);
}
int zzgl_bad_enum(u32 i, const char **fn, u32 *v, u32 *n)
{
    if (i >= ZGL_BADENUM_MAX || !g_badenum[i].fn) return 0;
    *fn = g_badenum[i].fn; *v = g_badenum[i].v; *n = g_badenum[i].n;
    return 1;
}

/* ------------------------------------------------------------------ */
/* matrices                                                            */
/* ------------------------------------------------------------------ */
static float *cur_matrix(void) { return (G.matrix_mode == ZGL_PROJECTION) ? G.pr[G.pr_top] : G.mv[G.mv_top]; }
/* 0.7.3: 16-float copies without memcpy.  In the Quake II blob memcpy is the release's safe_mem.c
   (built -O0, byte by byte, linked first by rule): about 15 cycles a byte.  The 120-byte copy of
   every triangle in draw_triangle cost ~2.7 us per triangle (ZZQuake2RIX 0.9 on the A4000:
   11.6 ms per frame of zzglEnd's 15.3).  Same values, copied as floats. */
static void cpy16(float *d, const float *s) { u32 i; for (i = 0; i < 16u; i++) d[i] = s[i]; }
static void mat_mul(float *o, const float *a, const float *b)   /* o = a * b, column-major */
{
    float r[16];
    u32 i, j, k;
    for (i = 0; i < 4u; i++) for (j = 0; j < 4u; j++) {
        float s = 0.0f;
        for (k = 0; k < 4u; k++) s += a[k * 4u + j] * b[i * 4u + k];
        r[i * 4u + j] = s;
    }
    cpy16(o, r);
}
static void matrices_changed(void) { G.mvp_dirty = 1; G.nm_dirty = 1; }
/* ZZMiniGL: normal matrix = inverse transpose of the modelview's upper 3x3 (cofactors / determinant); the
   identity when singular.  Row-major: n' = nm * n. */
static void normal_matrix_of(float *nm, const float *m)
{
    float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    float c00 = e * i - f * h, c01 = -(d * i - f * g), c02 = d * h - e * g;
    float c10 = -(b * i - c * h), c11 = a * i - c * g, c12 = -(a * h - b * g);
    float c20 = b * f - c * e, c21 = -(a * f - c * d), c22 = a * e - b * d;
    float det = a * c00 + b * c01 + c * c02, r;
    if (det > -1.0e-30f && det < 1.0e-30f) { nm[0] = 1.0f; nm[1] = 0.0f; nm[2] = 0.0f; nm[3] = 0.0f; nm[4] = 1.0f; nm[5] = 0.0f; nm[6] = 0.0f; nm[7] = 0.0f; nm[8] = 1.0f; return; }
    r = 1.0f / det;
    nm[0] = c00 * r; nm[1] = c01 * r; nm[2] = c02 * r;
    nm[3] = c10 * r; nm[4] = c11 * r; nm[5] = c12 * r;
    nm[6] = c20 * r; nm[7] = c21 * r; nm[8] = c22 * r;
}
static void update_normal_matrix(void)
{
    if (!G.nm_dirty) return;
    G.nm_dirty = 0;
    normal_matrix_of(G.nm, G.mv[G.mv_top]);
}
/* ZZMiniGL: general 4x4 inverse (column-major), for GL_EYE_PLANE; returns 0 when singular */
static int mat_invert(float *o, const float *m)
{
    float inv[16], det;
    u32 i;
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (det > -1.0e-30f && det < 1.0e-30f) return 0;
    det = 1.0f / det;
    for (i = 0; i < 16u; i++) o[i] = inv[i] * det;
    return 1;
}
static void update_mvp(void)
{
    const float *p = G.pr[G.pr_top];
    if (!G.mvp_dirty) return;
    mat_mul(G.mvp, p, G.mv[G.mv_top]);
    G.mvp_dirty = 0;
    /* near plane: for a perspective projection (p[15] == 0) w_clip = -z_eye and the frustum's
       near plane is w = n = p[14] / (p[10] - 1); for an orthographic one w = 1 */
    if (p[15] == 0.0f && p[10] != 1.0f) {
        float n = p[14] / (p[10] - 1.0f);
        G.w_min = (n > 0.001f) ? n : 0.001f;
    } else G.w_min = 0.001f;
}

void zzglMatrixMode(u32 mode) { if (mode == ZGL_MODELVIEW || mode == ZGL_PROJECTION) G.matrix_mode = mode; else if (mode == ZGL_TEXTURE) G.matrix_mode = mode; else bad_enum("glMatrixMode", mode); }
void zzglLoadIdentity(void) { if (G.matrix_mode == ZGL_TEXTURE) return; cpy16(cur_matrix(), IDENTITY); matrices_changed(); }
void zzglLoadMatrixf(const float *m) { if (G.matrix_mode == ZGL_TEXTURE) return; cpy16(cur_matrix(), m); matrices_changed(); }
void zzglMultMatrixf(const float *m) { if (G.matrix_mode == ZGL_TEXTURE) return; mat_mul(cur_matrix(), cur_matrix(), m); matrices_changed(); }
void zzglPushMatrix(void)
{
    if (G.matrix_mode == ZGL_PROJECTION) { if (G.pr_top + 1u >= ZZGL_MATRIX_STACK) { set_error(ZGL_INVALID_OPERATION); return; } cpy16(G.pr[G.pr_top + 1u], G.pr[G.pr_top]); G.pr_top++; }
    else if (G.matrix_mode == ZGL_MODELVIEW) { if (G.mv_top + 1u >= ZZGL_MATRIX_STACK) { set_error(ZGL_INVALID_OPERATION); return; } cpy16(G.mv[G.mv_top + 1u], G.mv[G.mv_top]); G.mv_top++; }
}
void zzglPopMatrix(void)
{
    if (G.matrix_mode == ZGL_PROJECTION) { if (G.pr_top == 0u) { set_error(ZGL_INVALID_OPERATION); return; } G.pr_top--; }
    else if (G.matrix_mode == ZGL_MODELVIEW) { if (G.mv_top == 0u) { set_error(ZGL_INVALID_OPERATION); return; } G.mv_top--; }
    matrices_changed();
}
void zzglTranslatef(float x, float y, float z)
{
    float m[16]; cpy16(m, IDENTITY); m[12] = x; m[13] = y; m[14] = z; zzglMultMatrixf(m);
}
void zzglScalef(float x, float y, float z)
{
    float m[16]; cpy16(m, IDENTITY); m[0] = x; m[5] = y; m[10] = z; zzglMultMatrixf(m);
}
void zzglRotatef(float angle, float x, float y, float z)
{
    float m[16], len = sqrtf(x * x + y * y + z * z), c, s, t;
    if (len == 0.0f) return;
    x /= len; y /= len; z /= len;
    c = cosf(angle * 3.14159265f / 180.0f); s = sinf(angle * 3.14159265f / 180.0f); t = 1.0f - c;
    m[0] = t * x * x + c;     m[4] = t * x * y - s * z; m[8] = t * x * z + s * y;  m[12] = 0.0f;
    m[1] = t * x * y + s * z; m[5] = t * y * y + c;     m[9] = t * y * z - s * x;  m[13] = 0.0f;
    m[2] = t * x * z - s * y; m[6] = t * y * z + s * x; m[10] = t * z * z + c;     m[14] = 0.0f;
    m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f; m[15] = 1.0f;
    zzglMultMatrixf(m);
}
void zzglOrtho(double l, double r, double b, double t, double n, double f)
{
    float m[16];
    memset(m, 0, sizeof m);
    m[0] = (float)(2.0 / (r - l)); m[5] = (float)(2.0 / (t - b)); m[10] = (float)(-2.0 / (f - n));
    m[12] = (float)(-(r + l) / (r - l)); m[13] = (float)(-(t + b) / (t - b)); m[14] = (float)(-(f + n) / (f - n)); m[15] = 1.0f;
    zzglMultMatrixf(m);
}
void zzglFrustum(double l, double r, double b, double t, double n, double f)
{
    float m[16];
    memset(m, 0, sizeof m);
    m[0] = (float)(2.0 * n / (r - l)); m[5] = (float)(2.0 * n / (t - b));
    m[8] = (float)((r + l) / (r - l)); m[9] = (float)((t + b) / (t - b)); m[10] = (float)(-(f + n) / (f - n)); m[11] = -1.0f;
    m[14] = (float)(-2.0 * f * n / (f - n));
    zzglMultMatrixf(m);
}
void zzglGetFloatv(u32 pname, float *params)
{
    if (pname == ZGL_MODELVIEW_MATRIX) memcpy(params, G.mv[G.mv_top], 64u);
    else if (pname == ZGL_PROJECTION_MATRIX) memcpy(params, G.pr[G.pr_top], 64u);
    else bad_enum("glGetFloatv", pname);
}
void zzglGetIntegerv(u32 pname, s32 *params)
{
    if (pname == ZGL_MAX_TEXTURE_SIZE) *params = 256;
    else bad_enum("glGetIntegerv", pname);
}

/* ------------------------------------------------------------------ */
/* viewport, scissor, depth range                                      */
/* ------------------------------------------------------------------ */
static void viewport_update(void)
{
    /* RasterIX and OpenGL both count y upwards; the demos' 481-line buffers keep line 0 for the
       fences, hence +1; the 1/32-pixel margins keep every bounding box inside 640x481 (D2 review) */
    /* 0.3: exact OpenGL mapping, NDC [-1, 1] -> [vp_x, vp_x + w], shifted by -0.5 pixel because
       RasterIX samples a pixel at its integer corner and OpenGL at its centre (x + 0.5).  ZZGL 0.2
       mapped [-1, 1] onto [0, w - 0.5] (scale w/2 - 0.25): the sample point drifted from the
       pixel's left edge to its centre across the screen, so 1:1 2D (HUD, console, menus) fell
       between texels and the bilinear TMU smeared every character (A4000, 14/10).  The screen
       clip (clip_x1 = w - 0.53125) still keeps every bounding box inside 640 x 481. */
    /* 0.5: plus 1/32 pixel in x (one step of the descriptor's fixed point).  With the exact mapping,
       edges through half-pixel vertices (R_PolyBlend's full-screen quad: its diagonal, slope 1) pass
       exactly through sample points, and RasterIX draws such pixels for both triangles: blended
       twice, a visible line (0.3).  The shift moves those edges off the sample grid without touching
       the descriptors (the fill rule of librixgpu 1.2.2 did, and the G4C core disliked it, 0.4).
       2D still samples inside the right texel (1/32 texel off its centre at 1:1). */
    float w = (float)G.vp_w, h = (float)G.vp_h;
    G.vp.scale_x = w * 0.5f; G.vp.trans_x = (float)G.vp_x + w * 0.5f - 0.5f + 0.03125f;
    G.vp.scale_y = h * 0.5f; G.vp.trans_y = (float)G.vp_y + 1.0f + h * 0.5f - 0.5f;
    G.vp.scale_z = (G.dr_far - G.dr_near) * 0.5f; G.vp.trans_z = (G.dr_far + G.dr_near) * 0.5f;
    G.clip_x0 = (float)G.vp_x; G.clip_x1 = (float)(G.vp_x + G.vp_w) - 0.53125f;
    G.clip_y0 = (float)G.vp_y + 1.0f; G.clip_y1 = (float)(G.vp_y + 1 + G.vp_h) - 0.53125f;
    if (G.clip_x0 < RIX_CLIP_X0) G.clip_x0 = RIX_CLIP_X0;
    /* 0.7.6: the screen clip of the frame geometry (librixgpu 1.4): w - 0.53125, h_alloc - 0.53125, i.e.
       RIX_CLIP_X1 / RIX_CLIP_Y1 (639.46875, 480.46875) at 640 x 481 */
    if (G.clip_x1 > (float)rix_geo_w - 0.53125f) G.clip_x1 = (float)rix_geo_w - 0.53125f;
    if (G.clip_y0 < RIX_CLIP_Y0) G.clip_y0 = RIX_CLIP_Y0;
    if (G.clip_y1 > (float)rix_geo_h_alloc - 0.53125f) G.clip_y1 = (float)rix_geo_h_alloc - 0.53125f;
}
void zzglViewport(s32 x, s32 y, s32 w, s32 h)
{
    if (w <= 0 || h <= 0) { set_error(ZGL_INVALID_VALUE); return; }
    G.vp_x = x; G.vp_y = y; G.vp_w = w; G.vp_h = h;
    viewport_update();
}
void zzglDepthRange(double zNear, double zFar) { G.dr_near = (float)zNear; G.dr_far = (float)zFar; viewport_update(); }
void zzglScissor(s32 x, s32 y, s32 w, s32 h) { G.sc_x = x; G.sc_y = y; G.sc_w = w; G.sc_h = h; G.scissor_dirty = 1; }

/* ------------------------------------------------------------------ */
/* raster state -> librixgpu registers                                 */
/* ------------------------------------------------------------------ */
static u32 map_func(u32 f)
{
    switch (f) {
    case ZGL_NEVER: return RIX_TF_NEVER; case ZGL_LESS: return RIX_TF_LESS; case ZGL_EQUAL: return RIX_TF_EQUAL;
    case ZGL_LEQUAL: return RIX_TF_LEQUAL; case ZGL_GREATER: return RIX_TF_GREATER; case ZGL_NOTEQUAL: return RIX_TF_NOTEQUAL;
    case ZGL_GEQUAL: return RIX_TF_GEQUAL; default: return RIX_TF_ALWAYS;
    }
}
static u32 map_blend(u32 f)
{
    /* RasterIX blend factor table (RegisterAndDescriptorDefines.vh); the Amiga validated ZERO, ONE,
       SRC_COLOR (as destination factor), SRC_ALPHA, ONE_MINUS_SRC_ALPHA; the others are the RTL's
       numbering, not yet probed */
    switch (f) {
    case ZGL_ZERO: return RIX_BF_ZERO; case ZGL_ONE: return RIX_BF_ONE;
    case ZGL_DST_COLOR: return RIX_BF_DST_COLOR; case ZGL_SRC_COLOR: return RIX_BF_SRC_COLOR;
    case ZGL_ONE_MINUS_DST_COLOR: return 4u; case ZGL_ONE_MINUS_SRC_COLOR: return 5u;
    case ZGL_SRC_ALPHA: return RIX_BF_SRC_ALPHA; case ZGL_ONE_MINUS_SRC_ALPHA: return RIX_BF_ONE_MINUS_SRC_ALPHA;
    case ZGL_DST_ALPHA: return 8u; case ZGL_ONE_MINUS_DST_ALPHA: return 9u; case ZGL_SRC_ALPHA_SATURATE: return 10u;
    default: return RIX_BF_ONE;
    }
}
static u32 current_features(void)
{
    u32 f = 0u;
    if (G.en_texture && G.tex[G.bound].valid) f |= RIX_FEAT_TMU0;
    if (G.en_depth && G.cfg.zbuf) f |= RIX_FEAT_DEPTH_TEST;   /* ZZMiniGL: no depth test without a depth buffer */
    if (G.en_blend) f |= RIX_FEAT_BLENDING;
    if (G.en_alpha) f |= RIX_FEAT_ALPHA_TEST;
    return f;
}
static u32 current_fragment(void)
{
    u32 ref = (u32)(G.alpha_ref * 255.0f + 0.5f);
    u32 fr;
    if (ref > 255u) ref = 255u;
    /* ZZMiniGL: a context without a depth buffer (zzgl_config.zbuf = 0) never tests nor writes depth:
       the depth address register is 0 then, and a depth write would land at DDR address 0.
       A8.29: with GL_DEPTH_TEST disabled the depth buffer is not written either (OpenGL: no test, no
       update).  The GPU writes the depth whenever the mask bit is set, test feature or not: a background
       drawn with the test off and glDepthMask left on hid everything drawn behind it afterwards. */
    fr = rix_fragment((G.en_depth && G.cfg.zbuf) ? map_func(G.depth_func) : RIX_TF_ALWAYS, (G.en_depth && G.cfg.zbuf) ? G.depth_mask : 0, map_func(G.alpha_func), ref, map_blend(G.blend_s), map_blend(G.blend_d));
    if (!G.color_mask) fr &= ~RIX_FRAG_COLOR_MASK;
    return fr;
}
static int frame_begin_if_needed(void);
/* 0.7.4: retired texture versions: pages a queued or running bind may still read, freed only once the frame
   they were retired in is over (the caller waits every frame's fence before the next frame begins) */
#define ZZGL_MAX_RETIRED 128u   /* ZZHalfLife 0.8.0: 64 -> 128, pipelined frames keep two frames of retired versions */
static struct { rix_texture t; u32 frame; } g_ret[ZZGL_MAX_RETIRED];
static u32 g_ret_n;
static int g_zzgl_versioning;
void zzgl_set_tex_versioning(int on) { g_zzgl_versioning = on ? 1 : 0; }
/* ZZHalfLife 0.8.0: pipelined frames (zzgl_set_pipelined, librixgpu rix_frame_set_pipelined): frame N's
   binds may still be queued when N+1 begins; what N retired is freed one frame later, once N's fence was
   waited (the caller waits frame N at the end of N+1) */
static int g_zzgl_pipe_req;
void zzgl_set_pipelined(int on) { g_zzgl_pipe_req = on ? 1 : 0; }
static u32 pipe_lag(void) { return rix_frame_pipelined() ? 1u : 0u; }
static void ret_free_old(void)
{
    u32 i, k = 0u, lag = pipe_lag();
    for (i = 0; i < g_ret_n; i++) {
        if (g_ret[i].frame + lag < G.frame_no) rix_tex_free(&g_ret[i].t);
        else g_ret[k++] = g_ret[i];
    }
    g_ret_n = k;
}
/* ZZHalfLife 0.8.3 (build 54): every retired version at once.  Only called from an upload, which
   drained the frame first (zzglTexImage2D): no queued bind can still read these pages. */
static void ret_free_all(void)
{
    u32 i;
    for (i = 0; i < g_ret_n; i++) rix_tex_free(&g_ret[i].t);
    g_ret_n = 0;
}
/* ZZHalfLife 0.8.3 (build 54): the pool is full and an upload does not fit.  The caller is given one
   chance to release what it no longer needs before the image is halved or refused; Half-Life's engine
   only frees the previous map's models at the END of the new map's precache, so their pages sit in
   the 16 MiB pool while the new map uploads (c2a5e -> c2a5f: 43 textures refused, A4000, 13/11).
   Called at most once per upload, and never from a frame (uploads drain first). */
static void (*g_oom_cb)(void);
void zzgl_set_oom_cb(void (*cb)(void)) { g_oom_cb = cb; }
static u32 g_oom_calls;
u32 zzgl_oom_calls(void) { return g_oom_calls; }
static int g_zzgl_defer_req;   /* 0.7.2: zzgl_set_deferred, applied when the next frame opens */
static int g_zzgl_clear_flush; /* 0.7.5 */
static int g_zzgl_clear_split;
static int (*g_zzgl_precleared)(void);
static int g_zzgl_clear_seen;
u32 zzgl_clr_us[2], zzgl_clr_n[2], zzgl_preclear_hits;
void zzgl_set_clear_split(int on) { g_zzgl_clear_split = on ? 1 : 0; }
void zzgl_set_preclear_hook(int (*fn)(void)) { g_zzgl_precleared = fn; }
void zzgl_set_clear_flush(int on) { g_zzgl_clear_flush = on ? 1 : 0; }
void zzgl_set_deferred(int on) { g_zzgl_defer_req = on ? 1 : 0; }
static int flush_state(void)
{
    int rc;
    u32 feat, frag, env;
    rc = frame_begin_if_needed();
    if (rc) return rc;
    rc = rix_frame_pump(&G.frame);       /* 0.7.2: deferred mode: submit what the GPU can take now (no-op otherwise) */
    if (rc) return rc;
    if (G.scissor_dirty) {
        if (G.scissor_on && G.sc_w > 0 && G.sc_h > 0) {
            s32 x0 = G.sc_x, y0 = G.sc_y + 1, x1 = G.sc_x + G.sc_w, y1 = G.sc_y + 1 + G.sc_h;
            if (x0 < 0) x0 = 0; if (y0 < 1) y0 = 1; if (x1 > (s32)rix_geo_w) x1 = (s32)rix_geo_w; if (y1 > (s32)rix_geo_h_alloc) y1 = (s32)rix_geo_h_alloc;
            rc = rix_frame_scissor(&G.frame, (u32)x0, (u32)y0, (u32)x1, (u32)y1);
        } else rc = rix_frame_scissor(&G.frame, 0u, 0u, 0u, 0u);
        if (rc) return rc;
        G.scissor_dirty = 0; G.st.state_words += 6u;
    }
    if (G.env_dirty) {
        env = (G.tex_env == ZGL_REPLACE) ? RIX_TMU_ENV_REPLACE : RIX_TMU_ENV_MODULATE;   /* DECAL -> MODULATE (unused by ref_gl) */
        if (env != G.env_sent) { rc = rix_frame_reg(&G.frame, RIX_REG_TMU0_ENV, env); if (rc) return rc; G.env_sent = env; G.st.state_words += 2u; }
        G.env_dirty = 0;
    }
    if (G.en_texture && G.tex[G.bound].valid && G.bound != G.bound_sent) {
        u32 b0 = G.frame.tex_binds, tb = rix_now();
        rc = rix_frame_texture(&G.frame, &G.tex[G.bound].t);
        G.st.bind_ticks += (u32)(rix_now() - tb);
        if (rc) return rc;
        if (G.frame.tex_binds != b0) {
            G.st.binds++;
            g_os[g_org].binds++; g_os[g_org].pages += G.tex[G.bound].t.pages;   /* 0.8.5 */
            G.tex[G.bound].bind_seq = rix_frame_last_bind_seq(); G.tex[G.bound].bind_frame = G.frame_no;   /* 0.7.4 */
        } else G.st.binds_skipped++;
        G.bound_sent = G.bound;
        G.tex[G.bound].last_frame = G.frame_no;
    }
    if (G.state_dirty) {
        feat = current_features(); frag = current_fragment();
        if (frag != G.fragment_sent) { rc = rix_frame_fragment(&G.frame, frag); if (rc) return rc; G.fragment_sent = frag; G.st.state_words += 2u; }
        if (feat != G.features_sent) { rc = rix_frame_features(&G.frame, feat); if (rc) return rc; G.features_sent = feat; G.st.state_words += 2u; }
        G.state_dirty = 0;
    }
    return 0;
}

void zzglEnable(u32 cap)
{
    switch (cap) {
    case ZGL_TEXTURE_2D: G.en_texture = 1; break; case ZGL_DEPTH_TEST: G.en_depth = 1; break;
    case ZGL_BLEND: G.en_blend = 1; break; case ZGL_ALPHA_TEST: G.en_alpha = 1; break;
    case ZGL_CULL_FACE: G.en_cull = 1; return; case ZGL_SCISSOR_TEST: G.scissor_on = 1; G.scissor_dirty = 1; return;
    case ZGL_STENCIL_TEST: case ZGL_FOG: case ZGL_POLYGON_OFFSET_FILL: return;   /* not on this GPU: ref_gl's defaults never need them */
    /* ZZMiniGL */
    case ZGL_TEXTURE_GEN_S: G.texgen_on[0] = 1; G.texgen_any = 1; return;
    case ZGL_TEXTURE_GEN_T: G.texgen_on[1] = 1; G.texgen_any = 1; return;
    case ZGL_TEXTURE_GEN_R: case ZGL_TEXTURE_GEN_Q: return;                      /* no 3D/projective textures: accepted, ignored */
    case ZGL_DITHER: G.dither = 1; return;                                       /* applied at texture conversion (RGB565 TMU) */
    case ZGL_NORMALIZE: return;                                                  /* sphere mapping normalizes anyway */
    case ZGL_LIGHTING: case ZGL_COLOR_MATERIAL: return;                          /* no fixed-function lighting (documented) */
    case ZGL_POINT_SMOOTH: case ZGL_LINE_SMOOTH: case ZGL_POLYGON_SMOOTH: return;
    default: if (cap >= ZGL_LIGHT0 && cap < ZGL_LIGHT0 + 8u) return; bad_enum("glEnable", cap); return;
    }
    G.state_dirty = 1;
}
void zzglDisable(u32 cap)
{
    switch (cap) {
    case ZGL_TEXTURE_2D: G.en_texture = 0; break; case ZGL_DEPTH_TEST: G.en_depth = 0; break;
    case ZGL_BLEND: G.en_blend = 0; break; case ZGL_ALPHA_TEST: G.en_alpha = 0; break;
    case ZGL_CULL_FACE: G.en_cull = 0; return; case ZGL_SCISSOR_TEST: G.scissor_on = 0; G.scissor_dirty = 1; return;
    case ZGL_STENCIL_TEST: case ZGL_FOG: case ZGL_POLYGON_OFFSET_FILL: return;
    /* ZZMiniGL */
    case ZGL_TEXTURE_GEN_S: G.texgen_on[0] = 0; G.texgen_any = G.texgen_on[1]; return;
    case ZGL_TEXTURE_GEN_T: G.texgen_on[1] = 0; G.texgen_any = G.texgen_on[0]; return;
    case ZGL_TEXTURE_GEN_R: case ZGL_TEXTURE_GEN_Q: return;
    case ZGL_DITHER: G.dither = 0; return;
    case ZGL_NORMALIZE: case ZGL_LIGHTING: case ZGL_COLOR_MATERIAL: return;
    case ZGL_POINT_SMOOTH: case ZGL_LINE_SMOOTH: case ZGL_POLYGON_SMOOTH: return;
    default: if (cap >= ZGL_LIGHT0 && cap < ZGL_LIGHT0 + 8u) return; bad_enum("glDisable", cap); return;
    }
    G.state_dirty = 1;
}
/* ---- ZZMiniGL: texture coordinate generation state ---- */
void zzglTexGeni(u32 coord, u32 pname, s32 param)
{
    u32 k;
    if (coord == ZGL_R || coord == ZGL_Q) return;
    if (coord != ZGL_S && coord != ZGL_T) { bad_enum("glTexGeni", coord); return; }
    k = coord - ZGL_S;
    if (pname == ZGL_TEXGEN_MODELVIEW_ZZ) {
        /* ZZ_texgen_frame: freeze (1) or release (0) the texgen frame; applies to S and T alike */
        u32 i;
        if (param) { for (i = 0; i < 16u; i++) G.texgen_mv[i] = G.mv[G.mv_top][i]; normal_matrix_of(G.texgen_nm, G.texgen_mv); G.texgen_fixed = 1; }
        else G.texgen_fixed = 0;
        G.st.texgen_fixed = (u32)G.texgen_fixed;
        return;
    }
    if (pname != ZGL_TEXTURE_GEN_MODE) { bad_enum("glTexGeni", pname); return; }
    if ((u32)param != ZGL_SPHERE_MAP && (u32)param != ZGL_OBJECT_LINEAR && (u32)param != ZGL_EYE_LINEAR) { bad_enum("glTexGeni", (u32)param); return; }
    G.texgen_mode[k] = (u32)param;
}
void zzglTexGenfv(u32 coord, u32 pname, const float *v)
{
    u32 k;
    if (coord == ZGL_R || coord == ZGL_Q) return;
    if (coord != ZGL_S && coord != ZGL_T) { bad_enum("glTexGenfv", coord); return; }
    k = coord - ZGL_S;
    if (pname == ZGL_TEXTURE_GEN_MODE) { zzglTexGeni(coord, pname, (s32)v[0]); return; }
    if (pname == ZGL_OBJECT_PLANE) { G.texgen_obj[k][0] = v[0]; G.texgen_obj[k][1] = v[1]; G.texgen_obj[k][2] = v[2]; G.texgen_obj[k][3] = v[3]; return; }
    if (pname == ZGL_EYE_PLANE) {
        /* the spec: the plane is multiplied by the inverse of the modelview in effect at this call */
        float inv[16];
        const float *m = G.mv[G.mv_top];
        if (!mat_invert(inv, m)) { G.texgen_eye[k][0] = v[0]; G.texgen_eye[k][1] = v[1]; G.texgen_eye[k][2] = v[2]; G.texgen_eye[k][3] = v[3]; return; }
        /* p' = p * M^-1 (row vector times matrix, column-major storage inv[col*4+row]) */
        G.texgen_eye[k][0] = v[0] * inv[0] + v[1] * inv[1] + v[2] * inv[2] + v[3] * inv[3];
        G.texgen_eye[k][1] = v[0] * inv[4] + v[1] * inv[5] + v[2] * inv[6] + v[3] * inv[7];
        G.texgen_eye[k][2] = v[0] * inv[8] + v[1] * inv[9] + v[2] * inv[10] + v[3] * inv[11];
        G.texgen_eye[k][3] = v[0] * inv[12] + v[1] * inv[13] + v[2] * inv[14] + v[3] * inv[15];
        return;
    }
    bad_enum("glTexGenfv", pname);
}
void zzglDepthFunc(u32 func) { G.depth_func = func; G.state_dirty = 1; }
void zzglDepthMask(u32 flag) { G.depth_mask = flag ? 1 : 0; G.state_dirty = 1; }
void zzglBlendFunc(u32 s, u32 d) { G.blend_s = s; G.blend_d = d; G.state_dirty = 1; }
void zzglAlphaFunc(u32 func, float ref) { G.alpha_func = func; G.alpha_ref = ref; G.state_dirty = 1; }
void zzglColorMask(u32 r, u32 g, u32 b, u32 a) { G.color_mask = (r || g || b || a) ? 1 : 0; G.state_dirty = 1; }
void zzglCullFace(u32 mode) { G.cull_mode = mode; }
void zzglFrontFace(u32 mode) { G.front_face = mode; }
void zzglShadeModel(u32 mode) { G.shade_model = mode; }
void zzglPointSize(float size) { if (size > 0.0f) G.point_size = size; else set_error(ZGL_INVALID_VALUE); }   /* ZZMiniGL A8.23 */
void zzglPolygonMode(u32 face, u32 mode) { (void)face; G.polygon_mode = mode; }
void zzglTexEnvf(u32 target, u32 pname, float param)
{
    (void)target;
    if (pname == ZGL_TEXTURE_ENV_MODE) { G.tex_env = (u32)param; G.env_dirty = 1; }
}
void zzglHint(u32 target, u32 mode) { (void)target; (void)mode; }
void zzglDrawBuffer(u32 mode) { (void)mode; }
void zzglReadBuffer(u32 mode) { (void)mode; }
void zzglClearColor(float r, float g, float b, float a)
{
    u32 R = (u32)(r * 255.0f + 0.5f), Gg = (u32)(g * 255.0f + 0.5f), B = (u32)(b * 255.0f + 0.5f), A = (u32)(a * 255.0f + 0.5f);
    if (R > 255u) R = 255u; if (Gg > 255u) Gg = 255u; if (B > 255u) B = 255u; if (A > 255u) A = 255u;
    G.clear_rgba = RIX_RGBA(R, Gg, B, A);
}
void zzglClearDepth(double d) { G.clear_depth = (float)d; }
/* Keep the scissor conversion identical for normal and split clears. */
static int zzgl_clear_rect(int color, int depth, u32 d16)
{
    if (G.scissor_on && G.sc_w > 0 && G.sc_h > 0) {
        s32 x0 = G.sc_x, y0 = G.sc_y + 1, x1 = G.sc_x + G.sc_w, y1 = G.sc_y + 1 + G.sc_h;
        if (x0 < 0) x0 = 0; if (y0 < 1) y0 = 1;
        if (x1 > (s32)rix_geo_w) x1 = (s32)rix_geo_w;
        if (y1 > (s32)rix_geo_h_alloc) y1 = (s32)rix_geo_h_alloc;
        return rix_frame_clear(&G.frame, color, G.clear_rgba, depth, d16,
                               (u32)x0, (u32)y0, (u32)x1, (u32)y1);
    }
    /* ZZMiniGL (A1 rule, restored in A6): never the whole buffer.  GPU row 0 is the fence row (line 480:
       frame, chunk and bind fences); a clear colour whose RGB565 value equals a pending fence value
       makes that fence look passed, and the next DDR list is then submitted behind a texture load that
       has not executed (rule 11: FSC deadlock, power cycle).  The base cleared rows 0..480 (x1 = y1 = 0);
       black, the usual clear colour of the games, never matches a fence value, any MiniGL colour can.
       Same pixels minus the fence row. */
    return rix_frame_clear(&G.frame, color, G.clear_rgba, depth, d16, 0u, 1u, rix_geo_w, rix_geo_h_alloc);
}
void zzglClear(u32 mask)
{
    int rc, full;
    u32 d16 = (u32)(G.clear_depth * 65535.0f + 0.5f);
    int color = (mask & ZGL_COLOR_BUFFER_BIT) ? 1 : 0;
    int depth = ((mask & ZGL_DEPTH_BUFFER_BIT) && G.cfg.zbuf) ? 1 : 0;
    if (d16 > 0xFFFFu) d16 = 0xFFFFu;
    if (!color && !depth) return;
    rc = frame_begin_if_needed();
    if (rc) { set_error(ZGL_OUT_OF_MEMORY); return; }
    full = !G.scissor_on || (G.sc_x <= 0 && G.sc_y <= 0 &&
           G.sc_x + G.sc_w >= (s32)G.cfg.width &&
           G.sc_y + G.sc_h >= (s32)G.cfg.height);
    if (color && !g_zzgl_clear_seen && g_zzgl_precleared &&
        G.frame.tris == 0u && (G.clear_rgba & 0xFFFFFF00u) == 0u && full &&
        g_zzgl_precleared()) {
        color = 0;
        zzgl_preclear_hits++;
    }
    g_zzgl_clear_seen = 1;
    if (depth) {
        if (g_z_clean && d16 == 0xFFFFu) {
            depth = 0; G.st.zclear_skipped++;
            if (!color) return;
        } else {
            G.st.zclear_gpu++; g_z_clean = 0;
        }
    }
    /* Split is a synchronous diagnostic mode only: no GPU wait is attributed
       to a clear while deferred or pipelined frames are active. */
    if (g_zzgl_clear_split && color && depth && full &&
        !rix_frame_deferred() && !rix_frame_pipelined()) {
        int k;
        rc = rix_frame_flush(&G.frame);
        for (k = 0; k < 2 && rc == 0; k++) {
            u32 before = G.frame.ringwait_us;
            rc = zzgl_clear_rect(k == 0, k == 1, d16);
            if (!rc) rc = rix_frame_flush(&G.frame);
            if (!rc) { zzgl_clr_us[k] += G.frame.ringwait_us - before; zzgl_clr_n[k]++; }
        }
    } else {
        rc = zzgl_clear_rect(color, depth, d16);
        if (!rc && g_zzgl_clear_flush && depth)
            rc = rix_frame_flush(&G.frame);
    }
    if (rc) set_error(ZGL_OUT_OF_MEMORY);
    G.features_sent = 0xFFFFFFFFu;
    G.fragment_sent = 0xFFFFFFFFu;
    G.state_dirty = 1;
}
/* 0.7.5: pump the deferred queue from outside a draw (the presentation loop) */
/* 0.8.0: also between frames (pipelined: the end of the previous frame is still queued) */
void zzgl_pump(void) { if (G.frame_open || rix_frame_pipelined()) (void)rix_frame_pump(&G.frame); }

/* ------------------------------------------------------------------ */
/* frame                                                               */
/* ------------------------------------------------------------------ */
int zzgl_init(const zzgl_config *cfg)
{
    int rc;
    memset(&G, 0, sizeof G);
    G.cfg = *cfg;
    rc = rix_texpool_init(cfg->pool_base, cfg->pool_pages, cfg->pool_cacheable);
    if (rc) return rc;
    G.matrix_mode = ZGL_MODELVIEW;
    memcpy(G.mv[0], IDENTITY, 64u); memcpy(G.pr[0], IDENTITY, 64u); G.mvp_dirty = 1;
    G.vp_x = 0; G.vp_y = 0; G.vp_w = (s32)cfg->width; G.vp_h = (s32)cfg->height; G.dr_near = 0.0f; G.dr_far = 1.0f; viewport_update();
    G.depth_func = ZGL_LESS; G.depth_mask = 1; G.color_mask = 1; G.blend_s = ZGL_ONE; G.blend_d = ZGL_ZERO; G.alpha_func = ZGL_ALWAYS; G.alpha_ref = 0.0f;
    G.cull_mode = ZGL_BACK; G.front_face = ZGL_CCW; G.shade_model = ZGL_SMOOTH; G.polygon_mode = ZGL_FILL; G.tex_env = ZGL_MODULATE;
    G.cur_r = G.cur_g = G.cur_b = G.cur_a = 1.0f; G.clear_depth = 1.0f; G.clear_rgba = RIX_RGBA(0u, 0u, 0u, 255u);
    /* ZZMiniGL */
    g_texture_debug_fnv = 0u;
    G.cur_nx = 0.0f; G.cur_ny = 0.0f; G.cur_nz = 1.0f; G.nm_dirty = 1;
    G.texgen_mode[0] = ZGL_EYE_LINEAR; G.texgen_mode[1] = ZGL_EYE_LINEAR;       /* spec defaults: EYE_LINEAR, planes (1,0,0,0) and (0,1,0,0) */
    G.texgen_obj[0][0] = 1.0f; G.texgen_eye[0][0] = 1.0f; G.texgen_obj[1][1] = 1.0f; G.texgen_eye[1][1] = 1.0f;
    G.dither = 0;
    G.point_size = 1.0f;
    G.tex_next_id = 1u; G.bound = 0u; G.bound_sent = 0xFFFFFFFFu;
    G.features_sent = 0xFFFFFFFFu; G.fragment_sent = 0xFFFFFFFFu; G.env_sent = 0xFFFFFFFFu;
    G.state_dirty = 1; G.env_dirty = 1;
    return 0;
}
void zzgl_set_target(const zzgl_target *t) { G.target = *t; }
int zzgl_frame_open(void) { return G.frame_open; }
const rix_frame *zzgl_frame(void) { return &G.frame; }
u32 zzgl_tris_frame(void) { return G.st.tris_out; }
const zzgl_stats *zzgl_get_stats(void) { return &G.st; }

static int frame_begin_if_needed(void)
{
    rix_frame_desc d;
    int rc;
    if (G.frame_open) return 0;
    memset(&d, 0, sizeof d);
    d.color = G.target.color; d.zbuf = G.cfg.zbuf;
    g_z_clean = 0;
    if (g_zbuf_hook && G.cfg.zbuf) {                  /* 0.8.6: before anything of the frame can be submitted */
        u32 zb = G.cfg.zbuf;
        int clean = g_zbuf_hook(&zb);
        if (zb) { d.zbuf = zb; g_z_clean = clean ? 1 : 0; }
    }
    d.features = current_features(); d.fragment = current_fragment();
    d.clear_color = 0; d.clear_depth = 0;             /* clears are in-stream (zzglClear) */
    rix_frame_set_deferred(g_zzgl_defer_req);   /* 0.7.2: only between frames (the queue is empty here) */
    rix_frame_set_pipelined(g_zzgl_pipe_req && g_zzgl_defer_req);   /* 0.8.0: applied when nothing is pending */
    rc = rix_frame_begin(&G.frame, &d);
    if (rc) return rc;
    G.frame_open = 1;
    g_zzgl_clear_seen = 0;
    G.frame_no++;
    ret_free_old();                                   /* 0.7.4: versions retired in earlier frames (their fences passed) */
    G.features_sent = d.features; G.fragment_sent = d.fragment; G.env_sent = 0xFFFFFFFFu; G.env_dirty = 1;
    G.bound_sent = 0xFFFFFFFFu; G.scissor_dirty = 1; G.state_dirty = 0;
    memset(&G.st, 0, sizeof G.st);
    G.st.open_tick = rix_now();                       /* 0.8.0 */
    rix_frame_set_phase(RIX_PH_REF);                  /* Core1 in ref_gl from here */
    return 0;
}
int zzgl_swap(void)
{
    int rc;
    if (!G.frame_open) { rc = frame_begin_if_needed(); if (rc) return rc; }   /* an empty frame still carries its fence */
    rc = rix_frame_end(&G.frame, G.target.fence);
    G.frame_open = 0;
    return rc;
}
void zzglFinish(void) { }
void zzglFlush(void) { }
u32 zzglGetError(void) { u32 e = G.error; G.error = ZGL_NO_ERROR; return e; }
const unsigned char *zzglGetString(u32 name)
{
    switch (name) {
    case ZGL_VENDOR: return (const unsigned char *)"Xanxi";
    case ZGL_RENDERER: return (const unsigned char *)"ZZGL RasterIX (ZZ9000 G4C, librixgpu 1.2)";
    case ZGL_VERSION: return (const unsigned char *)"1.1 ZZGL 0.1";
    case ZGL_EXTENSIONS: return (const unsigned char *)"";
    default: return (const unsigned char *)"";
    }
}

/* ------------------------------------------------------------------ */
/* textures                                                            */
/* ------------------------------------------------------------------ */
static void tex_free_slot(u32 i) { if (G.tex[i].valid) { rix_tex_free(&G.tex[i].t); G.tex[i].valid = 0; } }
int zzgl_tex_valid(u32 texnum) { return (texnum < ZZGL_MAX_TEXTURES && G.tex[texnum].valid) ? 1 : 0; }
/* ZZHalfLife 0.7.9: the eviction below is off by default.  Quick II's ref_gl re-uploaded an evicted
   texture when it needed it again; Xash3D's ref_gl uploads every texture once at map load and
   keeps drawing with its name, so an evicted world texture would stay broken for the rest of the
   map.  Instead, an upload that does not fit is retried at half and quarter resolution
   (zzglTexImage2D), counted; only then refused. */
static int g_evict;
static u32 g_evicted, g_uploads_total, g_tex_downscaled, g_tex_refused, g_sub_dropped;
u32 zzgl_sub_dropped(void) { return g_sub_dropped; }
void zzgl_set_evict(int on) { g_evict = on ? 1 : 0; }
void zzgl_tex_counts(u32 *uploads, u32 *downscaled, u32 *refused, u32 *evicted)
{ *uploads = g_uploads_total; *downscaled = g_tex_downscaled; *refused = g_tex_refused; *evicted = g_evicted; }
u32 zzgl_live_textures(void) { u32 i, n = 0u; for (i = 0; i < ZZGL_MAX_TEXTURES; i++) if (G.tex[i].valid) n++; return n; }
static int tex_make_room(u32 pages_needed)
{
    if (!g_evict) return rix_texpool_largest_free() >= pages_needed;
    /* the pool is full: drop the textures not used in the current or the previous frame
       (lightmaps too, ref_gl re-uploads what it needs); ref_gl's own GL_FreeUnusedImages runs
       at level changes, this is the per-frame safety net */
    u32 i;
    for (i = 0; i < ZZGL_MAX_TEXTURES; i++) {
        if (!G.tex[i].valid || i == G.bound) continue;
        if (G.tex[i].last_frame + 2u < G.frame_no) { tex_free_slot(i); g_evicted++; }
        if (rix_texpool_largest_free() >= pages_needed) return 1;
    }
    return rix_texpool_largest_free() >= pages_needed;
}
/* ZZMiniGL: GL_DITHER off = the plain truncation of the base (r>>3, g>>2, b>>3); on = rounded 4x4 Bayer
   in texel space, anchored on the texture (g_dith_x0/y0 = the sub-image offset) */
static const unsigned char bayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static u32 g_dith_x0, g_dith_y0;
static inline u32 quant(u32 v, u32 levels, u32 dith)
{
    return (v * levels * 32u + dith * 255u) / (255u * 32u);
}
static void convert_rgba8_to_565(const unsigned char *src, u32 w, u32 h, u32 src_stride_px, u32 bpp, u16 *dst, u32 dst_stride_px, int *has_alpha)
{
    u32 x, y;
    int alpha = 0;
    for (y = 0; y < h; y++) {
        const unsigned char *s = src + y * src_stride_px * bpp;
        u16 *d = dst + y * dst_stride_px;
        for (x = 0; x < w; x++) {
            u32 r = s[0], g = (bpp == 1u) ? s[0] : s[1], b = (bpp == 1u) ? s[0] : s[2];
            if (bpp == 4u && s[3] < 128u) alpha = 1;
            if (G.dither) {   /* ZZMiniGL */
                u32 dith = 2u * bayer4[(((y + g_dith_y0) & 3u) << 2) | ((x + g_dith_x0) & 3u)] + 1u;
                d[x] = (u16)((quant(r, 31u, dith) << 11) | (quant(g, 63u, dith) << 5) | quant(b, 31u, dith));
            } else
            d[x] = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            s += bpp;
        }
    }
    if (has_alpha) *has_alpha = alpha;
}
/* RGBA5551 (RasterIX: R [15:11] G [10:6] B [5:1] A [0]): the texel alpha survives as 0/1, which is all
   the alpha test of ref_gl (GREATER 0.666 on alpha-keyed pics, sprites, grates) needs */
static void convert_rgba8_to_5551(const unsigned char *src, u32 w, u32 h, u32 src_stride_px, u32 bpp, u16 *dst, u32 dst_stride_px)
{
    u32 x, y;
    for (y = 0; y < h; y++) {
        const unsigned char *s = src + y * src_stride_px * bpp;
        u16 *d = dst + y * dst_stride_px;
        for (x = 0; x < w; x++) {
            u32 r = s[0], g = s[1], b = s[2], a = (bpp == 4u) ? s[3] : 255u;
            if (G.dither) {   /* ZZMiniGL */
                u32 dith = 2u * bayer4[(((y + g_dith_y0) & 3u) << 2) | ((x + g_dith_x0) & 3u)] + 1u;
                d[x] = (u16)((quant(r, 31u, dith) << 11) | (quant(g, 31u, dith) << 6) | (quant(b, 31u, dith) << 1) | (a >= 128u ? 1u : 0u));
            } else
            d[x] = (u16)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a >= 128u ? 1u : 0u));
            s += bpp;
        }
    }
}
/* does the RGBA source carry any transparent texel? (decides the format before the upload) */
static int rgba8_has_alpha(const unsigned char *src, u32 w, u32 h, u32 bpp)
{
    u32 n = w * h, i;
    if (bpp != 4u) return 0;
    for (i = 0; i < n; i++) if (src[i * 4u + 3u] < 128u) return 1;
    return 0;
}
/* ZZHalfLife 0.7.8: does it carry intermediate alpha levels (not just "opaque" and "hole")?
   A tenth of a percent of such texels at least, so that the antialiased rim of an alpha-keyed
   sprite does not cost it its colour depth */
static int rgba8_has_alpha_levels(const unsigned char *src, u32 w, u32 h, u32 bpp)
{
    u32 n = w * h, i, k = 0u;
    if (bpp != 4u) return 0;
    for (i = 0; i < n; i++) { u32 a = src[i * 4u + 3u]; if (a >= 24u && a <= 231u) k++; }
    return k * 1000u >= n && k >= 4u;
}
static void convert_rgba8_to_4444(const unsigned char *src, u32 w, u32 h, u32 src_stride_px, u32 bpp, u16 *dst, u32 dst_stride_px)
{
    u32 x, y;
    for (y = 0; y < h; y++) {
        const unsigned char *s = src + y * src_stride_px * bpp;
        u16 *d = dst + y * dst_stride_px;
        for (x = 0; x < w; x++) {
            u32 r = s[0], g = s[1], b = s[2], a = (bpp == 4u) ? s[3] : 255u;
            /* rounded to the nearest of 16 levels (x * 15 + 127) / 255 */
            r = (r * 15u + 127u) / 255u; g = (g * 15u + 127u) / 255u; b = (b * 15u + 127u) / 255u; a = (a * 15u + 127u) / 255u;
            d[x] = (u16)((r << 12) | (g << 8) | (b << 4) | a);
            s += bpp;
        }
    }
}
/* ZZHalfLife 0.7.7: names are reused.  0.7.6 handed them out monotonically (tex_next_id only
   grew), which Quake II's ref_gl never noticed (it numbers its textures itself); Xash3D's ref_gl
   calls glGenTextures for every texture and glDeleteTextures for the previous map's, so after
   about 2300 textures (a dozen maps) every GenTextures failed: GL_OUT_OF_MEMORY on each upload,
   the default (purple) texture everywhere (A4000, 03/11, c1a0e then c1a0c).  A name is taken
   from GenTextures to DeleteTextures whether or not an image was uploaded (`named`); the search
   starts after the last name given and wraps.  The top ZZGL_RESERVED_NAMES slots are never
   given out: the GL shim keeps the skybox sides (5800.. remapped to 2296..2303) and its
   measurement textures there. */
#define ZZGL_RESERVED_NAMES 16u
void zzglGenTextures(s32 n, u32 *textures)
{
    s32 i;
    const u32 limit = ZZGL_MAX_TEXTURES - ZZGL_RESERVED_NAMES;
    for (i = 0; i < n; i++) {
        u32 tries = 0u, id = G.tex_next_id;
        if (id < 1u || id >= limit) id = 1u;
        while (tries < limit && (G.tex[id].named || G.tex[id].valid)) { id++; if (id >= limit) id = 1u; tries++; }
        if (tries >= limit) { set_error(ZGL_OUT_OF_MEMORY); textures[i] = 0u; continue; }
        G.tex[id].named = 1;
        textures[i] = id;
        G.tex_next_id = id + 1u;
    }
}
/* 0.8.0: while a queued bind may still read a deleted texture's pages (deferred mode, something
   pending), the pages are retired like an old version (freed after the frame, ret_free_old) instead of
   freed at once: a version or an upload could otherwise take and rewrite them first.  Retired list
   full: drained, then freed. */
static void tex_delete_slot(u32 i)
{
    if (G.tex[i].valid && rix_frame_deferred() && rix_frame_pending()) {
        if (g_ret_n < ZZGL_MAX_RETIRED) {
            g_ret[g_ret_n].t = G.tex[i].t; g_ret[g_ret_n].frame = G.frame_no; g_ret_n++;
            G.tex[i].valid = 0;
            return;
        }
        (void)rix_frame_drain(&G.frame);
    }
    tex_free_slot(i);
}
void zzglDeleteTextures(s32 n, const u32 *textures)
{
    s32 i;
    for (i = 0; i < n; i++) if (textures[i] < ZZGL_MAX_TEXTURES) { tex_delete_slot(textures[i]); G.tex[textures[i]].named = 0; if (G.bound == textures[i]) G.bound_sent = 0xFFFFFFFFu; }
}
void zzglBindTexture(u32 target, u32 texnum)
{
    (void)target;
    if (texnum >= ZZGL_MAX_TEXTURES) { set_error(ZGL_INVALID_VALUE); return; }
    G.bound = texnum;
    G.state_dirty = 1;
}
void zzglTexParameterf(u32 target, u32 pname, float param)
{
    zgl_tex *t = &G.tex[G.bound];
    (void)target;
    if (pname == ZGL_TEXTURE_MAG_FILTER) {
        int lin = ((u32)param != ZGL_NEAREST) ? 1 : 0;
        t->mag_linear = lin;
        if (t->valid) { t->t.cfg = tex_cfg_of(t, lin); if (G.bound == G.bound_sent) G.bound_sent = 0xFFFFFFFFu; }
    }
    if (pname == ZGL_TEXTURE_WRAP_S || pname == ZGL_TEXTURE_WRAP_T) {   /* ZZHalfLife 0.8.1 */
        u32 m = (u32)param;
        int cl = (m == ZGL_CLAMP || m == ZGL_CLAMP_TO_EDGE || m == ZGL_CLAMP_TO_BORDER) ? 1 : 0;
        if (pname == ZGL_TEXTURE_WRAP_S) t->clamp_s = cl; else t->clamp_t = cl;
        if (t->valid) { t->t.cfg = tex_cfg_of(t, t->mag_linear); if (G.bound == G.bound_sent) G.bound_sent = 0xFFFFFFFFu; }
    }
    /* MIN_FILTER: one level, the magnification filter decides */
}
/* 0.7.4: a new copy of t's pages; the old ones go to the retired list.  0 = impossible (pool or list full). */
static int tex_new_version(zgl_tex *t)
{
    rix_texture nt;
    u32 i, words, t0 = rix_now();
    volatile u32 *d;
    const volatile u32 *s;
    if (g_ret_n >= ZZGL_MAX_RETIRED) return 0;
    if (rix_tex_alloc(&nt, t->t.w, t->t.h, t->mag_linear)) return 0;
    nt.cfg = t->t.cfg;                                   /* same format (RGB565 / RGBA5551) and filter */
    words = nt.pages * RIX_TEX_PAGE_BYTES / 4u;
    d = (volatile u32 *)rix_ptr(nt.base);
    s = (const volatile u32 *)rix_ptr(t->t.base);
    for (i = 0; i < words; i++) d[i] = s[i];
    dsb();
    if (G.cfg.pool_cacheable) rix_mem_clean(nt.base, nt.pages * RIX_TEX_PAGE_BYTES);
    g_ret[g_ret_n].t = t->t; g_ret[g_ret_n].frame = G.frame_no; g_ret_n++;
    t->t = nt;
    t->bind_seq = 0u; t->bind_frame = 0u;
    if (G.bound == G.bound_sent) G.bound_sent = 0xFFFFFFFFu;
    G.st.tex_versions++;
    G.st.tex_version_us += rix_ticks_to_us((u32)(rix_now() - t0));
    return 1;
}
/* 0.7.9: box-filter halving of an RGB/RGBA8 image (the retry of an upload that does not fit) */
/* ZZMiniGL A6.1: CPU-only scratch, written before it is read; ZZMiniGL links it in the blob's
   non-cached section (.zz_nc, not cleared at start-up) so the cached image keeps its margin */
#if defined(ZZGL_HALF_BUF_NC)
static unsigned char g_half_buf[2][128u * 128u * 4u] __attribute__((section(".zz_nc"), aligned(32)));
#else
static unsigned char g_half_buf[2][128u * 128u * 4u];
#endif
static const unsigned char *rgba8_half(const unsigned char *src, u32 w, u32 h, u32 bpp, int which)
{
    unsigned char *d = g_half_buf[which];
    u32 x, y, c, w2 = w / 2u, h2 = h / 2u;
    for (y = 0; y < h2; y++)
        for (x = 0; x < w2; x++) {
            const unsigned char *a = src + ((2u * y) * w + 2u * x) * bpp, *b = a + w * bpp;
            for (c = 0; c < bpp; c++) d[(y * w2 + x) * bpp + c] = (unsigned char)((a[c] + a[bpp + c] + b[c] + b[bpp + c] + 2u) >> 2);
        }
    return d;
}
void zzglTexImage2D(u32 target, s32 level, s32 internalformat, s32 width, s32 height, s32 border, u32 format, u32 type, const void *pixels)
{
    zgl_tex *t = &G.tex[G.bound];
    u32 w = (u32)width, h = (u32)height, bpp, t0 = rix_now();
    int alpha = 0, rc;
    (void)target; (void)internalformat; (void)border;
    if (level != 0) return;                                       /* no mipmaps: level 0 only */
    if (type != ZGL_UNSIGNED_BYTE || (format != ZGL_RGBA && format != ZGL_RGB && format != ZGL_LUMINANCE)) { bad_enum("glTexImage2D", type != ZGL_UNSIGNED_BYTE ? type : format); return; }
    if (w < 8u || h < 8u || w > 256u || h > 256u || (w & (w - 1u)) || (h & (h - 1u))) { set_error(ZGL_INVALID_VALUE); return; }
    bpp = (format == ZGL_RGBA) ? 4u : ((format == ZGL_RGB) ? 3u : 1u);
    /* 0.7.2, deferred mode: no queued bind may read pages that are about to be freed, reused or
       rewritten: everything submitted and the last bind's fence passed first */
    if ((G.frame_open || rix_frame_pending()) && rix_frame_drain(&G.frame)) { set_error(ZGL_OUT_OF_MEMORY); return; }   /* 0.8.0: between frames too */
    g_uploads_total++;
    t->shift = 0;
    if (!t->valid || t->w != w || t->h != h) {
        u32 pages = (w * h * 2u + RIX_TEX_PAGE_BYTES - 1u) / RIX_TEX_PAGE_BYTES, shift = 0u, asked = 0u;
        tex_free_slot(G.bound);
        for (;;) {
            if (rix_texpool_largest_free() >= pages || tex_make_room(pages)) {
                rc = rix_tex_alloc(&t->t, w, h, t->mag_linear);
                if (!rc) break;
            }
            /* 0.8.3 (build 54): before giving up any resolution, the versions retired by earlier
               frames (no frame advances during a level load, so ret_free_old never runs) and the
               caller's own chance to free (g_oom_cb) */
            if (!asked) {
                asked = 1u;
                ret_free_all();
                if (g_oom_cb) { g_oom_calls++; g_oom_cb(); }
                continue;
            }
            /* 0.7.9: does not fit: half the resolution, twice at most, never under 8 */
            if (!pixels || shift >= 2u || w < 16u || h < 16u) { g_tex_refused++; set_error(ZGL_OUT_OF_MEMORY); return; }
            pixels = rgba8_half((const unsigned char *)pixels, w, h, bpp, (int)shift);
            w /= 2u; h /= 2u; shift++;
            pages = (w * h * 2u + RIX_TEX_PAGE_BYTES - 1u) / RIX_TEX_PAGE_BYTES;
        }
        if (shift) g_tex_downscaled++;
        t->valid = 1; t->w = w; t->h = h; t->shift = (int)shift;
        t->is_lightmap = (G.bound >= 1024u && G.bound < 1152u) ? 1 : 0;
    }
    if (pixels) {
        alpha = rgba8_has_alpha((const unsigned char *)pixels, w, h, bpp);
        t->fmt4444 = 0; t->fmt5551 = 0;
        if (g_alpha4444 && rgba8_has_alpha_levels((const unsigned char *)pixels, w, h, bpp)) {
            convert_rgba8_to_4444((const unsigned char *)pixels, w, h, w, bpp, G.convert, w);
            t->fmt4444 = 1; alpha = 1;
            g_tex4444_count++;
        } else if (alpha && g_alpha_textures) {
            convert_rgba8_to_5551((const unsigned char *)pixels, w, h, w, bpp, G.convert, w);
            t->fmt5551 = 1;
        } else {
            convert_rgba8_to_565((const unsigned char *)pixels, w, h, w, bpp, G.convert, w, NULL);
        }
        t->t.cfg = tex_cfg_of(t, t->mag_linear);
        rix_tex_write_rgb565(&t->t, G.convert, 0);          /* 16-bit texels either way */
        texture_debug_hash_actual(&t->t);                  /* ZZMiniGL: once per upload, never per frame */
    }
    t->alpha = alpha;
    t->last_frame = G.frame_no;
    if (G.bound == G.bound_sent) G.bound_sent = 0xFFFFFFFFu;        /* its content changed: rebind (rule 10: not while the frame that binds it is in flight) */
    rix_frame_texture_invalidate(t->t.base);
    G.st.tex_uploads++;
    G.st.upload_us += rix_ticks_to_us((u32)(rix_now() - t0));
}
void zzglTexSubImage2D(u32 target, s32 level, s32 xoffset, s32 yoffset, s32 width, s32 height, u32 format, u32 type, const void *pixels)
{
    zgl_tex *t = &G.tex[G.bound];
    u32 bpp, t0 = rix_now();
    (void)target;
    if (level != 0 || !pixels) return;
    if (!t->valid) { set_error(ZGL_INVALID_OPERATION); return; }
    if (type != ZGL_UNSIGNED_BYTE || (format != ZGL_RGBA && format != ZGL_RGB && format != ZGL_LUMINANCE)) { bad_enum("glTexSubImage2D", type != ZGL_UNSIGNED_BYTE ? type : format); return; }
    bpp = (format == ZGL_RGBA) ? 4u : ((format == ZGL_RGB) ? 3u : 1u);
    if (t->shift && xoffset >= 0 && yoffset >= 0 && width > 0 && height > 0) {
        /* 0.7.9: a texture stored at a reduced size (the pool was full at its upload): the rectangle
           is reduced the same way when it lies on the reduced grid, dropped otherwise */
        u32 sh = (u32)t->shift, m = (1u << sh) - 1u, ww = (u32)width, hh = (u32)height, k;
        const unsigned char *px = (const unsigned char *)pixels;
        if ((((u32)xoffset | (u32)yoffset | ww | hh) & m) || ww > 256u || hh > 256u) { g_sub_dropped++; return; }
        for (k = 0; k < sh; k++) { px = rgba8_half(px, ww, hh, bpp, (int)k); ww /= 2u; hh /= 2u; }
        pixels = px; xoffset >>= sh; yoffset >>= sh; width = (s32)ww; height = (s32)hh;
    }
    if (xoffset < 0 || yoffset < 0 || width <= 0 || height <= 0 || (u32)(xoffset + width) > t->w || (u32)(yoffset + height) > t->h) { set_error(ZGL_INVALID_VALUE); return; }
    if ((G.frame_open || rix_frame_pending()) && rix_frame_deferred()) {
        /* 0.7.4: pages that a bind of this frame has not read yet are never rewritten: a new version (copy of the
           whole texture, then the rectangle) or, if that is impossible, the 0.7.2 drain.  0.8.0, pipelined: a
           bind of the previous frame too, and between frames */
        int busy = (t->bind_frame + pipe_lag() >= G.frame_no && t->bind_seq != 0u && !rix_frame_bind_passed(&G.frame, t->bind_seq));
#ifdef ZZQ2RIX_HOST
        { extern int zzgl_test_force_busy; if (zzgl_test_force_busy) busy = 1; }   /* bench: every update makes a version */
#endif
        if (busy && !(g_zzgl_versioning && tex_new_version(t))) {
            if (g_zzgl_versioning) G.st.tex_version_fail++;
            if (rix_frame_drain(&G.frame)) { set_error(ZGL_OUT_OF_MEMORY); return; }
        }
    }
    g_dith_x0 = (u32)xoffset; g_dith_y0 = (u32)yoffset;   /* ZZMiniGL: the dither pattern stays anchored on the texture */
    if (t->fmt4444) convert_rgba8_to_4444((const unsigned char *)pixels, (u32)width, (u32)height, (u32)width, bpp, G.convert, (u32)width);
    else if (t->fmt5551) convert_rgba8_to_5551((const unsigned char *)pixels, (u32)width, (u32)height, (u32)width, bpp, G.convert, (u32)width);
    else convert_rgba8_to_565((const unsigned char *)pixels, (u32)width, (u32)height, (u32)width, bpp, G.convert, (u32)width, NULL);
    g_dith_x0 = 0u; g_dith_y0 = 0u;
    rix_tex_write_rect_rgb565(&t->t, (u32)xoffset, (u32)yoffset, (u32)width, (u32)height, G.convert, 0);
    rix_frame_texture_invalidate(t->t.base);
    if (G.bound == G.bound_sent) G.bound_sent = 0xFFFFFFFFu;
    G.st.tex_subuploads++;
    G.st.upload_us += rix_ticks_to_us((u32)(rix_now() - t0));
}
void zzglReadPixels(s32 x, s32 y, s32 w, s32 h, u32 format, u32 type, void *pixels)
{
    /* screenshots: the last swapped buffer, RGB888, bottom-up like OpenGL */
    const volatile u16 *fb = (const volatile u16 *)rix_ptr(G.target.color);
    unsigned char *d = (unsigned char *)pixels;
    s32 i, j;
    if (type != ZGL_UNSIGNED_BYTE || (format != ZGL_RGB && format != ZGL_RGBA)) { bad_enum("glReadPixels", type != ZGL_UNSIGNED_BYTE ? type : format); return; }
    for (j = 0; j < h; j++) {
        s32 gy = y + j;                                            /* OpenGL y-up */
        s32 line = (s32)(rix_geo_h_alloc - 1u) - (gy + 1);         /* memory line of RasterIX y = gy + 1 */
        for (i = 0; i < w; i++) {
            u16 p = (gy >= 0 && gy < (s32)rix_geo_h_alloc - 1 && x + i >= 0 && x + i < (s32)rix_geo_w) ? fb[line * (s32)rix_geo_w + x + i] : 0u;   /* 0.7.6: the whole frame geometry */
            *d++ = (unsigned char)(((p >> 11) & 31u) << 3); *d++ = (unsigned char)(((p >> 5) & 63u) << 2); *d++ = (unsigned char)((p & 31u) << 3);
            if (format == ZGL_RGBA) *d++ = 255u;
        }
    }
}

/* ------------------------------------------------------------------ */
/* primitives                                                          */
/* ------------------------------------------------------------------ */
void zzglColor3f(float r, float g, float b) { G.cur_r = r; G.cur_g = g; G.cur_b = b; G.cur_a = 1.0f; }
void zzglColor3fv(const float *v) { G.cur_r = v[0]; G.cur_g = v[1]; G.cur_b = v[2]; G.cur_a = 1.0f; }
void zzglColor4f(float r, float g, float b, float a) { G.cur_r = r; G.cur_g = g; G.cur_b = b; G.cur_a = a; }
void zzglColor4fv(const float *v) { G.cur_r = v[0]; G.cur_g = v[1]; G.cur_b = v[2]; G.cur_a = v[3]; }
void zzglColor3ubv(const unsigned char *v) { G.cur_r = v[0] / 255.0f; G.cur_g = v[1] / 255.0f; G.cur_b = v[2] / 255.0f; G.cur_a = 1.0f; }
void zzglColor4ubv(const unsigned char *v) { G.cur_r = v[0] / 255.0f; G.cur_g = v[1] / 255.0f; G.cur_b = v[2] / 255.0f; G.cur_a = v[3] / 255.0f; }
void zzglTexCoord2f(float s, float t) { G.cur_s = s; G.cur_t = t; }
void zzglTexCoord2fv(const float *v) { G.cur_s = v[0]; G.cur_t = v[1]; }

/* 0.8.8 (ZZHalfLife build 92): zzgl_set_early_cull (zzgl.h).  draw_triangle finds a back face only
   after it has copied the three vertices, clamped their colours, brought the texture coordinates
   near 0 and projected all three - and a vertex of a strip is projected again for each of the three
   triangles it belongs to.  A4000, 16/11, Xen: of 5 300 triangles of studio models a heavy frame,
   2 450 are thrown away there, on frames where the GPU waits for Core1.
   With it, the screen position of a vertex is worked out ONCE, when the vertex arrives, with the
   arithmetic of rix_project (same operations, same order: same bits), and a triangle whose three
   vertices are in front of the near plane is tested on those before anything else.  A triangle that
   passes goes through draw_triangle untouched, so what is sent is the same, word for word; only
   the work spent on the triangles that were going to be dropped changes.  Costs one division a
   vertex: for the origins named in the mask only (the models), off by default. */
static int g_ec;                                       /* zzgl_set_early_cull */
static u32 g_ec_mask;
static int g_ec_batch;                                 /* this Begin/End keeps the screen positions */
static float g_ec_sx[ZZGL_MAX_VERTS], g_ec_sy[ZZGL_MAX_VERTS];
static u32 g_ec_dropped;
void zzgl_set_early_cull(int on, u32 origin_mask) { g_ec = on ? 1 : 0; g_ec_mask = origin_mask; }
u32 zzgl_early_culled(void) { return g_ec_dropped; }
/* x and y of rix_project (librixgpu/rixclip.c): (x * iw) * scale + trans */
static inline void ec_project(const zgl_vert *v, float *sx, float *sy)
{
    const float iw = 1.0f / v->w;
    *sx = v->x * iw * G.vp.scale_x + G.vp.trans_x;
    *sy = v->y * iw * G.vp.scale_y + G.vp.trans_y;
}
/* the signed area of draw_triangle's cull test, same formula, and its verdict */
static inline float ec_area(float ax, float ay, float bx, float by, float cx, float cy)
{
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}
/* The verdict of draw_triangle's test - fa == 0, or the face turned away - read on the BITS of fa.
   On a Cortex-A9 a float comparison is a VCMP and then a transfer of the FPU's flags to the core,
   the crossing that made the descriptors slow before build 59; a triangle would pay five of them
   here.  Same answer for every bit pattern: +0 and -0 are 0, a negative number or -infinity is
   below 0, a NaN is neither (zzgl_early_cull_check compares the two on this machine). */
static inline int ec_drops(float fa)
{
    u32 b;
    __builtin_memcpy(&b, &fa, sizeof b);              /* zzgl.c is built -fno-builtin: plain memcpy is a call here */
    {
        const u32 m = b & 0x7FFFFFFFu;
        int ccw, front;
        if (m == 0u) return 1;                                        /* fa == 0.0f */
        ccw = (b >> 31) && m <= 0x7F800000u;                          /* fa < 0.0f */
        front = (G.front_face == ZGL_CCW) ? ccw : !ccw;
        return (G.cull_mode == ZGL_BACK && !front) || (G.cull_mode == ZGL_FRONT && front);
    }
}
/* w >= G.w_min on the bits too, when w_min is above 0 (g_ec_wmin, set at zzglBegin): positive
   floats are in the order of their bits, a negative w or -0 has the top bit set and a NaN is above
   +infinity - none of them is in front, as the float comparison says. */
static u32 g_ec_wmin;                                  /* the bits of G.w_min; 0 = w_min is not above 0: compare as floats */
static inline int ec_in_front(const zgl_vert *v)
{
    u32 b;
    if (!g_ec_wmin) return v->w >= G.w_min;
    __builtin_memcpy(&b, &v->w, sizeof b);
    return b >= g_ec_wmin && b <= 0x7F800000u;
}

void zzglBegin(u32 mode)
{
    if (G.in_begin) { set_error(ZGL_INVALID_OPERATION); return; }
    if (mode > ZGL_POLYGON) { bad_enum("glBegin", mode); return; }
    update_mvp();
    {   u32 tf = rix_now();
        if (flush_state()) set_error(ZGL_OUT_OF_MEMORY);
        G.st.flush_ticks += (u32)(rix_now() - tf); }
    G.in_begin = 1; G.begin_mode = mode; G.vb_n = 0u;
    g_ec_batch = g_ec && G.en_cull && G.cull_mode != ZGL_FRONT_AND_BACK && g_org < 32u && ((g_ec_mask >> g_org) & 1u);   /* 0.8.8 */
    if (g_ec_batch) { g_ec_wmin = 0u; if (G.w_min > 0.0f) memcpy(&g_ec_wmin, &G.w_min, sizeof g_ec_wmin); }
    if (G.depth_mask) g_z_clean = 0;                   /* 0.8.6: this batch may write the depth */
    G.st.begins++;
    g_os[g_org].batches++;                             /* 0.8.5 */
}
/* ZZMiniGL: one vertex through the fixed-function stage: clip position (MVP), texture coordinates from
   the attribute or generated (sphere map from the eye-space position and normal through the normal
   matrix, object/eye linear planes), colour.  nrm/uv/col may be NULL: the current values are used.
   Without texgen it computes exactly what the base's add_vertex computed. */
static void xform_vertex(zgl_vert *o, const float *pos, const float *nrm, const float *uv, const float *col)
{
    const float *m = G.mvp;
    float x = pos[0], y = pos[1], z = pos[2];
    o->x = m[0] * x + m[4] * y + m[8] * z + m[12];
    o->y = m[1] * x + m[5] * y + m[9] * z + m[13];
    o->z = m[2] * x + m[6] * y + m[10] * z + m[14];
    o->w = m[3] * x + m[7] * y + m[11] * z + m[15];
    if (G.texgen_any) {
        const float *mv = G.texgen_fixed ? G.texgen_mv : G.mv[G.mv_top];
        const float *nmx;
        float ex = mv[0] * x + mv[4] * y + mv[8] * z + mv[12];
        float ey = mv[1] * x + mv[5] * y + mv[9] * z + mv[13];
        float ez = mv[2] * x + mv[6] * y + mv[10] * z + mv[14];
        float rx = 0.0f, ry = 0.0f, mm = 0.0f;
        int sphere = (G.texgen_on[0] && G.texgen_mode[0] == ZGL_SPHERE_MAP) || (G.texgen_on[1] && G.texgen_mode[1] == ZGL_SPHERE_MAP);
        u32 k;
        if (sphere) {
            /* GL 1.1 2.10.4: u = normalized eye position, n' = unit eye normal, r = u - 2 n'(n'.u),
               m = 2 sqrt(rx^2 + ry^2 + (rz + 1)^2), s = rx/m + 1/2, t = ry/m + 1/2 */
            float n0 = nrm ? nrm[0] : G.cur_nx, n1 = nrm ? nrm[1] : G.cur_ny, n2 = nrm ? nrm[2] : G.cur_nz;
            float nx, ny, nz, nl, vl, ux, uy, uz, d, rz;
            if (G.texgen_fixed) nmx = G.texgen_nm; else { update_normal_matrix(); nmx = G.nm; }
            nx = nmx[0] * n0 + nmx[1] * n1 + nmx[2] * n2;
            ny = nmx[3] * n0 + nmx[4] * n1 + nmx[5] * n2;
            nz = nmx[6] * n0 + nmx[7] * n1 + nmx[8] * n2;
            nl = nx * nx + ny * ny + nz * nz; vl = ex * ex + ey * ey + ez * ez;
            if (nl > 1.0e-30f && vl > 1.0e-30f) {
                nl = 1.0f / sqrtf(nl); vl = 1.0f / sqrtf(vl);
                nx *= nl; ny *= nl; nz *= nl; ux = ex * vl; uy = ey * vl; uz = ez * vl;
                d = 2.0f * (nx * ux + ny * uy + nz * uz);
                rx = ux - nx * d; ry = uy - ny * d; rz = uz - nz * d;
                mm = rx * rx + ry * ry + (rz + 1.0f) * (rz + 1.0f);
                mm = (mm > 1.0e-30f) ? 0.5f / sqrtf(mm) : 0.0f;
            }
        }
        for (k = 0; k < 2u; k++) {
            float v;
            if (!G.texgen_on[k]) { v = k ? (uv ? uv[1] : G.cur_t) : (uv ? uv[0] : G.cur_s); }
            else if (G.texgen_mode[k] == ZGL_SPHERE_MAP) v = (k ? ry : rx) * mm + 0.5f;
            else if (G.texgen_mode[k] == ZGL_OBJECT_LINEAR) { const float *pl = G.texgen_obj[k]; v = pl[0] * x + pl[1] * y + pl[2] * z + pl[3]; }
            else { const float *pl = G.texgen_eye[k]; v = pl[0] * ex + pl[1] * ey + pl[2] * ez + pl[3]; }
            if (k) o->t = v; else o->s = v;
        }
    } else {
        o->s = uv ? uv[0] : G.cur_s; o->t = uv ? uv[1] : G.cur_t;
    }
    if (col) { o->r = col[0]; o->g = col[1]; o->b = col[2]; o->a = col[3]; }
    else { o->r = G.cur_r; o->g = G.cur_g; o->b = G.cur_b; o->a = G.cur_a; }
    G.st.verts++;
}
static void add_vertex(float x, float y, float z)
{
    zgl_vert *o;
    float pos[3];
    if (!G.in_begin) { set_error(ZGL_INVALID_OPERATION); return; }
    if (G.vb_n >= ZZGL_MAX_VERTS) { set_error(ZGL_OUT_OF_MEMORY); return; }
    o = &G.vb[G.vb_n++];
    pos[0] = x; pos[1] = y; pos[2] = z;
    xform_vertex(o, pos, NULL, NULL, NULL);            /* ZZMiniGL: texgen; identical result without it */
    if (g_ec_batch) { const u32 i = G.vb_n - 1u; ec_project(o, &g_ec_sx[i], &g_ec_sy[i]); }   /* 0.8.8 */
}
void zzglVertex2f(float x, float y) { add_vertex(x, y, 0.0f); }
void zzglVertex3f(float x, float y, float z) { add_vertex(x, y, z); }
void zzglVertex3fv(const float *v) { add_vertex(v[0], v[1], v[2]); }
void zzglNormal3f(float x, float y, float z) { G.cur_nx = x; G.cur_ny = y; G.cur_nz = z; }   /* ZZMiniGL */

static int g_zzgl_prof;      /* 0.7.1: zzgl_set_profile */
#if ZZGL_ENABLE_WALK_MODEL
static int g_walkstat;
void zzgl_set_walk_stats(int on) { g_walkstat = on ? 1 : 0; }
#else
void zzgl_set_walk_stats(int on) { (void)on; }
#endif
static int emit_tri(void *ctx, const u32 *desc39)
{
    (void)ctx;
    G.st.tris_out++;
#if ZZGL_ENABLE_WALK_MODEL
    if (g_walkstat) {                                  /* optional edge walker statistics */
        zzgl_origin_stat *os = &g_os[g_org];
        rix_walk_t w;
        rix_walk(desc39, rix_geo_w, rix_geo_h_alloc, &w);
        os->wk_desc++; os->wk_clocks += w.clocks; os->wk_far += w.far_clocks; os->wk_end += w.end_clocks; os->wk_lines += w.lines; os->wk_empty += w.empty;
    }
#endif
    return rix_frame_tri(&G.frame, desc39);
}
/* one triangle: cull (front = counter-clockwise in y-up screen space, OpenGL's default), near clip,
   project, screen clip, fan */
/* ZZMiniGL A8.16: additive triangles (blend destination factor ONE) drawn twice on their shared edges.
   RasterIX draws a pixel whose sample point lies exactly on an edge for both triangles that share it
   (no fill rule: the biased edge values of librixgpu 1.2.2 are not walked correctly by the core), so an
   additive fan (GLQuake's dynamic light flashes and glows: centre + 16 rim vertices, edges at multiples
   of 22.5 degrees on screen) showed a bright line along a horizontal, vertical or diagonal edge whenever
   that edge fell on the sample grid (one frame in 32 or so; GLQuake A8.15 on the A4000: a bright
   diagonal line in explosions; host bench: 181 frames of demo1, up to 270 pixels).  The vertices of
   such triangles are snapped to x = n + 1/2, y = m + 9/32: an edge between two snapped vertices that is
   horizontal, vertical or diagonal can then never pass through a sample point, and any other edge only
   for rare long slopes.  A vertex moves by less than a pixel, the same way for every triangle that
   shares it (no crack).  Only additive triangles are touched. */
int zzgl_additive_snap = 1;
static void snap_additive(rix_vertex *v, u32 n)
{
    u32 k;
    for (k = 0; k < n; k++) {
        float x = (float)(s32)v[k].sx + 0.5f, y = (float)(s32)v[k].sy + 0.28125f;
        if (x > G.clip_x1) x -= 1.0f;
        if (x < G.clip_x0) x += 1.0f;
        if (y > G.clip_y1) y -= 1.0f;
        if (y < G.clip_y0) y += 1.0f;
        v[k].sx = x; v[k].sy = y;
    }
}
static int additive_now(void) { return zzgl_additive_snap && G.en_blend && G.blend_d == ZGL_ONE; }
static void draw_triangle(const zgl_vert *a, const zgl_vert *b, const zgl_vert *c)
{
    rix_clipvert in[3], nc[RIX_CLIP_MAX];
    rix_vertex pv[RIX_CLIP_MAX], scb[RIX_CLIP_MAX];
    const rix_vertex *sc;                              /* ZZMiniGL (A3b): pv itself when already inside the screen */
    u32 n, m, k;
    int all_front;
    const zgl_vert *src[3];
    src[0] = a; src[1] = b; src[2] = c;
    G.st.tris_in++;
    g_os[g_org].tris_in++;                             /* 0.8.5, build 85 */
    if (g_ec_batch && ec_in_front(a) && ec_in_front(b) && ec_in_front(c)) {   /* 0.8.8: the cull test of below, first */
        const u32 ia = (u32)(a - G.vb), ib = (u32)(b - G.vb), ic = (u32)(c - G.vb);
        if (ec_drops(ec_area(g_ec_sx[ia], g_ec_sy[ia], g_ec_sx[ib], g_ec_sy[ib], g_ec_sx[ic], g_ec_sy[ic]))) { G.st.culled++; g_ec_dropped++; return; }
    }
#ifdef ZZGL_NEON
    if (rix_neon) {
    /* the same copy and the same clamps, in NEON.  zgl_vert and rix_clipvert
       have the same layout (x,y,z,w, u,v, r,g,b,a), so the position is one quadword and the colour
       another.  The eight scalar comparisons per vertex were the costly part: on a Cortex-A9 each
       float compare is a VCMP followed by a transfer of the FPU flags to the ARM side, the same
       expensive crossing that the descriptors paid 27 times (build 59).  Twenty-four of them per
       triangle become two instructions.  Same result for every finite value; a NaN colour, which
       the scalar code left as it was, now clamps to 0 or 1, which is better rather than worse. */
    {
        const float32x4_t ZERO = vdupq_n_f32(0.0f), ONE = vdupq_n_f32(1.0f);
        for (k = 0; k < 3u; k++) {
            const zgl_vert *v = src[k];
            const zgl_vert *cs = (G.shade_model == ZGL_FLAT) ? c : v;
            float32x4_t col = vminq_f32(vmaxq_f32(vld1q_f32(&cs->r), ZERO), ONE);
            vst1q_f32(&in[k].x, vld1q_f32(&v->x));     /* x, y, z, w */
            vst1_f32(&in[k].u, vld1_f32(&v->s));       /* u, v */
            vst1q_f32(&in[k].r, col);                  /* r, g, b, a */
        }
    }
    } else
#endif
    for (k = 0; k < 3u; k++) {
        const zgl_vert *v = src[k];
        in[k].x = v->x; in[k].y = v->y; in[k].z = v->z; in[k].w = v->w;
        in[k].u = v->s; in[k].v = v->t;
        if (G.shade_model == ZGL_FLAT) { in[k].r = c->r; in[k].g = c->g; in[k].b = c->b; in[k].a = c->a; }
        else { in[k].r = v->r; in[k].g = v->g; in[k].b = v->b; in[k].a = v->a; }
        if (in[k].r > 1.0f) in[k].r = 1.0f; if (in[k].g > 1.0f) in[k].g = 1.0f; if (in[k].b > 1.0f) in[k].b = 1.0f; if (in[k].a > 1.0f) in[k].a = 1.0f;
        if (in[k].r < 0.0f) in[k].r = 0.0f; if (in[k].g < 0.0f) in[k].g = 0.0f; if (in[k].b < 0.0f) in[k].b = 0.0f; if (in[k].a < 0.0f) in[k].a = 0.0f;
    }
    /* 0.2: texture coordinates brought near 0 by a whole number of repetitions (same texels with
       REPEAT): ref_gl's world polygons carry absolute coordinates (tens of repetitions, hundreds on
       long walls) and the TMU's fixed-point interpolation loses precision far from 0 (ZZQuake2TGL
       had the same cure for TinyGL).  Done per triangle on the smallest coordinate. */
    {
        float mu = in[0].u, mv = in[0].v, fu, fv;
        if (in[1].u < mu) mu = in[1].u; if (in[2].u < mu) mu = in[2].u;
        if (in[1].v < mv) mv = in[1].v; if (in[2].v < mv) mv = in[2].v;
        fu = (float)(int)mu; if (fu > mu) fu -= 1.0f;
        fv = (float)(int)mv; if (fv > mv) fv -= 1.0f;
        /* ZZMiniGL (A4R): not on an axis the texture clamps (GL_CLAMP*): a shift by whole repetitions is
           invisible with REPEAT only; with CLAMP the coordinates must reach the TMU as given. */
        if (G.en_texture && G.tex[G.bound].valid) { if (G.tex[G.bound].clamp_s) fu = 0.0f; if (G.tex[G.bound].clamp_t) fv = 0.0f; }
        if (fu != 0.0f || fv != 0.0f) for (k = 0; k < 3u; k++) { in[k].u -= fu; in[k].v -= fv; }
    }
    /* 0.3: optional shift of the sample point by a fraction of a texel (zzgl_set_texel_bias; ZZQuake2RIX:
       +0.25 for 2D).  A 1:1 2D draw samples exactly at texel centres; a TMU that applies its bilinear
       half-texel offset also in nearest mode would then sit exactly on a texel boundary and pick either
       neighbour.  +0.25 texel lands inside the right texel under both conventions. */
    if (G.texel_bias != 0.0f && G.tex[G.bound].valid) {
        float bu = G.texel_bias / (float)(1u << G.tex[G.bound].t.log2w), bv = G.texel_bias / (float)(1u << G.tex[G.bound].t.log2h);
        for (k = 0; k < 3u; k++) { in[k].u += bu; in[k].v += bv; }
    }
    all_front = (in[0].w >= G.w_min && in[1].w >= G.w_min && in[2].w >= G.w_min);
    /* 0.7.3: no copy of in[] when no vertex is behind the near plane (was a 120-byte memcpy) */
    if (all_front) { n = 3u; rix_project(in, n, &G.vp, pv); }
    else {
        n = rix_clip_near(in, 3u, G.w_min, nc);
        G.st.clipped++;
        if (n < 3u) return;
        rix_project(nc, n, &G.vp, pv);
    }
    if (G.en_cull && G.cull_mode != ZGL_FRONT_AND_BACK) {
        /* 0.3: the signed area in float, same formula and sign as rix_tri_area() (< 0 for a
           counter-clockwise triangle in y-up window coordinates).  rix_tri_area() works in s32
           1/32-pixel units and is only safe AFTER the screen clip: here the vertices are not
           clipped to the screen yet, and a floor or a wall next to the eye projects thousands of
           pixels outside it - the s32 area overflowed, its sign flipped at random and the
           triangle was culled as a back face (ZZQuake2RIX 0.2 on the A4000: near walls and
           floors flickering or vanishing). */
        float fa = (pv[2].sx - pv[0].sx) * (pv[1].sy - pv[0].sy) - (pv[2].sy - pv[0].sy) * (pv[1].sx - pv[0].sx);
        int ccw = (fa < 0.0f), front = (G.front_face == ZGL_CCW) ? ccw : !ccw;
#ifdef ZZQ2RIX_HOST
        { s32 ia = rix_tri_area(&pv[0], &pv[1], &pv[2]); if ((ia < 0) != (fa < 0.0f) && fa != 0.0f) zzgl_dbg_cull_flip++; }
#endif
        if (fa == 0.0f || (G.cull_mode == ZGL_BACK && !front) || (G.cull_mode == ZGL_FRONT && front)) { G.st.culled++; return; }
    } else if (G.en_cull) { G.st.culled++; return; }
    for (k = 0; k < n; k++) { if (pv[k].z < 0.0f) pv[k].z = 0.0f; if (pv[k].z > 1.0f) pv[k].z = 1.0f; }
    /* ZZMiniGL (A3b): the common completely-inside triangle uses the projected vertices directly (the
       bounds are exactly those given to rix_clip_screen: same descriptor); everything else, NaN
       included (the comparisons fail), goes through the clipper as before. */
    if (n == 3u &&
        pv[0].sx >= G.clip_x0 && pv[0].sx <= G.clip_x1 && pv[0].sy >= G.clip_y0 && pv[0].sy <= G.clip_y1 &&
        pv[1].sx >= G.clip_x0 && pv[1].sx <= G.clip_x1 && pv[1].sy >= G.clip_y0 && pv[1].sy <= G.clip_y1 &&
        pv[2].sx >= G.clip_x0 && pv[2].sx <= G.clip_x1 && pv[2].sy >= G.clip_y0 && pv[2].sy <= G.clip_y1) {
        m = 3u; sc = pv; G.st.fast_clip_tris++;
    } else {
        m = rix_clip_screen(pv, n, G.clip_x0, G.clip_y0, G.clip_x1, G.clip_y1, scb);
        sc = scb; G.st.screen_clip_tris++;
        if (m < 3u) return;
        if (m != n) G.st.clipped++;
    }
    if (additive_now()) {   /* A8.16 */
        static rix_vertex snb[RIX_CLIP_MAX];
        for (k = 0; k < m; k++) snb[k] = sc[k];
        snap_additive(snb, m);
        sc = snb;
    }
    {   /* 0.8.5: what this polygon asks of the GPU.  sc[] is the polygon AFTER the screen clip, so
           the fan's areas are the pixels really walked (a sprite half off the screen counts half).
           Before the depth test: a rejected pixel is still a pixel the GPU visits. */
        zzgl_origin_stat *os = &g_os[g_org];
        os->tris += m - 2u;
        if (g_org_px) {
            float a2 = 0.0f;
            for (k = 1; k + 1u < m; k++) {
                float c2 = (sc[k].sx - sc[0].sx) * (sc[k + 1u].sy - sc[0].sy) - (sc[k + 1u].sx - sc[0].sx) * (sc[k].sy - sc[0].sy);
                a2 += c2 < 0.0f ? -c2 : c2;
            }
            if (G.en_blend) os->px_blend += 0.5f * a2; else os->px_opaque += 0.5f * a2;
            if (g_org_box) {                           /* 0.8.6: the box of each triangle of the fan */
                float bx = 0.0f;
                for (k = 1; k + 1u < m; k++) {
                    float x0 = sc[0].sx, x1 = sc[0].sx, y0 = sc[0].sy, y1 = sc[0].sy;
                    if (sc[k].sx < x0) x0 = sc[k].sx; if (sc[k].sx > x1) x1 = sc[k].sx;
                    if (sc[k].sy < y0) y0 = sc[k].sy; if (sc[k].sy > y1) y1 = sc[k].sy;
                    if (sc[k + 1u].sx < x0) x0 = sc[k + 1u].sx; if (sc[k + 1u].sx > x1) x1 = sc[k + 1u].sx;
                    if (sc[k + 1u].sy < y0) y0 = sc[k + 1u].sy; if (sc[k + 1u].sy > y1) y1 = sc[k + 1u].sy;
                    bx += (x1 - x0) * (y1 - y0);
                }
                os->px_box += bx;
            }
        }
    }
    if (g_zzgl_prof) {
        /* 0.7.1: the same loop, timed: descriptor computation and descriptor write apart */
        for (k = 1; k + 1u < m; k++) {
            u32 d[RIX_TRI_DESC_WORDS], t0 = rix_now(), t1, ok;
            ok = (u32)rix_tri_desc(&sc[0], &sc[k], &sc[k + 1u], d);
            t1 = rix_now();
            G.st.desc_ticks += t1 - t0;
            if (ok) {
                int rc = emit_tri(NULL, d);
                G.st.emit_ticks += rix_now() - t1;
                if (rc) { set_error(ZGL_OUT_OF_MEMORY); return; }
            }
        }
        return;
    }
    for (k = 1; k + 1u < m; k++) {
        u32 d[RIX_TRI_DESC_WORDS];
        if (rix_tri_desc(&sc[0], &sc[k], &sc[k + 1u], d)) {
            if (emit_tri(NULL, d)) { set_error(ZGL_OUT_OF_MEMORY); return; }
        }
    }
}
static void zzglEnd_body(void);
void zzglEnd(void)
{
    u32 t0 = rix_now(), w0 = G.frame.bind_wait_us, fo = G.frame_open;
    zzglEnd_body();
    G.st.end_ticks += (u32)(rix_now() - t0);
    if (fo) G.st.end_bwait_us += G.frame.bind_wait_us - w0;
}
void zzgl_set_profile(int on) { g_zzgl_prof = on ? 1 : 0; }
/* 0.8.7: zzgl_set_batch_reject (zzgl.h).  1 when the n vertices (clip space) are all outside the
   same side of the view volume.  A NaN compares false everywhere: such a batch is never dropped.
   Scalar on purpose.  A batch in view is given up at its first vertex inside, and one out of view
   costs five compares a vertex against 2.5 us a triangle.  A NEON form was written and thrown away
   before any run: this compiler (GCC 13.2) turns vclt_f32 / vcgt_f32 into scalar compares and a
   trip through the stack on ARMv7 without fast-math - read in the disassembly, slower than this. */
static int g_batch_reject;
void zzgl_set_batch_reject(int on) { g_batch_reject = on ? 1 : 0; }
static int batch_outside(const zgl_vert *v, u32 n)
{
    u32 i, all = 31u;
    for (i = 0; i < n && all; i++) {
        const float x = v[i].x, y = v[i].y, w = v[i].w;
        u32 o = 0u;
        if (x < -w) o |= 1u; if (x > w) o |= 2u; if (y < -w) o |= 4u; if (y > w) o |= 8u; if (w < G.w_min) o |= 16u;
        all &= o;
    }
    return n != 0u && all != 0u;
}
/* 0.8.7: the check of batch_outside on the machine it runs on (zzgl.h).  The test as compiled
   against a second way of saying it, plane by plane, on 4000 batches made here: batches wholly
   outside one side, the same with ONE vertex put back inside (anywhere), vertices exactly on a
   plane (not outside), a NaN (never dropped), and anything at all. */
static int batch_outside_ref(const zgl_vert *v, u32 n)
{
    u32 i, k;
    if (!n) return 0;
    for (k = 0; k < 5u; k++) {
        for (i = 0; i < n; i++) {
            const float x = v[i].x, y = v[i].y, w = v[i].w;
            const int out = k == 0u ? x < -w : k == 1u ? x > w : k == 2u ? y < -w : k == 3u ? y > w : w < G.w_min;
            if (!out) break;
        }
        if (i == n) return 1;
    }
    return 0;
}
u32 zzgl_batch_reject_check(u32 *batches, u32 *outside)
{
    static zgl_vert v[40];
    const float keep = G.w_min;
    u32 seed = 0x87u, bad = 0u, nout = 0u, b, i;
#define ZBR_RND() (seed = seed * 1664525u + 1013904223u, (seed >> 8) & 0xFFFFu)
#define ZBR_F()   ((float)ZBR_RND() * (1.0f / 65536.0f))            /* 0 .. 1 */
    G.w_min = 0.25f;
    for (b = 0; b < 4000u; b++) {
        const u32 n = 1u + ZBR_RND() % 40u, kind = ZBR_RND() % 10u, side = ZBR_RND() % 5u;
        int ref, a;
        for (i = 0; i < n; i++) {
            float w = 0.5f + 4.0f * ZBR_F(), x = (2.0f * ZBR_F() - 1.0f) * w, y = (2.0f * ZBR_F() - 1.0f) * w;   /* inside */
            if (kind >= 8u) { x *= 3.0f; y *= 3.0f; if (ZBR_RND() & 1u) w = 2.0f * ZBR_F() - 1.0f; }          /* anything */
            else if (side == 0u) x = -w * (1.01f + ZBR_F());
            else if (side == 1u) x = w * (1.01f + ZBR_F());
            else if (side == 2u) y = -w * (1.01f + ZBR_F());
            else if (side == 3u) y = w * (1.01f + ZBR_F());
            else w = 0.24f - ZBR_F();
            v[i].x = x; v[i].y = y; v[i].z = 0.0f; v[i].w = w;
        }
        if (kind >= 3u && kind < 8u) {                 /* one vertex of an outside batch changed */
            zgl_vert *p = &v[ZBR_RND() % n];
            const float w = 0.5f + 4.0f * ZBR_F();
            if (kind == 7u) { union { u32 u; float f; } q; q.u = 0x7FC00000u; p->x = q.f; }                    /* a NaN */
            else if (kind == 6u) {                     /* exactly on the plane it was outside of */
                if (side == 0u) p->x = -p->w; else if (side == 1u) p->x = p->w; else if (side == 2u) p->y = -p->w; else if (side == 3u) p->y = p->w; else p->w = G.w_min;
            } else { p->w = w; p->x = (2.0f * ZBR_F() - 1.0f) * w; p->y = (2.0f * ZBR_F() - 1.0f) * w; }      /* inside */
        }
        ref = batch_outside_ref(v, n);
        if (ref) nout++;
        a = batch_outside(v, n);
        if (!a != !ref) bad++;
    }
#undef ZBR_RND
#undef ZBR_F
    G.w_min = keep;
    if (batches) *batches = 4000u;
    if (outside) *outside = nout;
    return bad;
}

/* 0.8.9 (build 93): what "the same" means between the two projections.  The A4000, 17/11, the check refused the option, 48 triangles of 4975 different, where the PC found none.  An
   emulated Cortex-A9 gives the same 48 with rix_project in NEON and 0 with its scalar path: the 48
   are the triangles this check gives a NaN with its sign bit set.  The scalar FPU hands such a NaN
   on as it is (FFC00000); NEON on ARMv7 always answers the default NaN (7FC00000).  Not a number
   either way, and the verdict - which is what the early test is for - is the same.  So: same bits,
   or a NaN on both sides (counted apart and reported). */
static inline int ec_same(float a, float b)
{
    u32 x, y;
    __builtin_memcpy(&x, &a, sizeof x); __builtin_memcpy(&y, &b, sizeof y);
    if (x == y) return 1;
    return ((x & 0x7FFFFFFFu) > 0x7F800000u && (y & 0x7FFFFFFFu) > 0x7F800000u) ? 2 : 0;
}
static u32 g_ec_chk_nan, g_ec_chk_what;
/* after zzgl_early_cull_check: which of its tests failed (1 the near-plane test on the bits, 2 a
   screen position, 4 the area, 8 the verdict), and on how many triangles a NaN had two spellings */
u32 zzgl_early_cull_check_detail(u32 *nan_two_ways) { if (nan_two_ways) *nan_two_ways = g_ec_chk_nan; return g_ec_chk_what; }

/* 0.8.8: the early test checked where it runs (zzgl.h): 6000 triangles made here under a 640 x 480
   viewport - ordinary ones, slivers, triangles with two vertices at the same place, vertices next
   to the eye (thousands of pixels off the screen), vertices behind the near plane.  For each, the
   positions ec_project gives are compared bit for bit with rix_project's, and the verdict with the
   one draw_triangle reaches, for the four settings of glFrontFace x glCullFace.  Returns the
   triangles on which anything differs: must be 0. */
u32 zzgl_early_cull_check(u32 *tris, u32 *culled)
{
    const rix_viewport keep_vp = G.vp;
    const float keep_w = G.w_min;
    const u32 keep_ff = G.front_face, keep_cm = G.cull_mode;
    u32 seed = 2463534242u, t, k, bad = 0u, ncull = 0u, ntested = 0u;
    g_ec_chk_nan = 0u; g_ec_chk_what = 0u;
#define ZEC_RND() (seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, seed)
#define ZEC_F() ((float)(ZEC_RND() & 0xFFFFFFu) / 16777215.0f)
    G.vp.scale_x = 320.0f; G.vp.trans_x = 320.0f - 0.5f + 0.03125f;
    G.vp.scale_y = 240.0f; G.vp.trans_y = 1.0f + 240.0f - 0.5f;
    G.vp.scale_z = 0.5f; G.vp.trans_z = 0.5f;
    G.w_min = 0.25f;
    for (t = 0; t < 6000u; t++) {
        zgl_vert v[3];
        rix_clipvert in[3];
        rix_vertex pv[3];
        float sx[3], sy[3], fa_e, fa_r;
        const u32 kind = t % 8u;
        int diff = 0, front_all = 1, nan2 = 0;
        memset(v, 0, sizeof v); memset(in, 0, sizeof in);
        for (k = 0; k < 3u; k++) {
            float w = 0.3f + 60.0f * ZEC_F();
            if (kind == 1u) w = 0.2501f + 0.01f * ZEC_F();                 /* next to the eye */
            if (kind == 2u && k == 2u) w = -1.0f + 1.2f * ZEC_F();         /* one vertex behind or just at the near plane */
            v[k].w = w;
            v[k].x = (3.0f * ZEC_F() - 1.5f) * w;
            v[k].y = (3.0f * ZEC_F() - 1.5f) * w;
            v[k].z = (2.0f * ZEC_F() - 1.0f) * w;
            if (kind == 1u) { v[k].x *= 40.0f; v[k].y *= 40.0f; }           /* thousands of pixels outside */
        }
        if (kind == 3u) { v[1] = v[0]; }                                   /* two vertices at the same place: area 0 */
        if (kind == 4u) { v[2].x = v[0].x + (v[1].x - v[0].x) * 0.5f; v[2].y = v[0].y + (v[1].y - v[0].y) * 0.5f; v[2].w = v[0].w + (v[1].w - v[0].w) * 0.5f; }   /* a sliver */
        if (kind == 6u) {                                                  /* the values a comparison on the bits could get wrong */
            static const u32 odd[8] = { 0x7FC00000u /* NaN */, 0xFFC00000u /* -NaN */, 0x7F800000u /* +inf */, 0xFF800000u /* -inf */,
                                        0x80000000u /* -0 */, 0x00000000u /* +0 */, 0x00000001u /* the smallest */, 0x7F7FFFFFu /* the largest */ };
            const u32 w = odd[(t / 8u) % 8u], which = (t / 64u) % 3u;
            float f;
            memcpy(&f, &w, sizeof f);
            if ((t / 192u) & 1u) v[which].w = f; else v[which].x = f;
        }
        if (kind == 7u) { v[0].x *= 1.0e30f; v[1].y *= -1.0e30f; }             /* an area that overflows: infinities, and NaN from inf - inf */
        memcpy(&g_ec_wmin, &G.w_min, sizeof g_ec_wmin);                    /* as zzglBegin sets it: w_min is above 0 here */
        for (k = 0; k < 3u; k++) {
            in[k].x = v[k].x; in[k].y = v[k].y; in[k].z = v[k].z; in[k].w = v[k].w;
            if (!(v[k].w >= G.w_min)) front_all = 0;
            if (!ec_in_front(&v[k]) != !(v[k].w >= G.w_min)) { diff = 1; g_ec_chk_what |= 1u; }   /* the test on the bits against the float comparison */
        }
        if (!front_all) { if (diff) bad++; continue; }                     /* draw_triangle clips those first: the early test leaves them alone */
        ntested++;
        for (k = 0; k < 3u; k++) ec_project(&v[k], &sx[k], &sy[k]);
        rix_project(in, 3u, &G.vp, pv);
        for (k = 0; k < 3u; k++) {
            const int s1 = ec_same(sx[k], pv[k].sx), s2 = ec_same(sy[k], pv[k].sy);
            if (!s1 || !s2) { diff = 1; g_ec_chk_what |= 2u; }
            else if (s1 == 2 || s2 == 2) nan2 = 1;
        }
        fa_e = ec_area(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]);
        fa_r = (pv[2].sx - pv[0].sx) * (pv[1].sy - pv[0].sy) - (pv[2].sy - pv[0].sy) * (pv[1].sx - pv[0].sx);
        { const int s = ec_same(fa_e, fa_r); if (!s) { diff = 1; g_ec_chk_what |= 4u; } else if (s == 2) nan2 = 1; }
        for (k = 0; k < 4u; k++) {
            int ccw, front, ref, e;
            G.front_face = (k & 1u) ? ZGL_CW : ZGL_CCW; G.cull_mode = (k & 2u) ? ZGL_FRONT : ZGL_BACK;
            ccw = (fa_r < 0.0f); front = (G.front_face == ZGL_CCW) ? ccw : !ccw;
            ref = (fa_r == 0.0f || (G.cull_mode == ZGL_BACK && !front) || (G.cull_mode == ZGL_FRONT && front));
            e = ec_drops(fa_e);
            if (!e != !ref) { diff = 1; g_ec_chk_what |= 8u; }              /* the verdict: never anything but equal */
            if (k == 0u && ref) ncull++;
        }
        if (nan2) g_ec_chk_nan++;
        if (diff) bad++;
    }
#undef ZEC_RND
#undef ZEC_F
    G.vp = keep_vp; G.w_min = keep_w; G.front_face = keep_ff; G.cull_mode = keep_cm;
    if (tris) *tris = ntested;
    if (culled) *culled = ncull;
    return bad;
}

/* 0.8.10 (ZZHalfLife build 97): zzglEndIndexed (zzgl.h).  The Begin/End buffer holds each vertex ONCE
   and the triangles are triples of indices into it.  A4000, 17/11: ref_gl draws
   the studio models as indexed triangle lists; unrolled, a vertex was recorded, replayed and
   transformed for each triangle that uses it - 8 682 vertices a frame for 3 544 triangles.
   draw_triangle is given the same three transformed vertices as before, in the same order: what
   reaches the GPU is the same.  The batch test (REJECT) runs on the vertices of the buffer: all of
   them beyond one side = every triangle beyond it. */
static void zzglEndIndexed_body(const unsigned char *idx, u32 n)
{
    u32 i;
    const u32 nv = G.vb_n;
    const zgl_vert *v = G.vb;
    if (!G.in_begin) { set_error(ZGL_INVALID_OPERATION); return; }
    G.in_begin = 0;
    if (G.begin_mode != ZGL_TRIANGLES || !idx) { set_error(ZGL_INVALID_OPERATION); return; }
    if (G.polygon_mode != ZGL_FILL) return;
    if (g_batch_reject && nv >= 3u && batch_outside(v, nv)) {
        const u32 t = n / 3u;
        G.st.tris_in += t; g_os[g_org].tris_in += t; g_os[g_org].tris_rej += t;
        return;
    }
    for (i = 0; i + 2u < n; i += 3u) {
        const u32 a = idx[i], b = idx[i + 1u], c = idx[i + 2u];
        if (a < nv && b < nv && c < nv) draw_triangle(&v[a], &v[b], &v[c]);
    }
}
void zzglEndIndexed(const unsigned char *idx, u32 n)
{
    u32 t0 = rix_now(), w0 = G.frame.bind_wait_us, fo = G.frame_open;
    zzglEndIndexed_body(idx, n);
    G.st.end_ticks += (u32)(rix_now() - t0);
    if (fo) G.st.end_bwait_us += G.frame.bind_wait_us - w0;
}

/* ZZMiniGL A8.23: GL_POINTS.  RasterIX draws triangles only: a point is a square of whole pixels (the
   point size rounded down since A8.24, at least one, at most 16), centred on the vertex, at the vertex's
   depth, two triangles with its colour and texture coordinates.  A8.23 rounded to the nearest: GLQuake
   R200 asks for 1.5 at 320x240 and got squares of 2 pixels, coarse on that screen; it now gets 1.  A point behind the near plane is not drawn.  Points
   are not faces: the cull test does not apply to them. */
static void draw_point(const zgl_vert *v)
{
    zgl_vert q[4];
    float px = (float)(int)G.point_size, hx, hy;
    int cull = G.en_cull;
    if (!(v->w >= G.w_min) || !(v->w > 0.0f)) return;
    if (px < 1.0f) px = 1.0f; if (px > 16.0f) px = 16.0f;
    hx = 0.5f * px * v->w / G.vp.scale_x; hy = 0.5f * px * v->w / G.vp.scale_y;
    q[0] = q[1] = q[2] = q[3] = *v;
    q[0].x -= hx; q[0].y -= hy; q[1].x += hx; q[1].y -= hy; q[2].x += hx; q[2].y += hy; q[3].x -= hx; q[3].y += hy;
    G.en_cull = 0;
    draw_triangle(&q[0], &q[1], &q[2]); draw_triangle(&q[0], &q[2], &q[3]);
    G.en_cull = cull;
}
/* ZZMiniGL A8.27: GL_LINES, GL_LINE_STRIP and GL_LINE_LOOP.  The rasteriser draws triangles only: a line
   is a quad one pixel wide (half a pixel on each side of the segment, measured on the screen), two
   triangles, the attributes of each end carried by its two corners.  No end caps; the width is one
   pixel (glLineWidth is not offered).  A segment with an end behind the near plane is not drawn (it
   would have to be cut there first; nothing asks for it so far).  Lines are not faces: no cull test.
   A segment of no length on the screen draws nothing. */
static void draw_line(const zgl_vert *a, const zgl_vert *b)
{
    zgl_vert q[4];
    float dx, dy, len, nx, ny;
    int cull = G.en_cull;
    if (!(a->w >= G.w_min) || !(a->w > 0.0f) || !(b->w >= G.w_min) || !(b->w > 0.0f)) return;
    dx = (b->x / b->w - a->x / a->w) * G.vp.scale_x; dy = (b->y / b->w - a->y / a->w) * G.vp.scale_y;
    len = sqrtf(dx * dx + dy * dy);
    if (!(len > 1.0e-6f)) return;
    nx = -0.5f * dy / (len * G.vp.scale_x); ny = 0.5f * dx / (len * G.vp.scale_y);   /* half a pixel across, in x/w and y/w */
    q[0] = q[1] = *a; q[2] = q[3] = *b;
    q[0].x -= nx * a->w; q[0].y -= ny * a->w; q[1].x += nx * a->w; q[1].y += ny * a->w;
    q[2].x += nx * b->w; q[2].y += ny * b->w; q[3].x -= nx * b->w; q[3].y -= ny * b->w;
    G.en_cull = 0;
    draw_triangle(&q[0], &q[1], &q[2]); draw_triangle(&q[0], &q[2], &q[3]);
    G.en_cull = cull;
}
static void zzglEnd_body(void)
{
    u32 i, n = G.vb_n;
    const zgl_vert *v = G.vb;
    if (!G.in_begin) { set_error(ZGL_INVALID_OPERATION); return; }
    G.in_begin = 0;
    if (G.polygon_mode != ZGL_FILL) return;                       /* gl_showtris / wireframe: not drawn */
    if (g_batch_reject && n >= 3u && batch_outside(v, n)) {       /* 0.8.7: nothing of this batch is in view */
        u32 t = 0u;
        switch (G.begin_mode) {
        case ZGL_TRIANGLES: t = n / 3u; break;
        case ZGL_TRIANGLE_STRIP: case ZGL_TRIANGLE_FAN: case ZGL_POLYGON: t = n - 2u; break;
        case ZGL_QUADS: t = (n / 4u) * 2u; break;
        case ZGL_QUAD_STRIP: t = n >= 4u ? ((n - 2u) / 2u) * 2u : 0u; break;
        default: break;
        }
        G.st.tris_in += t; g_os[g_org].tris_in += t; g_os[g_org].tris_rej += t;
        return;
    }
    switch (G.begin_mode) {
    case ZGL_TRIANGLES: for (i = 0; i + 2u < n; i += 3u) draw_triangle(&v[i], &v[i + 1u], &v[i + 2u]); break;
    case ZGL_TRIANGLE_STRIP: for (i = 2; i < n; i++) { if (i & 1u) draw_triangle(&v[i - 1u], &v[i - 2u], &v[i]); else draw_triangle(&v[i - 2u], &v[i - 1u], &v[i]); } break;
    case ZGL_TRIANGLE_FAN: case ZGL_POLYGON: for (i = 2; i < n; i++) draw_triangle(&v[0], &v[i - 1u], &v[i]); break;
    case ZGL_QUADS: for (i = 0; i + 3u < n; i += 4u) { draw_triangle(&v[i], &v[i + 1u], &v[i + 2u]); draw_triangle(&v[i], &v[i + 2u], &v[i + 3u]); } break;
    case ZGL_QUAD_STRIP: for (i = 3; i < n; i += 2u) { draw_triangle(&v[i - 3u], &v[i - 2u], &v[i]); draw_triangle(&v[i - 3u], &v[i], &v[i - 1u]); } break;
    case ZGL_POINTS: g_ec_batch = 0; for (i = 0; i < n; i++) draw_point(&v[i]); break;   /* ZZMiniGL A8.23; the early cull reads G.vb only */
    case ZGL_LINES: g_ec_batch = 0; for (i = 0; i + 1u < n; i += 2u) draw_line(&v[i], &v[i + 1u]); break;   /* ZZMiniGL A8.27 */
    case ZGL_LINE_STRIP: g_ec_batch = 0; for (i = 1; i < n; i++) draw_line(&v[i - 1u], &v[i]); break;
    case ZGL_LINE_LOOP: g_ec_batch = 0; for (i = 1; i < n; i++) draw_line(&v[i - 1u], &v[i]); if (n > 2u) draw_line(&v[n - 1u], &v[0]); break;
    default: break;
    }
}

/* ================================================================== */
/* ZZMiniGL (A3 .. A6): client arrays - resident store (GLLockArrays)  */
/* and indexed draws                                                   */
/* ================================================================== */
static float g_store[ZZGL_MAX_ARRAY_VERTS * 12u];   /* packed vertices as received (fmt order: pos, normal, uv, colour) */
static u32 g_store_fmt, g_store_n, g_store_stride;  /* stride in floats */
static zgl_vert g_xv[ZZGL_MAX_ARRAY_VERTS];          /* transformed cache, valid when g_stamp[i] == g_stamp_cur */
static u16 g_stamp[ZZGL_MAX_ARRAY_VERTS];
static u16 g_stamp_cur;
/* A3f: per-draw projection cache of the indexed GL_TRIANGLES path (screen coordinates depend on the
   matrices and the viewport of the call, so they are never reused across draws) */
static rix_vertex g_projected[ZZGL_MAX_ARRAY_VERTS];
static u16 g_project_stamp[ZZGL_MAX_ARRAY_VERTS];
static unsigned char g_project_valid[ZZGL_MAX_ARRAY_VERTS];
static int project_indexed_vertex(const zgl_vert *v, rix_vertex *o)
{
    rix_clipvert in;
    /* the generic path stays the authority for every nontrivial case: only when the per-triangle
       repetition shift is certainly zero, the vertex is in front and inside the screen, and the
       colour needs no clamp */
    if (!(v->w >= G.w_min &&
          v->s >= 0.0f && v->s < 1.0f &&
          v->t >= 0.0f && v->t < 1.0f &&
          v->r >= 0.0f && v->r <= 1.0f &&
          v->g >= 0.0f && v->g <= 1.0f &&
          v->b >= 0.0f && v->b <= 1.0f &&
          v->a >= 0.0f && v->a <= 1.0f)) return 0;
    in.x = v->x; in.y = v->y; in.z = v->z; in.w = v->w;
    in.u = v->s; in.v = v->t;
    in.r = v->r; in.g = v->g; in.b = v->b; in.a = v->a;
    rix_project(&in, 1u, &G.vp, o);
    if (!(o->sx >= G.clip_x0 && o->sx <= G.clip_x1 &&
          o->sy >= G.clip_y0 && o->sy <= G.clip_y1)) return 0;
    if (o->z < 0.0f) o->z = 0.0f;
    if (o->z > 1.0f) o->z = 1.0f;
    return 1;
}
u32 zzgl_arrays_load(u32 fmt, u32 count, const float *packed)
{
    u32 i, stride;
    if (!(fmt & ZGL_VF_POS3) || count == 0u || count > ZZGL_MAX_ARRAY_VERTS) return ZGL_INVALID_VALUE;
    stride = 3u + ((fmt & ZGL_VF_NORMAL) ? 3u : 0u) + ((fmt & ZGL_VF_UV) ? 2u : 0u) + ((fmt & ZGL_VF_COLOR) ? 4u : 0u);
    for (i = 0; i < count * stride; i++) g_store[i] = packed[i];
    g_store_fmt = fmt; g_store_n = count; g_store_stride = stride;
    g_stamp_cur = 0u;
    for (i = 0; i < count; i++) { g_stamp[i] = 0xFFFFu; g_project_stamp[i] = 0xFFFFu; }
    return 0u;
}
void zzgl_arrays_unlock(void) { g_store_n = 0u; }
u32 zzgl_resident_count(void) { return g_store_n; }
static const zgl_vert *resident_vertex(u32 i)
{
    const float *v;
    if (g_stamp[i] == g_stamp_cur) return &g_xv[i];
    v = &g_store[i * g_store_stride];
    {
        const float *nrm = (g_store_fmt & ZGL_VF_NORMAL) ? v + 3 : NULL;
        const float *uv = (g_store_fmt & ZGL_VF_UV) ? v + 3 + (nrm ? 3 : 0) : NULL;
        const float *col = (g_store_fmt & ZGL_VF_COLOR) ? v + 3 + (nrm ? 3 : 0) + (uv ? 2 : 0) : NULL;
        xform_vertex(&g_xv[i], v, nrm, uv, col);
        G.st.verts_xformed++;
    }
    g_stamp[i] = g_stamp_cur;
    return &g_xv[i];
}
/* primitive assembly for a stream of vertices (any mode).  The fan / polygon pivot is copied: the
   caller's vertex storage may be a rolling window (zzgl_draw_arrays). */
typedef struct { const zgl_vert *v[4]; zgl_vert pivot, first; u32 n, k, mode; } zgl_asm;
static void asm_begin(zgl_asm *a, u32 mode) { a->n = 0u; a->k = 0u; a->mode = mode; }
static void asm_push(zgl_asm *a, const zgl_vert *v)
{
    switch (a->mode) {
    case ZGL_TRIANGLES:
        a->v[a->n++] = v; if (a->n == 3u) { draw_triangle(a->v[0], a->v[1], a->v[2]); a->n = 0u; } break;
    case ZGL_TRIANGLE_STRIP:
        a->v[a->n < 3u ? a->n : 2u] = v;
        if (a->n < 3u) a->n++;
        if (a->n == 3u) {
            /* strip parity: odd triangles swap the first two vertices (same as zzglEnd) */
            if (a->k & 1u) draw_triangle(a->v[1], a->v[0], a->v[2]); else draw_triangle(a->v[0], a->v[1], a->v[2]);
            a->k++;
            a->v[0] = a->v[1]; a->v[1] = a->v[2];
        }
        break;
    case ZGL_TRIANGLE_FAN: case ZGL_POLYGON:
        if (a->n == 0u) { a->pivot = *v; a->v[0] = &a->pivot; a->n = 1u; }
        else if (a->n == 1u) { a->v[1] = v; a->n = 2u; }
        else { draw_triangle(a->v[0], a->v[1], v); a->v[1] = v; }
        break;
    case ZGL_QUADS:
        a->v[a->n++] = v;
        if (a->n == 4u) { draw_triangle(a->v[0], a->v[1], a->v[2]); draw_triangle(a->v[0], a->v[2], a->v[3]); a->n = 0u; }
        break;
    case ZGL_QUAD_STRIP:
        a->v[a->n < 4u ? a->n : 3u] = v;
        if (a->n < 4u) a->n++;
        if (a->n == 4u) {
            draw_triangle(a->v[0], a->v[1], a->v[3]); draw_triangle(a->v[0], a->v[3], a->v[2]);
            a->v[0] = a->v[2]; a->v[1] = a->v[3]; a->n = 2u;
        }
        break;
    /* ZZMiniGL A8.27: points and lines of the array draws (draw_point, draw_line).  The previous
       vertex of a strip is copied: the caller's storage may be a rolling window */
    case ZGL_POINTS: draw_point(v); break;
    case ZGL_LINES:
        if (a->n == 0u) { a->pivot = *v; a->n = 1u; } else { draw_line(&a->pivot, v); a->n = 0u; }
        break;
    case ZGL_LINE_STRIP: case ZGL_LINE_LOOP:
        if (a->n == 0u) { a->first = *v; a->pivot = *v; a->n = 1u; }
        else { draw_line(&a->pivot, v); a->pivot = *v; a->n = 2u; a->k++; }
        break;
    default: break;
    }
}
/* the end of the stream: a line loop closes on its first vertex */
static void asm_end(zgl_asm *a)
{
    if (a->mode == ZGL_LINE_LOOP && a->n == 2u && a->k >= 2u) draw_line(&a->pivot, &a->first);
}
/* the state flush and bookkeeping zzglBegin does, for an array draw */
static int draw_prepare(u32 mode)
{
    u32 tf = rix_now();
    if (G.in_begin) { set_error(ZGL_INVALID_OPERATION); return 0; }
    if (mode > ZGL_POLYGON) { bad_enum("glDrawElements", mode); return 0; }
    update_mvp();
    if (flush_state()) { set_error(ZGL_OUT_OF_MEMORY); return 0; }
    G.st.flush_ticks += (u32)(rix_now() - tf);
    g_ec_batch = 0;                                    /* the 0.8.8 early cull keeps screen positions of G.vb only */
    if (G.depth_mask) g_z_clean = 0;                   /* 0.8.6: this batch may write the depth */
    G.st.begins++;
    g_os[g_org].batches++;
    return G.polygon_mode == ZGL_FILL;
}
void zzgl_draw_elements(u32 mode, u32 count, const unsigned short *idx)
{
    zgl_asm a;
    u32 i, t0 = rix_now(), t1;
    if (!draw_prepare(mode)) return;
    if (g_store_n == 0u) { set_error(ZGL_INVALID_OPERATION); return; }
    g_stamp_cur++;
    if (g_stamp_cur == 0xFFFFu) {
        g_stamp_cur = 0u;
        for (i = 0; i < g_store_n; i++) { g_stamp[i] = 0xFFFFu; g_project_stamp[i] = 0xFFFFu; }
    }
    for (i = 0; i < count; i++) if (idx[i] >= g_store_n) { set_error(ZGL_INVALID_VALUE); return; }
    /* transform pass first (texgen is the costly part), then assembly: the two timings stay separate */
    for (i = 0; i < count; i++) (void)resident_vertex(idx[i]);
    t1 = rix_now(); G.st.xform_ticks += (u32)(t1 - t0);
    /* A3f: indexed GL_TRIANGLES project a resident vertex once per draw; anything needing near/screen
       clipping, repetition rebasing, culling, flat shading or a texel bias keeps the generic path */
    if (mode == ZGL_TRIANGLES && !G.en_cull &&
        G.shade_model != ZGL_FLAT && !g_zzgl_prof && G.texel_bias == 0.0f) {
        for (i = 0; i < count; i++) {
            u32 j = (u32)idx[i];
            if (g_project_stamp[j] != g_stamp_cur) {
                g_project_stamp[j] = g_stamp_cur;
                g_project_valid[j] = (unsigned char)project_indexed_vertex(&g_xv[j], &g_projected[j]);
            }
        }
        for (i = 0; i + 2u < count; i += 3u) {
            u32 k0 = idx[i], k1 = idx[i + 1u], k2 = idx[i + 2u];
            if (g_project_valid[k0] && g_project_valid[k1] && g_project_valid[k2]) {
                u32 d[RIX_TRI_DESC_WORDS];
                G.st.tris_in++; g_os[g_org].tris_in++; g_os[g_org].tris++;
                G.st.fast_clip_tris++;
                if (additive_now()) {   /* A8.16 */
                    rix_vertex t3[3];
                    t3[0] = g_projected[k0]; t3[1] = g_projected[k1]; t3[2] = g_projected[k2];
                    snap_additive(t3, 3u);
                    if (rix_tri_desc(&t3[0], &t3[1], &t3[2], d) && emit_tri(NULL, d)) { set_error(ZGL_OUT_OF_MEMORY); return; }
                } else if (rix_tri_desc(&g_projected[k0], &g_projected[k1], &g_projected[k2], d) &&
                    emit_tri(NULL, d)) { set_error(ZGL_OUT_OF_MEMORY); return; }
            } else {
                draw_triangle(&g_xv[k0], &g_xv[k1], &g_xv[k2]);
            }
        }
        G.st.idx_tris += count / 3u;
    } else {
        asm_begin(&a, mode);
        for (i = 0; i < count; i++) asm_push(&a, &g_xv[idx[i]]);
        asm_end(&a);
        if (mode == ZGL_TRIANGLES) G.st.idx_tris += count / 3u;
    }
    G.st.setup_ticks += (u32)(rix_now() - t1);
}
void zzgl_draw_arrays(u32 mode, u32 fmt, u32 count, const float *packed)
{
    zgl_asm a;
    zgl_vert win[4];
    u32 i, stride, w = 0u, t0 = rix_now();
    if (!draw_prepare(mode)) return;
    if (!(fmt & ZGL_VF_POS3)) { set_error(ZGL_INVALID_VALUE); return; }
    stride = 3u + ((fmt & ZGL_VF_NORMAL) ? 3u : 0u) + ((fmt & ZGL_VF_UV) ? 2u : 0u) + ((fmt & ZGL_VF_COLOR) ? 4u : 0u);
    asm_begin(&a, mode);
    for (i = 0; i < count; i++) {
        const float *v = packed + i * stride;
        const float *nrm = (fmt & ZGL_VF_NORMAL) ? v + 3 : NULL;
        const float *uv = (fmt & ZGL_VF_UV) ? v + 3 + (nrm ? 3 : 0) : NULL;
        const float *col = (fmt & ZGL_VF_COLOR) ? v + 3 + (nrm ? 3 : 0) + (uv ? 2 : 0) : NULL;
        /* a rolling window of 4: the assembler keeps at most the last 3 (strips, quads); the fan pivot is copied */
        zgl_vert *o = &win[w]; w = (w + 1u) & 3u;
        xform_vertex(o, v, nrm, uv, col);
        G.st.verts_xformed++;
        asm_push(&a, o);
    }
    asm_end(&a);
    G.st.setup_ticks += (u32)(rix_now() - t0);
}
/* ==== end ZZMiniGL ==== */
