#include "abi.h"
#include "os.h"
#include "blackbox.h"
extern void zz_missing(unsigned int);
extern GLcontext zz_current;
/* A8 call trace: one counter per slot, the slot names for the report */
Z zz_calls[205];
Z zz_last_slot;   /* A8.3: the slot entered last (heartbeat) */
/* A8.4: every call is also written to the black box of an armed context (entered, returned) */
volatile unsigned short *zz_bb;
unsigned int zz_bb_seq;
static inline void body_GLActiveTextureARB(GLcontext context, GLenum unit) { Z start_tick,io;  if(!zz_profile){impl_GLActiveTextureARB(context, unit);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLActiveTextureARB(context, unit); zz_profile_add(start_tick,io,1); }
static void timed_GLActiveTextureARB(GLcontext context, GLenum unit) { Z bb_s; zz_calls[0]++; zz_last_slot=0; ZZ_BB_IN(0u,bb_s); body_GLActiveTextureARB(context, unit); ZZ_BB_OUT(0u,bb_s); }
static inline void body_GLAlphaFunc(GLcontext context, GLenum func, GLclampf ref) { Z start_tick,io;  if(!zz_profile){impl_GLAlphaFunc(context, func, ref);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLAlphaFunc(context, func, ref); zz_profile_add(start_tick,io,1); }
static void timed_GLAlphaFunc(GLcontext context, GLenum func, GLclampf ref) { Z bb_s; zz_calls[1]++; zz_last_slot=1; ZZ_BB_IN(1u,bb_s); body_GLAlphaFunc(context, func, ref); ZZ_BB_OUT(1u,bb_s); }
static inline void body_GLArrayElement(GLcontext context, GLint i) { zz_missing(3); }
static void missing_GLArrayElement(GLcontext context, GLint i) { Z bb_s; zz_calls[2]++; zz_last_slot=2; ZZ_BB_IN(2u,bb_s); body_GLArrayElement(context, i); ZZ_BB_OUT(2u,bb_s); }
static inline void body_GLBegin(GLcontext context, GLenum mode) { Z start_tick,io;  if(!zz_profile){impl_GLBegin(context, mode);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLBegin(context, mode); zz_profile_add(start_tick,io,1); }
static void timed_GLBegin(GLcontext context, GLenum mode) { Z bb_s; zz_calls[3]++; zz_last_slot=3; ZZ_BB_IN(3u,bb_s); body_GLBegin(context, mode); ZZ_BB_OUT(3u,bb_s); }
static inline void body_GLBindTexture(GLcontext context, GLenum target, GLuint texture) { Z start_tick,io;  if(!zz_profile){impl_GLBindTexture(context, target, texture);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLBindTexture(context, target, texture); zz_profile_add(start_tick,io,3); }
static void timed_GLBindTexture(GLcontext context, GLenum target, GLuint texture) { Z bb_s; zz_calls[4]++; zz_last_slot=4; ZZ_BB_IN(4u,bb_s); body_GLBindTexture(context, target, texture); ZZ_BB_OUT(4u,bb_s); }
static inline void body_GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor) { Z start_tick,io;  if(!zz_profile){impl_GLBlendFunc(context, sfactor, dfactor);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLBlendFunc(context, sfactor, dfactor); zz_profile_add(start_tick,io,1); }
static void timed_GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor) { Z bb_s; zz_calls[5]++; zz_last_slot=5; ZZ_BB_IN(5u,bb_s); body_GLBlendFunc(context, sfactor, dfactor); ZZ_BB_OUT(5u,bb_s); }
static inline void body_GLClear(GLcontext context, GLbitfield mask) { Z start_tick,io;  if(!zz_profile){impl_GLClear(context, mask);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLClear(context, mask); zz_profile_add(start_tick,io,1); }
static void timed_GLClear(GLcontext context, GLbitfield mask) { Z bb_s; zz_calls[6]++; zz_last_slot=6; ZZ_BB_IN(6u,bb_s); body_GLClear(context, mask); ZZ_BB_OUT(6u,bb_s); }
static inline void body_GLClearColor(GLcontext context, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) { Z start_tick,io;  if(!zz_profile){impl_GLClearColor(context, red, green, blue, alpha);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLClearColor(context, red, green, blue, alpha); zz_profile_add(start_tick,io,1); }
static void timed_GLClearColor(GLcontext context, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) { Z bb_s; zz_calls[7]++; zz_last_slot=7; ZZ_BB_IN(7u,bb_s); body_GLClearColor(context, red, green, blue, alpha); ZZ_BB_OUT(7u,bb_s); }
static inline void body_GLClearDepth(GLcontext context, GLclampd depth) { Z start_tick,io;  if(!zz_profile){impl_GLClearDepth(context, depth);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLClearDepth(context, depth); zz_profile_add(start_tick,io,1); }
static void timed_GLClearDepth(GLcontext context, GLclampd depth) { Z bb_s; zz_calls[8]++; zz_last_slot=8; ZZ_BB_IN(8u,bb_s); body_GLClearDepth(context, depth); ZZ_BB_OUT(8u,bb_s); }
static inline void body_GLColor3fv(GLcontext context, GLfloat *v) { Z start_tick,io;  if(!zz_profile){impl_GLColor3fv(context, v);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLColor3fv(context, v); zz_profile_add(start_tick,io,1); }
static void timed_GLColor3fv(GLcontext context, GLfloat *v) { Z bb_s; zz_calls[9]++; zz_last_slot=9; ZZ_BB_IN(9u,bb_s); body_GLColor3fv(context, v); ZZ_BB_OUT(9u,bb_s); }
static inline void body_GLColor3ubv(GLcontext context, GLubyte *v) { Z start_tick,io;  if(!zz_profile){impl_GLColor3ubv(context, v);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLColor3ubv(context, v); zz_profile_add(start_tick,io,1); }
static void timed_GLColor3ubv(GLcontext context, GLubyte *v) { Z bb_s; zz_calls[10]++; zz_last_slot=10; ZZ_BB_IN(10u,bb_s); body_GLColor3ubv(context, v); ZZ_BB_OUT(10u,bb_s); }
static inline void body_GLColor4f(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) { Z start_tick,io;  if(!zz_profile){impl_GLColor4f(context, red, green, blue, alpha);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLColor4f(context, red, green, blue, alpha); zz_profile_add(start_tick,io,1); }
static void timed_GLColor4f(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) { Z bb_s; zz_calls[11]++; zz_last_slot=11; ZZ_BB_IN(11u,bb_s); body_GLColor4f(context, red, green, blue, alpha); ZZ_BB_OUT(11u,bb_s); }
static inline void body_GLColor4fv(GLcontext context, GLfloat *v) { Z start_tick,io;  if(!zz_profile){impl_GLColor4fv(context, v);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLColor4fv(context, v); zz_profile_add(start_tick,io,1); }
static void timed_GLColor4fv(GLcontext context, GLfloat *v) { Z bb_s; zz_calls[12]++; zz_last_slot=12; ZZ_BB_IN(12u,bb_s); body_GLColor4fv(context, v); ZZ_BB_OUT(12u,bb_s); }
static inline void body_GLColor4ub(GLcontext context, GLubyte red, GLubyte green, GLubyte blue, GLubyte alhpa) { Z start_tick,io;  if(!zz_profile){impl_GLColor4ub(context, red, green, blue, alhpa);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLColor4ub(context, red, green, blue, alhpa); zz_profile_add(start_tick,io,1); }
static void timed_GLColor4ub(GLcontext context, GLubyte red, GLubyte green, GLubyte blue, GLubyte alhpa) { Z bb_s; zz_calls[13]++; zz_last_slot=13; ZZ_BB_IN(13u,bb_s); body_GLColor4ub(context, red, green, blue, alhpa); ZZ_BB_OUT(13u,bb_s); }
static inline void body_GLColor4ubv(GLcontext context, GLubyte *v) { Z start_tick,io;  if(!zz_profile){impl_GLColor4ubv(context, v);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLColor4ubv(context, v); zz_profile_add(start_tick,io,1); }
static void timed_GLColor4ubv(GLcontext context, GLubyte *v) { Z bb_s; zz_calls[14]++; zz_last_slot=14; ZZ_BB_IN(14u,bb_s); body_GLColor4ubv(context, v); ZZ_BB_OUT(14u,bb_s); }
static inline void body_GLColorMask(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) { Z start_tick,io;  if(!zz_profile){impl_GLColorMask(context, red, green, blue, alpha);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLColorMask(context, red, green, blue, alpha); zz_profile_add(start_tick,io,1); }
static void timed_GLColorMask(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) { Z bb_s; zz_calls[15]++; zz_last_slot=15; ZZ_BB_IN(15u,bb_s); body_GLColorMask(context, red, green, blue, alpha); ZZ_BB_OUT(15u,bb_s); }
static inline void body_GLColorPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { Z start_tick,io;  if(!zz_profile){impl_GLColorPointer(context, size, type, stride, pointer);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLColorPointer(context, size, type, stride, pointer); zz_profile_add(start_tick,io,1); }
static void timed_GLColorPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { Z bb_s; zz_calls[16]++; zz_last_slot=16; ZZ_BB_IN(16u,bb_s); body_GLColorPointer(context, size, type, stride, pointer); ZZ_BB_OUT(16u,bb_s); }
static inline void body_GLColorTable(GLcontext context, GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data) { zz_missing(18); }
static void missing_GLColorTable(GLcontext context, GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data) { Z bb_s; zz_calls[17]++; zz_last_slot=17; ZZ_BB_IN(17u,bb_s); body_GLColorTable(context, target, internalformat, width, format, type, data); ZZ_BB_OUT(17u,bb_s); }
static inline void body_GLCullFace(GLcontext context, GLenum mode) { Z start_tick,io;  if(!zz_profile){impl_GLCullFace(context, mode);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLCullFace(context, mode); zz_profile_add(start_tick,io,1); }
static void timed_GLCullFace(GLcontext context, GLenum mode) { Z bb_s; zz_calls[18]++; zz_last_slot=18; ZZ_BB_IN(18u,bb_s); body_GLCullFace(context, mode); ZZ_BB_OUT(18u,bb_s); }
static inline void body_GLDeleteTextures(GLcontext context, GLsizei n, const GLuint *textures) { Z start_tick,io;  if(!zz_profile){impl_GLDeleteTextures(context, n, textures);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLDeleteTextures(context, n, textures); zz_profile_add(start_tick,io,1); }
static void timed_GLDeleteTextures(GLcontext context, GLsizei n, const GLuint *textures) { Z bb_s; zz_calls[19]++; zz_last_slot=19; ZZ_BB_IN(19u,bb_s); body_GLDeleteTextures(context, n, textures); ZZ_BB_OUT(19u,bb_s); }
static inline void body_GLDepthFunc(GLcontext context, GLenum func) { Z start_tick,io;  if(!zz_profile){impl_GLDepthFunc(context, func);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLDepthFunc(context, func); zz_profile_add(start_tick,io,1); }
static void timed_GLDepthFunc(GLcontext context, GLenum func) { Z bb_s; zz_calls[20]++; zz_last_slot=20; ZZ_BB_IN(20u,bb_s); body_GLDepthFunc(context, func); ZZ_BB_OUT(20u,bb_s); }
static inline void body_GLDepthMask(GLcontext context, GLboolean flag) { Z start_tick,io;  if(!zz_profile){impl_GLDepthMask(context, flag);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLDepthMask(context, flag); zz_profile_add(start_tick,io,1); }
static void timed_GLDepthMask(GLcontext context, GLboolean flag) { Z bb_s; zz_calls[21]++; zz_last_slot=21; ZZ_BB_IN(21u,bb_s); body_GLDepthMask(context, flag); ZZ_BB_OUT(21u,bb_s); }
static inline void body_GLDepthRange(GLcontext context, GLclampd n, GLclampd f) { Z start_tick,io;  if(!zz_profile){impl_GLDepthRange(context, n, f);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLDepthRange(context, n, f); zz_profile_add(start_tick,io,1); }
static void timed_GLDepthRange(GLcontext context, GLclampd n, GLclampd f) { Z bb_s; zz_calls[22]++; zz_last_slot=22; ZZ_BB_IN(22u,bb_s); body_GLDepthRange(context, n, f); ZZ_BB_OUT(22u,bb_s); }
static inline void body_GLDisableClientState(GLcontext context, GLenum cap) { Z start_tick,io;  if(!zz_profile){impl_GLDisableClientState(context, cap);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLDisableClientState(context, cap); zz_profile_add(start_tick,io,1); }
static void timed_GLDisableClientState(GLcontext context, GLenum cap) { Z bb_s; zz_calls[23]++; zz_last_slot=23; ZZ_BB_IN(23u,bb_s); body_GLDisableClientState(context, cap); ZZ_BB_OUT(23u,bb_s); }
static inline void body_GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count) { Z start_tick,io;  if(!zz_profile){impl_GLDrawArrays(context, mode, first, count);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLDrawArrays(context, mode, first, count); zz_profile_add(start_tick,io,1); }
static void timed_GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count) { Z bb_s; zz_calls[24]++; zz_last_slot=24; ZZ_BB_IN(24u,bb_s); body_GLDrawArrays(context, mode, first, count); ZZ_BB_OUT(24u,bb_s); }
static inline void body_GLDrawBuffer(GLcontext context, GLenum mode) { Z start_tick,io;  if(!zz_profile){impl_GLDrawBuffer(context, mode);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLDrawBuffer(context, mode); zz_profile_add(start_tick,io,1); }
static void timed_GLDrawBuffer(GLcontext context, GLenum mode) { Z bb_s; zz_calls[25]++; zz_last_slot=25; ZZ_BB_IN(25u,bb_s); body_GLDrawBuffer(context, mode); ZZ_BB_OUT(25u,bb_s); }
static inline void body_GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) { Z start_tick,io;  if(!zz_profile){impl_GLDrawElements(context, mode, count, type, indices);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLDrawElements(context, mode, count, type, indices); zz_profile_add(start_tick,io,1); }
static void timed_GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) { Z bb_s; zz_calls[26]++; zz_last_slot=26; ZZ_BB_IN(26u,bb_s); body_GLDrawElements(context, mode, count, type, indices); ZZ_BB_OUT(26u,bb_s); }
static inline void body_GLEnableClientState(GLcontext context, GLenum cap) { Z start_tick,io;  if(!zz_profile){impl_GLEnableClientState(context, cap);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLEnableClientState(context, cap); zz_profile_add(start_tick,io,1); }
static void timed_GLEnableClientState(GLcontext context, GLenum cap) { Z bb_s; zz_calls[27]++; zz_last_slot=27; ZZ_BB_IN(27u,bb_s); body_GLEnableClientState(context, cap); ZZ_BB_OUT(27u,bb_s); }
static inline void body_GLEnd(GLcontext context) { Z start_tick,io;  if(!zz_profile){impl_GLEnd(context);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLEnd(context); zz_profile_add(start_tick,io,1); }
static void timed_GLEnd(GLcontext context) { Z bb_s; zz_calls[28]++; zz_last_slot=28; ZZ_BB_IN(28u,bb_s); body_GLEnd(context); ZZ_BB_OUT(28u,bb_s); }
static inline void body_GLFinish(GLcontext context) { Z start_tick,io;  if(!zz_profile){impl_GLFinish(context);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLFinish(context); zz_profile_add(start_tick,io,0); }
static void timed_GLFinish(GLcontext context) { Z bb_s; zz_calls[29]++; zz_last_slot=29; ZZ_BB_IN(29u,bb_s); body_GLFinish(context); ZZ_BB_OUT(29u,bb_s); }
static inline void body_GLFlush(GLcontext context) { Z start_tick,io;  if(!zz_profile){impl_GLFlush(context);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLFlush(context); zz_profile_add(start_tick,io,0); }
static void timed_GLFlush(GLcontext context) { Z bb_s; zz_calls[30]++; zz_last_slot=30; ZZ_BB_IN(30u,bb_s); body_GLFlush(context); ZZ_BB_OUT(30u,bb_s); }
static inline void body_GLFogf(GLcontext context, GLenum pname, GLfloat param) { Z start_tick,io;  if(!zz_profile){impl_GLFogf(context, pname, param);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLFogf(context, pname, param); zz_profile_add(start_tick,io,1); }
static void timed_GLFogf(GLcontext context, GLenum pname, GLfloat param) { Z bb_s; zz_calls[31]++; zz_last_slot=31; ZZ_BB_IN(31u,bb_s); body_GLFogf(context, pname, param); ZZ_BB_OUT(31u,bb_s); }
static inline void body_GLFogfv(GLcontext context, GLenum pname, GLfloat *param) { Z start_tick,io;  if(!zz_profile){impl_GLFogfv(context, pname, param);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLFogfv(context, pname, param); zz_profile_add(start_tick,io,1); }
static void timed_GLFogfv(GLcontext context, GLenum pname, GLfloat *param) { Z bb_s; zz_calls[32]++; zz_last_slot=32; ZZ_BB_IN(32u,bb_s); body_GLFogfv(context, pname, param); ZZ_BB_OUT(32u,bb_s); }
static inline void body_GLFrontFace(GLcontext context, GLenum mode) { Z start_tick,io;  if(!zz_profile){impl_GLFrontFace(context, mode);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLFrontFace(context, mode); zz_profile_add(start_tick,io,1); }
static void timed_GLFrontFace(GLcontext context, GLenum mode) { Z bb_s; zz_calls[33]++; zz_last_slot=33; ZZ_BB_IN(33u,bb_s); body_GLFrontFace(context, mode); ZZ_BB_OUT(33u,bb_s); }
static inline void body_GLFrustum(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) { Z start_tick,io;  if(!zz_profile){impl_GLFrustum(context, left, right, bottom, top, zNear, zFar);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLFrustum(context, left, right, bottom, top, zNear, zFar); zz_profile_add(start_tick,io,1); }
static void timed_GLFrustum(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) { Z bb_s; zz_calls[34]++; zz_last_slot=34; ZZ_BB_IN(34u,bb_s); body_GLFrustum(context, left, right, bottom, top, zNear, zFar); ZZ_BB_OUT(34u,bb_s); }
static inline void body_GLGenTextures(GLcontext context, GLsizei n, GLuint *textures) { Z start_tick,io;  if(!zz_profile){impl_GLGenTextures(context, n, textures);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLGenTextures(context, n, textures); zz_profile_add(start_tick,io,1); }
static void timed_GLGenTextures(GLcontext context, GLsizei n, GLuint *textures) { Z bb_s; zz_calls[35]++; zz_last_slot=35; ZZ_BB_IN(35u,bb_s); body_GLGenTextures(context, n, textures); ZZ_BB_OUT(35u,bb_s); }
static inline void body_GLGetBooleanv(GLcontext context, GLenum pname, GLboolean *params) { zz_missing(37); }
static void missing_GLGetBooleanv(GLcontext context, GLenum pname, GLboolean *params) { Z bb_s; zz_calls[36]++; zz_last_slot=36; ZZ_BB_IN(36u,bb_s); body_GLGetBooleanv(context, pname, params); ZZ_BB_OUT(36u,bb_s); }
static inline GLenum body_GLGetError(GLcontext context) { Z start_tick,io; GLenum result; if(!zz_profile){return impl_GLGetError(context);} start_tick=zz_clock();io=zz_transfer_ticks();result=impl_GLGetError(context); zz_profile_add(start_tick,io,0);return result; }
static GLenum timed_GLGetError(GLcontext context) { Z bb_s; GLenum bb_r; zz_calls[37]++; zz_last_slot=37; ZZ_BB_IN(37u,bb_s); bb_r=body_GLGetError(context); ZZ_BB_OUT(37u,bb_s); return bb_r; }
static inline void body_GLGetFloatv(GLcontext context, GLenum pname, GLfloat *params) { Z start_tick,io;  if(!zz_profile){impl_GLGetFloatv(context, pname, params);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLGetFloatv(context, pname, params); zz_profile_add(start_tick,io,1); }
static void timed_GLGetFloatv(GLcontext context, GLenum pname, GLfloat *params) { Z bb_s; zz_calls[38]++; zz_last_slot=38; ZZ_BB_IN(38u,bb_s); body_GLGetFloatv(context, pname, params); ZZ_BB_OUT(38u,bb_s); }
static inline void body_GLGetIntegerv(GLcontext context, GLenum pname, GLint *params) { Z start_tick,io;  if(!zz_profile){impl_GLGetIntegerv(context, pname, params);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLGetIntegerv(context, pname, params); zz_profile_add(start_tick,io,1); }
static void timed_GLGetIntegerv(GLcontext context, GLenum pname, GLint *params) { Z bb_s; zz_calls[39]++; zz_last_slot=39; ZZ_BB_IN(39u,bb_s); body_GLGetIntegerv(context, pname, params); ZZ_BB_OUT(39u,bb_s); }
static inline const GLubyte* body_GLGetString(GLcontext context, GLenum name) { Z start_tick,io; const GLubyte* result; if(!zz_profile){return impl_GLGetString(context, name);} start_tick=zz_clock();io=zz_transfer_ticks();result=impl_GLGetString(context, name); zz_profile_add(start_tick,io,0);return result; }
static const GLubyte* timed_GLGetString(GLcontext context, GLenum name) { Z bb_s; const GLubyte* bb_r; zz_calls[40]++; zz_last_slot=40; ZZ_BB_IN(40u,bb_s); bb_r=body_GLGetString(context, name); ZZ_BB_OUT(40u,bb_s); return bb_r; }
static inline void body_GLHint(GLcontext context, GLenum target, GLenum mode) { Z start_tick,io;  if(!zz_profile){impl_GLHint(context, target, mode);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLHint(context, target, mode); zz_profile_add(start_tick,io,1); }
static void timed_GLHint(GLcontext context, GLenum target, GLenum mode) { Z bb_s; zz_calls[41]++; zz_last_slot=41; ZZ_BB_IN(41u,bb_s); body_GLHint(context, target, mode); ZZ_BB_OUT(41u,bb_s); }
static inline GLboolean body_GLIsEnabled(GLcontext context, GLenum cap) { zz_missing(43); return 0; }
static GLboolean missing_GLIsEnabled(GLcontext context, GLenum cap) { Z bb_s; GLboolean bb_r; zz_calls[42]++; zz_last_slot=42; ZZ_BB_IN(42u,bb_s); bb_r=body_GLIsEnabled(context, cap); ZZ_BB_OUT(42u,bb_s); return bb_r; }
static inline void body_GLLoadIdentity(GLcontext context) { Z start_tick,io;  if(!zz_profile){impl_GLLoadIdentity(context);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLLoadIdentity(context); zz_profile_add(start_tick,io,1); }
static void timed_GLLoadIdentity(GLcontext context) { Z bb_s; zz_calls[43]++; zz_last_slot=43; ZZ_BB_IN(43u,bb_s); body_GLLoadIdentity(context); ZZ_BB_OUT(43u,bb_s); }
static inline void body_GLLoadMatrixd(GLcontext context, const GLdouble *m) { Z start_tick,io;  if(!zz_profile){impl_GLLoadMatrixd(context, m);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLLoadMatrixd(context, m); zz_profile_add(start_tick,io,1); }
static void timed_GLLoadMatrixd(GLcontext context, const GLdouble *m) { Z bb_s; zz_calls[44]++; zz_last_slot=44; ZZ_BB_IN(44u,bb_s); body_GLLoadMatrixd(context, m); ZZ_BB_OUT(44u,bb_s); }
static inline void body_GLLoadMatrixf(GLcontext context, const GLfloat *m) { Z start_tick,io;  if(!zz_profile){impl_GLLoadMatrixf(context, m);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLLoadMatrixf(context, m); zz_profile_add(start_tick,io,1); }
static void timed_GLLoadMatrixf(GLcontext context, const GLfloat *m) { Z bb_s; zz_calls[45]++; zz_last_slot=45; ZZ_BB_IN(45u,bb_s); body_GLLoadMatrixf(context, m); ZZ_BB_OUT(45u,bb_s); }
static inline void body_GLLockArrays(GLcontext context, GLuint first, GLsizei count) { Z start_tick,io;  if(!zz_profile){impl_GLLockArrays(context, first, count);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLLockArrays(context, first, count); zz_profile_add(start_tick,io,1); }
static void timed_GLLockArrays(GLcontext context, GLuint first, GLsizei count) { Z bb_s; zz_calls[46]++; zz_last_slot=46; ZZ_BB_IN(46u,bb_s); body_GLLockArrays(context, first, count); ZZ_BB_OUT(46u,bb_s); }
static inline void body_GLMatrixMode(GLcontext context, GLenum mode) { Z start_tick,io;  if(!zz_profile){impl_GLMatrixMode(context, mode);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLMatrixMode(context, mode); zz_profile_add(start_tick,io,1); }
static void timed_GLMatrixMode(GLcontext context, GLenum mode) { Z bb_s; zz_calls[47]++; zz_last_slot=47; ZZ_BB_IN(47u,bb_s); body_GLMatrixMode(context, mode); ZZ_BB_OUT(47u,bb_s); }
static inline void body_GLMultMatrixd(GLcontext context, const GLdouble *m) { Z start_tick,io;  if(!zz_profile){impl_GLMultMatrixd(context, m);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLMultMatrixd(context, m); zz_profile_add(start_tick,io,1); }
static void timed_GLMultMatrixd(GLcontext context, const GLdouble *m) { Z bb_s; zz_calls[48]++; zz_last_slot=48; ZZ_BB_IN(48u,bb_s); body_GLMultMatrixd(context, m); ZZ_BB_OUT(48u,bb_s); }
static inline void body_GLMultMatrixf(GLcontext context, const GLfloat *m) { Z start_tick,io;  if(!zz_profile){impl_GLMultMatrixf(context, m);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLMultMatrixf(context, m); zz_profile_add(start_tick,io,1); }
static void timed_GLMultMatrixf(GLcontext context, const GLfloat *m) { Z bb_s; zz_calls[49]++; zz_last_slot=49; ZZ_BB_IN(49u,bb_s); body_GLMultMatrixf(context, m); ZZ_BB_OUT(49u,bb_s); }
static inline void body_GLMultiTexCoord2fARB(GLcontext context, GLenum unit, GLfloat s, GLfloat t) { Z start_tick,io;  if(!zz_profile){impl_GLMultiTexCoord2fARB(context, unit, s, t);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLMultiTexCoord2fARB(context, unit, s, t); zz_profile_add(start_tick,io,1); }
static void timed_GLMultiTexCoord2fARB(GLcontext context, GLenum unit, GLfloat s, GLfloat t) { Z bb_s; zz_calls[50]++; zz_last_slot=50; ZZ_BB_IN(50u,bb_s); body_GLMultiTexCoord2fARB(context, unit, s, t); ZZ_BB_OUT(50u,bb_s); }
static inline void body_GLMultiTexCoord2fvARB(GLcontext context, GLenum unit, GLfloat *v) { Z start_tick,io;  if(!zz_profile){impl_GLMultiTexCoord2fvARB(context, unit, v);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLMultiTexCoord2fvARB(context, unit, v); zz_profile_add(start_tick,io,1); }
static void timed_GLMultiTexCoord2fvARB(GLcontext context, GLenum unit, GLfloat *v) { Z bb_s; zz_calls[51]++; zz_last_slot=51; ZZ_BB_IN(51u,bb_s); body_GLMultiTexCoord2fvARB(context, unit, v); ZZ_BB_OUT(51u,bb_s); }
static inline void body_GLNormal3f(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { Z start_tick,io;  if(!zz_profile){impl_GLNormal3f(context, x, y, z);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLNormal3f(context, x, y, z); zz_profile_add(start_tick,io,1); }
static void timed_GLNormal3f(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { Z bb_s; zz_calls[52]++; zz_last_slot=52; ZZ_BB_IN(52u,bb_s); body_GLNormal3f(context, x, y, z); ZZ_BB_OUT(52u,bb_s); }
static inline void body_GLOrtho(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) { Z start_tick,io;  if(!zz_profile){impl_GLOrtho(context, left, right, bottom, top, zNear, zFar);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLOrtho(context, left, right, bottom, top, zNear, zFar); zz_profile_add(start_tick,io,1); }
static void timed_GLOrtho(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) { Z bb_s; zz_calls[53]++; zz_last_slot=53; ZZ_BB_IN(53u,bb_s); body_GLOrtho(context, left, right, bottom, top, zNear, zFar); ZZ_BB_OUT(53u,bb_s); }
static inline void body_GLPixelStorei(GLcontext context, GLenum pname, GLint param) { Z start_tick,io;  if(!zz_profile){impl_GLPixelStorei(context, pname, param);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLPixelStorei(context, pname, param); zz_profile_add(start_tick,io,1); }
static void timed_GLPixelStorei(GLcontext context, GLenum pname, GLint param) { Z bb_s; zz_calls[54]++; zz_last_slot=54; ZZ_BB_IN(54u,bb_s); body_GLPixelStorei(context, pname, param); ZZ_BB_OUT(54u,bb_s); }
static inline void body_GLPointSize(GLcontext context, GLfloat size) { Z start_tick,io;  if(!zz_profile){impl_GLPointSize(context, size);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLPointSize(context, size); zz_profile_add(start_tick,io,1); }
static void timed_GLPointSize(GLcontext context, GLfloat size) { Z bb_s; zz_calls[55]++; zz_last_slot=55; ZZ_BB_IN(55u,bb_s); body_GLPointSize(context, size); ZZ_BB_OUT(55u,bb_s); }
static inline void body_GLPolygonMode(GLcontext context, GLenum face, GLenum mode) { Z start_tick,io;  if(!zz_profile){impl_GLPolygonMode(context, face, mode);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLPolygonMode(context, face, mode); zz_profile_add(start_tick,io,1); }
static void timed_GLPolygonMode(GLcontext context, GLenum face, GLenum mode) { Z bb_s; zz_calls[56]++; zz_last_slot=56; ZZ_BB_IN(56u,bb_s); body_GLPolygonMode(context, face, mode); ZZ_BB_OUT(56u,bb_s); }
static inline void body_GLPopMatrix(GLcontext context) { Z start_tick,io;  if(!zz_profile){impl_GLPopMatrix(context);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLPopMatrix(context); zz_profile_add(start_tick,io,1); }
static void timed_GLPopMatrix(GLcontext context) { Z bb_s; zz_calls[57]++; zz_last_slot=57; ZZ_BB_IN(57u,bb_s); body_GLPopMatrix(context); ZZ_BB_OUT(57u,bb_s); }
static inline void body_GLPushMatrix(GLcontext context) { Z start_tick,io;  if(!zz_profile){impl_GLPushMatrix(context);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLPushMatrix(context); zz_profile_add(start_tick,io,1); }
static void timed_GLPushMatrix(GLcontext context) { Z bb_s; zz_calls[58]++; zz_last_slot=58; ZZ_BB_IN(58u,bb_s); body_GLPushMatrix(context); ZZ_BB_OUT(58u,bb_s); }
static inline void body_GLReadPixels(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels) { zz_missing(60); }
static void missing_GLReadPixels(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels) { Z bb_s; zz_calls[59]++; zz_last_slot=59; ZZ_BB_IN(59u,bb_s); body_GLReadPixels(context, x, y, width, height, format, type, pixels); ZZ_BB_OUT(59u,bb_s); }
static inline void body_GLRotated(GLcontext context, GLdouble angle, GLdouble x, GLdouble y, GLdouble z) { Z start_tick,io;  if(!zz_profile){impl_GLRotated(context, angle, x, y, z);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLRotated(context, angle, x, y, z); zz_profile_add(start_tick,io,1); }
static void timed_GLRotated(GLcontext context, GLdouble angle, GLdouble x, GLdouble y, GLdouble z) { Z bb_s; zz_calls[60]++; zz_last_slot=60; ZZ_BB_IN(60u,bb_s); body_GLRotated(context, angle, x, y, z); ZZ_BB_OUT(60u,bb_s); }
static inline void body_GLRotatef(GLcontext context, GLfloat angle, GLfloat x, GLfloat y, GLfloat z) { Z start_tick,io;  if(!zz_profile){impl_GLRotatef(context, angle, x, y, z);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLRotatef(context, angle, x, y, z); zz_profile_add(start_tick,io,1); }
static void timed_GLRotatef(GLcontext context, GLfloat angle, GLfloat x, GLfloat y, GLfloat z) { Z bb_s; zz_calls[61]++; zz_last_slot=61; ZZ_BB_IN(61u,bb_s); body_GLRotatef(context, angle, x, y, z); ZZ_BB_OUT(61u,bb_s); }
static inline void body_GLRotatefEXT(GLcontext context, GLfloat angle, const GLint xyz) { Z start_tick,io;  if(!zz_profile){impl_GLRotatefEXT(context, angle, xyz);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLRotatefEXT(context, angle, xyz); zz_profile_add(start_tick,io,1); }
static void timed_GLRotatefEXT(GLcontext context, GLfloat angle, const GLint xyz) { Z bb_s; zz_calls[62]++; zz_last_slot=62; ZZ_BB_IN(62u,bb_s); body_GLRotatefEXT(context, angle, xyz); ZZ_BB_OUT(62u,bb_s); }
static inline void body_GLRotatefEXTs(GLcontext context, GLfloat sin_an, GLfloat cos_an, const GLint xyz) { Z start_tick,io;  if(!zz_profile){impl_GLRotatefEXTs(context, sin_an, cos_an, xyz);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLRotatefEXTs(context, sin_an, cos_an, xyz); zz_profile_add(start_tick,io,1); }
static void timed_GLRotatefEXTs(GLcontext context, GLfloat sin_an, GLfloat cos_an, const GLint xyz) { Z bb_s; zz_calls[63]++; zz_last_slot=63; ZZ_BB_IN(63u,bb_s); body_GLRotatefEXTs(context, sin_an, cos_an, xyz); ZZ_BB_OUT(63u,bb_s); }
static inline void body_GLScaled(GLcontext context, GLdouble x, GLdouble y, GLdouble z) { Z start_tick,io;  if(!zz_profile){impl_GLScaled(context, x, y, z);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLScaled(context, x, y, z); zz_profile_add(start_tick,io,1); }
static void timed_GLScaled(GLcontext context, GLdouble x, GLdouble y, GLdouble z) { Z bb_s; zz_calls[64]++; zz_last_slot=64; ZZ_BB_IN(64u,bb_s); body_GLScaled(context, x, y, z); ZZ_BB_OUT(64u,bb_s); }
static inline void body_GLScalef(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { Z start_tick,io;  if(!zz_profile){impl_GLScalef(context, x, y, z);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLScalef(context, x, y, z); zz_profile_add(start_tick,io,1); }
static void timed_GLScalef(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { Z bb_s; zz_calls[65]++; zz_last_slot=65; ZZ_BB_IN(65u,bb_s); body_GLScalef(context, x, y, z); ZZ_BB_OUT(65u,bb_s); }
static inline void body_GLScissor(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height) { Z start_tick,io;  if(!zz_profile){impl_GLScissor(context, x, y, width, height);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLScissor(context, x, y, width, height); zz_profile_add(start_tick,io,1); }
static void timed_GLScissor(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height) { Z bb_s; zz_calls[66]++; zz_last_slot=66; ZZ_BB_IN(66u,bb_s); body_GLScissor(context, x, y, width, height); ZZ_BB_OUT(66u,bb_s); }
static inline void body_GLShadeModel(GLcontext context, GLenum mode) { Z start_tick,io;  if(!zz_profile){impl_GLShadeModel(context, mode);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLShadeModel(context, mode); zz_profile_add(start_tick,io,1); }
static void timed_GLShadeModel(GLcontext context, GLenum mode) { Z bb_s; zz_calls[67]++; zz_last_slot=67; ZZ_BB_IN(67u,bb_s); body_GLShadeModel(context, mode); ZZ_BB_OUT(67u,bb_s); }
static inline void body_GLTexCoord2f(GLcontext context, GLfloat s, GLfloat t) { Z start_tick,io;  if(!zz_profile){impl_GLTexCoord2f(context, s, t);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLTexCoord2f(context, s, t); zz_profile_add(start_tick,io,1); }
static void timed_GLTexCoord2f(GLcontext context, GLfloat s, GLfloat t) { Z bb_s; zz_calls[68]++; zz_last_slot=68; ZZ_BB_IN(68u,bb_s); body_GLTexCoord2f(context, s, t); ZZ_BB_OUT(68u,bb_s); }
static inline void body_GLTexCoord2fv(GLcontext context, GLfloat *v) { Z start_tick,io;  if(!zz_profile){impl_GLTexCoord2fv(context, v);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLTexCoord2fv(context, v); zz_profile_add(start_tick,io,1); }
static void timed_GLTexCoord2fv(GLcontext context, GLfloat *v) { Z bb_s; zz_calls[69]++; zz_last_slot=69; ZZ_BB_IN(69u,bb_s); body_GLTexCoord2fv(context, v); ZZ_BB_OUT(69u,bb_s); }
static inline void body_GLTexCoord4f(GLcontext context, GLfloat s, GLfloat t, GLfloat r, GLfloat q) { zz_missing(71); }
static void missing_GLTexCoord4f(GLcontext context, GLfloat s, GLfloat t, GLfloat r, GLfloat q) { Z bb_s; zz_calls[70]++; zz_last_slot=70; ZZ_BB_IN(70u,bb_s); body_GLTexCoord4f(context, s, t, r, q); ZZ_BB_OUT(70u,bb_s); }
static inline void body_GLTexCoord4fv(GLcontext context, GLfloat *v) { zz_missing(72); }
static void missing_GLTexCoord4fv(GLcontext context, GLfloat *v) { Z bb_s; zz_calls[71]++; zz_last_slot=71; ZZ_BB_IN(71u,bb_s); body_GLTexCoord4fv(context, v); ZZ_BB_OUT(71u,bb_s); }
static inline void body_GLTexCoordPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { Z start_tick,io;  if(!zz_profile){impl_GLTexCoordPointer(context, size, type, stride, pointer);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLTexCoordPointer(context, size, type, stride, pointer); zz_profile_add(start_tick,io,1); }
static void timed_GLTexCoordPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { Z bb_s; zz_calls[72]++; zz_last_slot=72; ZZ_BB_IN(72u,bb_s); body_GLTexCoordPointer(context, size, type, stride, pointer); ZZ_BB_OUT(72u,bb_s); }
static inline void body_GLTexEnvi(GLcontext context, GLenum target, GLenum pname, GLint param) { Z start_tick,io;  if(!zz_profile){impl_GLTexEnvi(context, target, pname, param);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLTexEnvi(context, target, pname, param); zz_profile_add(start_tick,io,1); }
static void timed_GLTexEnvi(GLcontext context, GLenum target, GLenum pname, GLint param) { Z bb_s; zz_calls[73]++; zz_last_slot=73; ZZ_BB_IN(73u,bb_s); body_GLTexEnvi(context, target, pname, param); ZZ_BB_OUT(73u,bb_s); }
static inline void body_GLTexGeni(GLcontext context, GLenum coord, GLenum mode, GLenum map) { Z start_tick,io;  if(!zz_profile){impl_GLTexGeni(context, coord, mode, map);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLTexGeni(context, coord, mode, map); zz_profile_add(start_tick,io,1); }
static void timed_GLTexGeni(GLcontext context, GLenum coord, GLenum mode, GLenum map) { Z bb_s; zz_calls[74]++; zz_last_slot=74; ZZ_BB_IN(74u,bb_s); body_GLTexGeni(context, coord, mode, map); ZZ_BB_OUT(74u,bb_s); }
static inline void body_GLTexImage2D(GLcontext context, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) { Z start_tick,io;  if(!zz_profile){impl_GLTexImage2D(context, target, level, internalformat, width, height, border, format, type, pixels);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLTexImage2D(context, target, level, internalformat, width, height, border, format, type, pixels); zz_profile_add(start_tick,io,4); }
static void timed_GLTexImage2D(GLcontext context, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) { Z bb_s; zz_calls[75]++; zz_last_slot=75; ZZ_BB_IN(75u,bb_s); body_GLTexImage2D(context, target, level, internalformat, width, height, border, format, type, pixels); ZZ_BB_OUT(75u,bb_s); }
static inline void body_GLTexParameteri(GLcontext context, GLenum target, GLenum pname, GLint param) { Z start_tick,io;  if(!zz_profile){impl_GLTexParameteri(context, target, pname, param);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLTexParameteri(context, target, pname, param); zz_profile_add(start_tick,io,1); }
static void timed_GLTexParameteri(GLcontext context, GLenum target, GLenum pname, GLint param) { Z bb_s; zz_calls[76]++; zz_last_slot=76; ZZ_BB_IN(76u,bb_s); body_GLTexParameteri(context, target, pname, param); ZZ_BB_OUT(76u,bb_s); }
static inline void body_GLTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) { Z start_tick,io;  if(!zz_profile){impl_GLTexSubImage2D(context, target, level, xoffset, yoffset, width, height, format, type, pixels);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLTexSubImage2D(context, target, level, xoffset, yoffset, width, height, format, type, pixels); zz_profile_add(start_tick,io,4); }
static void timed_GLTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) { Z bb_s; zz_calls[77]++; zz_last_slot=77; ZZ_BB_IN(77u,bb_s); body_GLTexSubImage2D(context, target, level, xoffset, yoffset, width, height, format, type, pixels); ZZ_BB_OUT(77u,bb_s); }
static inline void body_GLTranslated(GLcontext context, GLdouble x, GLdouble y, GLdouble z) { Z start_tick,io;  if(!zz_profile){impl_GLTranslated(context, x, y, z);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLTranslated(context, x, y, z); zz_profile_add(start_tick,io,1); }
static void timed_GLTranslated(GLcontext context, GLdouble x, GLdouble y, GLdouble z) { Z bb_s; zz_calls[78]++; zz_last_slot=78; ZZ_BB_IN(78u,bb_s); body_GLTranslated(context, x, y, z); ZZ_BB_OUT(78u,bb_s); }
static inline void body_GLTranslatef(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { Z start_tick,io;  if(!zz_profile){impl_GLTranslatef(context, x, y, z);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLTranslatef(context, x, y, z); zz_profile_add(start_tick,io,1); }
static void timed_GLTranslatef(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { Z bb_s; zz_calls[79]++; zz_last_slot=79; ZZ_BB_IN(79u,bb_s); body_GLTranslatef(context, x, y, z); ZZ_BB_OUT(79u,bb_s); }
static inline void body_GLULookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz) { zz_missing(81); }
static void missing_GLULookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz) { Z bb_s; zz_calls[80]++; zz_last_slot=80; ZZ_BB_IN(80u,bb_s); body_GLULookAt(ex, ey, ez, cx, cy, cz, ux, uy, uz); ZZ_BB_OUT(80u,bb_s); }
static inline void body_GLUPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar) { Z start_tick,io;  if(!zz_profile){impl_GLUPerspective(fovy, aspect, znear, zfar);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLUPerspective(fovy, aspect, znear, zfar); zz_profile_add(start_tick,io,1); }
static void timed_GLUPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar) { Z bb_s; zz_calls[81]++; zz_last_slot=81; ZZ_BB_IN(81u,bb_s); body_GLUPerspective(fovy, aspect, znear, zfar); ZZ_BB_OUT(81u,bb_s); }
static inline void body_GLUnlockArrays(GLcontext context) { Z start_tick,io;  if(!zz_profile){impl_GLUnlockArrays(context);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLUnlockArrays(context); zz_profile_add(start_tick,io,1); }
static void timed_GLUnlockArrays(GLcontext context) { Z bb_s; zz_calls[82]++; zz_last_slot=82; ZZ_BB_IN(82u,bb_s); body_GLUnlockArrays(context); ZZ_BB_OUT(82u,bb_s); }
static inline void body_GLVertex2fv(GLcontext context, GLfloat *v) { Z start_tick,io;  if(!zz_profile){impl_GLVertex2fv(context, v);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLVertex2fv(context, v); zz_profile_add(start_tick,io,2); }
static void timed_GLVertex2fv(GLcontext context, GLfloat *v) { Z bb_s; zz_calls[83]++; zz_last_slot=83; ZZ_BB_IN(83u,bb_s); body_GLVertex2fv(context, v); ZZ_BB_OUT(83u,bb_s); }
static inline void body_GLVertex3fv(GLcontext context, GLfloat *v) { Z start_tick,io;  if(!zz_profile){impl_GLVertex3fv(context, v);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLVertex3fv(context, v); zz_profile_add(start_tick,io,2); }
static void timed_GLVertex3fv(GLcontext context, GLfloat *v) { Z bb_s; zz_calls[84]++; zz_last_slot=84; ZZ_BB_IN(84u,bb_s); body_GLVertex3fv(context, v); ZZ_BB_OUT(84u,bb_s); }
static inline void body_GLVertex4f(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { Z start_tick,io;  if(!zz_profile){impl_GLVertex4f(context, x, y, z, w);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLVertex4f(context, x, y, z, w); zz_profile_add(start_tick,io,2); }
static void timed_GLVertex4f(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { Z bb_s; zz_calls[85]++; zz_last_slot=85; ZZ_BB_IN(85u,bb_s); body_GLVertex4f(context, x, y, z, w); ZZ_BB_OUT(85u,bb_s); }
static inline void body_GLVertex4fv(GLcontext context, GLfloat *v) { Z start_tick,io;  if(!zz_profile){impl_GLVertex4fv(context, v);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLVertex4fv(context, v); zz_profile_add(start_tick,io,2); }
static void timed_GLVertex4fv(GLcontext context, GLfloat *v) { Z bb_s; zz_calls[86]++; zz_last_slot=86; ZZ_BB_IN(86u,bb_s); body_GLVertex4fv(context, v); ZZ_BB_OUT(86u,bb_s); }
static inline void body_GLVertexPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { Z start_tick,io;  if(!zz_profile){impl_GLVertexPointer(context, size, type, stride, pointer);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLVertexPointer(context, size, type, stride, pointer); zz_profile_add(start_tick,io,2); }
static void timed_GLVertexPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { Z bb_s; zz_calls[87]++; zz_last_slot=87; ZZ_BB_IN(87u,bb_s); body_GLVertexPointer(context, size, type, stride, pointer); ZZ_BB_OUT(87u,bb_s); }
static inline void body_GLViewport(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height) { Z start_tick,io;  if(!zz_profile){impl_GLViewport(context, x, y, width, height);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLViewport(context, x, y, width, height); zz_profile_add(start_tick,io,1); }
static void timed_GLViewport(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height) { Z bb_s; zz_calls[88]++; zz_last_slot=88; ZZ_BB_IN(88u,bb_s); body_GLViewport(context, x, y, width, height); ZZ_BB_OUT(88u,bb_s); }
static inline void body_MGLClearPointer(GLcontext context) { impl_MGLClearPointer(context); }
static void counted_MGLClearPointer(GLcontext context) { Z bb_s; zz_calls[89]++; zz_last_slot=89; ZZ_BB_IN(89u,bb_s); body_MGLClearPointer(context); ZZ_BB_OUT(89u,bb_s); }
static inline void * body_MGLCreateContext(int offx, int offy, int w, int h) { return impl_MGLCreateContext(offx, offy, w, h); }
static void * counted_MGLCreateContext(int offx, int offy, int w, int h) { Z bb_s; void * bb_r; zz_calls[90]++; zz_last_slot=90; ZZ_BB_IN(90u,bb_s); bb_r=body_MGLCreateContext(offx, offy, w, h); ZZ_BB_OUT(90u,bb_s); return bb_r; }
static inline void * body_MGLCreateContextFromID(GLint ID, GLint *w, GLint *h) { zz_missing(92); return 0; }
static void * missing_MGLCreateContextFromID(GLint ID, GLint *w, GLint *h) { Z bb_s; void * bb_r; zz_calls[91]++; zz_last_slot=91; ZZ_BB_IN(91u,bb_s); bb_r=body_MGLCreateContextFromID(ID, w, h); ZZ_BB_OUT(91u,bb_s); return bb_r; }
static inline void body_MGLDeleteContext(GLcontext context) { impl_MGLDeleteContext(context); }
static void counted_MGLDeleteContext(GLcontext context) { Z bb_s; zz_calls[92]++; zz_last_slot=92; ZZ_BB_IN(92u,bb_s); body_MGLDeleteContext(context); ZZ_BB_OUT(92u,bb_s); }
static inline void body_MGLDrawMultitexBuffer(GLcontext context, GLenum BSrc, GLenum BDst, GLenum TexEnv) { impl_MGLDrawMultitexBuffer(context, BSrc, BDst, TexEnv); }
static void counted_MGLDrawMultitexBuffer(GLcontext context, GLenum BSrc, GLenum BDst, GLenum TexEnv) { Z bb_s; zz_calls[93]++; zz_last_slot=93; ZZ_BB_IN(93u,bb_s); body_MGLDrawMultitexBuffer(context, BSrc, BDst, TexEnv); ZZ_BB_OUT(93u,bb_s); }
static inline void body_MGLEnableSync(GLcontext context, GLboolean enable) { impl_MGLEnableSync(context, enable); }
static void counted_MGLEnableSync(GLcontext context, GLboolean enable) { Z bb_s; zz_calls[94]++; zz_last_slot=94; ZZ_BB_IN(94u,bb_s); body_MGLEnableSync(context, enable); ZZ_BB_OUT(94u,bb_s); }
static inline void body_MGLExit(GLcontext context) { impl_MGLExit(context); }
static void counted_MGLExit(GLcontext context) { Z bb_s; zz_calls[95]++; zz_last_slot=95; ZZ_BB_IN(95u,bb_s); body_MGLExit(context); ZZ_BB_OUT(95u,bb_s); }
static inline void * body_MGLGetInputWindowHandle(GLcontext context) { return impl_MGLGetInputWindowHandle(context); }
static void * counted_MGLGetInputWindowHandle(GLcontext context) { Z bb_s; void * bb_r; zz_calls[96]++; zz_last_slot=96; ZZ_BB_IN(96u,bb_s); bb_r=body_MGLGetInputWindowHandle(context); ZZ_BB_OUT(96u,bb_s); return bb_r; }
static inline void * body_MGLGetWindowHandle(GLcontext context) { return impl_MGLGetWindowHandle(context); }
static void * counted_MGLGetWindowHandle(GLcontext context) { Z bb_s; void * bb_r; zz_calls[97]++; zz_last_slot=97; ZZ_BB_IN(97u,bb_s); bb_r=body_MGLGetWindowHandle(context); ZZ_BB_OUT(97u,bb_s); return bb_r; }
static inline void body_MGLIdleFunc(GLcontext context, IdleFn i) { impl_MGLIdleFunc(context, i); }
static void counted_MGLIdleFunc(GLcontext context, IdleFn i) { Z bb_s; zz_calls[98]++; zz_last_slot=98; ZZ_BB_IN(98u,bb_s); body_MGLIdleFunc(context, i); ZZ_BB_OUT(98u,bb_s); }
static inline void body_MGLKeyFunc(GLcontext context, KeyHandlerFn k) { impl_MGLKeyFunc(context, k); }
static void counted_MGLKeyFunc(GLcontext context, KeyHandlerFn k) { Z bb_s; zz_calls[99]++; zz_last_slot=99; ZZ_BB_IN(99u,bb_s); body_MGLKeyFunc(context, k); ZZ_BB_OUT(99u,bb_s); }
static inline GLboolean body_MGLLockBack(GLcontext context, MGLLockInfo *info) { zz_missing(101); return 0; }
static GLboolean missing_MGLLockBack(GLcontext context, MGLLockInfo *info) { Z bb_s; GLboolean bb_r; zz_calls[100]++; zz_last_slot=100; ZZ_BB_IN(100u,bb_s); bb_r=body_MGLLockBack(context, info); ZZ_BB_OUT(100u,bb_s); return bb_r; }
static inline GLboolean body_MGLLockDisplay(GLcontext context) { return impl_MGLLockDisplay(context); }
static GLboolean counted_MGLLockDisplay(GLcontext context) { Z bb_s; GLboolean bb_r; zz_calls[101]++; zz_last_slot=101; ZZ_BB_IN(101u,bb_s); bb_r=body_MGLLockDisplay(context); ZZ_BB_OUT(101u,bb_s); return bb_r; }
static inline void body_MGLLockMode(GLcontext context, GLenum lockMode) { impl_MGLLockMode(context, lockMode); }
static void counted_MGLLockMode(GLcontext context, GLenum lockMode) { Z bb_s; zz_calls[102]++; zz_last_slot=102; ZZ_BB_IN(102u,bb_s); body_MGLLockMode(context, lockMode); ZZ_BB_OUT(102u,bb_s); }
static inline void body_MGLMainLoop(GLcontext context) { impl_MGLMainLoop(context); }
static void counted_MGLMainLoop(GLcontext context) { Z bb_s; zz_calls[103]++; zz_last_slot=103; ZZ_BB_IN(103u,bb_s); body_MGLMainLoop(context); ZZ_BB_OUT(103u,bb_s); }
static inline void body_MGLMinTriArea(GLcontext context, GLfloat area) { impl_MGLMinTriArea(context, area); }
static void counted_MGLMinTriArea(GLcontext context, GLfloat area) { Z bb_s; zz_calls[104]++; zz_last_slot=104; ZZ_BB_IN(104u,bb_s); body_MGLMinTriArea(context, area); ZZ_BB_OUT(104u,bb_s); }
static inline void body_MGLMouseFunc(GLcontext context, MouseHandlerFn m) { zz_missing(106); }
static void missing_MGLMouseFunc(GLcontext context, MouseHandlerFn m) { Z bb_s; zz_calls[105]++; zz_last_slot=105; ZZ_BB_IN(105u,bb_s); body_MGLMouseFunc(context, m); ZZ_BB_OUT(105u,bb_s); }
static inline void body_MGLPrintMatrix(GLcontext context, int mode) { zz_missing(107); }
static void missing_MGLPrintMatrix(GLcontext context, int mode) { Z bb_s; zz_calls[106]++; zz_last_slot=106; ZZ_BB_IN(106u,bb_s); body_MGLPrintMatrix(context, mode); ZZ_BB_OUT(106u,bb_s); }
static inline void body_MGLPrintMatrixStack(GLcontext context, int mode) { zz_missing(108); }
static void missing_MGLPrintMatrixStack(GLcontext context, int mode) { Z bb_s; zz_calls[107]++; zz_last_slot=107; ZZ_BB_IN(107u,bb_s); body_MGLPrintMatrixStack(context, mode); ZZ_BB_OUT(107u,bb_s); }
static inline GLboolean body_MGLResizeContext(GLcontext context, GLsizei width, GLsizei height) { return impl_MGLResizeContext(context, width, height); }
static GLboolean counted_MGLResizeContext(GLcontext context, GLsizei width, GLsizei height) { Z bb_s; GLboolean bb_r; zz_calls[108]++; zz_last_slot=108; ZZ_BB_IN(108u,bb_s); bb_r=body_MGLResizeContext(context, width, height); ZZ_BB_OUT(108u,bb_s); return bb_r; }
static inline void body_MGLSetPointer(GLcontext context) { impl_MGLSetPointer(context); }
static void counted_MGLSetPointer(GLcontext context) { Z bb_s; zz_calls[109]++; zz_last_slot=109; ZZ_BB_IN(109u,bb_s); body_MGLSetPointer(context); ZZ_BB_OUT(109u,bb_s); }
static inline void body_MGLSetState(GLcontext context, GLenum cap, GLboolean state) { impl_MGLSetState(context, cap, state); }
static void counted_MGLSetState(GLcontext context, GLenum cap, GLboolean state) { Z bb_s; zz_calls[110]++; zz_last_slot=110; ZZ_BB_IN(110u,bb_s); body_MGLSetState(context, cap, state); ZZ_BB_OUT(110u,bb_s); }
static inline void body_MGLSetZOffset(GLcontext context, GLfloat offset) { impl_MGLSetZOffset(context, offset); }
static void counted_MGLSetZOffset(GLcontext context, GLfloat offset) { Z bb_s; zz_calls[111]++; zz_last_slot=111; ZZ_BB_IN(111u,bb_s); body_MGLSetZOffset(context, offset); ZZ_BB_OUT(111u,bb_s); }
static inline void body_MGLSpecialFunc(GLcontext context, SpecialHandlerFn s) { zz_missing(113); }
static void missing_MGLSpecialFunc(GLcontext context, SpecialHandlerFn s) { Z bb_s; zz_calls[112]++; zz_last_slot=112; ZZ_BB_IN(112u,bb_s); body_MGLSpecialFunc(context, s); ZZ_BB_OUT(112u,bb_s); }
static inline void body_MGLSwitchDisplay(GLcontext context) { Z start_tick,io;  if(!zz_profile){impl_MGLSwitchDisplay(context);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_MGLSwitchDisplay(context); zz_profile_add(start_tick,io,0); }
static void timed_MGLSwitchDisplay(GLcontext context) { Z bb_s; zz_calls[113]++; zz_last_slot=113; ZZ_BB_IN(113u,bb_s); body_MGLSwitchDisplay(context); ZZ_BB_OUT(113u,bb_s); }
static inline void body_MGLTexMemStat(GLcontext context, GLint *Current, GLint *Peak) { zz_missing(115); }
static void missing_MGLTexMemStat(GLcontext context, GLint *Current, GLint *Peak) { Z bb_s; zz_calls[114]++; zz_last_slot=114; ZZ_BB_IN(114u,bb_s); body_MGLTexMemStat(context, Current, Peak); ZZ_BB_OUT(114u,bb_s); }
static inline void body_MGLUnlockDisplay(GLcontext context) { impl_MGLUnlockDisplay(context); }
static void counted_MGLUnlockDisplay(GLcontext context) { Z bb_s; zz_calls[115]++; zz_last_slot=115; ZZ_BB_IN(115u,bb_s); body_MGLUnlockDisplay(context); ZZ_BB_OUT(115u,bb_s); }
static inline void body_MGLWriteShotPPM(GLcontext context, char *filename) { impl_MGLWriteShotPPM(context, filename); }
static void counted_MGLWriteShotPPM(GLcontext context, char *filename) { Z bb_s; zz_calls[116]++; zz_last_slot=116; ZZ_BB_IN(116u,bb_s); body_MGLWriteShotPPM(context, filename); ZZ_BB_OUT(116u,bb_s); }
static inline void body_mglChooseGuardBand(GLboolean flag) { impl_mglChooseGuardBand(flag); }
static void counted_mglChooseGuardBand(GLboolean flag) { Z bb_s; zz_calls[117]++; zz_last_slot=117; ZZ_BB_IN(117u,bb_s); body_mglChooseGuardBand(flag); ZZ_BB_OUT(117u,bb_s); }
static inline void body_mglChooseMtexBufferSize(int size) { impl_mglChooseMtexBufferSize(size); }
static void counted_mglChooseMtexBufferSize(int size) { Z bb_s; zz_calls[118]++; zz_last_slot=118; ZZ_BB_IN(118u,bb_s); body_mglChooseMtexBufferSize(size); ZZ_BB_OUT(118u,bb_s); }
static inline void body_mglChooseNumberOfBuffers(int number) { impl_mglChooseNumberOfBuffers(number); }
static void counted_mglChooseNumberOfBuffers(int number) { Z bb_s; zz_calls[119]++; zz_last_slot=119; ZZ_BB_IN(119u,bb_s); body_mglChooseNumberOfBuffers(number); ZZ_BB_OUT(119u,bb_s); }
static inline void body_mglChoosePixelDepth(int depth) { impl_mglChoosePixelDepth(depth); }
static void counted_mglChoosePixelDepth(int depth) { Z bb_s; zz_calls[120]++; zz_last_slot=120; ZZ_BB_IN(120u,bb_s); body_mglChoosePixelDepth(depth); ZZ_BB_OUT(120u,bb_s); }
static inline void body_mglChooseTextureBufferSize(int size) { zz_missing(122); }
static void missing_mglChooseTextureBufferSize(int size) { Z bb_s; zz_calls[121]++; zz_last_slot=121; ZZ_BB_IN(121u,bb_s); body_mglChooseTextureBufferSize(size); ZZ_BB_OUT(121u,bb_s); }
static inline void body_mglChooseVertexBufferSize(int size) { impl_mglChooseVertexBufferSize(size); }
static void counted_mglChooseVertexBufferSize(int size) { Z bb_s; zz_calls[122]++; zz_last_slot=122; ZZ_BB_IN(122u,bb_s); body_mglChooseVertexBufferSize(size); ZZ_BB_OUT(122u,bb_s); }
static inline void body_mglChooseWindowMode(GLboolean flag) { impl_mglChooseWindowMode(flag); }
static void counted_mglChooseWindowMode(GLboolean flag) { Z bb_s; zz_calls[123]++; zz_last_slot=123; ZZ_BB_IN(123u,bb_s); body_mglChooseWindowMode(flag); ZZ_BB_OUT(123u,bb_s); }
static inline GLint body_mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn) { zz_missing(125); return 0; }
static GLint missing_mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn) { Z bb_s; GLint bb_r; zz_calls[124]++; zz_last_slot=124; ZZ_BB_IN(124u,bb_s); bb_r=body_mglGetSupportedScreenModes(CallbackFn); ZZ_BB_OUT(124u,bb_s); return bb_r; }
static inline void body_mglProhibitAlphaFallback(GLboolean flag) { zz_missing(126); }
static void missing_mglProhibitAlphaFallback(GLboolean flag) { Z bb_s; zz_calls[125]++; zz_last_slot=125; ZZ_BB_IN(125u,bb_s); body_mglProhibitAlphaFallback(flag); ZZ_BB_OUT(125u,bb_s); }
static inline void body_mglProhibitMipMapping(GLboolean flag) { zz_missing(127); }
static void missing_mglProhibitMipMapping(GLboolean flag) { Z bb_s; zz_calls[126]++; zz_last_slot=126; ZZ_BB_IN(126u,bb_s); body_mglProhibitMipMapping(flag); ZZ_BB_OUT(126u,bb_s); }
static inline void body_mglProposeCloseDesktop(GLboolean closeme) { impl_mglProposeCloseDesktop(closeme); }
static void counted_mglProposeCloseDesktop(GLboolean closeme) { Z bb_s; zz_calls[127]++; zz_last_slot=127; ZZ_BB_IN(127u,bb_s); body_mglProposeCloseDesktop(closeme); ZZ_BB_OUT(127u,bb_s); }
static inline void body_GLPolygonOffset(GLcontext context, GLfloat factor, GLfloat units) { zz_missing(129); }
static void missing_GLPolygonOffset(GLcontext context, GLfloat factor, GLfloat units) { Z bb_s; zz_calls[128]++; zz_last_slot=128; ZZ_BB_IN(128u,bb_s); body_GLPolygonOffset(context, factor, units); ZZ_BB_OUT(128u,bb_s); }
static inline void body_GLClientActiveTextureARB(GLcontext context, GLenum unit) { Z start_tick,io;  if(!zz_profile){impl_GLClientActiveTextureARB(context, unit);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLClientActiveTextureARB(context, unit); zz_profile_add(start_tick,io,1); }
static void timed_GLClientActiveTextureARB(GLcontext context, GLenum unit) { Z bb_s; zz_calls[129]++; zz_last_slot=129; ZZ_BB_IN(129u,bb_s); body_GLClientActiveTextureARB(context, unit); ZZ_BB_OUT(129u,bb_s); }
static inline void body_GLInterleavedArrays(GLcontext context, GLenum format, GLsizei stride, const GLvoid *pointer) { zz_missing(131); }
static void missing_GLInterleavedArrays(GLcontext context, GLenum format, GLsizei stride, const GLvoid *pointer) { Z bb_s; zz_calls[130]++; zz_last_slot=130; ZZ_BB_IN(130u,bb_s); body_GLInterleavedArrays(context, format, stride, pointer); ZZ_BB_OUT(130u,bb_s); }
static inline void body_GLMultiDrawArrays(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount) { zz_missing(132); }
static void missing_GLMultiDrawArrays(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount) { Z bb_s; zz_calls[131]++; zz_last_slot=131; ZZ_BB_IN(131u,bb_s); body_GLMultiDrawArrays(context, mode, first, count, primcount); ZZ_BB_OUT(131u,bb_s); }
static inline void body_GLBlendEquation(GLcontext context, GLenum mode) { zz_missing(133); }
static void missing_GLBlendEquation(GLcontext context, GLenum mode) { Z bb_s; zz_calls[132]++; zz_last_slot=132; ZZ_BB_IN(132u,bb_s); body_GLBlendEquation(context, mode); ZZ_BB_OUT(132u,bb_s); }
static inline void body_GLBlendFuncSeparate(GLcontext context, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) { zz_missing(134); }
static void missing_GLBlendFuncSeparate(GLcontext context, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) { Z bb_s; zz_calls[133]++; zz_last_slot=133; ZZ_BB_IN(133u,bb_s); body_GLBlendFuncSeparate(context, srcRGB, dstRGB, srcAlpha, dstAlpha); ZZ_BB_OUT(133u,bb_s); }
static inline GLboolean body_GLIsTexture(GLcontext context, GLuint texture) { Z start_tick,io; GLboolean result; if(!zz_profile){return impl_GLIsTexture(context, texture);} start_tick=zz_clock();io=zz_transfer_ticks();result=impl_GLIsTexture(context, texture); zz_profile_add(start_tick,io,1);return result; }
static GLboolean timed_GLIsTexture(GLcontext context, GLuint texture) { Z bb_s; GLboolean bb_r; zz_calls[134]++; zz_last_slot=134; ZZ_BB_IN(134u,bb_s); bb_r=body_GLIsTexture(context, texture); ZZ_BB_OUT(134u,bb_s); return bb_r; }
static inline void body_mglChooseZBufferDepth(int bits) { zz_missing(136); }
static void missing_mglChooseZBufferDepth(int bits) { Z bb_s; zz_calls[135]++; zz_last_slot=135; ZZ_BB_IN(135u,bb_s); body_mglChooseZBufferDepth(bits); ZZ_BB_OUT(135u,bb_s); }
static inline void body_GLLineWidth(GLcontext context, GLfloat width) { zz_missing(137); }
static void missing_GLLineWidth(GLcontext context, GLfloat width) { Z bb_s; zz_calls[136]++; zz_last_slot=136; ZZ_BB_IN(136u,bb_s); body_GLLineWidth(context, width); ZZ_BB_OUT(136u,bb_s); }
static inline void body_GLTexGenfv(GLcontext context, GLenum coord, GLenum pname, const GLfloat *params) { Z start_tick,io;  if(!zz_profile){impl_GLTexGenfv(context, coord, pname, params);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLTexGenfv(context, coord, pname, params); zz_profile_add(start_tick,io,1); }
static void timed_GLTexGenfv(GLcontext context, GLenum coord, GLenum pname, const GLfloat *params) { Z bb_s; zz_calls[137]++; zz_last_slot=137; ZZ_BB_IN(137u,bb_s); body_GLTexGenfv(context, coord, pname, params); ZZ_BB_OUT(137u,bb_s); }
static inline void body_GLCopyTexImage2D(GLcontext context, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) { zz_missing(139); }
static void missing_GLCopyTexImage2D(GLcontext context, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) { Z bb_s; zz_calls[138]++; zz_last_slot=138; ZZ_BB_IN(138u,bb_s); body_GLCopyTexImage2D(context, target, level, internalformat, x, y, width, height, border); ZZ_BB_OUT(138u,bb_s); }
static inline void body_GLCopyTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) { zz_missing(140); }
static void missing_GLCopyTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) { Z bb_s; zz_calls[139]++; zz_last_slot=139; ZZ_BB_IN(139u,bb_s); body_GLCopyTexSubImage2D(context, target, level, xoffset, yoffset, x, y, width, height); ZZ_BB_OUT(139u,bb_s); }
static inline GLboolean body_GLAreTexturesResident(GLcontext context, GLsizei n, const GLuint *textures, GLboolean *residences) { zz_missing(141); return 0; }
static GLboolean missing_GLAreTexturesResident(GLcontext context, GLsizei n, const GLuint *textures, GLboolean *residences) { Z bb_s; GLboolean bb_r; zz_calls[140]++; zz_last_slot=140; ZZ_BB_IN(140u,bb_s); bb_r=body_GLAreTexturesResident(context, n, textures, residences); ZZ_BB_OUT(140u,bb_s); return bb_r; }
static inline void body_GLEdgeFlag(GLcontext context, GLboolean flag) { zz_missing(142); }
static void missing_GLEdgeFlag(GLcontext context, GLboolean flag) { Z bb_s; zz_calls[141]++; zz_last_slot=141; ZZ_BB_IN(141u,bb_s); body_GLEdgeFlag(context, flag); ZZ_BB_OUT(141u,bb_s); }
static inline void body_GLEdgeFlagPointer(GLcontext context, GLsizei stride, const GLvoid *pointer) { zz_missing(143); }
static void missing_GLEdgeFlagPointer(GLcontext context, GLsizei stride, const GLvoid *pointer) { Z bb_s; zz_calls[142]++; zz_last_slot=142; ZZ_BB_IN(142u,bb_s); body_GLEdgeFlagPointer(context, stride, pointer); ZZ_BB_OUT(142u,bb_s); }
static inline void body_GLEdgeFlagv(GLcontext context, const GLboolean *flag) { zz_missing(144); }
static void missing_GLEdgeFlagv(GLcontext context, const GLboolean *flag) { Z bb_s; zz_calls[143]++; zz_last_slot=143; ZZ_BB_IN(143u,bb_s); body_GLEdgeFlagv(context, flag); ZZ_BB_OUT(143u,bb_s); }
static inline void body_GLGetDoublev(GLcontext context, GLenum pname, GLdouble *params) { zz_missing(145); }
static void missing_GLGetDoublev(GLcontext context, GLenum pname, GLdouble *params) { Z bb_s; zz_calls[144]++; zz_last_slot=144; ZZ_BB_IN(144u,bb_s); body_GLGetDoublev(context, pname, params); ZZ_BB_OUT(144u,bb_s); }
static inline void body_GLGetPointerv(GLcontext context, GLenum pname, GLvoid **params) { zz_missing(146); }
static void missing_GLGetPointerv(GLcontext context, GLenum pname, GLvoid **params) { Z bb_s; zz_calls[145]++; zz_last_slot=145; ZZ_BB_IN(145u,bb_s); body_GLGetPointerv(context, pname, params); ZZ_BB_OUT(145u,bb_s); }
static inline void body_GLIndexi(GLcontext context, GLint c) { zz_missing(147); }
static void missing_GLIndexi(GLcontext context, GLint c) { Z bb_s; zz_calls[146]++; zz_last_slot=146; ZZ_BB_IN(146u,bb_s); body_GLIndexi(context, c); ZZ_BB_OUT(146u,bb_s); }
static inline void body_GLIndexiv(GLcontext context, const GLint *c) { zz_missing(148); }
static void missing_GLIndexiv(GLcontext context, const GLint *c) { Z bb_s; zz_calls[147]++; zz_last_slot=147; ZZ_BB_IN(147u,bb_s); body_GLIndexiv(context, c); ZZ_BB_OUT(147u,bb_s); }
static inline void body_GLIndexPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer) { zz_missing(149); }
static void missing_GLIndexPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer) { Z bb_s; zz_calls[148]++; zz_last_slot=148; ZZ_BB_IN(148u,bb_s); body_GLIndexPointer(context, type, stride, pointer); ZZ_BB_OUT(148u,bb_s); }
static inline void body_GLPrioritizeTextures(GLcontext context, GLsizei n, const GLuint *textures, const GLclampf *priorities) { zz_missing(150); }
static void missing_GLPrioritizeTextures(GLcontext context, GLsizei n, const GLuint *textures, const GLclampf *priorities) { Z bb_s; zz_calls[149]++; zz_last_slot=149; ZZ_BB_IN(149u,bb_s); body_GLPrioritizeTextures(context, n, textures, priorities); ZZ_BB_OUT(149u,bb_s); }
static inline void body_GLReadBuffer(GLcontext context, GLenum mode) { zz_missing(151); }
static void missing_GLReadBuffer(GLcontext context, GLenum mode) { Z bb_s; zz_calls[150]++; zz_last_slot=150; ZZ_BB_IN(150u,bb_s); body_GLReadBuffer(context, mode); ZZ_BB_OUT(150u,bb_s); }
static inline void * body_MGLCreateContextFromWindow(struct Window *window) { return impl_MGLCreateContextFromWindow(window); }
static void * counted_MGLCreateContextFromWindow(struct Window *window) { Z bb_s; void * bb_r; zz_calls[151]++; zz_last_slot=151; ZZ_BB_IN(151u,bb_s); bb_r=body_MGLCreateContextFromWindow(window); ZZ_BB_OUT(151u,bb_s); return bb_r; }
static inline void * body_MGLCreateContextFromBitMap(struct BitMap *bitmap) { zz_missing(153); return 0; }
static void * missing_MGLCreateContextFromBitMap(struct BitMap *bitmap) { Z bb_s; void * bb_r; zz_calls[152]++; zz_last_slot=152; ZZ_BB_IN(152u,bb_s); bb_r=body_MGLCreateContextFromBitMap(bitmap); ZZ_BB_OUT(152u,bb_s); return bb_r; }
static inline void body_GLLightf(GLcontext context, GLenum light, GLenum pname, GLfloat param) { zz_missing(154); }
static void missing_GLLightf(GLcontext context, GLenum light, GLenum pname, GLfloat param) { Z bb_s; zz_calls[153]++; zz_last_slot=153; ZZ_BB_IN(153u,bb_s); body_GLLightf(context, light, pname, param); ZZ_BB_OUT(153u,bb_s); }
static inline void body_GLLightfv(GLcontext context, GLenum light, GLenum pname, const GLfloat *params) { zz_missing(155); }
static void missing_GLLightfv(GLcontext context, GLenum light, GLenum pname, const GLfloat *params) { Z bb_s; zz_calls[154]++; zz_last_slot=154; ZZ_BB_IN(154u,bb_s); body_GLLightfv(context, light, pname, params); ZZ_BB_OUT(154u,bb_s); }
static inline void body_GLMaterialf(GLcontext context, GLenum face, GLenum pname, GLfloat param) { zz_missing(156); }
static void missing_GLMaterialf(GLcontext context, GLenum face, GLenum pname, GLfloat param) { Z bb_s; zz_calls[155]++; zz_last_slot=155; ZZ_BB_IN(155u,bb_s); body_GLMaterialf(context, face, pname, param); ZZ_BB_OUT(155u,bb_s); }
static inline void body_GLMaterialfv(GLcontext context, GLenum face, GLenum pname, const GLfloat *params) { zz_missing(157); }
static void missing_GLMaterialfv(GLcontext context, GLenum face, GLenum pname, const GLfloat *params) { Z bb_s; zz_calls[156]++; zz_last_slot=156; ZZ_BB_IN(156u,bb_s); body_GLMaterialfv(context, face, pname, params); ZZ_BB_OUT(156u,bb_s); }
static inline void body_GLLightModelf(GLcontext context, GLenum pname, GLfloat param) { zz_missing(158); }
static void missing_GLLightModelf(GLcontext context, GLenum pname, GLfloat param) { Z bb_s; zz_calls[157]++; zz_last_slot=157; ZZ_BB_IN(157u,bb_s); body_GLLightModelf(context, pname, param); ZZ_BB_OUT(157u,bb_s); }
static inline void body_GLLightModelfv(GLcontext context, GLenum pname, const GLfloat *params) { zz_missing(159); }
static void missing_GLLightModelfv(GLcontext context, GLenum pname, const GLfloat *params) { Z bb_s; zz_calls[158]++; zz_last_slot=158; ZZ_BB_IN(158u,bb_s); body_GLLightModelfv(context, pname, params); ZZ_BB_OUT(158u,bb_s); }
static inline void body_GLGetLightfv(GLcontext context, GLenum light, GLenum pname, GLfloat *params) { zz_missing(160); }
static void missing_GLGetLightfv(GLcontext context, GLenum light, GLenum pname, GLfloat *params) { Z bb_s; zz_calls[159]++; zz_last_slot=159; ZZ_BB_IN(159u,bb_s); body_GLGetLightfv(context, light, pname, params); ZZ_BB_OUT(159u,bb_s); }
static inline void body_GLGetMaterialfv(GLcontext context, GLenum face, GLenum pname, GLfloat *params) { zz_missing(161); }
static void missing_GLGetMaterialfv(GLcontext context, GLenum face, GLenum pname, GLfloat *params) { Z bb_s; zz_calls[160]++; zz_last_slot=160; ZZ_BB_IN(160u,bb_s); body_GLGetMaterialfv(context, face, pname, params); ZZ_BB_OUT(160u,bb_s); }
static inline GLuint body_GLGenLists(GLcontext context, GLsizei range) { zz_missing(162); return 0; }
static GLuint missing_GLGenLists(GLcontext context, GLsizei range) { Z bb_s; GLuint bb_r; zz_calls[161]++; zz_last_slot=161; ZZ_BB_IN(161u,bb_s); bb_r=body_GLGenLists(context, range); ZZ_BB_OUT(161u,bb_s); return bb_r; }
static inline void body_GLDeleteLists(GLcontext context, GLuint list, GLsizei range) { zz_missing(163); }
static void missing_GLDeleteLists(GLcontext context, GLuint list, GLsizei range) { Z bb_s; zz_calls[162]++; zz_last_slot=162; ZZ_BB_IN(162u,bb_s); body_GLDeleteLists(context, list, range); ZZ_BB_OUT(162u,bb_s); }
static inline GLboolean body_GLIsList(GLcontext context, GLuint list) { zz_missing(164); return 0; }
static GLboolean missing_GLIsList(GLcontext context, GLuint list) { Z bb_s; GLboolean bb_r; zz_calls[163]++; zz_last_slot=163; ZZ_BB_IN(163u,bb_s); bb_r=body_GLIsList(context, list); ZZ_BB_OUT(163u,bb_s); return bb_r; }
static inline void body_GLNewList(GLcontext context, GLuint list, GLenum mode) { zz_missing(165); }
static void missing_GLNewList(GLcontext context, GLuint list, GLenum mode) { Z bb_s; zz_calls[164]++; zz_last_slot=164; ZZ_BB_IN(164u,bb_s); body_GLNewList(context, list, mode); ZZ_BB_OUT(164u,bb_s); }
static inline void body_GLEndList(GLcontext context) { zz_missing(166); }
static void missing_GLEndList(GLcontext context) { Z bb_s; zz_calls[165]++; zz_last_slot=165; ZZ_BB_IN(165u,bb_s); body_GLEndList(context); ZZ_BB_OUT(165u,bb_s); }
static inline void body_GLCallList(GLcontext context, GLuint list) { zz_missing(167); }
static void missing_GLCallList(GLcontext context, GLuint list) { Z bb_s; zz_calls[166]++; zz_last_slot=166; ZZ_BB_IN(166u,bb_s); body_GLCallList(context, list); ZZ_BB_OUT(166u,bb_s); }
static inline GLint body_GLUBuild2DMipmaps(GLcontext context, GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *data) { zz_missing(168); return 0; }
static GLint missing_GLUBuild2DMipmaps(GLcontext context, GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *data) { Z bb_s; GLint bb_r; zz_calls[167]++; zz_last_slot=167; ZZ_BB_IN(167u,bb_s); bb_r=body_GLUBuild2DMipmaps(context, target, internalFormat, width, height, format, type, data); ZZ_BB_OUT(167u,bb_s); return bb_r; }
static inline const GLubyte * body_GLUErrorString(GLenum errCode) { zz_missing(169); return 0; }
static const GLubyte * missing_GLUErrorString(GLenum errCode) { Z bb_s; const GLubyte * bb_r; zz_calls[168]++; zz_last_slot=168; ZZ_BB_IN(168u,bb_s); bb_r=body_GLUErrorString(errCode); ZZ_BB_OUT(168u,bb_s); return bb_r; }
static inline GLUquadricObj * body_GLUNewQuadric(void) { zz_missing(170); return 0; }
static GLUquadricObj * missing_GLUNewQuadric(void) { Z bb_s; GLUquadricObj * bb_r; zz_calls[169]++; zz_last_slot=169; ZZ_BB_IN(169u,bb_s); bb_r=body_GLUNewQuadric(); ZZ_BB_OUT(169u,bb_s); return bb_r; }
static inline void body_GLUDeleteQuadric(GLUquadricObj *q) { zz_missing(171); }
static void missing_GLUDeleteQuadric(GLUquadricObj *q) { Z bb_s; zz_calls[170]++; zz_last_slot=170; ZZ_BB_IN(170u,bb_s); body_GLUDeleteQuadric(q); ZZ_BB_OUT(170u,bb_s); }
static inline void body_GLUQuadricNormals(GLUquadricObj *q, GLenum normals) { zz_missing(172); }
static void missing_GLUQuadricNormals(GLUquadricObj *q, GLenum normals) { Z bb_s; zz_calls[171]++; zz_last_slot=171; ZZ_BB_IN(171u,bb_s); body_GLUQuadricNormals(q, normals); ZZ_BB_OUT(171u,bb_s); }
static inline void body_GLUQuadricTexture(GLUquadricObj *q, GLboolean textureCoords) { zz_missing(173); }
static void missing_GLUQuadricTexture(GLUquadricObj *q, GLboolean textureCoords) { Z bb_s; zz_calls[172]++; zz_last_slot=172; ZZ_BB_IN(172u,bb_s); body_GLUQuadricTexture(q, textureCoords); ZZ_BB_OUT(172u,bb_s); }
static inline void body_GLUQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle) { zz_missing(174); }
static void missing_GLUQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle) { Z bb_s; zz_calls[173]++; zz_last_slot=173; ZZ_BB_IN(173u,bb_s); body_GLUQuadricDrawStyle(q, drawStyle); ZZ_BB_OUT(173u,bb_s); }
static inline void body_GLUQuadricOrientation(GLUquadricObj *q, GLenum orientation) { zz_missing(175); }
static void missing_GLUQuadricOrientation(GLUquadricObj *q, GLenum orientation) { Z bb_s; zz_calls[174]++; zz_last_slot=174; ZZ_BB_IN(174u,bb_s); body_GLUQuadricOrientation(q, orientation); ZZ_BB_OUT(174u,bb_s); }
static inline void body_GLUCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks) { zz_missing(176); }
static void missing_GLUCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks) { Z bb_s; zz_calls[175]++; zz_last_slot=175; ZZ_BB_IN(175u,bb_s); body_GLUCylinder(q, base, top, height, slices, stacks); ZZ_BB_OUT(175u,bb_s); }
static inline void body_GLUSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks) { zz_missing(177); }
static void missing_GLUSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks) { Z bb_s; zz_calls[176]++; zz_last_slot=176; ZZ_BB_IN(176u,bb_s); body_GLUSphere(q, radius, slices, stacks); ZZ_BB_OUT(176u,bb_s); }
static inline void body_GLUDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops) { zz_missing(178); }
static void missing_GLUDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops) { Z bb_s; zz_calls[177]++; zz_last_slot=177; ZZ_BB_IN(177u,bb_s); body_GLUDisk(q, inner, outer, slices, loops); ZZ_BB_OUT(177u,bb_s); }
static inline void body_GLUTInit(int *argcp, char **argv) { zz_missing(179); }
static void missing_GLUTInit(int *argcp, char **argv) { Z bb_s; zz_calls[178]++; zz_last_slot=178; ZZ_BB_IN(178u,bb_s); body_GLUTInit(argcp, argv); ZZ_BB_OUT(178u,bb_s); }
static inline void body_GLUTInitDisplayMode(unsigned int mode) { zz_missing(180); }
static void missing_GLUTInitDisplayMode(unsigned int mode) { Z bb_s; zz_calls[179]++; zz_last_slot=179; ZZ_BB_IN(179u,bb_s); body_GLUTInitDisplayMode(mode); ZZ_BB_OUT(179u,bb_s); }
static inline void body_GLUTInitWindowSize(int width, int height) { zz_missing(181); }
static void missing_GLUTInitWindowSize(int width, int height) { Z bb_s; zz_calls[180]++; zz_last_slot=180; ZZ_BB_IN(180u,bb_s); body_GLUTInitWindowSize(width, height); ZZ_BB_OUT(180u,bb_s); }
static inline void body_GLUTInitWindowPosition(int x, int y) { zz_missing(182); }
static void missing_GLUTInitWindowPosition(int x, int y) { Z bb_s; zz_calls[181]++; zz_last_slot=181; ZZ_BB_IN(181u,bb_s); body_GLUTInitWindowPosition(x, y); ZZ_BB_OUT(181u,bb_s); }
static inline int body_GLUTCreateWindow(const char *title) { zz_missing(183); return 0; }
static int missing_GLUTCreateWindow(const char *title) { Z bb_s; int bb_r; zz_calls[182]++; zz_last_slot=182; ZZ_BB_IN(182u,bb_s); bb_r=body_GLUTCreateWindow(title); ZZ_BB_OUT(182u,bb_s); return bb_r; }
static inline void body_GLUTMainLoop(void) { zz_missing(184); }
static void missing_GLUTMainLoop(void) { Z bb_s; zz_calls[183]++; zz_last_slot=183; ZZ_BB_IN(183u,bb_s); body_GLUTMainLoop(); ZZ_BB_OUT(183u,bb_s); }
static inline void body_GLUTDisplayFunc(void (*func)(void)) { zz_missing(185); }
static void missing_GLUTDisplayFunc(void (*func)(void)) { Z bb_s; zz_calls[184]++; zz_last_slot=184; ZZ_BB_IN(184u,bb_s); body_GLUTDisplayFunc(func); ZZ_BB_OUT(184u,bb_s); }
static inline void body_GLUTIdleFunc(void (*func)(void)) { zz_missing(186); }
static void missing_GLUTIdleFunc(void (*func)(void)) { Z bb_s; zz_calls[185]++; zz_last_slot=185; ZZ_BB_IN(185u,bb_s); body_GLUTIdleFunc(func); ZZ_BB_OUT(185u,bb_s); }
static inline void body_GLUTKeyboardFunc(void (*func)(unsigned char key, int x, int y)) { zz_missing(187); }
static void missing_GLUTKeyboardFunc(void (*func)(unsigned char key, int x, int y)) { Z bb_s; zz_calls[186]++; zz_last_slot=186; ZZ_BB_IN(186u,bb_s); body_GLUTKeyboardFunc(func); ZZ_BB_OUT(186u,bb_s); }
static inline void body_GLUTReshapeFunc(void (*func)(int width, int height)) { zz_missing(188); }
static void missing_GLUTReshapeFunc(void (*func)(int width, int height)) { Z bb_s; zz_calls[187]++; zz_last_slot=187; ZZ_BB_IN(187u,bb_s); body_GLUTReshapeFunc(func); ZZ_BB_OUT(187u,bb_s); }
static inline void body_GLUTSwapBuffers(void) { zz_missing(189); }
static void missing_GLUTSwapBuffers(void) { Z bb_s; zz_calls[188]++; zz_last_slot=188; ZZ_BB_IN(188u,bb_s); body_GLUTSwapBuffers(); ZZ_BB_OUT(188u,bb_s); }
static inline void body_GLUTPostRedisplay(void) { zz_missing(190); }
static void missing_GLUTPostRedisplay(void) { Z bb_s; zz_calls[189]++; zz_last_slot=189; ZZ_BB_IN(189u,bb_s); body_GLUTPostRedisplay(); ZZ_BB_OUT(189u,bb_s); }
static inline int body_GLUTGet(GLenum state) { zz_missing(191); return 0; }
static int missing_GLUTGet(GLenum state) { Z bb_s; int bb_r; zz_calls[190]++; zz_last_slot=190; ZZ_BB_IN(190u,bb_s); bb_r=body_GLUTGet(state); ZZ_BB_OUT(190u,bb_s); return bb_r; }
static inline void body_GLUTGameModeString(const char *string) { zz_missing(192); }
static void missing_GLUTGameModeString(const char *string) { Z bb_s; zz_calls[191]++; zz_last_slot=191; ZZ_BB_IN(191u,bb_s); body_GLUTGameModeString(string); ZZ_BB_OUT(191u,bb_s); }
static inline int body_GLUTEnterGameMode(void) { zz_missing(193); return 0; }
static int missing_GLUTEnterGameMode(void) { Z bb_s; int bb_r; zz_calls[192]++; zz_last_slot=192; ZZ_BB_IN(192u,bb_s); bb_r=body_GLUTEnterGameMode(); ZZ_BB_OUT(192u,bb_s); return bb_r; }
static inline void body_GLUTLeaveGameMode(void) { zz_missing(194); }
static void missing_GLUTLeaveGameMode(void) { Z bb_s; zz_calls[193]++; zz_last_slot=193; ZZ_BB_IN(193u,bb_s); body_GLUTLeaveGameMode(); ZZ_BB_OUT(193u,bb_s); }
static inline int body_GLUTGameModeGet(GLenum query) { zz_missing(195); return 0; }
static int missing_GLUTGameModeGet(GLenum query) { Z bb_s; int bb_r; zz_calls[194]++; zz_last_slot=194; ZZ_BB_IN(194u,bb_s); bb_r=body_GLUTGameModeGet(query); ZZ_BB_OUT(194u,bb_s); return bb_r; }
static inline void body_GLUTSolidCube(GLdouble size) { zz_missing(196); }
static void missing_GLUTSolidCube(GLdouble size) { Z bb_s; zz_calls[195]++; zz_last_slot=195; ZZ_BB_IN(195u,bb_s); body_GLUTSolidCube(size); ZZ_BB_OUT(195u,bb_s); }
static inline void body_GLUTSolidSphere(GLdouble radius, GLint slices, GLint stacks) { zz_missing(197); }
static void missing_GLUTSolidSphere(GLdouble radius, GLint slices, GLint stacks) { Z bb_s; zz_calls[196]++; zz_last_slot=196; ZZ_BB_IN(196u,bb_s); body_GLUTSolidSphere(radius, slices, stacks); ZZ_BB_OUT(196u,bb_s); }
static inline void body_GLUTSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks) { zz_missing(198); }
static void missing_GLUTSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks) { Z bb_s; zz_calls[197]++; zz_last_slot=197; ZZ_BB_IN(197u,bb_s); body_GLUTSolidCone(base, height, slices, stacks); ZZ_BB_OUT(197u,bb_s); }
static inline void body_GLUTSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings) { zz_missing(199); }
static void missing_GLUTSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings) { Z bb_s; zz_calls[198]++; zz_last_slot=198; ZZ_BB_IN(198u,bb_s); body_GLUTSolidTorus(innerRadius, outerRadius, sides, rings); ZZ_BB_OUT(198u,bb_s); }
static inline void body_GLUTSolidDodecahedron(void) { zz_missing(200); }
static void missing_GLUTSolidDodecahedron(void) { Z bb_s; zz_calls[199]++; zz_last_slot=199; ZZ_BB_IN(199u,bb_s); body_GLUTSolidDodecahedron(); ZZ_BB_OUT(199u,bb_s); }
static inline void body_GLNormalPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer) { Z start_tick,io;  if(!zz_profile){impl_GLNormalPointer(context, type, stride, pointer);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLNormalPointer(context, type, stride, pointer); zz_profile_add(start_tick,io,1); }
static void timed_GLNormalPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer) { Z bb_s; zz_calls[200]++; zz_last_slot=200; ZZ_BB_IN(200u,bb_s); body_GLNormalPointer(context, type, stride, pointer); ZZ_BB_OUT(200u,bb_s); }
static inline void body_GLUQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn) { zz_missing(202); }
static void missing_GLUQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn) { Z bb_s; zz_calls[201]++; zz_last_slot=201; ZZ_BB_IN(201u,bb_s); body_GLUQuadricCallback(q, which, fn); ZZ_BB_OUT(201u,bb_s); }
static inline void body_GLColorMaterial(GLcontext context, GLenum face, GLenum mode) { zz_missing(203); }
static void missing_GLColorMaterial(GLcontext context, GLenum face, GLenum mode) { Z bb_s; zz_calls[202]++; zz_last_slot=202; ZZ_BB_IN(202u,bb_s); body_GLColorMaterial(context, face, mode); ZZ_BB_OUT(202u,bb_s); }
static inline void body_GLTexEnvfv(GLcontext context, GLenum target, GLenum pname, const GLfloat *params) { zz_missing(204); }
static void missing_GLTexEnvfv(GLcontext context, GLenum target, GLenum pname, const GLfloat *params) { Z bb_s; zz_calls[203]++; zz_last_slot=203; ZZ_BB_IN(203u,bb_s); body_GLTexEnvfv(context, target, pname, params); ZZ_BB_OUT(203u,bb_s); }
static inline void body_GLNormal3fv(GLcontext context, GLfloat *n) { Z start_tick,io;  if(!zz_profile){impl_GLNormal3fv(context, n);return;} start_tick=zz_clock();io=zz_transfer_ticks();impl_GLNormal3fv(context, n); zz_profile_add(start_tick,io,1); }
static void timed_GLNormal3fv(GLcontext context, GLfloat *n) { Z bb_s; zz_calls[204]++; zz_last_slot=204; ZZ_BB_IN(204u,bb_s); body_GLNormal3fv(context, n); ZZ_BB_OUT(204u,bb_s); }
const ZZDispatch zz_dispatch = {5, sizeof(ZZDispatch), 2, 0, &zz_current,
timed_GLActiveTextureARB,
timed_GLAlphaFunc,
missing_GLArrayElement,
timed_GLBegin,
timed_GLBindTexture,
timed_GLBlendFunc,
timed_GLClear,
timed_GLClearColor,
timed_GLClearDepth,
timed_GLColor3fv,
timed_GLColor3ubv,
timed_GLColor4f,
timed_GLColor4fv,
timed_GLColor4ub,
timed_GLColor4ubv,
timed_GLColorMask,
timed_GLColorPointer,
missing_GLColorTable,
timed_GLCullFace,
timed_GLDeleteTextures,
timed_GLDepthFunc,
timed_GLDepthMask,
timed_GLDepthRange,
timed_GLDisableClientState,
timed_GLDrawArrays,
timed_GLDrawBuffer,
timed_GLDrawElements,
timed_GLEnableClientState,
timed_GLEnd,
timed_GLFinish,
timed_GLFlush,
timed_GLFogf,
timed_GLFogfv,
timed_GLFrontFace,
timed_GLFrustum,
timed_GLGenTextures,
missing_GLGetBooleanv,
timed_GLGetError,
timed_GLGetFloatv,
timed_GLGetIntegerv,
timed_GLGetString,
timed_GLHint,
missing_GLIsEnabled,
timed_GLLoadIdentity,
timed_GLLoadMatrixd,
timed_GLLoadMatrixf,
timed_GLLockArrays,
timed_GLMatrixMode,
timed_GLMultMatrixd,
timed_GLMultMatrixf,
timed_GLMultiTexCoord2fARB,
timed_GLMultiTexCoord2fvARB,
timed_GLNormal3f,
timed_GLOrtho,
timed_GLPixelStorei,
timed_GLPointSize,
timed_GLPolygonMode,
timed_GLPopMatrix,
timed_GLPushMatrix,
missing_GLReadPixels,
timed_GLRotated,
timed_GLRotatef,
timed_GLRotatefEXT,
timed_GLRotatefEXTs,
timed_GLScaled,
timed_GLScalef,
timed_GLScissor,
timed_GLShadeModel,
timed_GLTexCoord2f,
timed_GLTexCoord2fv,
missing_GLTexCoord4f,
missing_GLTexCoord4fv,
timed_GLTexCoordPointer,
timed_GLTexEnvi,
timed_GLTexGeni,
timed_GLTexImage2D,
timed_GLTexParameteri,
timed_GLTexSubImage2D,
timed_GLTranslated,
timed_GLTranslatef,
missing_GLULookAt,
timed_GLUPerspective,
timed_GLUnlockArrays,
timed_GLVertex2fv,
timed_GLVertex3fv,
timed_GLVertex4f,
timed_GLVertex4fv,
timed_GLVertexPointer,
timed_GLViewport,
counted_MGLClearPointer,
counted_MGLCreateContext,
missing_MGLCreateContextFromID,
counted_MGLDeleteContext,
counted_MGLDrawMultitexBuffer,
counted_MGLEnableSync,
counted_MGLExit,
counted_MGLGetInputWindowHandle,
counted_MGLGetWindowHandle,
counted_MGLIdleFunc,
counted_MGLKeyFunc,
missing_MGLLockBack,
counted_MGLLockDisplay,
counted_MGLLockMode,
counted_MGLMainLoop,
counted_MGLMinTriArea,
missing_MGLMouseFunc,
missing_MGLPrintMatrix,
missing_MGLPrintMatrixStack,
counted_MGLResizeContext,
counted_MGLSetPointer,
counted_MGLSetState,
counted_MGLSetZOffset,
missing_MGLSpecialFunc,
timed_MGLSwitchDisplay,
missing_MGLTexMemStat,
counted_MGLUnlockDisplay,
counted_MGLWriteShotPPM,
counted_mglChooseGuardBand,
counted_mglChooseMtexBufferSize,
counted_mglChooseNumberOfBuffers,
counted_mglChoosePixelDepth,
missing_mglChooseTextureBufferSize,
counted_mglChooseVertexBufferSize,
counted_mglChooseWindowMode,
missing_mglGetSupportedScreenModes,
missing_mglProhibitAlphaFallback,
missing_mglProhibitMipMapping,
counted_mglProposeCloseDesktop,
missing_GLPolygonOffset,
timed_GLClientActiveTextureARB,
missing_GLInterleavedArrays,
missing_GLMultiDrawArrays,
missing_GLBlendEquation,
missing_GLBlendFuncSeparate,
timed_GLIsTexture,
missing_mglChooseZBufferDepth,
missing_GLLineWidth,
timed_GLTexGenfv,
missing_GLCopyTexImage2D,
missing_GLCopyTexSubImage2D,
missing_GLAreTexturesResident,
missing_GLEdgeFlag,
missing_GLEdgeFlagPointer,
missing_GLEdgeFlagv,
missing_GLGetDoublev,
missing_GLGetPointerv,
missing_GLIndexi,
missing_GLIndexiv,
missing_GLIndexPointer,
missing_GLPrioritizeTextures,
missing_GLReadBuffer,
counted_MGLCreateContextFromWindow,
missing_MGLCreateContextFromBitMap,
missing_GLLightf,
missing_GLLightfv,
missing_GLMaterialf,
missing_GLMaterialfv,
missing_GLLightModelf,
missing_GLLightModelfv,
missing_GLGetLightfv,
missing_GLGetMaterialfv,
missing_GLGenLists,
missing_GLDeleteLists,
missing_GLIsList,
missing_GLNewList,
missing_GLEndList,
missing_GLCallList,
missing_GLUBuild2DMipmaps,
missing_GLUErrorString,
missing_GLUNewQuadric,
missing_GLUDeleteQuadric,
missing_GLUQuadricNormals,
missing_GLUQuadricTexture,
missing_GLUQuadricDrawStyle,
missing_GLUQuadricOrientation,
missing_GLUCylinder,
missing_GLUSphere,
missing_GLUDisk,
missing_GLUTInit,
missing_GLUTInitDisplayMode,
missing_GLUTInitWindowSize,
missing_GLUTInitWindowPosition,
missing_GLUTCreateWindow,
missing_GLUTMainLoop,
missing_GLUTDisplayFunc,
missing_GLUTIdleFunc,
missing_GLUTKeyboardFunc,
missing_GLUTReshapeFunc,
missing_GLUTSwapBuffers,
missing_GLUTPostRedisplay,
missing_GLUTGet,
missing_GLUTGameModeString,
missing_GLUTEnterGameMode,
missing_GLUTLeaveGameMode,
missing_GLUTGameModeGet,
missing_GLUTSolidCube,
missing_GLUTSolidSphere,
missing_GLUTSolidCone,
missing_GLUTSolidTorus,
missing_GLUTSolidDodecahedron,
timed_GLNormalPointer,
missing_GLUQuadricCallback,
missing_GLColorMaterial,
missing_GLTexEnvfv,
timed_GLNormal3fv
};
