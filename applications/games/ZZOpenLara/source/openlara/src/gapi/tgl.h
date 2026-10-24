#ifndef H_GAPI_TGL
#define H_GAPI_TGL

/* ZZOpenLaraTGL A2.4: OpenLara GAPI on the TinyGL of ZZQuake2TGL 0.6.1, rendered by Core1.
 *
 * TinyGL: the vendored tree of ZZQuake2 (tinygl/, SHA-256 in Core1/tgl/tinygl_vendored.sha256),
 * patched at build time on a copy by Core1/tgl/zzq2tgl_patch_tinygl.py, with its context and
 * allocator Core1/tgl/zzq2tgl_tgl.c: exactly the TinyGL that runs Quake II's ref_gl in ZZQuake2TGL.
 * That TinyGL already has what OpenLara's fixed-function path needs: GL_MODULATE, real alpha
 * blending (SRC_ALPHA / ONE_MINUS_SRC_ALPHA, ONE / ONE, DST_COLOR / ZERO ...), the alpha test
 * (zz_tgl_set_alpha_limit), RGBA textures and glTexSubImage2D, a bilinear sampler (zz_tgl_set_linear).
 *
 * Same fixed-function path as gapi/zz.h (the RasterIX GAPI): vertex lighting in software, one
 * texture per 256x256 tile (SPLIT_BY_TILE), immediate mode. Only the scissor (the engine clips every
 * room to its portals) is not in TinyGL: the viewport becomes the scissor rectangle and the
 * projection is rescaled to match, so TinyGL's frustum clipping cuts the triangles exactly at the
 * rectangle. FILTER=1 turns the bilinear sampler on for the textures (default 0 here: four texels
 * per pixel on Core1, as zz_filter 2 in ZZQuake2TGL).
 * The picture (0x00RRGGBB, Core::width x Core::height = the screen) is shown by present.c.
 */

#include "core.h"

#define PROFILE_MARKER(title)
#define PROFILE_LABEL(id, name, label)
#define PROFILE_TIMING(time)

#include "TGL/gl.h"

extern "C" {
int  zz_tgl_open(int w, int h);
const unsigned *zz_tgl_frame(int *w, int *h);
void zz_tgl_set_modulate(int on);
void zz_tgl_set_linear(int on);
void zz_tgl_set_qpersp(int on);   /* A2.18 */
void zz_tgl_set_subpix(int on);   /* A2.18 */
void zz_tgl_set_scissor(int x0, int y0, int x1, int y1);   /* A2.18 */
void zz_tgl_set_alpha_limit(unsigned limit);
}

namespace GAPI {

    using namespace Core;

    typedef ::Vertex Vertex;

    int  cullMode, blendMode;
    bool depthWrite, depthTest;
    bool withFOG = false;
    int  cullEnabled = 1;    /* zzopenlara.cfg CULL=0 */
    bool tglFilter = false;  /* zzopenlara.cfg FILTER=1: bilinear textures */
    int  tglSubpix = 1;      /* A2.18, zzopenlara.cfg TGLSUBPIX=0: vertices on whole pixels (TinyGL's own) */
    int  tglPersp = 1;       /* A2.18, zzopenlara.cfg TGLPERSP=0: TinyGL's own perspective (divided by the depth) */

    /* clip state: GL convention (origin bottom left), see applyClip */
    short4 zzViewport, zzScissor;
    mat4   zzProj;
    bool   tglClipDirty = true, tglClipEmpty = false;

    const unsigned *tglPixels() {
        return zz_tgl_frame(NULL, NULL);
    }

    static int tglTexDim(int v) {
        int p = 8;
        while (p < v && p < 256) p <<= 1;
        return p;
    }

// Shader: fixed function, every call is a no-op
    struct Shader {
        void init(Core::Pass pass, int type, int *def, int defCount) {}
        void deinit() {}
        void bind() {}
        void validate() {}
        void setParam(UniformType uType, const vec4  &value, int count = 1) {}
        void setParam(UniformType uType, const mat4  &value, int count = 1) {}
    };

// Texture
    struct Texture {
        uint32     ID;
        int        width, height, depth, origWidth, origHeight, origDepth;
        TexFormat  fmt;
        uint32     opt;
        int        upW, upH;
        bool       linear;

        Texture(int width, int height, int depth, uint32 opt) : ID(0), width(width), height(height), depth(depth), origWidth(width), origHeight(height), origDepth(depth), fmt(FMT_RGBA), opt(opt), upW(0), upH(0), linear(false) {}

        /* source (any engine format, origWidth x origHeight) -> upW x upH RGBA bytes (as gapi/zz.h) */
        void convert(uint8 *dst, const void *data) {
            int sw = origWidth, sh = origHeight;
            if (sw > upW) sw = upW;
            if (sh > upH) sh = upH;
            for (int y = 0; y < upH; y++) {
                int sy = (y < sh) ? y : (sh - 1);
                uint8 *d = dst + y * upW * 4;
                for (int x = 0; x < upW; x++, d += 4) {
                    int sx = (x < sw) ? x : (sw - 1);
                    int i = sy * origWidth + sx;
                    if (!data) { d[0] = d[1] = d[2] = d[3] = 255; continue; }
                    switch (fmt) {
                        case FMT_LUMINANCE : {
                            uint8 l = ((const uint8*)data)[i];
                            d[0] = d[1] = d[2] = l; d[3] = 255;
                            break;
                        }
                        case FMT_RGB16 : {
                            uint16 c = ((const uint16*)data)[i];
                            d[0] = uint8((c >> 11) << 3); d[1] = uint8(((c >> 5) & 63) << 2); d[2] = uint8((c & 31) << 3); d[3] = 255;
                            break;
                        }
                        case FMT_RGBA16 : {
                            uint16 c = ((const uint16*)data)[i];
                            d[0] = uint8((c >> 11) << 3); d[1] = uint8(((c >> 6) & 31) << 3); d[2] = uint8(((c >> 1) & 31) << 3); d[3] = (c & 1) ? 255 : 0;
                            break;
                        }
                        case FMT_RGBA : {
                            const uint8 *s = (const uint8*)data + i * 4;
                            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                            break;
                        }
                        default :
                            d[0] = d[1] = d[2] = d[3] = 255;
                    }
                }
            }
        }

        void upload(void *data, bool sub) {
            uint8 *rgba = new uint8[upW * upH * 4];
            convert(rgba, data);
            if (sub)
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, upW, upH, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            else
                glTexImage2D(GL_TEXTURE_2D, 0, 4, upW, upH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            delete[] rgba;
        }

        void init(void *data) {
            ASSERT((opt & OPT_PROXY) == 0);
            upW = tglTexDim(width);
            upH = tglTexDim(height);
            {
                GLuint id = 0;
                glGenTextures(1, &id);
                ID = (uint32)id;
            }
            if (!ID) ol_fail("E302 TEXTURE ID", width, height);
            linear = tglFilter && (opt & OPT_NEAREST) == 0;
            Core::active.textures[0] = NULL;
            bind(0);
            upload(data, false);
        }

        void deinit() {
            if (ID) {
                GLuint id = ID;
                glDeleteTextures(1, &id);
                ID = 0;
            }
        }

        void generateMipMap() {}

        void update(void *data) {
            ASSERT((opt & (OPT_VOLUME | OPT_CUBEMAP)) == 0);
            if (!ID) return;
            Core::active.textures[0] = NULL;
            bind(0);
            upload(data, true);
        }

        void bind(int sampler) {
            if (opt & OPT_PROXY) return;
            ASSERT(ID);
            if (Core::active.textures[sampler] != this) {
                Core::active.textures[sampler] = this;
                if (sampler != sDiffuse) {
                    return;
                }
                glBindTexture(GL_TEXTURE_2D, ID);
                zz_tgl_set_linear(linear ? 1 : 0);
            }
        }

        void unbind(int sampler) {
            if (Core::active.textures[sampler]) {
                Core::active.textures[sampler] = NULL;
            }
        }

        void setFilterQuality(int value) {
            linear = tglFilter && (opt & OPT_NEAREST) == 0 && (value > Core::Settings::LOW);
            if (Core::active.textures[sDiffuse] == this) zz_tgl_set_linear(linear ? 1 : 0);
        }
    };

// Mesh: system memory, DIP walks the index list
    struct Mesh {
        Index  *iBuffer;
        Vertex *vBuffer;

        int    iCount;
        int    vCount;
        int    aCount;
        bool   dynamic;

        Mesh(bool dynamic) : iBuffer(NULL), vBuffer(NULL), dynamic(dynamic) {}

        void init(Index *indices, int iCount, ::Vertex *vertices, int vCount, int aCount) {
            this->iCount = iCount;
            this->vCount = vCount;
            this->aCount = aCount;
            iBuffer = new Index[iCount];
            vBuffer = new GAPI::Vertex[vCount];
            update(indices, iCount, vertices, vCount);
        }

        void deinit() {
            delete[] iBuffer;
            delete[] vBuffer;
            iBuffer = NULL;
            vBuffer = NULL;
        }

        void update(Index *indices, int iCount, ::Vertex *vertices, int vCount) {
            if (indices && iCount) {
                memcpy(iBuffer, indices, iCount * sizeof(Index));
            }
            if (vertices && vCount) {
                memcpy(vBuffer, vertices, vCount * sizeof(GAPI::Vertex));
            }
        }

        void bind(const MeshRange &range) const {}

        void initNextRange(MeshRange &range, int &aIndex) const {
            range.aIndex = -1;
        }
    };

    void init() {
        LOG("Renderer : TinyGL of ZZQuake2TGL 0.6.1 on Core1, %dx%d\n", Core::width, Core::height);

        support.maxVectors    = 0;
        support.maxAniso      = 0;
        support.texMinSize    = 8;
        support.texNPOT       = false;
        support.texCUBE       = false;
        support.VBO           = false;
        support.VAO           = false;
        support.depthTexture  = false;
        support.shadowSampler = false;
        support.discardFrame  = false;
        support.derivatives   = false;
        support.tex3D         = false;
        support.texRG         = false;
        support.texBorder     = false;
        support.texMaxLevel   = false;

        if (!zz_tgl_open(Core::width, Core::height)) ol_fail("E320 TINYGL OPEN", Core::width, Core::height);

        zz_tgl_set_modulate(1);
        zz_tgl_set_alpha_limit(255);
        zz_tgl_set_qpersp(tglPersp);   /* A2.18 */
        zz_tgl_set_subpix(tglSubpix);  /* A2.18 */
        glEnable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        glClearColor(0, 0, 0, 0);
        glShadeModel(GL_SMOOTH);
        glFrontFace(GL_CCW);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        depthWrite = depthTest = true;
        zzViewport = zzScissor = short4(0, 0, Core::width, Core::height);
        zzProj.identity();
        tglClipDirty = true;
    }

    void deinit() {}

    inline mat4::ProjRange getProjRange() {
        return mat4::PROJ_NEG_POS;
    }

    mat4 ortho(float l, float r, float b, float t, float znear, float zfar) {
        mat4 m;
        m.ortho(getProjRange(), l, r, b, t, znear, zfar);
        return m;
    }

    mat4 perspective(float fov, float aspect, float znear, float zfar, float eye) {
        mat4 m;
        m.perspective(getProjRange(), fov, aspect, znear, zfar, eye);
        return m;
    }

    bool beginFrame() {
        return true;
    }

    void endFrame() {}

    void resetState() {}

    int cacheRenderTarget(bool depth, int width, int height) {
        return 0;
    }

    void bindTarget(Texture *target, int face) {}

    void discardTarget(bool color, bool depth) {}

    void copyTarget(Texture *dst, int xOffset, int yOffset, int x, int y, int width, int height) {}

    void setFullscreen(bool enable) {}

    void setVSync(bool enable) {}

    void waitVBlank() {}

    /* TinyGL writes Z whenever the mask is on, even with the test off (GL does not): mask = write && test */
    void applyDepthMask() {
        glDepthMask((depthWrite && depthTest) ? GL_TRUE : GL_FALSE);
    }

    /* TinyGL clears the whole buffer: the clip rectangle does not apply, as in the RIX GAPI */
    void clear(bool color, bool depth) {
        int mask = (color ? GL_COLOR_BUFFER_BIT : 0) | (depth ? GL_DEPTH_BUFFER_BIT : 0);
        if (mask) {
            if (depth) glDepthMask(GL_TRUE);
            glClear(mask);
            if (depth) applyDepthMask();
        }
    }

    void setClearColor(const vec4 &color) {
        glClearColor(color.x, color.y, color.z, color.w);
    }

    void setViewport(const short4 &v) {
        zzViewport = v;
        tglClipDirty = true;
    }

    void setScissor(const short4 &s) {
        zzScissor = s;
        tglClipDirty = true;
    }

    /* A2.18: the engine's viewport and projection as they are, and the scissor rectangle (inside the viewport
       and the buffer) given to the triangle rasterizer (zz_tgl_set_scissor, Core1/tgl/ol_ztriangle.h). Up to
       A2.17 the viewport was narrowed to the scissor and the projection rescaled to match: exact on paper, but
       the same vertex then landed a few hundredths of a pixel apart in two rooms seen through different portals
       (hidden while TinyGL rounded the vertices to whole pixels; thin black cracks with sub-pixel vertices) */
    void applyClip() {
        tglClipDirty = false;
        int W = Core::width, H = Core::height;
        int vx = zzViewport.x, vy = zzViewport.y, vw = zzViewport.z, vh = zzViewport.w;
        int x0 = max(max(int(zzScissor.x), vx), 0);
        int y0 = max(max(int(zzScissor.y), vy), 0);
        int x1 = min(min(int(zzScissor.x + zzScissor.z), vx + vw), W);
        int y1 = min(min(int(zzScissor.y + zzScissor.w), vy + vh), H);
        tglClipEmpty = (x1 <= x0 || y1 <= y0 || vw <= 0 || vh <= 0);
        if (tglClipEmpty) return;
        mat4 p = zzProj;
        glViewport(vx, H - (vy + vh), vw, vh);   /* TinyGL: origin top left */
        zz_tgl_set_scissor(x0, H - y1, x1, H - y0);
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf((float*)&p);
    }

    void setDepthTest(bool enable) {
        depthTest = enable;
        if (enable)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        applyDepthMask();
    }

    void setDepthWrite(bool enable) {
        depthWrite = enable;
        applyDepthMask();
    }

    void setColorWrite(bool r, bool g, bool b, bool a) {}

    /* OpenLara: glAlphaFunc(GL_GREATER, 0.5) -> texels of alpha >= 128 kept: (255 - alpha) <= 127 */
    void setAlphaTest(bool enable) {
        zz_tgl_set_alpha_limit(enable ? 127u : 255u);
    }

    void setCullMode(int rsMask) {
        cullMode = rsMask;
        if (!cullEnabled) rsMask = 0;
        switch (rsMask) {
            case RS_CULL_BACK  : glCullFace(GL_BACK);  break;
            case RS_CULL_FRONT : glCullFace(GL_FRONT); break;
            default            : glDisable(GL_CULL_FACE); return;
        }
        glEnable(GL_CULL_FACE);
    }

    void setBlendMode(int rsMask) {
        blendMode = rsMask;
        switch (rsMask) {
            case RS_BLEND_ALPHA   : glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
            case RS_BLEND_ADD     : glBlendFunc(GL_ONE, GL_ONE);                       break;
            case RS_BLEND_MULT    : glBlendFunc(GL_DST_COLOR, GL_ZERO);                break;
            case RS_BLEND_PREMULT : glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);       break;
            default               : glDisable(GL_BLEND); return;
        }
        glEnable(GL_BLEND);
    }

    void setViewProj(const mat4 &mView, const mat4 &mProj) {
        zzProj = mProj;
        tglClipDirty = true;
    }

    void updateLights(vec4 *lightPos, vec4 *lightColor, int count) {}

    void setFog(const vec4 &params) {}

    uint32 dipVerts, dipBatches;

    void DIP(Mesh *mesh, const MeshRange &range) {
        if (tglClipDirty) applyClip();
        if (tglClipEmpty || range.iCount <= 0) return;

        mat4 m = mView * mModel;
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf((float*)&m);

        bool isMirror = (Core::renderType == 4);
        bool isFlash  = (Core::renderType == 1);
        float ambient = isMirror ? 1.0f : Core::active.material.y;
        mat4 mModelInv = mModel.inverseOrtho();
        bool waterEnabled = (Core::params.y < 1000000.0f && Core::params.y > 0.0f);
        vec3 lightsRelPos[MAX_LIGHTS];
        if (!isFlash) {
            for (int j = 0; j < MAX_LIGHTS; j++) {
                if (lightColor[j].w >= 1.0f) continue;
                lightsRelPos[j] = mModelInv * lightPos[j].xyz();
            }
        }

        /* same lighting as gapi/zz.h; the alpha of a triangle is the one of its first vertex (TinyGL) */
        glBegin(GL_TRIANGLES);
        dipBatches++;
        for (int i = 0; i < range.iCount; i++) {
            GAPI::Vertex* v = &mesh->vBuffer[range.vStart + mesh->iBuffer[range.iStart + i]];
            vec3 color = vec3(v->light.x / 255.0f, v->light.y / 255.0f, v->light.z / 255.0f);
            vec3 coord = vec3(float(v->coord.x), float(v->coord.y), float(v->coord.z));
            if (!isFlash) {
                vec3 normal = vec3(float(v->normal.x), float(v->normal.y), float(v->normal.z)).normal();
                color *= ambient;
                for (int j = 0; j < MAX_LIGHTS; j++) {
                    if (lightColor[j].w >= 1.0f) continue;
                    vec3 pos = lightsRelPos[j];
                    vec3 dir = (pos - coord) * lightColor[j].w;
                    float att = dir.length2();
                    if (att == 0.0f || att == 1.0f) continue;
                    float lum = normal.dot(dir / sqrtf(att));
                    vec3 light = lightColor[j].xyz();
                    light *= max(0.0f, lum) * max(0.0f, 1.0f - att);
                    color += light;
                }
            }

            if (waterEnabled) {
                color *= 0.5f + fabsf(sinf(coord.dot(vec3(1.0f / 1024.0f)) + Core::params.x)) * 0.75f;
                color *= vec3(0.6f, 0.9f, 0.9f);
            }

            if (isMirror)
                color *= Core::active.material.xyz();
            else
                color *= vec3(v->color.x, v->color.y, v->color.z) * (1.25f / 255.0f);

            glColor4f(min(1.0f, max(0.0f, color.x)), min(1.0f, max(0.0f, color.y)), min(1.0f, max(0.0f, color.z)),
                      v->light.w * (1.0f / 255.0f));
            glTexCoord2f((float)v->texCoord.x / 32767.0f, (float)v->texCoord.y / 32767.0f);
            glVertex3f(coord.x, coord.y, coord.z);
            dipVerts++;
        }
        glEnd();
    }

    vec4 copyPixel(int x, int y) {
        return vec4(0.0f);
    }
}

#endif
