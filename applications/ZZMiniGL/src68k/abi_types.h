/* ABI-facing type widths from MiniGL 29.1. Pointer-only SDK records are opaque. */
typedef unsigned long ULONG;
typedef void GLvoid;
typedef unsigned char GLubyte;
typedef int GLint;typedef unsigned int GLuint,GLenum,GLboolean;
typedef unsigned long GLbitfield;typedef long GLsizei;
typedef float GLfloat,GLclampf;typedef double GLdouble,GLclampd;
typedef struct ZZContext *GLcontext;
typedef struct MGLLockInfo MGLLockInfo;
typedef struct GLUquadricObj GLUquadricObj;
struct Window;struct BitMap;
typedef void (*IdleFn)(void);
typedef void (*KeyHandlerFn)(char);
typedef void (*MouseHandlerFn)(int,int,GLbitfield);
typedef void (*SpecialHandlerFn)(unsigned int);
typedef struct MGLScreenMode MGLScreenMode;
typedef GLboolean (*MGLScreenModeCallback)(MGLScreenMode *);
typedef void (*MGLUfuncptr)();
