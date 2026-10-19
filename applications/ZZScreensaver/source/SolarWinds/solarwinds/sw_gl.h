/* Minimal fixed-function API used by the scene; no general GL emulation. 0BSD. */
#ifndef SW_GL_H
#define SW_GL_H
#ifdef SW_MESA_PREVIEW
/* Host-only declarations: the installed libGL exports this stable GL 1.x ABI.
 * This is not used by the bare-metal build. */
void glBegin(unsigned); void glEnd(void);
void glBindTexture(unsigned,unsigned); void glBlendFunc(unsigned,unsigned);
void glClear(unsigned); void glClearColor(float,float,float,float);
void glColor3f(float,float,float); void glColor4f(float,float,float,float);
void glDisable(unsigned); void glEnable(unsigned);
void glFrustum(double,double,double,double,double,double);
void glLoadIdentity(void); void glMatrixMode(unsigned);
void glOrtho(double,double,double,double,double,double);
void glTexCoord2f(float,float); void glTexEnvi(unsigned,unsigned,int);
void glTexImage2D(unsigned,int,int,int,int,int,unsigned,unsigned,const void*);
void glTexParameteri(unsigned,unsigned,int);
void glTranslatef(float,float,float); void glVertex3f(float,float,float);
void glViewport(int,int,int,int);
#define swBegin glBegin
#define swEnd glEnd
#define swBindTexture glBindTexture
#define swBlendFunc glBlendFunc
#define swClear glClear
#define swClearColor glClearColor
#define swColor3f glColor3f
#define swColor4f glColor4f
#define swDisable glDisable
#define swEnable glEnable
#define swFrustum glFrustum
#define swLoadIdentity glLoadIdentity
#define swMatrixMode glMatrixMode
#define swOrtho glOrtho
#define swTexCoord2f glTexCoord2f
#define swTexEnv glTexEnvi
#define swTexImage2D glTexImage2D
#define swTexParameter glTexParameteri
#define swTranslatef glTranslatef
#define swVertex3f glVertex3f
#define swViewport glViewport
#else
#include "zzgl.h"
#define swBegin zzglBegin
#define swEnd zzglEnd
#define swBindTexture zzglBindTexture
#define swBlendFunc zzglBlendFunc
#define swClear zzglClear
#define swClearColor zzglClearColor
#define swColor3f zzglColor3f
#define swColor4f zzglColor4f
#define swDisable zzglDisable
#define swEnable zzglEnable
#define swFrustum zzglFrustum
#define swLoadIdentity zzglLoadIdentity
#define swMatrixMode zzglMatrixMode
#define swOrtho zzglOrtho
#define swTexCoord2f zzglTexCoord2f
#define swTexEnv zzglTexEnvf
#define swTexImage2D zzglTexImage2D
#define swTexParameter zzglTexParameterf
#define swTranslatef zzglTranslatef
#define swVertex3f zzglVertex3f
#define swViewport zzglViewport
#endif
#define SW_COLOR_BUFFER_BIT 0x00004000
#define SW_QUADS 7u
#define SW_PROJECTION 0x1701u
#define SW_MODELVIEW 0x1700u
#define SW_TEXTURE_2D 0x0DE1u
#define SW_BLEND 0x0BE2u
#define SW_DEPTH_TEST 0x0B71u
#define SW_ALPHA_TEST 0x0BC0u
#define SW_CULL_FACE 0x0B44u
#define SW_ONE 1u
#define SW_SRC_ALPHA 0x0302u
#define SW_ONE_MINUS_SRC_ALPHA 0x0303u
#define SW_RGB 0x1907u
#define SW_UNSIGNED_BYTE 0x1401u
#define SW_LINEAR 0x2601u
#define SW_TEXTURE_MIN_FILTER 0x2801u
#define SW_TEXTURE_MAG_FILTER 0x2800u
#define SW_TEXTURE_ENV 0x2300u
#define SW_TEXTURE_ENV_MODE 0x2200u
#define SW_MODULATE 0x2100u
#endif
