/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * GL/gl.h for ref_gl in ZZQuake2TGL.
 *
 * ref_gl includes <GL/gl.h> for the TYPES and the ENUMS of OpenGL 1.1
 * only: every call goes through the qgl* pointers (qgl.h), bound to
 * TinyGL in zzq2tgl_qgl.c. This header takes the types and the enums
 * from TinyGL (same numeric values as OpenGL) and adds the few that
 * TinyGL does not declare, with their OpenGL values.
 *
 * ASCII only.
 */
#ifndef ZZQ2TGL_GL_SHIM_H
#define ZZQ2TGL_GL_SHIM_H

#include <TGL/gl.h>

#ifndef APIENTRY
#define APIENTRY
#endif

typedef unsigned int GLbitfield_zz;
#define GLbitfield GLbitfield_zz
typedef float  GLclampf;
typedef double GLclampd;
#endif
