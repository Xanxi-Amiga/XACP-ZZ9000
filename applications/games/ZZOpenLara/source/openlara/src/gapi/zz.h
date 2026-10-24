#ifndef H_GAPI_ZZ
#define H_GAPI_ZZ

/* ZZOpenLaraRIX A2: OpenLara GAPI for the RasterIX GPU of the ZZ9000 (G4C), through ZZGL 0.7.5 and
 * librixgpu 1.3.2 (the validated ZZQuake2RIX 0.9.5 stack).
 *
 * This is the fixed-function path of gapi/gl.h (FFP), rewritten on the zzgl* entry points:
 *   - no shaders, no VBO/VAO, no render targets, no cube/volume/depth textures (the engine's FFP and
 *     SPLIT_BY_TILE profile never needs them: environment maps, shadows and water return early);
 *   - one TMU, textures 8..256 power of two, RGB/RGBA bytes only, wrap repeat only (ZZGL 0.7.5);
 *   - a Begin/End batch holds at most ZZGL_MAX_VERTS (64) vertices: DIP splits a range into
 *     batches of 21 triangles;
 *   - vertex lighting in software, as gl.h does under FFP (same formula, same clamps).
 *
 * Nothing here talks to librixgpu directly: the frame cycle (target, fence, presentation) belongs to
 * the backend (Core1/zzq2rix_gpu.c, 0.9.5), called through zz_rix_begin_frame / zz_rix_end_frame.
 */

#include "core.h"

#define PROFILE_MARKER(title)
#define PROFILE_LABEL(id, name, label)
#define PROFILE_TIMING(time)

extern "C" {
#include "zzgl.h"
void zz_rix_begin_frame(void);
void zz_rix_end_frame(void);
/* A2.7: measurement (Core1/zzq2rix_gpu.c, librixgpu): batches by category, global timer */
void zz_rix_cat_begin(void);
void zz_rix_cat_end(int c);
u32  rix_now(void);
/* A2.20: a full-screen picture (title screen, video frame) drawn by Core1 instead of the black preclear */
void zz_rix_set_bg(const unsigned short *img, int w, int h, int sx0, int sy0, int sx1, int sy1,
                   float dx0, float dy0, float dx1, float dy1);
}

#ifdef ZZQ2RIX_HOST
extern int g_host_dbg_dip;   /* tests/host_game.cpp: OL_DBG_DIP=tick traces the draws of that frame */
#endif

namespace GAPI {

    using namespace Core;

    typedef ::Vertex Vertex;

    bool zzKeepImage = false;   /* A2.20: set around Texture::Load of the title screen (inventory.h) */
    int  cullMode, blendMode;
    bool depthWrite;
    bool withFOG = false;    /* ZZGL has no fog; GAPI::withFOG is written by the settings loader */
    int  cullEnabled = 1;    /* zzopenlara.cfg CULL=0: no face culling (diagnostic, costs fill rate) */

    /* A2.7: what the engine is drawing, for the batch categories of the backend's report (olstat.txt).
       Set by the render functions of mesh.h / level.h / inventory.h through CatScope (_GAPI_ZZ only);
       the order is the one of g_cat_name in Core1/zzq2rix_gpu.c. */
    enum { CAT_ROOM, CAT_ROOMDYN, CAT_SPRITE, CAT_MODEL, CAT_OTHER, CAT_INV, CAT_2D };
    int olCat = CAT_OTHER;
    struct CatScope {
        int prev;
        CatScope(int c) : prev(olCat) { olCat = c; }
        ~CatScope() { olCat = prev; }
    };
    /* A2.7: global-timer ticks of the current frame in DIP, beginFrame, endFrame (read and reset by
       Core1/ol_game.cpp after each Game::render) */
    u32 dipTicks, beginTicks, endTicks, dipCalls;

    /* A2.8: what ZZGL currently holds, as set by the engine (the sorted opaque pass below replays its own
       state per draw and puts these back afterwards) */
    u32    curTexID;                       /* last zzglBindTexture */
    short4 curScissor, curViewport;
    mat4   curProj;
    int    curCull = -1;                   /* effective RS_CULL_* mask (after CULL=0) */
    bool   curAlpha, curDepthTest;
    int    sortOn = 3;                     /* zzopenlara.cfg RIXSORT=0: the engine's order (A2.7), 1: opaque models
                                              sorted (A2.8), 2: opaque rooms and models sorted together, 3: and
                                              the alpha-keyed pass too (A2.10) */
    int    drawDist = 0;                   /* A2.11, zzopenlara.cfg DRAWDIST=n (blocks of 1024): as the 1996 engine,
                                              faces fade to black from 0.6 n to n (12 to 20 for n = 20) and the
                                              triangles wholly beyond n are not sent. 0 = off (whole view) */
    uint32 dipFar, dipFarTotal;           /* triangles dropped beyond DRAWDIST */
    int    scisCull = 1;                   /* A2.11, zzopenlara.cfg SCISCULL=0: keep the triangles that lie wholly
                                              outside the scissor (the portal rectangle of their room) */
    uint32 dipScis, dipScisTotal;
    int    zNear = 32;                     /* A2.10, zzopenlara.cfg ZNEAR=n: near plane of the camera (camera.h;
                                              OpenLara's 32 by default, first person 16; the PSP port, 16-bit Z:
                                              256). 128 = depth four times finer at a distance, offered as a
                                              separate test (one variable per hardware test) */
    /* A2.14, zzopenlara.cfg RIXZTRICK=1: no depth clear at the start of a frame (Quake's gl_ztrick). Even
       frames draw into depth [0, 0.49999] with LEQUAL, odd frames into [1, 0.5] with GEQUAL: what the
       previous frame left lies on the far side of every test of this one. Half the depth precision. A
       depth clear later in a frame (inventory) is done by the GPU, to the far end of the frame's half, so
       that the next frame still finds everything on its far side. The first frame clears. */
    int    zTrick = 1;   /* A2.16: on by default (clean on the A4000, 10A and 7A, 22.10) */
    /* A2.17, zzopenlara.cfg RIXLITCACHE=0: the vertex colours of the static meshes (rooms, models) are
       computed again at every draw, as up to A2.16. 1 (default): a static mesh keeps, per vertex, the lit
       vertex of its last draw with the value of everything that went into it (ambient); a draw with the
       same value takes it as it is (same bits). Only for draws without dynamic lights, water, mirror,
       flash or DRAWDIST fade, which are computed every time as before */
    int    litCache = 1;
    uint32 litCacheHits;
#ifdef ZZQ2RIX_HOST
    unsigned long long hostLit[4];   /* bench: vertices lit, by (dynamic lights, water) */
#endif
    int    zTrickParity, zTrickReady;
    uint32 zTrickSkipped, zTrickCleared;
    void   sortFlush();

    /* ZZGL accepts 8..256, powers of two */
    static int zzTexDim(int v) {
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
        int        upW, upH;     /* dimensions really uploaded to ZZGL */
        uint16     *zzImg;       /* A2.20: RGB565 copy of a full-screen picture (video frames, title screen) */
        int        zzW, zzH;     /* A2.21: its layout (origWidth x origHeight when stored) */

        Texture(int width, int height, int depth, uint32 opt) : ID(0), width(width), height(height), depth(depth), origWidth(width), origHeight(height), origDepth(depth), fmt(FMT_RGBA), opt(opt), upW(0), upH(0), zzImg(NULL), zzW(0), zzH(0) {}

        /* A2.20: the pictures shown by zzBackground (not by the GPU: larger than its 256 x 256 textures) */
        bool keepImage() const { return (opt & OPT_DYNAMIC) || zzKeepImage; }
        void storeImage(const void *data) {
            /* A2.21: the data is origWidth x origHeight at this moment: a video frame (320 x 120 in a 512 x 128
               texture), or Texture::Load's padded picture (it sets origWidth to the real size afterwards) */
            int n = origWidth * origHeight;
            if (!data) return;
            if (zzImg && n != zzW * zzH) { delete[] zzImg; zzImg = NULL; }
            if (!zzImg) zzImg = new uint16[n];
            zzW = origWidth; zzH = origHeight;
            for (int i = 0; i < n; i++) {
                int r, g, b;
                switch (fmt) {
                    case FMT_RGB16  : zzImg[i] = ((const uint16*)data)[i]; continue;
                    case FMT_RGBA16 : { uint16 c = ((const uint16*)data)[i]; zzImg[i] = uint16((c & 0xFFC0) | ((c >> 1) & 31)); continue; }
                    case FMT_LUMINANCE : r = g = b = ((const uint8*)data)[i]; break;
                    default : { const uint8 *s = (const uint8*)data + i * 4; r = s[0]; g = s[1]; b = s[2]; }
                }
                zzImg[i] = uint16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }

        /* one slice/face of the source, any engine format, into a upW x upH RGBA8 buffer (the source
           is origWidth x origHeight; the rest of the buffer repeats the last texel of a row/column, so
           a padded texture samples like a clamped one) */
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
                        default : /* depth, shadow, RG float/half: not representable, white */
                            d[0] = d[1] = d[2] = d[3] = 255;
                    }
                }
            }
        }

        void setFilter(bool filter) {
            zzglTexParameterf(ZGL_TEXTURE_2D, ZGL_TEXTURE_MIN_FILTER, filter ? ZGL_LINEAR : ZGL_NEAREST);
            zzglTexParameterf(ZGL_TEXTURE_2D, ZGL_TEXTURE_MAG_FILTER, filter ? ZGL_LINEAR : ZGL_NEAREST);
        }

        void init(void *data) {
            ASSERT((opt & OPT_PROXY) == 0);

            if (keepImage()) {   /* A2.20: kept on Core1, never sent to the GPU */
                storeImage(data);
                return;
            }

            bool filter = (opt & OPT_NEAREST) == 0;

            upW = zzTexDim(width);
            upH = zzTexDim(height);

            {
                u32 id = 0;   /* librixgpu's u32 is unsigned long */
                zzglGenTextures(1, &id);
                ID = (uint32)id;
            }
            if (!ID) ol_fail("E302 TEXTURE ID", width, height);

            Core::active.textures[0] = NULL;
            bind(0);
            setFilter(filter);

            uint8 *rgba = new uint8[upW * upH * 4];
            convert(rgba, data);
            zzglTexImage2D(ZGL_TEXTURE_2D, 0, ZGL_RGBA, upW, upH, 0, ZGL_RGBA, ZGL_UNSIGNED_BYTE, rgba);
            delete[] rgba;

            unsigned err = zzglGetError();
            if (err) ol_fail("E301 TEXTURE", (int)ID, (int)err);
        }

        void deinit() {
            delete[] zzImg;
            zzImg = NULL;
            if (ID) {
                u32 id = ID;
                zzglDeleteTextures(1, &id);
                ID = 0;
            }
        }

        void generateMipMap() {}

        void update(void *data) {
            ASSERT((opt & (OPT_VOLUME | OPT_CUBEMAP)) == 0);
            if (keepImage() || zzImg) { storeImage(data); return; }   /* A2.20 */
            if (!ID) return;
            sortFlush();   /* A2.8: queued draws of the pass see the texture as it was */
            bind(0);
            uint8 *rgba = new uint8[upW * upH * 4];
            convert(rgba, data);
            zzglTexSubImage2D(ZGL_TEXTURE_2D, 0, 0, 0, upW, upH, ZGL_RGBA, ZGL_UNSIGNED_BYTE, rgba);
            delete[] rgba;
        }

        void bind(int sampler) {
            if (opt & OPT_PROXY) return;
            if (!ID) return;   /* A2.20: a picture kept on Core1 (zzImg) has no GPU texture */

            if (Core::active.textures[sampler] != this) {
                Core::active.textures[sampler] = this;
                if (sampler != sDiffuse) {
                    return;
                }
                zzglBindTexture(ZGL_TEXTURE_2D, ID);
                curTexID = ID;
            }
        }

        void unbind(int sampler) {
            if (Core::active.textures[sampler]) {
                Core::active.textures[sampler] = NULL;
                if (sampler != sDiffuse) {
                    return;
                }
                /* no "texture 0" on this GPU: the next bind of a real texture resets the state */
            }
        }

        void setFilterQuality(int value) {
            bool filter = (opt & OPT_NEAREST) == 0 && (value > Core::Settings::LOW);
            if (!ID) return;
            Core::active.textures[0] = NULL;
            bind(0);
            setFilter(filter);
        }
    };

// Mesh: system memory only (ZZGL has no vertex arrays); DIP walks the index list
    struct Mesh {
        Index  *iBuffer;
        Vertex *vBuffer;

        int    iCount;
        int    vCount;
        int    aCount;
        bool   dynamic;

        /* A2.17: lit vertex cache (static meshes, RIXLITCACHE=1): lcTag[i] = key of the lit value in lc[i]
           (0 = none); made at the first draw that can use it */
        zzgl_vtx *lc;
        u32      *lcTag;

        Mesh(bool dynamic) : iBuffer(NULL), vBuffer(NULL), dynamic(dynamic), lc(NULL), lcTag(NULL) {}

        void init(Index *indices, int iCount, ::Vertex *vertices, int vCount, int aCount) {
            this->iCount = iCount;
            this->vCount = vCount;
            this->aCount = aCount;

            ASSERT(sizeof(GAPI::Vertex) == sizeof(::Vertex));

            iBuffer = new Index[iCount];
            vBuffer = new GAPI::Vertex[vCount];
            update(indices, iCount, vertices, vCount);
        }

        void deinit() {
            delete[] iBuffer;
            delete[] vBuffer;
            iBuffer = NULL;
            vBuffer = NULL;
            delete[] lc;
            delete[] lcTag;
            lc = NULL;
            lcTag = NULL;
        }

        void update(Index *indices, int iCount, ::Vertex *vertices, int vCount) {
            ASSERT(sizeof(GAPI::Vertex) == sizeof(::Vertex));
            if (indices && iCount) {
                memcpy(iBuffer, indices, iCount * sizeof(Index));
            }
            if (vertices && vCount) {
                memcpy(vBuffer, vertices, vCount * sizeof(GAPI::Vertex));
                if (lcTag) for (int k = 0; k < this->vCount; k++) lcTag[k] = 0;   /* A2.17: new vertices, cache void */
            }
        }

        void bind(const MeshRange &range) const {}

        void initNextRange(MeshRange &range, int &aIndex) const {
            range.aIndex = -1;
        }
    };

    void init() {
        LOG("Renderer : RasterIX G4C through ZZGL 0.7.5 / librixgpu 1.3.2\n");

        /* Core::init has zeroed support; only what this GPU offers */
        support.maxVectors    = 0;
        support.maxAniso      = 0;
        support.texMinSize    = 8;      /* ZZGL: 8 x 8 at least (Core's white/black textures) */
        support.texNPOT       = false;  /* the Texture constructor rounds up to a power of two */
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

        zzglEnable(ZGL_SCISSOR_TEST);
        zzglDepthFunc(ZGL_LEQUAL);
        zzglEnable(ZGL_TEXTURE_2D);
        zzglAlphaFunc(ZGL_GREATER, 0.5f);
        zzglClearColor(0, 0, 0, 0);
        zzglShadeModel(ZGL_SMOOTH);
        zzglTexEnvf(ZGL_TEXTURE_ENV, ZGL_TEXTURE_ENV_MODE, ZGL_MODULATE);
        zzglFrontFace(ZGL_CCW);
        depthWrite = true;
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
        u32 t0 = rix_now();
        zz_rix_begin_frame();
        beginTicks += (u32)(rix_now() - t0);
        return true;
    }

    /* A2.20: a full-screen picture (inventory.h, renderTitleBG: title screen, video frames). The GPU textures
       are 256 x 256 at most: Core1 draws the picture into the colour buffer instead of the black of RIXPRECLEAR
       (Core1/zzq2rix_gpu.c, preclear_fill), under the 3D of the frame. dx/dy: screen fractions, u/v: fractions
       of the texture as the engine sees it (width x height). */
    template <class T> void zzBackground(T *tex, float dx0, float dy0, float dx1, float dy1,
                                         float u0, float v0, float u1, float v1, uint8 alpha) {
        if (!tex || !tex->zzImg || alpha < 128) return;
        zz_rix_set_bg(tex->zzImg, tex->zzW, tex->zzH,
                      int(u0 * tex->width + 0.5f), int(v0 * tex->height + 0.5f),
                      int(u1 * tex->width + 0.5f), int(v1 * tex->height + 0.5f), dx0, dy0, dx1, dy1);
    }

    void endFrame() {
        sortFlush();
        u32 t0 = rix_now();
        zz_rix_end_frame();
        endTicks += (u32)(rix_now() - t0);
    }

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

    void clear(bool color, bool depth) {
        sortFlush();
        if (zTrick && depth) {   /* A2.14 */
            if (!zzgl_frame_open()) {   /* the first clear of the frame: the other half */
                zTrickParity ^= 1;
                if (zTrickParity) { zzglDepthRange(1.0, 0.5);     zzglDepthFunc(ZGL_GEQUAL); zzglClearDepth(0.5); }
                else              { zzglDepthRange(0.0, 0.49999); zzglDepthFunc(ZGL_LEQUAL); zzglClearDepth(0.49999); }
                if (zTrickReady) { depth = false; zTrickSkipped++; }
                else             { zTrickReady = 1; zTrickCleared++; }
            } else zTrickCleared++;
        } else if (!zTrick && zTrickReady) {   /* switched off: back to the whole range */
            zzglDepthRange(0.0, 1.0); zzglDepthFunc(ZGL_LEQUAL); zzglClearDepth(1.0);
            zTrickReady = 0;
        }
        uint32 mask = (color ? ZGL_COLOR_BUFFER_BIT : 0) | (depth ? ZGL_DEPTH_BUFFER_BIT : 0);
        if (mask) {
            if (depth && !depthWrite) {
                zzglDepthMask(ZGL_TRUE);
                zzglClear(mask);
                zzglDepthMask(ZGL_FALSE);
            } else {
                zzglClear(mask);
            }
        }
    }

    void setClearColor(const vec4 &color) {
        zzglClearColor(color.x, color.y, color.z, color.w);
    }

    void setViewport(const short4 &v) {
        zzglViewport(v.x, v.y, v.z, v.w);
        curViewport = v;
    }

    void setScissor(const short4 &s) {
        zzglScissor(s.x, s.y, s.z, s.w);
        curScissor = s;
    }

    void setDepthTest(bool enable) {
        curDepthTest = enable;
        if (enable)
            zzglEnable(ZGL_DEPTH_TEST);
        else
            zzglDisable(ZGL_DEPTH_TEST);
    }

    void setDepthWrite(bool enable) {
        depthWrite = enable;
        zzglDepthMask(enable ? ZGL_TRUE : ZGL_FALSE);
    }

    void setColorWrite(bool r, bool g, bool b, bool a) {
        zzglColorMask(r, g, b, a);
    }

    void setAlphaTest(bool enable) {
        curAlpha = enable;
        if (enable)
            zzglEnable(ZGL_ALPHA_TEST);
        else
            zzglDisable(ZGL_ALPHA_TEST);
    }

    static void applyCull(int rsMask) {
        switch (rsMask) {
            case RS_CULL_BACK  : zzglCullFace(ZGL_BACK);  break;
            case RS_CULL_FRONT : zzglCullFace(ZGL_FRONT); break;
            default            : zzglDisable(ZGL_CULL_FACE); return;
        }
        zzglEnable(ZGL_CULL_FACE);
    }

    void setCullMode(int rsMask) {
        cullMode = rsMask;
        if (!cullEnabled) rsMask = 0;
        curCull = rsMask;
        switch (rsMask) {
            case RS_CULL_BACK  : zzglCullFace(ZGL_BACK);  break;
            case RS_CULL_FRONT : zzglCullFace(ZGL_FRONT); break;
            default            : zzglDisable(ZGL_CULL_FACE); return;
        }
        zzglEnable(ZGL_CULL_FACE);
    }

    void setBlendMode(int rsMask) {
        blendMode = rsMask;
        switch (rsMask) {
            case RS_BLEND_ALPHA   : zzglBlendFunc(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA); break;
            case RS_BLEND_ADD     : zzglBlendFunc(ZGL_ONE, ZGL_ONE);                       break;
            case RS_BLEND_MULT    : zzglBlendFunc(ZGL_DST_COLOR, ZGL_ZERO);                break;
            case RS_BLEND_PREMULT : zzglBlendFunc(ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);       break;
            default               : zzglDisable(ZGL_BLEND); return;
        }
        zzglEnable(ZGL_BLEND);
    }

    void setViewProj(const mat4 &mView, const mat4 &mProj) {
        zzglMatrixMode(ZGL_PROJECTION);
        zzglLoadMatrixf((const float*)&mProj);
        curProj = mProj;
    }

    void updateLights(vec4 *lightPos, vec4 *lightColor, int count) {}

    void setFog(const vec4 &params) {}

    /* statistics of the current frame, read by the platform layer */
    uint32 dipVerts, dipBatches;

    /* ---- A2.8: the opaque pass sorted by texture ----------------------------------------------
       Every bind costs the GPU a drain and the load of the whole tile (256 x 256: ~0.23 ms) and Core1 the
       wait for both (A2.7 on the A4000, LEVEL1 640: 57 binds per frame, 10.4 ms of Core1 waiting in
       them, 46 of them for the models: one tile per mesh part). Between sortBegin() and sortEnd()
       (level.h renderOpaque: around renderEntities(0), the opaque models), a draw with no blending
       and depth test + write on is not sent: its vertices are lit as before (same code, same values)
       and kept with its state (texture, matrices, scissor, viewport, cull, alpha test, category);
       sortSend() sends them grouped by texture, in their order within a texture. Anything else in the
       pass (blending, no depth write, clear, texture update, end of frame) is sent after the queue.
       With the depth test the picture is the same except where two faces get the SAME 16-bit depth
       (then the later one wins). A2.8 kept the rooms in the engine's order for that reason: with the
       near plane at 32 their equal depths are many at a distance (host bench LEVEL1 frame 599: a far
       snow face showed through the arch in the engine's order, not with the rooms sorted).
       A2.10: that was the host model, which had no depth test (tests/sim_rix.c draws in order); with
       its 16-bit depth buffer the rooms sorted change 0 to ~110 pixels of a 640 x 480 picture (20
       levels, 5 pictures each), 0 to ~35 with ZNEAR=128 (camera.h: the 16-bit depth step at a
       distance d is about d * d / (65536 * znear)). So the rooms join the queue (RIXSORT=2, level.h
       renderOpaque): the A4000 room tour (A2.9) showed 150 to 1 200 binds per frame in the heavy
       views for levels of 3 to 13 tiles. For the models only the winner between touching parts of
       one model can change (a few pixels). */
    typedef zzgl_vtx SortVert;   /* A2.17: ZZGL's vertex layout (zzglVertexArray), the same fields as before */
    struct SortItem {
        u32    tex;
        int    v0, vn, cat, cull;
        bool   alpha;
        short4 scissor, viewport;
        float  mv[16], proj[16];
    };
    enum { SORT_MAX_ITEMS = 8192, SORT_MAX_VERTS = 196608, SORT_MAX_TEX = 64 };   /* A2.10: rooms too (4.7 MB of vertices) */
    static SortItem *sortItems;
    static SortVert *sortVerts;
    static int  sortNItems, sortNVerts, sortActive;
    uint32 sortFlushes, sortItemsSent;      /* statistics (olstat.txt) */

    static void copy16(float *d, const float *s) { for (int k = 0; k < 16; k++) d[k] = s[k]; }

    /* mode 1: the opaque pass (no blending); mode 2 (A2.10): the alpha-keyed pass of level.h
       renderTransparent (rooms, sprites and models with keyed tiles: premultiplied blending, alpha test
       GREATER 0.5, depth test and write). The engine draws that pass back to front; grouped by texture,
       the order only matters where two keyed faces overlap on their filtered edges (alpha between 0.5
       and 1): there the farther one, drawn after, now fails the depth test instead of showing through
       the edge (host bench LEVEL3A room 32: ~60 binds of that pass in one view) */
    void sortBegin(int mode = 1) {
        if (!sortOn) return;
        if (!sortItems) {
            sortItems = new SortItem[SORT_MAX_ITEMS];
            sortVerts = new SortVert[SORT_MAX_VERTS];
        }
        sortActive = mode;
    }

    static void emitVerts(const SortVert *sv, int n, int cat) {
        const int BATCH = 21 * 3;   /* ZZGL_MAX_VERTS = 64: 21 whole triangles per Begin/End */
        for (int i = 0; i < n; i += BATCH) {
            int k = n - i < BATCH ? n - i : BATCH;
            zz_rix_cat_begin();   /* A2.7: batch = zzglBegin .. zzglEnd, by category */
            zzglBegin(ZGL_TRIANGLES);
            dipBatches++;
            zzglVertexArray(sv + i, (u32)k);   /* A2.17: one call (the values of Color4ubv + TexCoord2f + Vertex3f) */
            zzglEnd();
            zz_rix_cat_end(cat);
        }
    }

    static void sortSend() {
        if (!sortNItems) { sortNVerts = 0; return; }
        sortFlushes++;
        /* the pass's own state for every queued draw: depth test and write; no blending (opaque pass)
           or premultiplied blending (keyed pass, A2.10) */
        if (sortActive == 2) { zzglEnable(ZGL_BLEND); zzglBlendFunc(ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA); }
        else zzglDisable(ZGL_BLEND);
        zzglEnable(ZGL_DEPTH_TEST);
        zzglDepthMask(ZGL_TRUE);
        /* textures in order of first use, draws in their order within a texture (A2.10: one list per
           texture instead of one pass over the queue per texture; same order) */
        u32 texs[SORT_MAX_TEX]; int ntex = 0, head[SORT_MAX_TEX], tail[SORT_MAX_TEX];
        static short nxt[SORT_MAX_ITEMS];
        for (int i = 0; i < sortNItems; i++) {
            int g = 0;
            while (g < ntex && texs[g] != sortItems[i].tex) g++;
            if (g == ntex) {
                if (ntex < SORT_MAX_TEX) { texs[ntex] = sortItems[i].tex; head[ntex] = -1; ntex++; }
                else g = SORT_MAX_TEX - 1;   /* more textures than that: they share the last list (each draw binds its own) */
            }
            nxt[i] = -1;
            if (head[g] < 0) head[g] = i; else nxt[tail[g]] = (short)i;
            tail[g] = i;
        }
        int cull = -2; bool alpha = !curAlpha; short4 sc = curScissor, vp = curViewport;
        bool first = true; const float *pj = NULL;
        for (int g = 0; g < ntex; g++)
            for (int i = head[g]; i >= 0; i = nxt[i]) {
                SortItem &it = sortItems[i];
                if (first || it.scissor.x != sc.x || it.scissor.y != sc.y || it.scissor.z != sc.z || it.scissor.w != sc.w) {
                    sc = it.scissor; zzglScissor(sc.x, sc.y, sc.z, sc.w);
                }
                if (first || it.viewport.x != vp.x || it.viewport.y != vp.y || it.viewport.z != vp.z || it.viewport.w != vp.w) {
                    vp = it.viewport; zzglViewport(vp.x, vp.y, vp.z, vp.w);
                }
                bool pdiff = !pj;
                for (int k = 0; !pdiff && k < 16; k++) pdiff = (pj[k] != it.proj[k]);
                if (pdiff) { zzglMatrixMode(ZGL_PROJECTION); zzglLoadMatrixf(it.proj); pj = it.proj; }
                if (it.cull != cull) { cull = it.cull; applyCull(cull); }
                /* A2.10: the first draw always sets it (before: "alpha = !curAlpha" skipped it when the engine had
                   turned the alpha test off after queuing draws that had it on: the keyed pass is flushed by the
                   first draw after it, with the test off) */
                if (first || it.alpha != alpha) { alpha = it.alpha; if (alpha) zzglEnable(ZGL_ALPHA_TEST); else zzglDisable(ZGL_ALPHA_TEST); }
                zzglBindTexture(ZGL_TEXTURE_2D, it.tex);
                zzglMatrixMode(ZGL_MODELVIEW);
                zzglLoadMatrixf(it.mv);
                emitVerts(sortVerts + it.v0, it.vn, it.cat);
                sortItemsSent++;
                first = false;
            }
        /* the engine's current state back */
        zzglScissor(curScissor.x, curScissor.y, curScissor.z, curScissor.w);
        zzglViewport(curViewport.x, curViewport.y, curViewport.z, curViewport.w);
        zzglMatrixMode(ZGL_PROJECTION); zzglLoadMatrixf((const float*)&curProj);
        if (curCull != -1) applyCull(curCull);
        if (curAlpha) zzglEnable(ZGL_ALPHA_TEST); else zzglDisable(ZGL_ALPHA_TEST);
        if (curTexID) zzglBindTexture(ZGL_TEXTURE_2D, curTexID);
        if (curDepthTest) zzglEnable(ZGL_DEPTH_TEST); else zzglDisable(ZGL_DEPTH_TEST);
        zzglDepthMask(depthWrite ? ZGL_TRUE : ZGL_FALSE);
        switch (blendMode) {
            case RS_BLEND_ALPHA   : zzglEnable(ZGL_BLEND); zzglBlendFunc(ZGL_SRC_ALPHA, ZGL_ONE_MINUS_SRC_ALPHA); break;
            case RS_BLEND_ADD     : zzglEnable(ZGL_BLEND); zzglBlendFunc(ZGL_ONE, ZGL_ONE);                       break;
            case RS_BLEND_MULT    : zzglEnable(ZGL_BLEND); zzglBlendFunc(ZGL_DST_COLOR, ZGL_ZERO);                break;
            case RS_BLEND_PREMULT : zzglEnable(ZGL_BLEND); zzglBlendFunc(ZGL_ONE, ZGL_ONE_MINUS_SRC_ALPHA);       break;
            default               : zzglDisable(ZGL_BLEND); break;
        }
        sortNItems = sortNVerts = 0;
    }

    /* queued draws out now (also the end of the sorted pass: active = 0) */
    void sortFlush() {
        if (!sortItems) return;
        u32 t0 = rix_now();
        sortSend();
        dipTicks += (u32)(rix_now() - t0);
    }
    void sortEnd() {
        sortFlush();
        sortActive = 0;
    }

    /* A2.9: per-call vertex cache (indexed by the vertex number in its mesh; a stamp per DIP call, so a
       dynamic mesh rewritten between two calls is never reused) and 1/255 as a table (the same floats
       as the division: x / 255.0f is correctly rounded, so is the table built with it) */
    static float     inv255[256];
    static u32      *vcStamp;
    static SortVert *vcVert;
    static int       vcCap;
    static u32       vcCur;
    uint32           dipLit, dipLitTotal, dipVertsTotal;   /* vertices lit / sent (olstat.txt) */
    uint32           dipCulled, dipCulledTotal;           /* A2.9: triangles dropped before lighting */
    #ifndef ZZ_CULL_SIGN
    #define ZZ_CULL_SIGN 1
    #endif
    static void vertCacheReserve(int n) {
        if (!inv255[255]) for (int k = 0; k < 256; k++) inv255[k] = float(k) / 255.0f;
        if (n <= vcCap) return;
        int cap = n + 4096;
        delete[] vcStamp; delete[] vcVert;
        vcStamp = new u32[cap];
        vcVert  = new SortVert[cap];
        for (int k = 0; k < cap; k++) vcStamp[k] = 0;
        vcCap = cap;
    }

    void DIP(Mesh *mesh, const MeshRange &range) {
        u32 tDip = rix_now();
        dipCalls++;
        mat4 m = mView * mModel;

        bool isBlend = (blendMode == RS_BLEND_ALPHA || blendMode == RS_BLEND_ADD || blendMode == RS_BLEND_MULT || blendMode == RS_BLEND_PREMULT);
        bool defer = sortActive && depthWrite && curDepthTest && curTexID && range.iCount > 0 &&
                     (sortActive == 2 ? (blendMode == RS_BLEND_PREMULT && curAlpha) : !isBlend);   /* A2.10: mode 2 */
        if (defer && (sortNItems >= SORT_MAX_ITEMS || sortNVerts + range.iCount > SORT_MAX_VERTS)) {
            sortSend();
            if (range.iCount > SORT_MAX_VERTS) defer = false;
        }
#ifdef ZZQ2RIX_HOST
        { static int k; if (!g_host_dbg_dip) k = 0; else { k++; const char *e = getenv("OL_DBG_SKIP"); if (e) { int a0 = atoi(e), a1 = a0; const char *d = strchr(e, '-'); if (d) a1 = atoi(d + 1); if (k >= a0 && k <= a1) return;   /* OL_DBG_SKIP=a-b: draws a..b of that frame left out */ } } }
        { if (g_host_dbg_dip) printf("HOST DIP cat=%d tex=%u n=%d active=%d defer=%d blend=%x dw=%d dt=%d alpha=%d cull=%d sc=%d,%d,%d,%d\n", olCat, (unsigned)curTexID, range.iCount, sortActive, defer, blendMode, depthWrite, curDepthTest, curAlpha, curCull, curScissor.x, curScissor.y, curScissor.z, curScissor.w); }
#endif
        if (!defer) {
            if (sortNItems) sortSend();   /* order kept with what the pass queued */
            zzglMatrixMode(ZGL_MODELVIEW);
            zzglLoadMatrixf((const float*)&m);
        }

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

        /* A2.9: the lights that are on (same order as the loop of A2.8: same sums) */
        int act[MAX_LIGHTS], nAct = 0;
        if (!isFlash)
            for (int j = 0; j < MAX_LIGHTS; j++)
                if (lightColor[j].w < 1.0f) act[nAct++] = j;
        vertCacheReserve(mesh->vCount);
        if (++vcCur == 0) { for (int k = 0; k < vcCap; k++) vcStamp[k] = 0; vcCur = 1; }

        /* A2.9: faces turned away from the eye are dropped here, before lighting, when ZZGL would cull
           them anyway (CULL_BACK keeps the counter-clockwise = front faces, CULL_FRONT the others). The
           test is done in the model's space: sign of the face normal against the eye, times the sign of
           the modelview's determinant (a mirroring matrix turns the winding over) and of the projection
           (ZZ_CULL_SIGN, checked on the host bench: same pictures as A2.8). Only faces clearly turned
           away go (margin 1e-4 of the scale); edge-on ones are left to ZZGL's own test. */
        int cullKeep = 0;   /* 0 = no test, +1 = keep front faces, -1 = keep back faces */
#ifdef ZZQ2RIX_HOST
        unsigned dbgWould = 0, dbgC0 = zzgl_get_stats()->culled, dbgO0 = zzgl_get_stats()->tris_out;
#endif
        vec3 eyeM;
        float cullSign = 1.0f;
        if (cullEnabled && (curCull == RS_CULL_BACK || curCull == RS_CULL_FRONT)) {
            cullKeep = (curCull == RS_CULL_BACK) ? 1 : -1;
            mat4 mi = m.inverse();
            eyeM = vec3(mi.e03, mi.e13, mi.e23);
            float d3 = m.e00 * (m.e11 * m.e22 - m.e21 * m.e12) - m.e01 * (m.e10 * m.e22 - m.e20 * m.e12) + m.e02 * (m.e10 * m.e21 - m.e20 * m.e11);
            cullSign = (d3 < 0.0f ? -1.0f : 1.0f) * float(ZZ_CULL_SIGN);
        }

        /* A2.11: draw distance. w of the clip space (distance along the view axis) is affine in the
           model coordinates, so its smallest value over a triangle is at a vertex: a triangle whose three
           vertices are beyond the limit is wholly beyond it. Only under a perspective projection
           (e33 = 0: not the 2D/UI ortho one) */
        float dwX = 0.0f, dwY = 0.0f, dwZ = 0.0f, dwW = 0.0f, dFar = 0.0f, dStart = 0.0f, dInv = 0.0f;
        bool persp = curProj.e33 == 0.0f;
        bool dist = drawDist > 0 && persp;
        /* A2.11: a triangle wholly outside the scissor (for a room: the rectangle of the portal it is seen
           through; OpenLara sends whole rooms) gives no pixel: dropped before lighting. Test in clip space,
           without division: all three vertices in front (w > 1) and beyond the same edge of the scissor
           widened by 2 pixels. Anything else goes on to ZZGL as before */
        bool scis = scisCull && persp && curViewport.z > 0 && curViewport.w > 0;
        float cxX = 0, cxY = 0, cxZ = 0, cxW = 0, cyX = 0, cyY = 0, cyZ = 0, cyW = 0, sL = 0, sR = 0, sB = 0, sT = 0;
        if (dist || scis) {
            dwX = curProj.e30 * m.e00 + curProj.e31 * m.e10 + curProj.e32 * m.e20 + curProj.e33 * m.e30;
            dwY = curProj.e30 * m.e01 + curProj.e31 * m.e11 + curProj.e32 * m.e21 + curProj.e33 * m.e31;
            dwZ = curProj.e30 * m.e02 + curProj.e31 * m.e12 + curProj.e32 * m.e22 + curProj.e33 * m.e32;
            dwW = curProj.e30 * m.e03 + curProj.e31 * m.e13 + curProj.e32 * m.e23 + curProj.e33 * m.e33;
        }
        if (dist) {
            dFar = float(drawDist) * 1024.0f;
            dStart = dFar * 0.6f;
            dInv = 1.0f / (dFar - dStart);
        }
        if (scis) {
            cxX = curProj.e00 * m.e00 + curProj.e01 * m.e10 + curProj.e02 * m.e20 + curProj.e03 * m.e30;
            cxY = curProj.e00 * m.e01 + curProj.e01 * m.e11 + curProj.e02 * m.e21 + curProj.e03 * m.e31;
            cxZ = curProj.e00 * m.e02 + curProj.e01 * m.e12 + curProj.e02 * m.e22 + curProj.e03 * m.e32;
            cxW = curProj.e00 * m.e03 + curProj.e01 * m.e13 + curProj.e02 * m.e23 + curProj.e03 * m.e33;
            cyX = curProj.e10 * m.e00 + curProj.e11 * m.e10 + curProj.e12 * m.e20 + curProj.e13 * m.e30;
            cyY = curProj.e10 * m.e01 + curProj.e11 * m.e11 + curProj.e12 * m.e21 + curProj.e13 * m.e31;
            cyZ = curProj.e10 * m.e02 + curProj.e11 * m.e12 + curProj.e12 * m.e22 + curProj.e13 * m.e32;
            cyW = curProj.e10 * m.e03 + curProj.e11 * m.e13 + curProj.e12 * m.e23 + curProj.e13 * m.e33;
            float ix = 2.0f / float(curViewport.z), iy = 2.0f / float(curViewport.w);
            sL = float(curScissor.x - 2 - curViewport.x) * ix - 1.0f;
            sR = float(curScissor.x + curScissor.z + 2 - curViewport.x) * ix - 1.0f;
            sB = float(curScissor.y - 2 - curViewport.y) * iy - 1.0f;
            sT = float(curScissor.y + curScissor.w + 2 - curViewport.y) * iy - 1.0f;
            if (sL <= -1.0f && sR >= 1.0f && sB <= -1.0f && sT >= 1.0f) scis = false;   /* whole viewport: ZZGL's own clipping */
        }

        /* A2.17: lit vertex cache (see litCache): the key is the bits of the only per-draw input of the plain
           path (ambient), plus one (0 = empty) */
        u32 lcKey = 0;
        if (litCache && !mesh->dynamic && !isFlash && !isMirror && nAct == 0 && !waterEnabled && !dist && mesh->vCount > 0) {
            union { float f; u32 u; } ab; ab.f = ambient;
            lcKey = ab.u + 1u;
            if (!mesh->lc) {
                mesh->lc    = new zzgl_vtx[mesh->vCount];
                mesh->lcTag = new u32[mesh->vCount];
                if (mesh->lc && mesh->lcTag) for (int k = 0; k < mesh->vCount; k++) mesh->lcTag[k] = 0;
            }
            if (!mesh->lc || !mesh->lcTag) lcKey = 0;
        }

        const int BATCH = 21 * 3;   /* ZZGL_MAX_VERTS = 64: 21 whole triangles per Begin/End */
        int inBatch = 0, outN = 0;
        SortVert lb[BATCH];         /* A2.17: the batch of a draw sent at once (zzglVertexArray) */
        SortVert *out = NULL;
        SortItem *itq = NULL;
        if (defer) {
            SortItem &it = sortItems[sortNItems++];
            it.tex = curTexID; it.v0 = sortNVerts; it.vn = 0; it.cat = olCat;
            it.cull = curCull; it.alpha = curAlpha; it.scissor = curScissor; it.viewport = curViewport;
            copy16(it.mv, (const float*)&m); copy16(it.proj, (const float*)&curProj);
            out = sortVerts + sortNVerts;
            itq = &it;
        }

        for (int i = 0; i + 2 < range.iCount; i += 3) {
            int tv[3];
            for (int k = 0; k < 3; k++) tv[k] = range.vStart + mesh->iBuffer[range.iStart + i + k];
            if (cullKeep) {
                const short4 &a = mesh->vBuffer[tv[0]].coord, &b = mesh->vBuffer[tv[1]].coord, &c = mesh->vBuffer[tv[2]].coord;
                vec3 pa = vec3(float(a.x), float(a.y), float(a.z));
                vec3 n = (vec3(float(b.x), float(b.y), float(b.z)) - pa).cross(vec3(float(c.x), float(c.y), float(c.z)) - pa);
                vec3 e = eyeM - pa;
                float sd = n.dot(e) * cullSign * float(cullKeep);
                /* A2.17: sd < -1e-4 |n| |e| without the two square roots: sd < 0 and sd^2 > 1e-8 |n|^2 |e|^2 (shorts:
                   at most ~1e31, no overflow) */
                bool away = sd < 0.0f && sd * sd > 1e-8f * n.length2() * e.length2();
#ifdef ZZQ2RIX_HOST
                if (away && getenv("OL_DBG_CULLCHK") && !defer) {
                    float scale = sqrtf(n.length2()) * sqrtf(e.length2());
                    /* host check: this triangle alone through ZZGL; it must not come out */
                    if (inBatch) { emitVerts(lb, inBatch, olCat); inBatch = 0; }
                    unsigned o0 = zzgl_get_stats()->tris_out;
                    zzglBegin(ZGL_TRIANGLES);
                    for (int k = 0; k < 3; k++) { const short4 &q = mesh->vBuffer[tv[k]].coord; zzglVertex3f(float(q.x), float(q.y), float(q.z)); }
                    zzglEnd();
                    if (zzgl_get_stats()->tris_out != o0)
                        printf("HOST CULLBAD cat=%d cull=%d sd=%g scale=%g a=%d,%d,%d b=%d,%d,%d c=%d,%d,%d e=%.1f,%.1f,%.1f\n", olCat, curCull, sd, scale,
                               a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, eyeM.x, eyeM.y, eyeM.z);
                    dbgWould++;
                    continue;
                }
#endif
                if (away) { dipCulled++; continue; }

            }
            if (dist || scis) {
                int beyond = 0, front = 0, oL = 0, oR = 0, oB = 0, oT = 0;
                for (int k = 0; k < 3; k++) {
                    const short4 &q = mesh->vBuffer[tv[k]].coord;
                    float qx = float(q.x), qy = float(q.y), qz = float(q.z);
                    float w = dwX * qx + dwY * qy + dwZ * qz + dwW;
                    if (dist && w > dFar) beyond++;
                    if (scis && w > 1.0f) {
                        front++;
                        float x = cxX * qx + cxY * qy + cxZ * qz + cxW, y = cyX * qx + cyY * qy + cyZ * qz + cyW;
                        oL += (x < sL * w); oR += (x > sR * w); oB += (y < sB * w); oT += (y > sT * w);
                    }
                }
                if (beyond == 3) { dipFar++; continue; }
                if (front == 3 && (oL == 3 || oR == 3 || oB == 3 || oT == 3)) { dipScis++; continue; }
            }
            for (int k = 0; k < 3; k++) {
                /* A2.9: a vertex used by several triangles of the call is lit once (the values are those
                   of A2.8: same code, same order of operations). A2.17: a static mesh keeps it from one
                   draw to the next (lcKey) */
                int vi = tv[k];
                SortVert tmp, *cv = &tmp;
                if (lcKey && vi >= 0 && vi < mesh->vCount) {
                    cv = &mesh->lc[vi];
                    if (mesh->lcTag[vi] == lcKey) { litCacheHits++; goto lit; }
                    mesh->lcTag[vi] = lcKey;
                } else if (vi >= 0 && vi < vcCap) {
                    cv = &vcVert[vi];
                    if (vcStamp[vi] == vcCur) goto lit;
                    vcStamp[vi] = vcCur;
                }
                    dipLit++;
#ifdef ZZQ2RIX_HOST
                    hostLit[(nAct ? 1 : 0) + (waterEnabled ? 2 : 0)]++;
#endif
                    {
                        GAPI::Vertex* v = &mesh->vBuffer[vi];
                        vec3 color = vec3(inv255[v->light.x], inv255[v->light.y], inv255[v->light.z]);
                        vec3 coord = vec3(float(v->coord.x), float(v->coord.y), float(v->coord.z));
                        if (!isFlash) {
                            color *= ambient;
                            if (nAct) {
                                vec3 normal = vec3(float(v->normal.x), float(v->normal.y), float(v->normal.z)).normal();
                                for (int a = 0; a < nAct; a++) {
                                    int j = act[a];
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
                        }

                        if (waterEnabled) {
                            color *= 0.5f + fabsf(sinf(coord.dot(vec3(1.0f / 1024.0f)) + Core::params.x)) * 0.75f;
                            color *= vec3(0.6f, 0.9f, 0.9f);
                        }

                        if (isMirror)
                            color *= Core::active.material.xyz() * 255.0f;
                        else
                            color *= vec3(v->color.x, v->color.y, v->color.z) * 1.25f;

                        if (dist) {   /* A2.11: fade to black from dStart to dFar (gapi/sw.h: 12 to 20 blocks) */
                            float w = dwX * coord.x + dwY * coord.y + dwZ * coord.z + dwW - dStart;
                            if (w > 0.0f) color *= clamp(1.0f - w * dInv, 0.0f, 1.0f);
                        }

                        cv->rgba[0] = (unsigned char)min(255, max(0, (int)color.x));
                        cv->rgba[1] = (unsigned char)min(255, max(0, (int)color.y));
                        cv->rgba[2] = (unsigned char)min(255, max(0, (int)color.z));
                        cv->rgba[3] = v->light.w;
                        cv->s = (float)v->texCoord.x / 32767.0f;
                        cv->t = (float)v->texCoord.y / 32767.0f;
                        cv->x = coord.x; cv->y = coord.y; cv->z = coord.z;
                    }
            lit:
                dipVerts++;
                if (defer) {
                    out[outN++] = *cv;
                    continue;
                }
                lb[inBatch] = *cv;
                if (++inBatch == BATCH) {   /* A2.17: the batch at once (same calls to ZZGL as before, same order) */
                    emitVerts(lb, BATCH, olCat);
                    inBatch = 0;
                }
            }
        }
#ifdef ZZQ2RIX_HOST
        if (getenv("OL_DBG_CULLCHK") && !defer) {
            unsigned zc = zzgl_get_stats()->culled - dbgC0;
            if (0) printf("HOST CULLCHK cat=%d n=%d cull=%d sign=%g would=%u zzgl=%u out=%u e=%.1f,%.1f,%.1f proj=%g,%g,%g,%g\n", olCat, range.iCount, curCull, cullSign, dbgWould, zc, zzgl_get_stats()->tris_out - dbgO0, eyeM.x, eyeM.y, eyeM.z, curProj.e00, curProj.e11, curProj.e32, curProj.e33);
        }
#endif
        if (itq) {
            itq->vn = outN;
            sortNVerts += outN;
            /* an item left empty (every face turned away) stays: it keeps the order in which the textures
               first appear, hence which part wins where two parts have the same depth (as in A2.8) */
        }
        if (inBatch) emitVerts(lb, inBatch, olCat);
        dipTicks += (u32)(rix_now() - tDip);
    }

    vec4 copyPixel(int x, int y) {
        ubyte4 c;
        zzglReadPixels(x, y, 1, 1, ZGL_RGBA, ZGL_UNSIGNED_BYTE, &c);
        return vec4(float(c.x), float(c.y), float(c.z), float(c.w)) * (1.0f / 255.0f);
    }
}

#endif
