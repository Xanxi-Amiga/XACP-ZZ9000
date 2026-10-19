/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * GL/gl.h for ref_gl in ZZQuake2RIX.
 *
 * ref_gl includes <GL/gl.h> for the TYPES and the ENUMS of OpenGL 1.1
 * only: every call goes through the qgl* pointers (qgl.h), bound to
 * ZZGL in zzq2rix_qgl.c.  This header is self-contained (no TinyGL):
 * the OpenGL 1.1 types and the enums ref_gl references, with their
 * standard numeric values (the same ZZGL uses, zzgl.h ZGL_*).
 *
 * ASCII only.
 */
#ifndef ZZQ2RIX_GL_SHIM_H
#define ZZQ2RIX_GL_SHIM_H

#ifndef APIENTRY
#define APIENTRY
#endif

typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef unsigned int   GLuint;
typedef float          GLfloat;
typedef float          GLclampf;
typedef double         GLdouble;
typedef double         GLclampd;
typedef void           GLvoid;

#define GL_FALSE                    0
#define GL_TRUE                     1
#define GL_NO_ERROR                 0

/* primitives */
#define GL_POINTS                   0x0000
#define GL_LINES                    0x0001
#define GL_LINE_STRIP               0x0003
#define GL_TRIANGLES                0x0004
#define GL_TRIANGLE_STRIP           0x0005
#define GL_TRIANGLE_FAN             0x0006
#define GL_QUADS                    0x0007
#define GL_POLYGON                  0x0009

/* comparison functions */
#define GL_NEVER                    0x0200
#define GL_LESS                     0x0201
#define GL_EQUAL                    0x0202
#define GL_LEQUAL                   0x0203
#define GL_GREATER                  0x0204
#define GL_NOTEQUAL                 0x0205
#define GL_GEQUAL                   0x0206
#define GL_ALWAYS                   0x0207

/* blend factors */
#define GL_ZERO                     0
#define GL_ONE                      1
#define GL_SRC_COLOR                0x0300
#define GL_ONE_MINUS_SRC_COLOR      0x0301
#define GL_SRC_ALPHA                0x0302
#define GL_ONE_MINUS_SRC_ALPHA      0x0303
#define GL_DST_ALPHA                0x0304
#define GL_ONE_MINUS_DST_ALPHA      0x0305
#define GL_DST_COLOR                0x0306
#define GL_ONE_MINUS_DST_COLOR      0x0307
#define GL_SRC_ALPHA_SATURATE       0x0308

/* faces, buffers */
#define GL_FRONT_LEFT               0x0400
#define GL_FRONT_RIGHT              0x0401
#define GL_BACK_LEFT                0x0402
#define GL_BACK_RIGHT               0x0403
#define GL_FRONT                    0x0404
#define GL_BACK                     0x0405
#define GL_FRONT_AND_BACK           0x0408
#define GL_CW                       0x0900
#define GL_CCW                      0x0901

/* capabilities */
#define GL_POINT_SMOOTH             0x0B10
#define GL_CULL_FACE                0x0B44
#define GL_DEPTH_TEST               0x0B71
#define GL_ALPHA_TEST               0x0BC0
#define GL_BLEND                    0x0BE2
#define GL_SCISSOR_TEST             0x0C11
#define GL_TEXTURE_2D               0x0DE1

/* matrices */
#define GL_MODELVIEW                0x1700
#define GL_PROJECTION               0x1701
#define GL_TEXTURE                  0x1702
#define GL_MODELVIEW_MATRIX         0x0BA6
#define GL_PROJECTION_MATRIX        0x0BA7

/* shading, polygon modes */
#define GL_FLAT                     0x1D00
#define GL_SMOOTH                   0x1D01
#define GL_POINT                    0x1B00
#define GL_LINE                     0x1B01
#define GL_FILL                     0x1B02

/* data types and pixel formats */
#define GL_BYTE                     0x1400
#define GL_UNSIGNED_BYTE            0x1401
#define GL_SHORT                    0x1402
#define GL_UNSIGNED_SHORT           0x1403
#define GL_INT                      0x1404
#define GL_UNSIGNED_INT             0x1405
#define GL_FLOAT                    0x1406
#define GL_COLOR_INDEX              0x1900
#define GL_RED                      0x1903
#define GL_GREEN                    0x1904
#define GL_BLUE                     0x1905
#define GL_ALPHA                    0x1906
#define GL_RGB                      0x1907
#define GL_RGBA                     0x1908
#define GL_LUMINANCE                0x1909
#define GL_LUMINANCE_ALPHA          0x190A
#define GL_R3_G3_B2                 0x2A10
#define GL_ALPHA8                   0x803C
#define GL_LUMINANCE8               0x8040
#define GL_INTENSITY8               0x804B
#define GL_RGB4                     0x804F
#define GL_RGB5                     0x8050
#define GL_RGB8                     0x8051
#define GL_RGBA2                    0x8055
#define GL_RGBA4                    0x8056
#define GL_RGB5_A1                  0x8057
#define GL_RGBA8                    0x8058
#define GL_RGB2_EXT                 0x804E

/* texture parameters and environment */
#define GL_TEXTURE_ENV              0x2300
#define GL_TEXTURE_ENV_MODE         0x2200
#define GL_MODULATE                 0x2100
#define GL_DECAL                    0x2101
#define GL_REPLACE                  0x1E01
#define GL_TEXTURE_MAG_FILTER       0x2800
#define GL_TEXTURE_MIN_FILTER       0x2801
#define GL_TEXTURE_WRAP_S           0x2802
#define GL_TEXTURE_WRAP_T           0x2803
#define GL_NEAREST                  0x2600
#define GL_LINEAR                   0x2601
#define GL_NEAREST_MIPMAP_NEAREST   0x2700
#define GL_LINEAR_MIPMAP_NEAREST    0x2701
#define GL_NEAREST_MIPMAP_LINEAR    0x2702
#define GL_LINEAR_MIPMAP_LINEAR     0x2703
#define GL_CLAMP                    0x2900
#define GL_REPEAT                   0x2901

/* vertex arrays */
#define GL_VERTEX_ARRAY             0x8074
#define GL_COLOR_ARRAY              0x8076
#define GL_TEXTURE_COORD_ARRAY      0x8078

/* clear bits */
#define GL_DEPTH_BUFFER_BIT         0x00000100
#define GL_STENCIL_BUFFER_BIT       0x00000400
#define GL_COLOR_BUFFER_BIT         0x00004000

/* strings */
#define GL_VENDOR                   0x1F00
#define GL_RENDERER                 0x1F01
#define GL_VERSION                  0x1F02
#define GL_EXTENSIONS               0x1F03

/* extensions ref_gl names (never bound here; qgl.h defines the rest) */
#define GL_TEXTURE_COLOR_TABLE_SGI  0x80BC

#endif
