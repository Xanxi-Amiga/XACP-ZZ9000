#ifndef H_GAPI_SW
#define H_GAPI_SW

#include "core.h"

#define PROFILE_MARKER(title)
#define PROFILE_LABEL(id, name, label)
#define PROFILE_TIMING(time)

//#define DITHER_FILTER

#if defined(_OS_LINUX) || defined(_OS_TNS) || defined(_OS_ZZ)
    #define COLOR_16
#endif

#ifdef COLOR_16
    #if defined(_OS_LINUX) || defined(_OS_TNS) || defined(_OS_ZZ)
        #define COLOR_FMT_565
        #define CONV_COLOR(r,g,b) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    #else
        #define COLOR_FMT_555
        #define CONV_COLOR(r,g,b) (((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3))
    #endif
#else 
    #define COLOR_FMT_888
    #define CONV_COLOR(r,g,b) ((r << 16) | (g << 8) | b)
#endif

#define SW_MAX_DIST  (20.0f * 1024.0f)
#define SW_FOG_START (12.0f * 1024.0f)

namespace GAPI {

    using namespace Core;

    typedef ::Vertex Vertex;

    #ifdef COLOR_16
        typedef uint16 ColorSW;
    #else
        typedef uint32 ColorSW;
    #endif
    typedef uint16 DepthSW;

    uint8   *swLightmap;
    uint8   swLightmapNone[32 * 256];
    uint8   swLightmapShade[32 * 256];
    ColorSW *swPalette;
    ColorSW swPaletteColor[256];
    ColorSW swPaletteWater[256];
    ColorSW swPaletteGray[256];
    uint8   swGradient[256];
    Tile8   *curTile;

    uint8 ambient;
    int32 lightsCount;

#ifdef _OS_ZZ
    /* ZZOpenLaraSW: members the engine of this branch names for every GAPI */
    bool withFOG = false;
    uint32 dipVerts, dipBatches;   /* read by the platform statistics (Core1/ol_game.cpp) */
    uint8 *swColorMem;
#endif

    struct LightSW {
        uint32 intensity;
        vec3   pos;
        float  radius;
    } lights[MAX_LIGHTS], lightsRel[MAX_LIGHTS];

// Shader
    struct Shader {
        void init(Pass pass, int type, int *def, int defCount) {}
        void deinit() {}
        void bind() {}
    #ifdef _OS_ZZ
        void validate() {}
    #endif
        void setParam(UniformType uType, const vec4  &value, int count = 1) {}
        void setParam(UniformType uType, const mat4  &value, int count = 1) {}
    };

// Texture
    struct Texture {
        uint8      *memory;
        int        width, height, origWidth, origHeight;
        TexFormat  fmt;
        uint32     opt;

        Texture(int width, int height, int depth, uint32 opt) : memory(0), width(width), height(height), origWidth(width), origHeight(height), fmt(FMT_RGBA), opt(opt) {}

        void init(void *data) {
            ASSERT((opt & OPT_PROXY) == 0);

            opt &= ~(OPT_CUBEMAP | OPT_MIPMAPS);

            memory = new uint8[width * height * 4];
            if (data) {
                update(data);
            }
        }

        void deinit() {
            if (memory) {
                delete[] memory;
            }
        }

        void generateMipMap() {}

        void update(void *data) {
        #ifdef _OS_ZZ
            /* the source holds origWidth x origHeight texels of fmt: 8x8 luminance (dither) is 64 bytes */
            int bpp = (fmt == FMT_LUMINANCE) ? 1 : ((fmt == FMT_RGB16 || fmt == FMT_RGBA16) ? 2 : 4);
            memcpy(memory, data, origWidth * origHeight * bpp);
        #else
            memcpy(memory, data, origWidth * origHeight * 4);
        #endif
        }

        void bind(int sampler) {
            Core::active.textures[sampler] = this;

            if (!this || (opt & OPT_PROXY)) return;
            ASSERT(memory);

            curTile = NULL;
        }

        void bindTileIndices(Tile8 *tile) {
            curTile = (Tile8*)tile;
        }

        void unbind(int sampler) {}

        void setFilterQuality(int value) {
            if (value > Settings::LOW)
                opt &= ~OPT_NEAREST;
            else
                opt |= OPT_NEAREST;
        }
    };

// Mesh
    struct Mesh {
        Index        *iBuffer;
        GAPI::Vertex *vBuffer;

        int          iCount;
        int          vCount;
        bool         dynamic;

        Mesh(bool dynamic) : iBuffer(NULL), vBuffer(NULL), dynamic(dynamic) {}

        void init(Index *indices, int iCount, ::Vertex *vertices, int vCount, int aCount) {
            this->iCount  = iCount;
            this->vCount  = vCount;

            iBuffer = new Index[iCount];
            vBuffer = new Vertex[vCount];

            update(indices, iCount, vertices, vCount);
        }

        void deinit() {
            delete[] iBuffer;
            delete[] vBuffer;
        }

        void update(Index *indices, int iCount, ::Vertex *vertices, int vCount) {
            if (indices) {
                memcpy(iBuffer, indices, iCount * sizeof(indices[0]));
            }

            if (vertices) {
                memcpy(vBuffer, vertices, vCount * sizeof(vertices[0]));
            }
        }

        void bind(const MeshRange &range) const {}

        void initNextRange(MeshRange &range, int &aIndex) const {
            range.aIndex = -1;
        }
    };


    int cullMode, blendMode;

    ColorSW *swColor;
    DepthSW *swDepth;
    short4  swClipRect;

#ifdef _OS_ZZ
    static inline int32 swDiv(int32 n, int32 d);
    static void swRastInit();
#endif

    struct VertexSW {
        int32 x, y, z, w;
        int32 u, v, l;

        inline VertexSW operator + (const VertexSW &p) const {
            VertexSW ret;
            ret.x = x + p.x;
            ret.y = y;
            ret.z = z + p.z;
            ret.w = w + p.w;
            ret.u = u + p.u;
            ret.v = v + p.v;
            ret.l = l + p.l;
            return ret;
        }

        inline VertexSW operator - (const VertexSW &p) const {
            VertexSW ret;
            ret.x = x - p.x;
            ret.y = y;
            ret.z = z - p.z;
            ret.w = w - p.w;
            ret.u = u - p.u;
            ret.v = v - p.v;
            ret.l = l - p.l;
            return ret;
        }

        inline VertexSW operator * (const int32 s) const {
            VertexSW ret;
            ret.x = x * s;
            ret.y = y;
            ret.z = z * s;
            ret.w = w * s;
            ret.u = u * s;
            ret.v = v * s;
            ret.l = l * s;
            return ret;
        }

        inline VertexSW operator / (const int32 s) const {
            VertexSW ret;
        #ifdef _OS_ZZ
            /* A2.12: swDiv (same quotients); w is never read by the rasterizer: not divided */
            ret.x = swDiv(x, s);
            ret.y = y;
            ret.z = swDiv(z, s);
            ret.w = w;
            ret.u = swDiv(u, s);
            ret.v = swDiv(v, s);
            ret.l = swDiv(l, s);
        #else
            ret.x = x / s;
            ret.y = y;
            ret.z = z / s;
            ret.w = w / s;
            ret.u = u / s;
            ret.v = v / s;
            ret.l = l / s;
        #endif
            return ret;
        }
    };

    Array<VertexSW> swVertices;
    Array<Index>    swIndices;
    Array<int32>    swTriangles;
    Array<int32>    swQuads;

#ifdef _OS_ZZ
    /* ---- A2.12 (ZZOpenLaraSW): measurement and the helpers of the faster paths -------------------
       Global-timer ticks (333 MHz) per frame, read and reset by Core1/ol_game.cpp after each frame. */
    uint32 swTClear, swTXform, swTRaster;
    uint32 swPrims, swCullDist, swCullBack, swCullClip, swVertsIn, swVertsXf, swVertsLit, swSpans, swPixels;
    /* A2.13: rasterizer choice (SWRAST), draw distance and fog start (SWDIST, blocks; 20 and 12 = the
       values of this branch), primitives clipped instead of dropped */
#ifdef ZZQ2RIX_HOST
    unsigned long long swHostPass, swHostWrite;   /* bench only: pixels past the depth test, pixels written */
#endif
    /* A2.14: lightmap and palette folded into one table of colours per (palette, shading): 32 light rows
       x 256 indices; one load per pixel instead of two. Built by initPalette (3 palettes x 2 shadings) */
    ColorSW *swLitPal[3][2];
    int      swPalIdx, swShadeIdx = 1;
    int    swRast = 1;
    float  swMaxDist = 30.0f * 1024.0f, swFogStart = 18.0f * 1024.0f;   /* A2.14: SWDIST=30 by default (20 in 1996) */
    uint32 swTLight;   /* A2.14: ticks of the vertex lighting, inside swTXform */
    uint32 swClipped;
    inline uint32 swTicks() {
    #ifdef ZZQ2RIX_HOST
        return ::ol_clock_us() * 333u;
    #else
        return *(volatile uint32*)0xF8F00200u;   /* ARM global timer, low word (read only, as the release diag) */
    #endif
    }

    /* the release memset is a byte loop (runtime/safe_mem.c, ~10 ns per byte measured on the A4000):
       the two clears of a 640 x 480 frame (1.2 MB) cost ~12 ms through it. Words here; the attribute
       keeps GCC from turning the loop back into a memset call */
    __attribute__((optimize("no-tree-loop-distribute-patterns")))
    static void swFill32(uint32 *d, uint32 v, uint32 n) {
        while (n >= 8) { d[0] = v; d[1] = v; d[2] = v; d[3] = v; d[4] = v; d[5] = v; d[6] = v; d[7] = v; d += 8; n -= 8; }
        while (n--) *d++ = v;
    }

    /* n / d with the truncation of C, without the library division (the Cortex-A9 has no divide
       instruction): q from a 32-bit reciprocal, then one exact correction. Same result bit for bit */
    enum { SW_INV_MAX = 4096 };
    static uint32 swInvTab[SW_INV_MAX];
    static void swInvInit() {
        for (int d = 2; d < SW_INV_MAX; d++) swInvTab[d] = uint32((1ULL << 32) / uint32(d));
    }
    static inline int32 swDiv(int32 n, int32 d) {
        if (d < 2 || d >= SW_INV_MAX) return (d == 1) ? n : n / d;
        uint32 a = n < 0 ? 0u - uint32(n) : uint32(n);
        uint32 q = uint32((uint64(a) * swInvTab[d]) >> 32);
        if (a - q * uint32(d) >= uint32(d)) q++;
        return n < 0 ? -int32(q) : int32(q);
    }
#endif

#ifdef _OS_ZZ
    void resize();
#endif

    void init() {
        LOG("Renderer : %s\n", "Software");
        LOG("Version  : %s\n", "0.1");
        swDepth = NULL;
    #ifdef _OS_ZZ
        swInvInit();
        swRastInit();
        /* the platform sets Core::width / height before Game::init; nothing calls resize() on this branch */
        swColorMem = NULL;
        resize();
    #endif
    }

    void deinit() {
        delete[] swDepth;
    #ifdef _OS_ZZ
        delete[] swColorMem;
        swColorMem = NULL;
        swDepth = NULL;
    #endif
        swVertices.clear();
        swIndices.clear();
        swTriangles.clear();
        swQuads.clear();
    }

    void resize() {
        delete[] swDepth;
        swDepth = new DepthSW[Core::width * Core::height];
    #ifdef _OS_ZZ
        /* ZZOpenLaraSW: the colour buffer belongs to the GAPI, 32-byte aligned (burst copy of present.c) */
        delete[] swColorMem;
        swColorMem = new uint8[Core::width * Core::height * sizeof(ColorSW) + 32];
        swColor = (ColorSW*)(((unsigned long)swColorMem + 31) & ~31ul);
    #endif
    }

    inline mat4::ProjRange getProjRange() {
        return mat4::PROJ_ZERO_POS;
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

#ifdef _OS_ZZ
    int cacheRenderTarget(bool depth, int width, int height) {
        return 0;
    }

    void setFullscreen(bool enable) {}
#endif

    void bindTarget(Texture *texture, int face) {}

    void discardTarget(bool color, bool depth) {}

    void copyTarget(Texture *dst, int xOffset, int yOffset, int x, int y, int width, int height) {}

    void setVSync(bool enable) {}

    void waitVBlank() {}

#ifdef _OS_ZZ
    /* ZZOpenLaraRIX A2.20: a full-screen picture (title screen, video frame: inventory.h renderTitleBG) copied
       into the colour buffer now, under the 3D drawn after it. dx/dy: screen fractions, u/v: fractions of the
       texture (width x height, the layout of its memory). */
    void zzBackground(Texture *tex, float dx0, float dy0, float dx1, float dy1,
                      float u0, float v0, float u1, float v1, uint8 alpha) {
        if (!tex || !tex->memory || alpha < 128 || !swColor) return;
        int W = Core::width, H = Core::height;
        int x0 = int(dx0 * W + 0.5f), x1 = int(dx1 * W + 0.5f), y0 = int(dy0 * H + 0.5f), y1 = int(dy1 * H + 0.5f);
        int sx0 = int(u0 * tex->width + 0.5f), sx1 = int(u1 * tex->width + 0.5f);
        int sy0 = int(v0 * tex->height + 0.5f), sy1 = int(v1 * tex->height + 0.5f);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > W) x1 = W;
        if (y1 > H) y1 = H;
        if (sx0 < 0) sx0 = 0;
        if (sy0 < 0) sy0 = 0;
        if (sx1 > tex->width) sx1 = tex->width;
        if (sy1 > tex->height) sy1 = tex->height;
        if (x1 <= x0 || y1 <= y0 || sx1 <= sx0 || sy1 <= sy0) return;
        /* A2.21: a video frame is stored origWidth x origHeight (320 x 120 in a 512 x 128 texture); the title
           picture comes padded by Texture::Load (width x height) */
        int stride = (tex->opt & OPT_DYNAMIC) ? tex->origWidth : tex->width;
        for (int y = y0; y < y1; y++) {
            int sy = sy0 + ((y - y0) * (sy1 - sy0)) / (y1 - y0);
            ColorSW *d = swColor + y * W;
            for (int x = x0; x < x1; x++) {
                int sx = sx0 + ((x - x0) * (sx1 - sx0)) / (x1 - x0);
                int i = sy * stride + sx;
                uint8 r, g, b;
                if (tex->fmt == FMT_RGB16) {
                    uint16 c = ((const uint16*)tex->memory)[i];
                    r = uint8((c >> 11) << 3); g = uint8(((c >> 5) & 63) << 2); b = uint8((c & 31) << 3);
                } else {
                    const uint8 *s = tex->memory + i * 4;
                    r = s[0]; g = s[1]; b = s[2];
                }
                d[x] = ColorSW(CONV_COLOR(r, g, b));
            }
        }
    }
#endif

    void clear(bool color, bool depth) {
    #ifdef _OS_ZZ
        /* A2.12: word fills (same bytes); the sizes are whole words (16-bit pixels, even counts) */
        uint32 t0 = swTicks();
        if (color) swFill32((uint32*)swColor, 0u, uint32(Core::width * Core::height * sizeof(ColorSW)) / 4u);
        if (depth) swFill32((uint32*)swDepth, 0xFFFFFFFFu, uint32(Core::width * Core::height * sizeof(DepthSW)) / 4u);
        swTClear += swTicks() - t0;
    #else
        if (color) {
            memset(swColor, 0x00, Core::width * Core::height * sizeof(ColorSW));
        }

        if (depth) {
            memset(swDepth, 0xFF, Core::width * Core::height * sizeof(DepthSW));
        }
    #endif
    }

    void setClearColor(const vec4 &color) {}

    void setViewport(const short4 &v) {}

    void setScissor(const short4 &s) {
        swClipRect.x = s.x;
        swClipRect.y = Core::active.viewport.w - (s.y + s.w);
        swClipRect.z = s.x + s.z;
        swClipRect.w = Core::active.viewport.w - s.y;
    }

    void setDepthTest(bool enable) {}

    void setDepthWrite(bool enable) {}

    void setColorWrite(bool r, bool g, bool b, bool a) {}

    void setAlphaTest(bool enable) {}

    void setCullMode(int rsMask) {}

    void setBlendMode(int rsMask) {}

    void setViewProj(const mat4 &mView, const mat4 &mProj) {}

    void updateLights(vec4 *lightPos, vec4 *lightColor, int count) {
        ambient = clamp(int32(active.material.y * 255), 0, 255);

        lightsCount = 0;
        for (int i = 0; i < count; i++) {
            if (lightColor[i].w >= 1.0f) {
                continue;
            }
            LightSW &light = lights[lightsCount++];
            vec4 &c = lightColor[i];
            light.intensity = uint32(((c.x + c.y + c.z) / 3.0f) * 255.0f);
            light.pos    = lightPos[i].xyz();
            light.radius = lightColor[i].w;
        }
    }

    void setFog(const vec4 &params) {}

    bool checkBackface(const VertexSW *a, const VertexSW *b, const VertexSW *c) {
        return ((b->x - a->x) >> 16) * (c->y - a->y) -
               ((c->x - a->x) >> 16) * (b->y - a->y) <= 0;
    }

    inline void sortVertices(VertexSW *&t, VertexSW *&m, VertexSW *&b) {
        if (t->y > m->y) swap(t, m);
        if (t->y > b->y) swap(t, b);
        if (m->y > b->y) swap(m, b);
    }

    inline void sortVertices(VertexSW *&t, VertexSW *&m, VertexSW *&b, VertexSW *&o) {
        if (t->y > m->y) swap(t, m);
        if (o->y > b->y) swap(o, b);
        if (t->y > o->y) swap(t, o);
        if (m->y > b->y) swap(m, b);
        if (m->y > o->y) swap(m, o);
    }

    inline void step(VertexSW &v, const VertexSW &d) {
        //v.w += d.w;
        v.u += d.u;
        v.v += d.v;
        v.l += d.l;
    }

    inline void step(VertexSW &v, const VertexSW &d, int32 count) {
        //v.w += d.w * count;
        v.u += d.u * count;
        v.v += d.v * count;
        v.l += d.l * count;
    }

    // https://www.flipcode.com/archives/Texturing_As_In_Unreal.shtml
    const int uvDither[8] = {
        32768, 16384,           0, 49152,   // (xx yy) for (y & 1 == 0)
        49152,     0,       32768, 16384    // (xx yy) for (y & 1 == 1)
    };

    void drawLine(const VertexSW &L, const VertexSW &R, int32 y) {
        int32 x1 = L.x >> 16;
        int32 x2 = R.x >> 16;

        int32 f = x2 - x1;
        if (f == 0) return;

    #ifdef _OS_ZZ
        /* A2.12: only z, u, v, l are stepped along a span (x and w of (R - L) / f were never read) */
        VertexSW dS;
        dS.z = swDiv(R.z - L.z, f);
        dS.u = swDiv(R.u - L.u, f);
        dS.v = swDiv(R.v - L.v, f);
        dS.l = swDiv(R.l - L.l, f);
        swSpans++;
    #else
        VertexSW dS = (R - L) / f;
    #endif
        VertexSW S  = L;

        if (x1 < swClipRect.x) {
            x1 = swClipRect.x - x1;
            S.z += dS.z * x1;
            step(S, dS, x1);
            x1 = swClipRect.x;
        }
        if (x2 > swClipRect.z) x2 = swClipRect.z;
    #ifdef _OS_ZZ
        if (x2 > x1) swPixels += uint32(x2 - x1);
    #endif

        int32 i = y * Core::width;

    #ifdef DITHER_FILTER
        const int *dithY = uvDither + ((y & 1) * 4);
    #endif

        for (int x = i + x1; x < i + x2; x++) {
            S.z += dS.z;

            DepthSW z = DepthSW(uint32(S.z) >> 16);

            if (swDepth[x] >= z) {
            #ifdef DITHER_FILTER
                const int *dithX = dithY + (x & 1);

                uint32 u = uint32(S.u + dithX[0]) >> 16;
                uint32 v = uint32(S.v + dithX[2]) >> 16;
            #else
                uint32 u = uint32(S.u) >> 16;
                uint32 v = uint32(S.v) >> 16;
            #endif

                uint8 index = curTile->index[(v << 8) + u];

                if (index != 0) {
                    index = swLightmap[((S.l >> (16 + 3)) << 8) + index];

                    swColor[x] = swPalette[index];
                    swDepth[x] = z;
                }
            }

            step(S, dS);
        }
    }

    void drawPart(const VertexSW &a, const VertexSW &b, const VertexSW &c, const VertexSW &d) {
        VertexSW L, R, dL, dR;
        int32 minY, maxY;

        int32 f = c.y - a.y;
        dL = (c - a) / f;
        dR = (d - b) / f;

        L = a;
        R = b;

        minY = a.y;
        maxY = c.y;

        if (maxY < swClipRect.y || minY >= swClipRect.w) return;

        if (minY < swClipRect.y) {
            minY = swClipRect.y - minY;
            L.x += dL.x * minY;
            L.z += dL.z * minY;
            R.x += dR.x * minY;
            R.z += dR.z * minY;
            step(L, dL, minY);
            step(R, dR, minY);
            minY = swClipRect.y;
        }

        if (maxY > swClipRect.w) maxY = swClipRect.w;

        for (int y = minY; y < maxY; y++) {
            drawLine(L, R, y);
            L.x += dL.x;
            L.z += dL.z;
            R.x += dR.x;
            R.z += dR.z;
            step(L, dL);
            step(R, dR);
        }
    }

    void drawTriangle(Index *indices) {
    /*
             t
            /\ <----- top triangle
         m /__\/ n
           \  /\
             \  \ <-- bottom triangle
               \ \
                 \\
                   \
                    b
    */
        VertexSW _n;
        VertexSW *t = swVertices.items + indices[0];
        VertexSW *m = swVertices.items + indices[1];
        VertexSW *b = swVertices.items + indices[2];
        VertexSW *n = &_n;

        if (checkBackface(t, m, b))
            return;

        int32 cx1 = swClipRect.x << 16;
        int32 cx2 = swClipRect.z << 16;

        if (t->x < cx1 && m->x < cx1 && b->x < cx1)
            return;

        if (t->x > cx2 && m->x > cx2 && b->x > cx2)
            return;

        sortVertices(t, m, b);

        if (b->y < swClipRect.y || t->y > swClipRect.w)
            return;

        *n = ((*b - *t) / (b->y - t->y) * (m->y - t->y)) + *t;
        n->y = m->y;

        if (m->x > n->x) {
            swap(m, n);
        }

        if (m->y != t->y) drawPart(*t, *t, *m, *n);
        if (m->y != b->y) drawPart(*m, *n, *b, *b);
    }

    void drawQuad(Index *indices) {
    /*
             t
            /\ <----- top triangle
         m /__\/ n
           \  /\
            \   \ <-- quad
           p \/__\ o
             /\  / 
               \/ <-- bottom triangle
               b
    */
        VertexSW _n;
        VertexSW _p;
        VertexSW *t = swVertices.items + indices[0];
        VertexSW *m = swVertices.items + indices[1];
        VertexSW *b = swVertices.items + indices[2];
        VertexSW *o = swVertices.items + indices[3];
        VertexSW *n = &_n;
        VertexSW *p = &_p;

        if (checkBackface(t, m, b))
            return;

        int32 cx1 = swClipRect.x << 16;
        int32 cx2 = swClipRect.z << 16;

        if (t->x < cx1 && m->x < cx1 && o->x < cx1 && b->x < cx1)
            return;

        if (t->x > cx2 && m->x > cx2 && o->x > cx2 && b->x > cx2)
            return;

        sortVertices(t, m, b, o);

        if (b->y < swClipRect.y || t->y > swClipRect.w)
            return;

        if (checkBackface(t, b, m) == checkBackface(t, b, o)) {

            VertexSW d = (*b - *t) / (b->y - t->y);

            *n = *t + d * (m->y - t->y);
            *p = *t + d * (o->y - t->y);

            n->y = m->y;
            p->y = o->y;

        } else {

            if (o->y != t->y) {
                *n = *t + ((*o - *t) / (o->y - t->y) * (m->y - t->y));
                n->y = m->y;
            }

            if (m->y != b->y) {
                *p = *b + ((*m - *b) / (m->y - b->y) * (o->y - b->y));
                p->y = o->y;
            }

        }

        if (o->y != t->y && m->x > n->x) swap(m, n);
        if (m->y != b->y && p->x > o->x) swap(p, o);

        if (t->y != m->y) drawPart(*t, *t, *m, *n);
        if (m->y != o->y) drawPart(*m, *n, *p, *o);
        if (o->y != b->y) drawPart(*p, *o, *b, *b);
    }

    void applyLighting(VertexSW &result, const Vertex &vertex, float depth) {
        vec3 coord  = vec3(float(vertex.coord.x), float(vertex.coord.y), float(vertex.coord.z));
        vec3 normal = vec3(float(vertex.normal.x), float(vertex.normal.y), float(vertex.normal.z)).normal();
        float lighting = 0.0f;
        for (int i = 0; i < lightsCount; i++) {
            LightSW &light = lightsRel[i];
            vec3 dir = (light.pos - coord) * light.radius;
            float att = dir.length2();
            float lum = normal.dot(dir / sqrtf(att));
            lighting += (max(0.0f, lum) * max(0.0f, 1.0f - att)) * light.intensity;
        }

        lighting += result.l;

    #ifdef _OS_ZZ
        depth -= swFogStart;
        if (depth > 0.0f) {
            lighting *= clamp(1.0f - depth / (swMaxDist - swFogStart), 0.0f, 1.0f);
        }
    #else
        depth -= SW_FOG_START;
        if (depth > 0.0f) {
            lighting *= clamp(1.0f - depth / (SW_MAX_DIST - SW_FOG_START), 0.0f, 1.0f);
        }
    #endif

        result.l = (255 - min(255, int32(lighting))) << 16;
    }

#ifdef _OS_ZZ
    /* A2.12: per-call vertex cache (a stamp per DIP call): a vertex shared by several faces of the call is
       projected and lit once, its VertexSW pushed once (the drawing reads it by index: same values) */
    struct SwCache { uint32 stamp; int32 slot; float cw; uint8 state; };   /* state: 1 projected, 2 out of range, 4 lit */
    static SwCache *swCache;
    static VertexSW *swCacheV;
    static int swCacheCap;
    static uint32 swCacheCur;

    /* project vertex vi (cached); false when the primitive must be skipped (w < 0 or beyond SW_MAX_DIST) */
    inline bool swProject(int vi, const Vertex *vertices, const mat4 &swMatrix, bool colored) {
        SwCache &c = swCache[vi];
        if (c.stamp == swCacheCur) return c.state != 2;
        c.stamp = swCacheCur; c.slot = -1;
        swVertsXf++;
        const Vertex &vertex = vertices[vi];
        vec4 cc = swMatrix * vec4(vertex.coord.x, vertex.coord.y, vertex.coord.z, 1.0f);
        if (cc.w < 0.0f || cc.w > SW_MAX_DIST) { c.state = 2; return false; }
        cc.x /= cc.w;
        cc.y /= cc.w;
        cc.z /= cc.w;
        cc.x = clamp(cc.x, -16384.0f, 16384.0f);
        cc.y = clamp(cc.y, -16384.0f, 16384.0f);
        VertexSW &result = swCacheV[vi];
        result.x = int32(cc.x) * 65536;   /* same value, defined for negative x (|x| <= 16384) */
        result.y = int32(cc.y);
        result.z = uint32(clamp(cc.z, 0.0f, 1.0f) * 65535.0f) << 16;
        result.w = int32(cc.w);
        if (colored) {
            result.u = vertex.color.x << 16;
            result.v = 0;
        } else {
            result.u = (vertex.texCoord.x << 16);
            result.v = (vertex.texCoord.y << 16);
        }
        result.w = result.w << 16;
        result.l = ((vertex.light.x * ambient) >> 8);
        c.cw = cc.w;
        c.state = 1;
        return true;
    }

    inline int32 swEmit(int vi, const Vertex *vertices) {
        SwCache &c = swCache[vi];
        if (!(c.state & 4)) {
            applyLighting(swCacheV[vi], vertices[vi], c.cw);
            c.state |= 4;
            swVertsLit++;
        }
        if (c.slot < 0) c.slot = swVertices.push(swCacheV[vi]);
        return c.slot;
    }

    /* the same primitives, in the same order, as the upstream loop below; those that drawTriangle /
       drawQuad would reject at once (facing away, wholly left / right / above / below the clip
       rectangle) are tested before lighting and not pushed */
    bool transform(const Index *indices, const Vertex *vertices, int iStart, int iCount, int vStart, int vCount) {
        swVertices.reset();
        swIndices.reset();
        swTriangles.reset();
        swQuads.reset();

        mat4 swMatrix;
        swMatrix.viewport(0.0f, (float)Core::height, (float)Core::width, -(float)Core::height, 0.0f, 1.0f);
        swMatrix = swMatrix * mViewProj * mModel;

        const bool colored = vertices[vStart + indices[iStart]].color.w == 142;

        int need = vStart + vCount;
        if (need > swCacheCap) {
            delete[] swCache; delete[] swCacheV;
            swCacheCap = need + 4096;
            swCache  = new SwCache[swCacheCap];
            swCacheV = new VertexSW[swCacheCap];
            for (int k = 0; k < swCacheCap; k++) swCache[k].stamp = 0;
            swCacheCur = 0;
        }
        if (++swCacheCur == 0) { for (int k = 0; k < swCacheCap; k++) swCache[k].stamp = 0; swCacheCur = 1; }

        int32 cx1 = swClipRect.x << 16, cx2 = swClipRect.z << 16;
        int i = 0;
        while (i < iCount) {
            int vi[4], n;
            const bool isTriangle = vertices[vStart + indices[iStart + i]].normal.w == 1;
            if (isTriangle) {   /* 3 indices */
                if (i + 3 > iCount) break;
                n = 3;
                for (int k = 0; k < 3; k++) vi[k] = vStart + indices[iStart + i + k];
                i += 3;
            } else {            /* quad split by the loader as 0 1 2 [0 2] 3: 6 indices */
                if (i + 6 > iCount) break;
                n = 4;
                vi[0] = vStart + indices[iStart + i];
                vi[1] = vStart + indices[iStart + i + 1];
                vi[2] = vStart + indices[iStart + i + 2];
                vi[3] = vStart + indices[iStart + i + 5];
                i += 6;
            }
            swPrims++;
            swVertsIn += n;
            bool ok = true;
            for (int k = 0; k < n && ok; k++) ok = swProject(vi[k], vertices, swMatrix, colored);
            if (!ok) { swCullDist++; continue; }
            const VertexSW *a = &swCacheV[vi[0]], *b = &swCacheV[vi[1]], *c = &swCacheV[vi[2]];
            if (checkBackface(a, b, c)) { swCullBack++; continue; }
            int32 minX = a->x, maxX = a->x, minY = a->y, maxY = a->y;
            for (int k = 1; k < n; k++) {
                const VertexSW *q = &swCacheV[vi[k]];
                if (q->x < minX) minX = q->x;
                if (q->x > maxX) maxX = q->x;
                if (q->y < minY) minY = q->y;
                if (q->y > maxY) maxY = q->y;
            }
            if (maxX < cx1 || minX > cx2 || maxY < swClipRect.y || minY > swClipRect.w) { swCullClip++; continue; }
            for (int k = 0; k < n; k++) swIndices.push(Index(swEmit(vi[k], vertices)));
            if (n == 3) swTriangles.push(swIndices.length - 3);
            else        swQuads.push(swIndices.length - 4);
        }

        return colored;
    }

    /* ---- A2.13: second rasterizer (SWRAST=1, the default; SWRAST=0 = the A2.12 path above) --------
       - polygons clipped in clip space against the near plane and a guard band of SW_GUARD pixels around
         the screen instead of being dropped when one corner is behind the camera (the black areas
         behind Lara) or beyond the draw distance (dropped only when every corner is beyond it);
       - sub-pixel vertex positions, pixel centres at +0.5 and the top-left fill rule: an edge shared by
         two triangles is drawn once, without gap or overlap (the cracks);
       - texture coordinates corrected for perspective every 16 pixels (u/w, v/w, 1/w; one VFP division
         per 16 pixels), clamped to the texel range of the polygon; depth and light affine as before;
       - the pixel loop is that of drawLine: same depth test, palette, lightmap and colour key */
    #define SW_GUARD 1024.0f

    struct SwC { float x, y, z, w, u, v, l; };                  /* clip space (after the viewport matrix), texels, light */
    struct SwP { float x, y, z, iw, uw, vw, l, u, v; };         /* screen x y, depth z/w, 1/w, u/w, v/w, light, texels */
    struct SwV { uint32 stamp; uint8 oc, state; SwC c; SwP p; }; /* state: 1 projected, 2 lit */
    static SwV   *swV;
    static int    swVCap;
    static uint32 swVCur;
    static float  swRcp[1024];   /* 1 / n, n < 1024 (span lengths and segments) */

    enum { SW_OC_N = 1, SW_OC_L = 2, SW_OC_R = 4, SW_OC_T = 8, SW_OC_B = 16, SW_OC_CLIP = 31, SW_OC_F = 32 };

    static void swRastInit() {
        swRcp[0] = 0.0f;
        for (int i = 1; i < 1024; i++) swRcp[i] = 1.0f / float(i);
    }

    static inline int32 swCeil(float f) { int32 i = int32(f); return i + (f > float(i) ? 1 : 0); }

    /* signed distance to a clip plane (>= 0 inside). Near plane: the projection is PROJ_ZERO_POS and the
       viewport maps [-1, 1] to [0, 1], so z / w = 0.5 on the near plane */
    static inline float swPlane(const SwC &c, int bit) {
        switch (bit) {
            case SW_OC_N : return c.z - 0.5f * c.w;
            case SW_OC_L : return c.x + SW_GUARD * c.w;
            case SW_OC_R : return (float(Core::width)  + SW_GUARD) * c.w - c.x;
            case SW_OC_T : return c.y + SW_GUARD * c.w;
            default      : return (float(Core::height) + SW_GUARD) * c.w - c.y;
        }
    }

    static inline uint8 swOutcode(const SwC &c) {
        uint8 oc = 0;
        if (swPlane(c, SW_OC_N) < 0.0f) oc |= SW_OC_N;
        if (swPlane(c, SW_OC_L) < 0.0f) oc |= SW_OC_L;
        if (swPlane(c, SW_OC_R) < 0.0f) oc |= SW_OC_R;
        if (swPlane(c, SW_OC_T) < 0.0f) oc |= SW_OC_T;
        if (swPlane(c, SW_OC_B) < 0.0f) oc |= SW_OC_B;
        if (c.w > swMaxDist) oc |= SW_OC_F;
        return oc;
    }

    /* the point of an edge on the plane, always computed from the inside end: two polygons sharing the
       edge get the same point bit for bit */
    static inline void swLerp(SwC &o, const SwC &in, const SwC &out, float din, float dout) {
        float t = din / (din - dout);
        o.x = in.x + (out.x - in.x) * t;
        o.y = in.y + (out.y - in.y) * t;
        o.z = in.z + (out.z - in.z) * t;
        o.w = in.w + (out.w - in.w) * t;
        o.u = in.u + (out.u - in.u) * t;
        o.v = in.v + (out.v - in.v) * t;
        o.l = in.l + (out.l - in.l) * t;
    }

    static int swClipPoly(SwC *dst, const SwC *src, int n, int bit) {
        int m = 0;
        for (int i = 0; i < n; i++) {
            const SwC &a = src[i], &b = src[i + 1 < n ? i + 1 : 0];
            float da = swPlane(a, bit), db = swPlane(b, bit);
            if (da >= 0.0f) dst[m++] = a;
            if ((da >= 0.0f) != (db >= 0.0f)) {
                if (da >= 0.0f) swLerp(dst[m++], a, b, da, db);
                else            swLerp(dst[m++], b, a, db, da);
            }
        }
        return m;
    }

    static inline void swProjectC(SwP &p, const SwC &c) {
        float iw = 1.0f / c.w;
        p.x  = c.x * iw;
        p.y  = c.y * iw;
        p.z  = clamp(c.z * iw, 0.0f, 1.0f);
        p.iw = iw;
        p.uw = c.u * iw;
        p.vw = c.v * iw;
        p.u  = c.u;
        p.v  = c.v;
        p.l  = c.l;
    }

    inline SwV &swPrep(int vi, const Vertex *vertices, const mat4 &M, bool colored) {
        SwV &s = swV[vi];
        if (s.stamp == swVCur) return s;
        s.stamp = swVCur;
        s.state = 0;
        swVertsXf++;
        const Vertex &vertex = vertices[vi];
        vec4 cc = M * vec4(vertex.coord.x, vertex.coord.y, vertex.coord.z, 1.0f);
        s.c.x = cc.x; s.c.y = cc.y; s.c.z = cc.z; s.c.w = cc.w;
        if (colored) { s.c.u = float(vertex.color.x);    s.c.v = 0.0f; }
        else         { s.c.u = float(vertex.texCoord.x); s.c.v = float(vertex.texCoord.y); }
        s.c.l = 0.0f;
        s.oc = swOutcode(s.c);
        return s;
    }

    inline void swLit(SwV &s, const Vertex &vertex) {
        if (s.state & 2) return;
        VertexSW t;
        t.l = ((vertex.light.x * ambient) >> 8);
        applyLighting(t, vertex, s.c.w);
        s.c.l = float(t.l) * (1.0f / 65536.0f);
        s.p.l = s.c.l;
        s.state |= 2;
        swVertsLit++;
    }

    inline void swProj(SwV &s) {
        if (s.state & 1) return;
        float l = s.p.l;
        swProjectC(s.p, s.c);
        s.p.l = l;
        s.state |= 1;
    }

    /* the clip rectangle of the frame, in pixels (left, top inclusive; right, bottom exclusive) */
    static int32 swRX0, swRY0, swRX1, swRY1;

    static inline uint32 swFixZ(float z) { z = clamp(z, 0.0f, 1.0f); return uint32(z * (65535.0f * 65536.0f)); }

    /* one convex polygon (a triangle, a quad of the level or a clipped polygon), any orientation (the
       caller culls). Scanline walk: every attribute is interpolated along the edges, then along the
       span (a non-planar quad keeps the look of drawQuad, and two polygons sharing an edge compute it
       from the same top vertex and slope: the same pixels and values, bit for bit) */
    /* A2.14: the pixels of one segment (at most 16): depth test, texel, colour key, light + palette in
       one table. The same results as the A2.13 loop: zz = Z >> 16; texel (V >> 16) * 256 + (U >> 16) with
       V < 2^24, so ((V >> 8) & 0xFF00); light row (L >> 19) * 256 = (L >> 11) & 0x1F00 (0 <= L < 2^24).
       Unrolled for 16 pixels and entered at pixel 16 - cnt (no counter: the registers hold the four
       values, the four steps, the two tables and the two rows). */
    #ifdef ZZQ2RIX_HOST
        #define SW_HOST_COUNT(index) { swHostPass++; if (index) swHostWrite++; }
    #else
        #define SW_HOST_COUNT(index)
    #endif
    #define SW_PIXEL(j) {                                                   \
            uint32 zz = z >> 16;                                            \
            if (uint32(zb[j]) >= zz) {                                      \
                uint32 index = tile[((v >> 8) & 0xFF00u) | (u >> 16)];      \
                SW_HOST_COUNT(index)                                        \
                if (index) {                                                \
                    cb[j] = lp[((uint32(l) >> 11) & 0x1F00u) + index];      \
                    zb[j] = DepthSW(zz);                                    \
                }                                                           \
            }                                                               \
            z += dZ; u += dU; v += dV; l += dL; }

    /* one span: the float state of the perspective, the fixed-point steps, the tables */
    struct SwSpan {
        float iw, uw, vw, gi, gu, gv, umin, umax, vmin, vmax, u0, v0;
        uint32 Z, dZ;
        int32 L, dL;
        const uint8 *tile;
        const ColorSW *lp;
    };

    /* a whole span, segment after segment (one call per span: the floats stay in VFP registers) */
    static __attribute__((noinline)) void swSpanRun(ColorSW *cb, DepthSW *zb, int32 n, SwSpan *s) {
        uint32 z = s->Z, dZ = s->dZ;
        int32 l = s->L, dL = s->dL;
        const uint8 *tile = s->tile;
        const ColorSW *lp = s->lp;
        float iw = s->iw, uw = s->uw, vw = s->vw, u0 = s->u0, v0 = s->v0;
        const float gi = s->gi, gu = s->gu, gv = s->gv, umin = s->umin, umax = s->umax, vmin = s->vmin, vmax = s->vmax;
        while (n > 0) {
            int32 cnt = n > 16 ? 16 : n;
            int32 seg = n > 16 ? 16 : n - 1;
            float u1 = u0, v1 = v0;
            uint32 u = uint32(u0 * 65536.0f), v = uint32(v0 * 65536.0f), dU = 0, dV = 0;
            if (seg > 0) {
                float fs = float(seg);
                iw += gi * fs; uw += gu * fs; vw += gv * fs;
                float w1 = 1.0f / (iw > 1e-12f ? iw : 1e-12f);
                u1 = clamp(uw * w1, umin, umax);
                v1 = clamp(vw * w1, vmin, vmax);
                dU = uint32(int32((u1 - u0) * 65536.0f * swRcp[seg]));
                dV = uint32(int32((v1 - v0) * 65536.0f * swRcp[seg]));
            }
            __builtin_prefetch((const char*)cb + 64, 1);
            __builtin_prefetch((const char*)zb + 64, 1);
            /* 1..16 pixels: the rows moved back by 16 - cnt and entered at that pixel (no counter) */
            int32 skip = 16 - cnt;
            cb -= skip; zb -= skip;
            switch (skip) {
                case 0:  SW_PIXEL(0)
                case 1:  SW_PIXEL(1)
                case 2:  SW_PIXEL(2)
                case 3:  SW_PIXEL(3)
                case 4:  SW_PIXEL(4)
                case 5:  SW_PIXEL(5)
                case 6:  SW_PIXEL(6)
                case 7:  SW_PIXEL(7)
                case 8:  SW_PIXEL(8)
                case 9:  SW_PIXEL(9)
                case 10: SW_PIXEL(10)
                case 11: SW_PIXEL(11)
                case 12: SW_PIXEL(12)
                case 13: SW_PIXEL(13)
                case 14: SW_PIXEL(14)
                default: SW_PIXEL(15)
            }
            cb += 16; zb += 16; n -= cnt;
            u0 = u1; v0 = v1;
        }
    }
    #undef SW_PIXEL

    struct SwE { int32 y0, y1; const SwP *t; float dx, dz, diw, duw, dvw, dl; };

    void swPoly(const SwP *const *v, int n) {
        SwE e[16];
        int ne = 0;
        int32 ya = 0x7FFFFFFF, yb = -0x7FFFFFFF;
        float umin = 256.0f, umax = 0.0f, vmin = 256.0f, vmax = 0.0f;
        for (int i = 0; i < n; i++) {
            const SwP *a = v[i], *b = v[i + 1 < n ? i + 1 : 0];
            umin = min(umin, a->u); umax = max(umax, a->u);
            vmin = min(vmin, a->v); vmax = max(vmax, a->v);
            if (a->y == b->y) continue;
            if (a->y > b->y) swap(a, b);
            int32 y0 = swCeil(a->y - 0.5f), y1 = swCeil(b->y - 0.5f);
            if (y0 >= y1) continue;
            SwE &E = e[ne++];
            float r = 1.0f / (b->y - a->y);
            E.y0 = y0; E.y1 = y1; E.t = a;
            E.dx  = (b->x  - a->x)  * r;
            E.dz  = (b->z  - a->z)  * r;
            E.diw = (b->iw - a->iw) * r;
            E.duw = (b->uw - a->uw) * r;
            E.dvw = (b->vw - a->vw) * r;
            E.dl  = (b->l  - a->l)  * r;
            ya = min(ya, y0); yb = max(yb, y1);
        }
        if (ne < 2) return;
        if (umin < 0.0f) umin = 0.0f;
        if (vmin < 0.0f) vmin = 0.0f;
        if (umax > 255.99f) umax = 255.99f;
        if (vmax > 255.99f) vmax = 255.99f;
        if (umax < umin) umax = umin;
        if (vmax < vmin) vmax = vmin;
        if (ya < swRY0) ya = swRY0;
        if (yb > swRY1) yb = swRY1;

        const uint8 *tile = curTile->index;
        const ColorSW *lp = swLitPal[swPalIdx][swShadeIdx];
        const int32 W = Core::width;

        for (int32 y = ya; y < yb; y++) {
            float yc = float(y) + 0.5f;
            const SwE *eL = NULL, *eR = NULL;
            float xL = 0.0f, xR = 0.0f;
            for (int k = 0; k < ne; k++) {
                const SwE &E = e[k];
                if (y < E.y0 || y >= E.y1) continue;
                float x = E.t->x + (yc - E.t->y) * E.dx;
                if (!eL || x < xL) { eL = &E; xL = x; }
                if (!eR || x > xR) { eR = &E; xR = x; }
            }
            if (!eL || eL == eR) continue;
            int32 x0 = swCeil(xL - 0.5f), x1e = swCeil(xR - 0.5f);
            if (x0 < swRX0) x0 = swRX0;
            if (x1e > swRX1) x1e = swRX1;
            int32 n = x1e - x0;
            if (n <= 0) continue;
            swSpans++;
            swPixels += uint32(n);

            float fL = yc - eL->t->y, fR = yc - eR->t->y;
            float zL  = eL->t->z  + fL * eL->dz,  zR  = eR->t->z  + fR * eR->dz;
            float iL  = eL->t->iw + fL * eL->diw, iR  = eR->t->iw + fR * eR->diw;
            float uL  = eL->t->uw + fL * eL->duw, uR  = eR->t->uw + fR * eR->duw;
            float vL  = eL->t->vw + fL * eL->dvw, vR  = eR->t->vw + fR * eR->dvw;
            float lL  = eL->t->l  + fL * eL->dl,  lR  = eR->t->l  + fR * eR->dl;
            float span = xR - xL;
            float rs = span > 1e-4f ? 1.0f / span : 0.0f;
            float gz = (zR - zL) * rs, gi = (iR - iL) * rs, gu = (uR - uL) * rs, gv = (vR - vL) * rs, gl = (lR - lL) * rs;
            float fx = float(x0) + 0.5f - xL;
            float z  = zL + gz * fx;
            float iw = iL + gi * fx;
            float uw = uL + gu * fx;
            float vw = vL + gv * fx;
            float l  = lL + gl * fx;

            /* depth and light: affine, from the clamped values at both ends of the span */
            float last = float(n - 1);
            uint32 Z = swFixZ(z);
            int32 dZ = 0, L, dL = 0;
            float l0 = clamp(l, 0.0f, 255.99f);
            L = int32(l0 * 65536.0f);
            if (n > 1) {
                float rn = (n - 1 < 1024) ? swRcp[n - 1] : 1.0f / last;
                dZ = int32((float(swFixZ(z + gz * last)) - float(Z)) * rn);
                dL = int32((clamp(l + gl * last, 0.0f, 255.99f) - l0) * 65536.0f * rn);
            }

            float w0 = 1.0f / (iw > 1e-12f ? iw : 1e-12f);
            SwSpan sp;
            sp.iw = iw; sp.uw = uw; sp.vw = vw; sp.gi = gi; sp.gu = gu; sp.gv = gv;
            sp.umin = umin; sp.umax = umax; sp.vmin = vmin; sp.vmax = vmax;
            sp.u0 = clamp(uw * w0, umin, umax); sp.v0 = clamp(vw * w0, vmin, vmax);
            sp.Z = Z; sp.dZ = uint32(dZ); sp.L = L; sp.dL = dL; sp.tile = tile; sp.lp = lp;
            swSpanRun(swColor + y * W + x0, swDepth + y * W + x0, n, &sp);
        }
    }

    /* transform, clip, light and draw one DIP call; returns the ticks spent drawing */
    uint32 swDraw(const Index *indices, const Vertex *vertices, int iStart, int iCount, int vStart, int vCount) {
        mat4 M;
        M.viewport(0.0f, (float)Core::height, (float)Core::width, -(float)Core::height, 0.0f, 1.0f);
        M = M * mViewProj * mModel;

        const bool colored = vertices[vStart + indices[iStart]].color.w == 142;
        Tile8 *oldTile = curTile;
        if (colored) curTile = (Tile8*)swGradient;

        int need = vStart + vCount;
        if (need > swVCap) {
            delete[] swV;
            swVCap = need + 4096;
            swV = new SwV[swVCap];
            for (int k = 0; k < swVCap; k++) swV[k].stamp = 0;
            swVCur = 0;
        }
        if (++swVCur == 0) { for (int k = 0; k < swVCap; k++) swV[k].stamp = 0; swVCur = 1; }

        swRX0 = max(int32(swClipRect.x), int32(0));
        swRY0 = max(int32(swClipRect.y), int32(0));
        swRX1 = min(int32(swClipRect.z), int32(Core::width));
        swRY1 = min(int32(swClipRect.w), int32(Core::height));
        const float fx0 = float(swRX0), fy0 = float(swRY0), fx1 = float(swRX1), fy1 = float(swRY1);

        uint32 ras = 0;
        int i = 0;
        while (i < iCount) {
            int vi[4], n;
            const bool isTriangle = vertices[vStart + indices[iStart + i]].normal.w == 1;
            if (isTriangle) {
                if (i + 3 > iCount) break;
                n = 3;
                for (int k = 0; k < 3; k++) vi[k] = vStart + indices[iStart + i + k];
                i += 3;
            } else {            /* quad split by the loader as 0 1 2 [0 2] 3 */
                if (i + 6 > iCount) break;
                n = 4;
                vi[0] = vStart + indices[iStart + i];
                vi[1] = vStart + indices[iStart + i + 1];
                vi[2] = vStart + indices[iStart + i + 2];
                vi[3] = vStart + indices[iStart + i + 5];
                i += 6;
            }
            swPrims++;
            swVertsIn += n;

            SwV *s[4];
            uint8 oa = 0xFF, oo = 0;
            for (int k = 0; k < n; k++) {
                s[k] = &swPrep(vi[k], vertices, M, colored);
                oa &= s[k]->oc;
                oo |= s[k]->oc;
            }
            if (oa & (SW_OC_F | SW_OC_N)) { swCullDist++; continue; }   /* wholly beyond the distance or behind the near plane */
            if (oa & SW_OC_CLIP) { swCullClip++; continue; }           /* wholly outside the guard band */

            if (!(oo & SW_OC_CLIP)) {
                for (int k = 0; k < n; k++) swProj(*s[k]);
                const SwP *p0 = &s[0]->p, *p1 = &s[1]->p, *p2 = &s[2]->p;
                /* facing: the first three corners, as checkBackface */
                if ((p1->x - p0->x) * (p2->y - p0->y) - (p2->x - p0->x) * (p1->y - p0->y) <= 0.0f) { swCullBack++; continue; }
                float minX = p0->x, maxX = p0->x, minY = p0->y, maxY = p0->y;
                for (int k = 1; k < n; k++) {
                    const SwP *q = &s[k]->p;
                    minX = min(minX, q->x); maxX = max(maxX, q->x);
                    minY = min(minY, q->y); maxY = max(maxY, q->y);
                }
                if (maxX < fx0 || minX > fx1 || maxY < fy0 || minY > fy1) { swCullClip++; continue; }
                uint32 tl = swTicks();
                for (int k = 0; k < n; k++) swLit(*s[k], vertices[vi[k]]);
                swTLight += swTicks() - tl;
                const SwP *pv[4] = { p0, p1, p2, &s[n - 1]->p };
                uint32 t0 = swTicks();
                swPoly(pv, n);
                ras += swTicks() - t0;
                continue;
            }

            /* clipping: near plane and guard band */
            swClipped++;
            SwC bufA[16], bufB[16];
            uint32 tl = swTicks();
            for (int k = 0; k < n; k++) { swLit(*s[k], vertices[vi[k]]); bufA[k] = s[k]->c; }
            swTLight += swTicks() - tl;
            SwC *src = bufA, *dst = bufB;
            int m = n;
            for (int bit = 1; bit <= SW_OC_B && m >= 3; bit <<= 1) {
                /* the points made on the near plane may lie outside the guard band: the side planes
                   are tested on the current polygon, not on the corners */
                bool out = false;
                if (bit == SW_OC_N) out = (oo & SW_OC_N) != 0;
                else for (int k = 0; k < m && !out; k++) out = swPlane(src[k], bit) < 0.0f;
                if (!out) continue;
                m = swClipPoly(dst, src, m, bit);
                SwC *tmp = src; src = dst; dst = tmp;
            }
            if (m < 3) { swCullClip++; continue; }
            SwP pts[16];
            float area = 0.0f;
            float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
            for (int k = 0; k < m; k++) {
                swProjectC(pts[k], src[k]);
                minX = min(minX, pts[k].x); maxX = max(maxX, pts[k].x);
                minY = min(minY, pts[k].y); maxY = max(maxY, pts[k].y);
            }
            for (int k = 1; k + 1 < m; k++)
                area += (pts[k].x - pts[0].x) * (pts[k + 1].y - pts[0].y) - (pts[k + 1].x - pts[0].x) * (pts[k].y - pts[0].y);
            if (area <= 0.0f) { swCullBack++; continue; }
            if (maxX < fx0 || minX > fx1 || maxY < fy0 || minY > fy1) { swCullClip++; continue; }
            const SwP *pv[16];
            for (int k = 0; k < m; k++) pv[k] = &pts[k];
            uint32 t0 = swTicks();
            swPoly(pv, m);
            ras += swTicks() - t0;
        }

        curTile = oldTile;
        return ras;
    }
#else
    bool transform(const Index *indices, const Vertex *vertices, int iStart, int iCount, int vStart) {
        swVertices.reset();
        swIndices.reset();
        swTriangles.reset();
        swQuads.reset();

        mat4 swMatrix;
        swMatrix.viewport(0.0f, (float)Core::height, (float)Core::width, -(float)Core::height, 0.0f, 1.0f);
        swMatrix = swMatrix * mViewProj * mModel;

        const bool colored = vertices[vStart + indices[iStart]].color.w == 142;
        int vIndex = 0;
        bool isTriangle = false;

        for (int i = 0; i < iCount; i++) {
            const Index  index   = indices[iStart + i];
            const Vertex &vertex = vertices[vStart + index];

            vIndex++;

            if (vIndex == 1) {
                isTriangle = vertex.normal.w == 1;
            } else {
                if (vIndex == 4) { // loader splits quads to two triangles with indices 012[02]3, we ignore [02] to make it quad again!
                    vIndex++;
                    i++;
                    continue;
                }
            }

            vec4 c;
            c = swMatrix * vec4(vertex.coord.x, vertex.coord.y, vertex.coord.z, 1.0f);

            if (c.w < 0.0f || c.w > SW_MAX_DIST) { // skip primitive
                if (isTriangle) {
                    i += 3 - vIndex;
                } else {
                    i += 6 - vIndex;
                }
                vIndex = 0;
                continue;
            }

            c.x /= c.w;
            c.y /= c.w;
            c.z /= c.w;
            c.x = clamp(c.x, -16384.0f, 16384.0f);
            c.y = clamp(c.y, -16384.0f, 16384.0f);

            VertexSW result;
        #ifdef _OS_ZZ
            result.x = int32(c.x) * 65536;   /* same value, defined for negative x (|x| <= 16384) */
        #else
            result.x = int32(c.x) << 16;
        #endif
            result.y = int32(c.y);
            result.z = uint32(clamp(c.z, 0.0f, 1.0f) * 65535.0f) << 16;
            result.w = int32(c.w);

            if (colored) {
                result.u = vertex.color.x << 16;
                result.v = 0;
            } else {
                result.u = (vertex.texCoord.x << 16);// / result.w;
                result.v = (vertex.texCoord.y << 16);// / result.w;
            }
            result.w = result.w << 16;
            result.l = ((vertex.light.x * ambient) >> 8);

            applyLighting(result, vertex, c.w);

            swIndices.push(swVertices.push(result));

            if (isTriangle && vIndex == 3) {
                swTriangles.push(swIndices.length - 3);
                vIndex = 0;
            } else if (vIndex == 6) {
                swQuads.push(swIndices.length - 4);
                vIndex = 0;
            }
        }

        return colored;
    }

#endif

    void transformLights() {
        memcpy(lightsRel, lights, sizeof(LightSW) * lightsCount);

        mat4 mModelInv = mModel.inverseOrtho();
        for (int i = 0; i < lightsCount; i++) {
            lightsRel[i].pos = mModelInv * lights[i].pos;
        }
    }

    void DIP(Mesh *mesh, const MeshRange &range) {
        if (curTile == NULL) {
            //uint32 *tex = (uint32*)Core::active.textures[0]->memory; // TODO
            return;
        }

        transformLights();
    #ifdef _OS_ZZ
        dipBatches++;
    #endif
       
    #ifdef _OS_ZZ
        if (swRast) {   /* A2.13 */
            uint32 t0 = swTicks();
            uint32 ras = swDraw(mesh->iBuffer, mesh->vBuffer, range.iStart, range.iCount, range.vStart, mesh->vCount);
            uint32 all = swTicks() - t0;
            swTRaster += ras;
            swTXform  += all - ras;
            return;
        }
        uint32 tx0 = swTicks();
        bool colored = transform(mesh->iBuffer, mesh->vBuffer, range.iStart, range.iCount, range.vStart, mesh->vCount);
        uint32 tx1 = swTicks();
        swTXform += tx1 - tx0;
    #else
        bool colored = transform(mesh->iBuffer, mesh->vBuffer, range.iStart, range.iCount, range.vStart);
    #endif

        Tile8 *oldTile = curTile;

        if (colored) {
            curTile = (Tile8*)swGradient;
        }

        for (int i = 0; i < swQuads.length; i++) {
            drawQuad(&swIndices[swQuads[i]]);
        }

        for (int i = 0; i < swTriangles.length; i++) {
            drawTriangle(&swIndices[swTriangles[i]]);
        }

        curTile = oldTile;
    #ifdef _OS_ZZ
        swTRaster += swTicks() - tx1;
    #endif
    }

#ifdef _OS_ZZ
    /* ZZOpenLara 1.0: the palette as read (8 bits per channel), for the colour of the UI bars (mesh.h addDynBar,
       addDynFrame): this renderer has neither the common textures nor vertex colours */
    Color24 swPal24[256];
    uint32  swNearKey[8];
    uint8   swNearVal[8];
    int     swNearNext;

    /* the palette index closest to r g b (1..255: index 0 is the transparent one); a few recent answers kept */
    uint8 swNearest(int r, int g, int b) {
        uint32 key = 0x1000000u | (uint32(r) << 16) | (uint32(g) << 8) | uint32(b);
        for (int i = 0; i < 8; i++) if (swNearKey[i] == key) return swNearVal[i];
        int best = 1, bestD = 0x7FFFFFFF;
        for (int i = 1; i < 256; i++) {
            int dr = int(swPal24[i].r) - r, dg = int(swPal24[i].g) - g, db = int(swPal24[i].b) - b;
            int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
            if (d < bestD) { bestD = d; best = i; }
        }
        swNearKey[swNearNext] = key; swNearVal[swNearNext] = uint8(best);
        swNearNext = (swNearNext + 1) & 7;
        return uint8(best);
    }
#endif

    void initPalette(Color24 *palette, uint8 *lightmap) {
    #ifdef _OS_ZZ
        for (int i = 0; i < 256; i++) swPal24[i] = palette[i];
        for (int i = 0; i < 8; i++) swNearKey[i] = 0;
    #endif
        for (uint32 i = 0; i < 256; i++) {
            const Color24 &p = palette[i];
            swPaletteColor[i] = CONV_COLOR(p.r, p.g, p.b);
            swPaletteWater[i] = CONV_COLOR((uint32(p.r) * 150) >> 8, (uint32(p.g) * 230) >> 8, (uint32(p.b) * 230) >> 8);
            swPaletteGray[i]  = CONV_COLOR((i * 57) >> 8, (i * 29) >> 8, (i * 112) >> 8);
            swGradient[i]     = i;
        }

        for (uint32 i = 0; i < 256 * 32; i++) {
            swLightmapNone[i]  = i % 256;
            swLightmapShade[i] = lightmap[i];
        }

        swLightmap = swLightmapShade;
        swPalette  = swPaletteColor;
    #ifdef _OS_ZZ
        /* A2.14: the folded tables (swLitPal) */
        ColorSW *pals[3] = { swPaletteColor, swPaletteWater, swPaletteGray };
        uint8   *maps[2] = { swLightmapNone, swLightmapShade };
        for (int p = 0; p < 3; p++)
            for (int s = 0; s < 2; s++) {
                if (!swLitPal[p][s]) swLitPal[p][s] = new ColorSW[32 * 256];
                for (int i = 0; i < 32 * 256; i++) swLitPal[p][s][i] = pals[p][maps[s][i]];
            }
        swPalIdx = 0; swShadeIdx = 1;
    #endif
    }

    void setPalette(ColorSW *palette) {
        swPalette = palette;
    #ifdef _OS_ZZ
        swPalIdx = palette == swPaletteWater ? 1 : (palette == swPaletteGray ? 2 : 0);
    #endif
    }

    void setShading(bool enabled) {
        swLightmap = enabled ? swLightmapShade : swLightmapNone;
    #ifdef _OS_ZZ
        swShadeIdx = enabled ? 1 : 0;
    #endif
    }

    vec4 copyPixel(int x, int y) {
        return vec4(0.0f); // TODO: read from framebuffer
    }
}

#endif
