/* zzgl.c - ZZGL 0.1 - Quake II's ref_gl OpenGL subset on librixgpu 1.2 (see zzgl.h)
 * SPDX-License-Identifier: 0BSD
 */
#include "zzgl.h"
#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    rix_texture t;
    u32 w, h;
    int valid, alpha, mag_linear, is_lightmap;
    int fmt5551;         /* 0.2: stored as RGBA5551 (alpha-keyed texture, zzgl_set_alpha_textures on) */
    u32 last_frame;
    u32 bind_seq, bind_frame;   /* 0.7.4: last bind of these pages (librixgpu sequence) and the frame it was in */
} zgl_tex;

typedef struct { float x, y, z, w; float s, t; float r, g, b, a; } zgl_vert;

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
    /* textures */
    zgl_tex tex[ZZGL_MAX_TEXTURES];
    u32 bound, bound_sent;
    u32 tex_next_id;
    u32 error;
    zzgl_stats st;
    u16 convert[256u * 256u];
} G;

static const float IDENTITY[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
#ifdef ZZQ2RIX_HOST
unsigned zzgl_dbg_cull_flip;   /* host bench: triangles whose s32 area sign disagreed with the float one */
#endif
static int g_alpha_textures;   /* 0.2: alpha-keyed uploads as RGBA5551 (zzgl_set_alpha_textures) */
void zzgl_set_alpha_textures(int on) { g_alpha_textures = on ? 1 : 0; }
void zzgl_set_texel_bias(float texels) { G.texel_bias = texels; }

static void set_error(u32 e) { if (G.error == ZGL_NO_ERROR) G.error = e; G.st.errors++; }

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
static void matrices_changed(void) { G.mvp_dirty = 1; }
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

void zzglMatrixMode(u32 mode) { if (mode == ZGL_MODELVIEW || mode == ZGL_PROJECTION) G.matrix_mode = mode; else if (mode == ZGL_TEXTURE) G.matrix_mode = mode; else set_error(ZGL_INVALID_ENUM); }
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
    else set_error(ZGL_INVALID_ENUM);
}
void zzglGetIntegerv(u32 pname, s32 *params)
{
    if (pname == ZGL_MAX_TEXTURE_SIZE) *params = 256;
    else set_error(ZGL_INVALID_ENUM);
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
    if (G.clip_x1 > (float)rix_lay_w - 0.53125f) G.clip_x1 = (float)rix_lay_w - 0.53125f;   /* A2.15: the right clip of the frame layout */
    if (G.clip_y0 < RIX_CLIP_Y0) G.clip_y0 = RIX_CLIP_Y0;
    if (G.clip_y1 > (float)rix_lay_h - 0.53125f) G.clip_y1 = (float)rix_lay_h - 0.53125f;
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
    if (G.en_depth) f |= RIX_FEAT_DEPTH_TEST;
    if (G.en_blend) f |= RIX_FEAT_BLENDING;
    if (G.en_alpha) f |= RIX_FEAT_ALPHA_TEST;
    return f;
}
static u32 current_fragment(void)
{
    u32 ref = (u32)(G.alpha_ref * 255.0f + 0.5f);
    u32 fr;
    if (ref > 255u) ref = 255u;
    fr = rix_fragment(G.en_depth ? map_func(G.depth_func) : RIX_TF_ALWAYS, G.depth_mask, map_func(G.alpha_func), ref, map_blend(G.blend_s), map_blend(G.blend_d));
    if (!G.color_mask) fr &= ~RIX_FRAG_COLOR_MASK;
    return fr;
}
static int frame_begin_if_needed(void);
/* 0.7.4: retired texture versions: pages a queued or running bind may still read, freed only once the frame
   they were retired in is over (the caller waits every frame's fence before the next frame begins) */
#define ZZGL_MAX_RETIRED 64u
static struct { rix_texture t; u32 frame; } g_ret[ZZGL_MAX_RETIRED];
static u32 g_ret_n;
static int g_zzgl_versioning;
void zzgl_set_tex_versioning(int on) { g_zzgl_versioning = on ? 1 : 0; }
static void ret_free_old(void)
{
    u32 i, k = 0u;
    for (i = 0; i < g_ret_n; i++) {
        if (g_ret[i].frame < G.frame_no) rix_tex_free(&g_ret[i].t);
        else g_ret[k++] = g_ret[i];
    }
    g_ret_n = k;
}
static int g_zzgl_defer_req;   /* 0.7.2: zzgl_set_deferred, applied when the next frame opens */
static int g_zzgl_clear_flush; /* 0.7.5 */
void zzgl_set_clear_flush(int on) { g_zzgl_clear_flush = on ? 1 : 0; }
/* A2.13 (ZZOpenLaraRIX): clear split for the measurement (SYNC mode: every chunk waited, so the wait of a
   chunk holding one clear alone is that clear's GPU time) and colour clears already done by Core1 */
static int g_zzgl_clear_split;
static int (*g_zzgl_precleared)(void);
u32 zzgl_clr_us[2], zzgl_clr_n[2], zzgl_preclear_hits;
void zzgl_set_clear_split(int on) { g_zzgl_clear_split = on ? 1 : 0; }
void zzgl_set_preclear_hook(int (*fn)(void)) { g_zzgl_precleared = fn; }
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
            if (x0 < 0) x0 = 0; if (y0 < 1) y0 = 1; if (x1 > (s32)rix_lay_w) x1 = (s32)rix_lay_w; if (y1 > (s32)rix_lay_h) y1 = (s32)rix_lay_h;
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
    default: set_error(ZGL_INVALID_ENUM); return;
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
    default: set_error(ZGL_INVALID_ENUM); return;
    }
    G.state_dirty = 1;
}
void zzglDepthFunc(u32 func) { G.depth_func = func; G.state_dirty = 1; }
void zzglDepthMask(u32 flag) { G.depth_mask = flag ? 1 : 0; G.state_dirty = 1; }
void zzglBlendFunc(u32 s, u32 d) { G.blend_s = s; G.blend_d = d; G.state_dirty = 1; }
void zzglAlphaFunc(u32 func, float ref) { G.alpha_func = func; G.alpha_ref = ref; G.state_dirty = 1; }
void zzglColorMask(u32 r, u32 g, u32 b, u32 a) { G.color_mask = (r || g || b || a) ? 1 : 0; G.state_dirty = 1; }
void zzglCullFace(u32 mode) { G.cull_mode = mode; }
void zzglFrontFace(u32 mode) { G.front_face = mode; }
void zzglShadeModel(u32 mode) { G.shade_model = mode; }
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
/* A2.13: the clear of zzglClear, the scissor box obeyed (OpenGL) */
static int clear_rect(int color, int depth, u32 d16)
{
    if (G.scissor_on && G.sc_w > 0 && G.sc_h > 0) {
        s32 x0 = G.sc_x, y0 = G.sc_y + 1, x1 = G.sc_x + G.sc_w, y1 = G.sc_y + 1 + G.sc_h;
        if (x0 < 0) x0 = 0; if (y0 < 1) y0 = 1; if (x1 > (s32)rix_lay_w) x1 = (s32)rix_lay_w; if (y1 > (s32)rix_lay_h) y1 = (s32)rix_lay_h;
        return rix_frame_clear(&G.frame, color, G.clear_rgba, depth, d16, (u32)x0, (u32)y0, (u32)x1, (u32)y1);
    }
    return rix_frame_clear(&G.frame, color, G.clear_rgba, depth, d16, 0u, 0u, 0u, 0u);
}
void zzglClear(u32 mask)
{
    int rc, full;
    u32 d16 = (u32)(G.clear_depth * 65535.0f + 0.5f);
    int color = (mask & ZGL_COLOR_BUFFER_BIT) ? 1 : 0, depth = (mask & ZGL_DEPTH_BUFFER_BIT) && G.cfg.zbuf ? 1 : 0;
    if (d16 > 0xFFFFu) d16 = 0xFFFFu;
    if (!color && !depth) return;
    rc = frame_begin_if_needed();
    if (rc) { set_error(ZGL_OUT_OF_MEMORY); return; }
    /* A2.13: the colour clear of a frame whose buffer Core1 has already filled with black (RIXPRECLEAR):
       first draw of the frame, black, the whole viewport; the hook answers for this frame's buffer and
       forgets it (a second clear in the frame is done by the GPU) */
    full = !G.scissor_on || (G.sc_x <= 0 && G.sc_y <= 0 && G.sc_x + G.sc_w >= (s32)G.cfg.width && G.sc_y + G.sc_h >= (s32)G.cfg.height);
    if (color && g_zzgl_precleared && G.frame.tris == 0u && (G.clear_rgba & 0xFFFFFF00u) == 0u && full && g_zzgl_precleared()) {
        color = 0;
        zzgl_preclear_hits++;
        if (!depth) return;
    }
    /* A2.13: SYNC mode measurement, full-screen colour + depth clear: each alone in its own chunk */
    if (g_zzgl_clear_split && color && depth && full) {
        u32 w;
        int k;
        rc = rix_frame_flush(&G.frame);                        /* what came before: not counted */
        for (k = 0; k < 2 && rc == 0; k++) {
            w = G.frame.ringwait_us;
            rc = clear_rect(k == 0, k == 1, d16);
            if (rc == 0) rc = rix_frame_flush(&G.frame);
            zzgl_clr_us[k] += G.frame.ringwait_us - w;
            zzgl_clr_n[k]++;
        }
        if (rc) set_error(ZGL_OUT_OF_MEMORY);
        G.features_sent = 0xFFFFFFFFu; G.fragment_sent = 0xFFFFFFFFu; G.state_dirty = 1;
        return;
    }
    /* the clear obeys the scissor when enabled (OpenGL): rect from the scissor box, else full */
    if (G.scissor_on && G.sc_w > 0 && G.sc_h > 0) {
        s32 x0 = G.sc_x, y0 = G.sc_y + 1, x1 = G.sc_x + G.sc_w, y1 = G.sc_y + 1 + G.sc_h;
        if (x0 < 0) x0 = 0; if (y0 < 1) y0 = 1; if (x1 > (s32)rix_lay_w) x1 = (s32)rix_lay_w; if (y1 > (s32)rix_lay_h) y1 = (s32)rix_lay_h;
        rc = rix_frame_clear(&G.frame, color, G.clear_rgba, depth, d16, (u32)x0, (u32)y0, (u32)x1, (u32)y1);
    } else rc = rix_frame_clear(&G.frame, color, G.clear_rgba, depth, d16, 0u, 0u, 0u, 0u);
    if (rc) set_error(ZGL_OUT_OF_MEMORY);
    G.features_sent = 0xFFFFFFFFu; G.fragment_sent = 0xFFFFFFFFu; G.state_dirty = 1;   /* the memsets rewrote both registers; chunk_restore put the frame's back */
    /* 0.7.5: the depth clear of a frame costs the GPU ~2-3 ms; closing its chunk at once lets the GPU clear
       while Core1 walks the BSP (zzgl_set_clear_flush) */
    if (g_zzgl_clear_flush && depth && rc == 0 && rix_frame_flush(&G.frame)) set_error(ZGL_OUT_OF_MEMORY);
    G.features_sent = 0xFFFFFFFFu; G.fragment_sent = 0xFFFFFFFFu; G.state_dirty = 1;   /* chunk_restore of the new chunk */
}
/* 0.7.5: pump the deferred queue from outside a draw (the presentation loop) */
void zzgl_pump(void) { if (G.frame_open) (void)rix_frame_pump(&G.frame); }

/* ------------------------------------------------------------------ */
/* frame                                                               */
/* ------------------------------------------------------------------ */
static void inv255_init(void);
int zzgl_init(const zzgl_config *cfg)
{
    int rc;
    memset(&G, 0, sizeof G);
    inv255_init();
    G.cfg = *cfg;
    rc = rix_texpool_init(cfg->pool_base, cfg->pool_pages, cfg->pool_cacheable);
    if (rc) return rc;
    G.matrix_mode = ZGL_MODELVIEW;
    memcpy(G.mv[0], IDENTITY, 64u); memcpy(G.pr[0], IDENTITY, 64u); G.mvp_dirty = 1;
    G.vp_x = 0; G.vp_y = 0; G.vp_w = (s32)cfg->width; G.vp_h = (s32)cfg->height; G.dr_near = 0.0f; G.dr_far = 1.0f; viewport_update();
    G.depth_func = ZGL_LESS; G.depth_mask = 1; G.color_mask = 1; G.blend_s = ZGL_ONE; G.blend_d = ZGL_ZERO; G.alpha_func = ZGL_ALWAYS; G.alpha_ref = 0.0f;
    G.cull_mode = ZGL_BACK; G.front_face = ZGL_CCW; G.shade_model = ZGL_SMOOTH; G.polygon_mode = ZGL_FILL; G.tex_env = ZGL_MODULATE;
    G.cur_r = G.cur_g = G.cur_b = G.cur_a = 1.0f; G.clear_depth = 1.0f; G.clear_rgba = RIX_RGBA(0u, 0u, 0u, 255u);
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
    d.features = current_features(); d.fragment = current_fragment();
    d.clear_color = 0; d.clear_depth = 0;             /* clears are in-stream (zzglClear) */
    rix_frame_set_deferred(g_zzgl_defer_req);   /* 0.7.2: only between frames (the queue is empty here) */
    rc = rix_frame_begin(&G.frame, &d);
    if (rc) return rc;
    G.frame_open = 1;
    G.frame_no++;
    ret_free_old();                                   /* 0.7.4: versions retired in earlier frames (their fences passed) */
    G.features_sent = d.features; G.fragment_sent = d.fragment; G.env_sent = 0xFFFFFFFFu; G.env_dirty = 1;
    G.bound_sent = 0xFFFFFFFFu; G.scissor_dirty = 1; G.state_dirty = 0;
    memset(&G.st, 0, sizeof G.st);
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
static int tex_make_room(u32 pages_needed)
{
    /* the pool is full: drop the textures not used in the current or the previous frame
       (lightmaps too, ref_gl re-uploads what it needs); ref_gl's own GL_FreeUnusedImages runs
       at level changes, this is the per-frame safety net */
    u32 i;
    for (i = 0; i < ZZGL_MAX_TEXTURES; i++) {
        if (!G.tex[i].valid || i == G.bound) continue;
        if (G.tex[i].last_frame + 2u < G.frame_no) tex_free_slot(i);
        if (rix_texpool_largest_free() >= pages_needed) return 1;
    }
    return rix_texpool_largest_free() >= pages_needed;
}
static void convert_rgba8_to_565(const unsigned char *src, u32 w, u32 h, u32 src_stride_px, u32 bpp, u16 *dst, u32 dst_stride_px, int *has_alpha)
{
    u32 x, y;
    int alpha = 0;
    for (y = 0; y < h; y++) {
        const unsigned char *s = src + y * src_stride_px * bpp;
        u16 *d = dst + y * dst_stride_px;
        for (x = 0; x < w; x++) {
            u32 r = s[0], g = s[1], b = s[2];
            if (bpp == 4u && s[3] < 128u) alpha = 1;
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
void zzglGenTextures(s32 n, u32 *textures)
{
    s32 i;
    for (i = 0; i < n; i++) {
        while (G.tex_next_id < ZZGL_MAX_TEXTURES && G.tex[G.tex_next_id].valid) G.tex_next_id++;
        if (G.tex_next_id >= ZZGL_MAX_TEXTURES) { set_error(ZGL_OUT_OF_MEMORY); textures[i] = 0u; continue; }
        textures[i] = G.tex_next_id++;
    }
}
void zzglDeleteTextures(s32 n, const u32 *textures)
{
    s32 i;
    for (i = 0; i < n; i++) if (textures[i] < ZZGL_MAX_TEXTURES) { tex_free_slot(textures[i]); if (G.bound == textures[i]) G.bound_sent = 0xFFFFFFFFu; }
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
        if (t->valid) { t->t.cfg = t->fmt5551 ? RIX_TMU_CFG_RGBA5551(t->t.log2w, t->t.log2h, lin) : RIX_TMU_CFG_RGB565(t->t.log2w, t->t.log2h, lin); if (G.bound == G.bound_sent) G.bound_sent = 0xFFFFFFFFu; }
    }
    /* MIN_FILTER: one level, the magnification filter decides; WRAP: repeat (clamp not exposed in 0.1) */
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
void zzglTexImage2D(u32 target, s32 level, s32 internalformat, s32 width, s32 height, s32 border, u32 format, u32 type, const void *pixels)
{
    zgl_tex *t = &G.tex[G.bound];
    u32 w = (u32)width, h = (u32)height, bpp, t0 = rix_now();
    int alpha = 0, rc;
    (void)target; (void)internalformat; (void)border;
    if (level != 0) return;                                       /* no mipmaps: level 0 only */
    if (type != ZGL_UNSIGNED_BYTE || (format != ZGL_RGBA && format != ZGL_RGB)) { set_error(ZGL_INVALID_ENUM); return; }
    if (w < 8u || h < 8u || w > 256u || h > 256u || (w & (w - 1u)) || (h & (h - 1u))) { set_error(ZGL_INVALID_VALUE); return; }
    bpp = (format == ZGL_RGBA) ? 4u : 3u;
    /* 0.7.2, deferred mode: no queued bind may read pages that are about to be freed, reused or
       rewritten: everything submitted and the last bind's fence passed first */
    if (G.frame_open && rix_frame_drain(&G.frame)) { set_error(ZGL_OUT_OF_MEMORY); return; }
    if (!t->valid || t->w != w || t->h != h) {
        u32 pages = (w * h * 2u + RIX_TEX_PAGE_BYTES - 1u) / RIX_TEX_PAGE_BYTES;
        tex_free_slot(G.bound);
        if (rix_texpool_largest_free() < pages && !tex_make_room(pages)) { set_error(ZGL_OUT_OF_MEMORY); return; }
        rc = rix_tex_alloc(&t->t, w, h, t->mag_linear);
        if (rc) { set_error(ZGL_OUT_OF_MEMORY); return; }
        t->valid = 1; t->w = w; t->h = h;
        t->is_lightmap = (G.bound >= 1024u && G.bound < 1152u) ? 1 : 0;
    }
    if (pixels) {
        alpha = rgba8_has_alpha((const unsigned char *)pixels, w, h, bpp);
        if (alpha && g_alpha_textures) {
            convert_rgba8_to_5551((const unsigned char *)pixels, w, h, w, bpp, G.convert, w);
            t->t.cfg = RIX_TMU_CFG_RGBA5551(t->t.log2w, t->t.log2h, t->mag_linear);
            t->fmt5551 = 1;
        } else {
            convert_rgba8_to_565((const unsigned char *)pixels, w, h, w, bpp, G.convert, w, NULL);
            t->t.cfg = RIX_TMU_CFG_RGB565(t->t.log2w, t->t.log2h, t->mag_linear);
            t->fmt5551 = 0;
        }
        rix_tex_write_rgb565(&t->t, G.convert, 0);          /* 16-bit texels either way */
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
    if (type != ZGL_UNSIGNED_BYTE || (format != ZGL_RGBA && format != ZGL_RGB)) { set_error(ZGL_INVALID_ENUM); return; }
    if (xoffset < 0 || yoffset < 0 || width <= 0 || height <= 0 || (u32)(xoffset + width) > t->w || (u32)(yoffset + height) > t->h) { set_error(ZGL_INVALID_VALUE); return; }
    bpp = (format == ZGL_RGBA) ? 4u : 3u;
    if (G.frame_open && rix_frame_deferred()) {
        /* 0.7.4: pages that a bind of this frame has not read yet are never rewritten: a new version (copy of the
           whole texture, then the rectangle) or, if that is impossible, the 0.7.2 drain */
        int busy = (t->bind_frame == G.frame_no && t->bind_seq != 0u && !rix_frame_bind_passed(&G.frame, t->bind_seq));
#ifdef ZZQ2RIX_HOST
        { extern int zzgl_test_force_busy; if (zzgl_test_force_busy) busy = 1; }   /* bench: every update makes a version */
#endif
        if (busy && !(g_zzgl_versioning && tex_new_version(t))) {
            if (g_zzgl_versioning) G.st.tex_version_fail++;
            if (rix_frame_drain(&G.frame)) { set_error(ZGL_OUT_OF_MEMORY); return; }
        }
    }
    if (t->fmt5551) convert_rgba8_to_5551((const unsigned char *)pixels, (u32)width, (u32)height, (u32)width, bpp, G.convert, (u32)width);
    else convert_rgba8_to_565((const unsigned char *)pixels, (u32)width, (u32)height, (u32)width, bpp, G.convert, (u32)width, NULL);
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
    if (type != ZGL_UNSIGNED_BYTE || (format != ZGL_RGB && format != ZGL_RGBA)) { set_error(ZGL_INVALID_ENUM); return; }
    for (j = 0; j < h; j++) {
        s32 gy = y + j;                                            /* OpenGL y-up */
        s32 line = (s32)(rix_lay_h - 1u) - (gy + 1);             /* memory line of RasterIX y = gy + 1 */
        for (i = 0; i < w; i++) {
            u16 p = (gy >= 0 && gy < (s32)G.cfg.height && x + i >= 0 && x + i < (s32)rix_lay_w) ? fb[line * (s32)rix_lay_w + x + i] : 0u;
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
/* ZZOpenLara A2.9: 1/255 from a table, the same floats as the division (correctly rounded both ways); the
   Cortex-A9 VFP divides in ~14 cycles, four times per vertex here */
static float g_inv255[256];
static void inv255_init(void) { u32 k; for (k = 0; k < 256u; k++) g_inv255[k] = (float)k / 255.0f; }
void zzglColor3ubv(const unsigned char *v) { G.cur_r = g_inv255[v[0]]; G.cur_g = g_inv255[v[1]]; G.cur_b = g_inv255[v[2]]; G.cur_a = 1.0f; }
void zzglColor4ubv(const unsigned char *v) { G.cur_r = g_inv255[v[0]]; G.cur_g = g_inv255[v[1]]; G.cur_b = g_inv255[v[2]]; G.cur_a = g_inv255[v[3]]; }
void zzglTexCoord2f(float s, float t) { G.cur_s = s; G.cur_t = t; }
void zzglTexCoord2fv(const float *v) { G.cur_s = v[0]; G.cur_t = v[1]; }

void zzglBegin(u32 mode)
{
    if (G.in_begin) { set_error(ZGL_INVALID_OPERATION); return; }
    if (mode > ZGL_POLYGON) { set_error(ZGL_INVALID_ENUM); return; }
    update_mvp();
    {   u32 tf = rix_now();
        if (flush_state()) set_error(ZGL_OUT_OF_MEMORY);
        G.st.flush_ticks += (u32)(rix_now() - tf); }
    G.in_begin = 1; G.begin_mode = mode; G.vb_n = 0u;
    G.st.begins++;
}
static void add_vertex(float x, float y, float z)
{
    const float *m = G.mvp;
    zgl_vert *o;
    if (!G.in_begin) { set_error(ZGL_INVALID_OPERATION); return; }
    if (G.vb_n >= ZZGL_MAX_VERTS) { set_error(ZGL_OUT_OF_MEMORY); return; }
    o = &G.vb[G.vb_n++];
    o->x = m[0] * x + m[4] * y + m[8] * z + m[12];
    o->y = m[1] * x + m[5] * y + m[9] * z + m[13];
    o->z = m[2] * x + m[6] * y + m[10] * z + m[14];
    o->w = m[3] * x + m[7] * y + m[11] * z + m[15];
    o->s = G.cur_s; o->t = G.cur_t; o->r = G.cur_r; o->g = G.cur_g; o->b = G.cur_b; o->a = G.cur_a;
    G.st.verts++;
}
void zzglVertex2f(float x, float y) { add_vertex(x, y, 0.0f); }
void zzglVertex3f(float x, float y, float z) { add_vertex(x, y, z); }
void zzglVertex3fv(const float *v) { add_vertex(v[0], v[1], v[2]); }
/* 0.7.6: see zzgl.h (the expressions of zzglColor4ubv, zzglTexCoord2f and add_vertex) */
void zzglVertexArray(const zzgl_vtx *v, u32 n)
{
    const float *m = G.mvp;
    zgl_vert *o;
    u32 i;
    if (!G.in_begin) { set_error(ZGL_INVALID_OPERATION); return; }
    if (n > ZZGL_MAX_VERTS - G.vb_n) { set_error(ZGL_OUT_OF_MEMORY); n = ZZGL_MAX_VERTS - G.vb_n; }
    o = &G.vb[G.vb_n];
    for (i = 0; i < n; i++, v++, o++) {
        float x = v->x, y = v->y, z = v->z;
        o->x = m[0] * x + m[4] * y + m[8] * z + m[12];
        o->y = m[1] * x + m[5] * y + m[9] * z + m[13];
        o->z = m[2] * x + m[6] * y + m[10] * z + m[14];
        o->w = m[3] * x + m[7] * y + m[11] * z + m[15];
        o->s = v->s; o->t = v->t;
        o->r = g_inv255[v->rgba[0]]; o->g = g_inv255[v->rgba[1]]; o->b = g_inv255[v->rgba[2]]; o->a = g_inv255[v->rgba[3]];
    }
    if (n) {   /* the current colour and texture coordinate are left as the separate calls leave them */
        const zzgl_vtx *l = v - 1;
        G.cur_r = g_inv255[l->rgba[0]]; G.cur_g = g_inv255[l->rgba[1]]; G.cur_b = g_inv255[l->rgba[2]]; G.cur_a = g_inv255[l->rgba[3]];
        G.cur_s = l->s; G.cur_t = l->t;
    }
    G.vb_n += n;
    G.st.verts += n;
}

static int g_zzgl_prof;      /* 0.7.1: zzgl_set_profile */
static int emit_tri(void *ctx, const u32 *desc39)
{
    (void)ctx;
    G.st.tris_out++;
    return rix_frame_tri(&G.frame, desc39);
}
/* one triangle: cull (front = counter-clockwise in y-up screen space, OpenGL's default), near clip,
   project, screen clip, fan */
static void draw_triangle(const zgl_vert *a, const zgl_vert *b, const zgl_vert *c)
{
    rix_clipvert in[3], nc[RIX_CLIP_MAX];
    rix_vertex pv[RIX_CLIP_MAX], sc[RIX_CLIP_MAX];
    u32 n, m, k;
    int all_front;
    const zgl_vert *src[3];
    src[0] = a; src[1] = b; src[2] = c;
    G.st.tris_in++;
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
    m = rix_clip_screen(pv, n, G.clip_x0, G.clip_y0, G.clip_x1, G.clip_y1, sc);
    if (m < 3u) return;
    if (m != n) G.st.clipped++;
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
static void zzglEnd_body(void)
{
    u32 i, n = G.vb_n;
    const zgl_vert *v = G.vb;
    if (!G.in_begin) { set_error(ZGL_INVALID_OPERATION); return; }
    G.in_begin = 0;
    if (G.polygon_mode != ZGL_FILL) return;                       /* gl_showtris / wireframe: not drawn */
    switch (G.begin_mode) {
    case ZGL_TRIANGLES: for (i = 0; i + 2u < n; i += 3u) draw_triangle(&v[i], &v[i + 1u], &v[i + 2u]); break;
    case ZGL_TRIANGLE_STRIP: for (i = 2; i < n; i++) { if (i & 1u) draw_triangle(&v[i - 1u], &v[i - 2u], &v[i]); else draw_triangle(&v[i - 2u], &v[i - 1u], &v[i]); } break;
    case ZGL_TRIANGLE_FAN: case ZGL_POLYGON: for (i = 2; i < n; i++) draw_triangle(&v[0], &v[i - 1u], &v[i]); break;
    case ZGL_QUADS: for (i = 0; i + 3u < n; i += 4u) { draw_triangle(&v[i], &v[i + 1u], &v[i + 2u]); draw_triangle(&v[i], &v[i + 2u], &v[i + 3u]); } break;
    case ZGL_QUAD_STRIP: for (i = 3; i < n; i += 2u) { draw_triangle(&v[i - 3u], &v[i - 2u], &v[i]); draw_triangle(&v[i - 3u], &v[i], &v[i - 1u]); } break;
    default: break;                                                /* points and lines: not on this GPU (ref_gl draws particles as triangles without point parameters) */
    }
}
