/* ZZScreensaverR: the "gl_common.h" that matrixview.c includes, ZZ9000 version.
 * Copyright (C) 2026 Xanxi. GPL-2.0-or-later. See COPYING. No warranty.
 *
 * MatrixView (MPlayer libvo/matrixview.c) draws through MPlayer's mpgl* wrappers.
 * Here those names are functions of mv_gl.c, which draws through ZZGL on Core1.
 * The three C library services the scene uses are routed to mv_sys.c: a Core1
 * blob has no heap, and one generator gives the same rain on Core1 and on the
 * PC bench.
 */
#ifndef ZZSM_GL_COMMON_H
#define ZZSM_GL_COMMON_H
#include <stddef.h>
#include <stdlib.h>

#define GL_QUADS              0x0007
#define GL_ONE                1
#define GL_BLEND              0x0BE2
#define GL_TEXTURE_2D         0x0DE1
#define GL_UNSIGNED_BYTE      0x1401
#define GL_LUMINANCE          0x1909
#define GL_MODULATE           0x2100
#define GL_TEXTURE_ENV_MODE   0x2200
#define GL_TEXTURE_ENV        0x2300
#define GL_LINEAR             0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_COLOR_BUFFER_BIT   0x00004000

void mpglBegin(unsigned mode);
void mpglEnd(void);
void mpglColor4ub(int r, int g, int b, int a);
void mpglTexCoord2f(float s, float t);
void mpglVertex3f(float x, float y, float z);
void mpglTexImage2D(unsigned target, int level, int internalformat, int width, int height,
                    int border, unsigned format, unsigned type, const void *pixels);
void mpglTexParameterf(unsigned target, unsigned pname, float param);
void mpglTexEnvi(unsigned target, unsigned pname, int param);
void mpglClearColor(float r, float g, float b, float a);
void mpglClear(unsigned mask);
void mpglEnable(unsigned cap);
void mpglBlendFunc(unsigned sfactor, unsigned dfactor);
void mpglViewport(int x, int y, int width, int height);
void mpglLoadMatrixf(const float *m);

void *mv_calloc(size_t count, size_t size);
void mv_free(void *block);
int mv_rand(void);

#define calloc mv_calloc
#define free mv_free
#define rand mv_rand
#undef RAND_MAX
#define RAND_MAX 0x7fffffff

#endif
