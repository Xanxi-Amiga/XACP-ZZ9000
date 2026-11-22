/* Byte protocol shared by the 68k producer, Core1 and the independent host harness.
 * A3: protocol v5 (resident client arrays, indexed draws, texgen, normals, matrices). */
#ifndef ZZ_WIRE_H
#define ZZ_WIRE_H
typedef unsigned int zu32;
typedef unsigned char zu8;
typedef char zz_u32_is_4[sizeof(zu32)==4?1:-1];
#define ZZ_BASE     0x04500000u
#define ZZ_CTRL     0x04650000u
#define ZZ_TRACE    (ZZ_CTRL + 128u)
/* A3: the coherent Core0 copy of the control block moved from +256 to +512 so that it can
   cover the A3 statistics words (8..55) without overlapping its own source. */
#define ZZ_SNAPSHOT (ZZ_CTRL + 512u)
#define ZZ_SNAPSHOT_BYTES 224u   /* words 8..63 of the control block (A5: 192 -> 224 for MH_STAGED and the DMA counters) */
#define ZZ_DATA     0x04650400u
#define ZZ_LIMIT    0x00041000u  /* fits one 256x256 RGBA image and its record */
#define ZZ_MAGIC    0x31474D5Au /* ZMG1 in memory */
#define ZZ_PROTO    18u  /* A8.23: Z_SHOW; 17 = A8.21: the static store after the vertex cache (offsets up to ZZ_VC_BYTES + ZZ_VS_BYTES); 16 = A8.19: Z_COLOR, Z_INDEX_KEEP, Z_INDEX_AGAIN; 15 = A8.18; 14 = A8.17 */
#define ZZ_MAX_TEXTURES 2560u   /* A9: names 0..2559 (A8 1280, A6/A7 1024): GLQuake numbers up to MAX_GLTEXTURES 2048 + lightmaps, scraps, players; Core1 builds ZZGL with -DZZGL_MAX_TEXTURES=2560u, state in .zz_arena */
#define ZZ_COOKIE   0xA248C715u
#define ZZ_MAXVERT  64u
#define ZZ_MAX_ARRAY_VERTS  8190u
/* A3: resident client arrays (GLLockArrays): at most this many vertices in Core1's store */
#define ZZ_MAX_RESIDENT_VERTS 3072u
/* A3 vertex format mask of Z_ARRAY_LOAD / Z_DRAW_ARRAYS (position xyz is always present) */
#define ZZ_VF_POS3   1u
#define ZZ_VF_NORMAL 2u   /* nx ny nz */
#define ZZ_VF_UV     4u   /* s t */
#define ZZ_VF_COLOR  8u   /* r g b a (floats) */
#define ZZ_VF_ALL    15u
/* A8.17: raw records.  The producer writes its own words (floats as they are in memory, colours as
   one word or four bytes) and says in the flags word whether they are big-endian (the 68k) or
   little-endian (the host harness); Core1 reorders and expands them.  Vertex layout, in this order:
   xyz, [nx ny nz], [s t], [s1 t1 (unit 1, ZZ_VF_UV1)], [colour: 1 word, or 4 floats for ZZ_RAWCOL_FLOAT] */
#define ZZ_VF_UV1    16u
#define ZZ_VF_RAWALL 31u
#define ZZ_RAW_BE    0x100u   /* flags: the words are big-endian */
#define ZZ_RAW_CONST 0x200u   /* Z_DRAW_RAW flags: one colour (4 floats) for the whole batch */
#define ZZ_RAWCOL_RGBA8 0u    /* flags low byte: four bytes r g b a in memory order */
#define ZZ_RAWCOL_FLOAT 1u    /* four floats */
#define ZZ_RAWCOL_ARGB  2u    /* one word a<<24|r<<16|g<<8|b (MiniGL MGL_UBYTE_ARGB) */
#define ZZ_RAWCOL_BGRA  3u    /* one word b<<24|g<<16|r<<8|a (MiniGL MGL_UBYTE_BGRA) */
#define ZZ_RAWCOL_RGB8  4u    /* three bytes r g b and one unused byte, alpha 1 */
#define ZZ_RAW_MAX_VERTS 3072u   /* Z_ARRAY_LOAD_RAW and Z_DRAW_ARRAYS_RAW */
/* A8.17: Core1's vertex cache (section 0x3E8 of the Core1 arena).  The 68k decides every offset and keeps a
   mirror of the same bytes: a load is replaced by Z_ARRAY_LOAD_CACHED only when the mirror holds exactly
   the record it was about to send (memory comparison, no hash trust). */
#define ZZ_VC_BYTES  (1024u*1024u)
/* A8.21: the static store: records the application declared unchanging (GL_RGL_static_arrays), kept by
   Core1 for as long as the 68k says, never compared again.  One offset space for both: [0, ZZ_VC_BYTES)
   is the vertex cache, [ZZ_VC_BYTES, ZZ_VC_BYTES + ZZ_VS_BYTES) the static store; a kept record lies in one. */
#define ZZ_VS_BYTES  (3u*1024u*1024u)
static inline int zkept_range_ok(zu32 off,zu32 len){
 if((off&3u)||(len&3u)||len<12u)return 0;
 if(off<ZZ_VC_BYTES)return len<=ZZ_VC_BYTES&&off<=ZZ_VC_BYTES-len;
 off-=ZZ_VC_BYTES;return len<=ZZ_VS_BYTES&&off<=ZZ_VS_BYTES-len;
}
#define ZZ_VC_NONE   0xFFFFFFFFu
/* A8.17: Z_IMAGE / Z_SUBIMAGE texel kinds besides 1, 3, 4 bytes: ARGB4444 words (MiniGL
   MGL_UNSIGNED_SHORT_4_4_4_4, GLQuake's lightmaps), stored by Core1 as RGB (alpha dropped as before) */
#define ZZ_TEX_ARGB4444_LE 0x4442u
#define ZZ_TEX_ARGB4444_BE 0x4443u
static inline zu32 zraw_vertex_bytes(zu32 fmt,zu32 flags) {
 return 12u + ((fmt & ZZ_VF_NORMAL) ? 12u : 0u) + ((fmt & ZZ_VF_UV) ? 8u : 0u) + ((fmt & ZZ_VF_UV1) ? 8u : 0u)
      + ((fmt & ZZ_VF_COLOR) ? (((flags & 0xFFu) == ZZ_RAWCOL_FLOAT) ? 16u : 4u) : 0u);
}
static inline zu32 zswap32(zu32 v) { return (v>>24)|((v>>8)&0xFF00u)|((v<<8)&0xFF0000u)|(v<<24); }
/* Only the low ZZ9000 video DDR aperture may be a direct P96 target. */
#define ZZ_P96_LOW_ARM  0x00200000u
#define ZZ_P96_HIGH_ARM 0x04000000u
/* Picasso96 RGBFTYPE bit positions, NOT RGBFF masks. */
#define ZZ_P96_FMT_RGB565_PC 4u   /* RGBFB_R5G6B5PC: little-endian */
#define ZZ_P96_FMT_RGB565_BE 10u  /* RGBFB_R5G6B5: Motorola/big-endian */
/* A1c protocol v3: direct-presentation copy strategy is explicit. */
#define ZZ_P96_COPY_SCALAR 0u
#define ZZ_P96_COPY_BURST32 1u
#define ZZ_P96_COPY_DMA     2u   /* A5: PL330 src->dst copy with the 16-bit swap (ZZHalfLife build 35); Core1 falls back to BURST32 */
#define MH_P96_PROBE 36u /* trace continuation, included in the Core0 snapshot */
#define MH_P96_COPY_US 37u
#define MH_P96_FAST_FRAMES 38u
#define MH_P96_REJECTS 39u
/* A3 Core1 statistics (words 40..55, in the snapshot) */
#define MH_DECODE_US   40u /* packet staging copy + validation pass */
#define MH_XFORM_US    41u /* vertex processing: MVP, normals, texgen */
#define MH_SETUP_US    42u /* triangle setup: clip, project, descriptors, list writes */
#define MH_RINGWAIT_US 43u /* Core1 stalled on a chunk ring slot or bind fence (GPU behind) */
#define MH_VERTS       44u /* vertices processed (texgen or not) */
#define MH_RESIDENT    45u /* vertices currently resident (GLLockArrays) */
#define MH_IDX_TRIS    46u /* triangles drawn through Z_DRAW_ELEMENTS */
#define MH_CHUNKS      47u /* display-list chunks submitted */
#define MH_GL_ERRORS   48u /* GL errors recorded (non-fatal, reported by GLGetError) */
/* A3b: fields still inside the existing 8..55 Core0-coherent snapshot. */
#define MH_FAST_CLIP_TRIS    49u  /* triangles already inside screen (no copy/clip) */
#define MH_SCREEN_CLIP_TRIS  50u  /* triangles still passed to screen clipper */
#define MH_DESC_US           51u  /* timed rix_tri_desc calls; PROFILE only */
#define MH_EMIT_US           52u  /* timed rix_frame_tri calls; PROFILE only */
#define MH_PROFILE_ACTIVE    53u  /* Core1 acknowledges profiler switch */
#define MH_TEX_FNV         54u /* FNV-1a over the actual DDR RGB565 texels, little-endian bytes */
#define MH_PROFILE_SWITCH    55u  /* input before Core1 launch; ZZMiniGLGPUProfile=ON */
#define MH_STAT_END    56u
/* A5 (protocol 10): words 57..63, inside the enlarged snapshot */
#define MH_STAGED      57u /* Core1: the packet with this sequence has been copied out of ZZ_DATA (or fully decoded in place):
                              the 68k may overwrite ZZ_DATA.  Written before the ACK of the same packet. */
#define MH_P96_DMA_FRAMES 58u /* direct presentations copied by the PL330 */
#define MH_P96_DMA_FAULTS 59u /* PL330 copies that faulted or timed out (completed by the CPU copy, DMA disabled for the run) */
#define MH_P96_DMA_INFO   60u
#define MH_NOZBUF         61u /* A6 input before launch (ZZMiniGLNoZ=ON): the context has no depth buffer (diagnostic, painter applications) */
#define MH_LIST_WORDS     62u /* RasterIX display-list words accumulated for completed frames */
#define MH_TEX_STATS      63u /* A8: texture uploads refused by ZZGL (pool full even at 1/4 size) in bits 0..15, uploads stored reduced in bits 16..31 */
#define MH_CTRL_WORDS     64u /* words the 68k clears at start and the snapshot covers (8..63) */ /* last fault: FTR, or 0xA5000001 timeout, 0xA5000002 not available, 0xA5000003 geometry */
enum zz_reg { MH_MAGIC, MH_VERSION, MH_COOKIE, MH_WIDTH, MH_HEIGHT, MH_BYTES,
 MH_PUBLISH, MH_RESERVED, MH_STATUS, MH_ACK, MH_ERROR, MH_PHASE, MH_CAPS,
 MH_BUF, MH_FRAMES, MH_CPU_US, MH_FENCE_US, MH_TRIS, MH_LAST_GL,
 MH_TIMER, MH_CHECK, MH_RUNS, MH_FAULT_PC, MH_FAULT_FAR, MH_FAULT_TYPE,
 MH_GPU_ID, MH_GPU_VER, MH_LADDER, MH_GPU_SPAN_US, MH_VARY, MH_NONZERO, MH_EXIT };
enum zz_status { ZZ_STARTING=1, ZZ_READY, ZZ_FATAL, ZZ_STOPPED };
enum zz_err { ZE_HEADER=0xA101, ZE_SIZE, ZE_OP, ZE_ARG, ZE_SEQ, ZE_BUSY,
 ZE_TIMEOUT, ZE_OWNER, ZE_P96, ZE_ABI, ZE_BOOT, ZE_GPU, ZE_AUTH, ZE_STALE,
 ZE_GUARD };   /* 0xA10F, A8.26: 800x600: a guard next to the buffers changed, or the depth clear did not reach the depth buffer */
enum zz_op { Z_CLEAR=1,Z_CLEARCOLOR,Z_VIEW,Z_MATRIX,Z_IDENTITY,Z_MOVE,Z_ROT,
 Z_SCALE,Z_ENABLE,Z_DEPTHFUNC,Z_DEPTHMASK,Z_BIND,Z_IMAGE,Z_DRAW,Z_PRESENT,
 Z_FINISH,Z_STOP,Z_FRUSTUM,Z_ORTHO,Z_PUSH,Z_POP,Z_BLEND,Z_ALPHA,Z_TEXENV,
 Z_TEXPAR,Z_DEPTHRANGE,Z_SHADE,Z_CULL,Z_FRONT,Z_SCISSOR,Z_CLEARD,Z_SUBIMAGE,Z_FLUSH,Z_P96_PROBE,Z_PRESENT_DIRECT,Z_DRAW_PACKED,
 /* A3 */
 Z_ARRAY_LOAD,      /* 37: fmt, count, count x packed vertex (floats) -> resident store */
 Z_ARRAY_UNLOCK,    /* 38: no data */
 Z_DRAW_ELEMENTS,   /* 39: mode, count, count x u16 index into the resident store (padded) */
 Z_DRAW_ARRAYS,     /* 40: mode, fmt, count, count x packed vertex (floats) */
 Z_TEXGEN,          /* 41: coord, pname, 4 words (mode in word 0, or the plane) */
 Z_DRAW_N,          /* 42: mode, count, count x 12 floats (xyz nxnynz rgba st) */
 Z_LOADMATRIX,      /* 43: 16 floats */
 Z_MULTMATRIX,      /* 44: 16 floats */
 Z_DELETE_TEX,      /* 45: n, n x u32 names */
 /* A7 */
 Z_PRESENT_NONE,    /* 46: no data; closes the frame like Z_PRESENT_DIRECT (fence, counters, buffer
                         flip) without any copy, useful for off-screen timing */
 /* A8.17 */
 Z_ARRAY_LOAD_RAW,  /* 47: store, fmt, flags, count, count x raw vertex -> resident store (+ unit-1 copy when ZZ_VF_UV1);
                         store != ZZ_VC_NONE: the record from fmt on is also kept at that offset of Core1's
                         vertex cache */
 Z_DRAW_ARRAYS_RAW, /* 48: mode, fmt, flags, count, count x raw vertex */
 Z_DRAW_ELEMENTS_MT,/* 49: mode, count, lightmap name, pass flags, app env, app bound, app blend s, d,
                         count x u16 index (padded): drawn as Z_DRAW_ELEMENTS, then queued by Core1 for the
                         unit-1 pass with the resident xyz and s1 t1 */
 Z_MT_FLUSH,        /* 50: pass flags, app env, app bound, app blend s, d: the queued unit-1 pass */
 Z_DRAW_RAW,        /* 51: mode, count, flags, [r g b a if ZZ_RAW_CONST], count x (xyz [rgba] st) raw words */
 Z_ARRAY_LOAD_CACHED,/* 52: offset, bytes: the Z_ARRAY_LOAD_RAW kept there (fmt on), loaded again */
 /* A8.18 */
 Z_DRAW_ELEMENTS_RAW,/* 53: mode, count, flags (ZZ_RAW_BE), count x u16 index in the producer's order (padded);
                         Z_DRAW_ELEMENTS_MT takes the same flag in its pass-flags word */
 /* A8.19 */
 Z_COLOR,           /* 54: r g b a (floats): the current colour, for the arrays that carry none */
 Z_INDEX_KEEP,      /* 55: offset, op, the payload of that op (Z_DRAW_ELEMENTS_RAW or Z_DRAW_ELEMENTS_MT): the record
                         from op on is kept at that offset of Core1's cache, then executed */
 Z_INDEX_AGAIN,     /* 56: offset, bytes: the record kept there (op on), executed again */
 Z_SHOW,            /* 57: A8.23: target, pitch, format, copy mode as Z_PRESENT_DIRECT: the last frame closed
                       (Z_PRESENT_NONE) is copied to the locked P96 bitmap; no frame is closed.  Last record
                       of its packet: the 68k holds the P96 lock until that packet's ACK */
 Z_POINTSIZE,       /* 58: A8.23: size (float): glPointSize */
 Z_OP_END };
/* 32-bit scalars are little-endian in the packet.  The 68k (big-endian) assembles them byte by
   byte; Core1 and the host harness (little-endian, unaligned loads allowed on ARMv7 and x86)
   use plain word accesses, which matters on Core1 where the packet lives in non-cacheable DDR
   (one bus transaction per access: four byte loads per word cost four round trips). */
#if defined(__arm__) || defined(__aarch64__) || defined(__i386__) || defined(__x86_64__)
#define ZZ_WORD_ACCESS 1
#endif
static inline zu32 zget(const zu8 *p) {
#ifdef ZZ_WORD_ACCESS
 zu32 v; __builtin_memcpy(&v,p,4); return v;
#else
 return (zu32)p[0]|((zu32)p[1]<<8)|((zu32)p[2]<<16)|((zu32)p[3]<<24);
#endif
}
static inline void zput(zu8 *p,zu32 v) {
#ifdef ZZ_WORD_ACCESS
 __builtin_memcpy(p,&v,4);
#else
 p[0]=(zu8)v; p[1]=(zu8)(v>>8); p[2]=(zu8)(v>>16); p[3]=(zu8)(v>>24);
#endif
}
static inline zu32 zget16(const zu8 *p) { return (zu32)p[0]|((zu32)p[1]<<8); }
static inline void zput16(zu8 *p,zu32 v) { p[0]=(zu8)v; p[1]=(zu8)(v>>8); }
static inline zu32 znext(zu32 n) { return n==0xffffffffu ? 1u : n+1u; }
static inline zu32 zfloat(float f) { union {float f;zu32 u;} v;v.f=f;return v.u; }
static inline float zreal(zu32 u) { union {float f;zu32 u;} v;v.u=u;return v.f; }
/* bytes of one packed vertex of format fmt (A3 arrays) */
static inline zu32 zvertex_bytes(zu32 fmt) {
 return 12u + ((fmt & ZZ_VF_NORMAL) ? 12u : 0u) + ((fmt & ZZ_VF_UV) ? 8u : 0u) + ((fmt & ZZ_VF_COLOR) ? 16u : 0u);
}
typedef int (*zz_visit)(zu32,const zu8 *,void *);
extern zu32 zz_wire_fail_op,zz_wire_fail_at,zz_wire_fail_word;   /* A8.8 */
int zpacket(const zu8 *packet,zu32 bytes,zu32 seq,zz_visit visit,void *ctx);
int zraw_load_ok(const zu8 *p,zu32 n);   /* A8.17 */
int zkept_draw_ok(const zu8 *p,zu32 n);  /* A8.19: a kept index record (op on), checked again before it is executed */
zu32 zkept_draw_bytes(const zu8 *p);     /* A8.19: its length, op word included (a checked record) */
#endif
