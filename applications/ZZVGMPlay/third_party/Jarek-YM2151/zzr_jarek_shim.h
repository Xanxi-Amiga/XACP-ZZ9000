#ifndef ZZVGM_JAREK_SHIM_H
#define ZZVGM_JAREK_SHIM_H
#include <stdint.h>
typedef uint8_t  UINT8;
typedef int8_t   INT8;
typedef uint16_t UINT16;
typedef int16_t  INT16;
typedef uint32_t UINT32;
typedef int32_t  INT32;
typedef void (*write8_handler)(UINT32 offset, UINT8 data);
#ifndef INLINE
# if defined(__GNUC__)
#  define INLINE static __inline__
# else
#  define INLINE static inline
# endif
#endif
#ifndef logerror
#define logerror(...) ((void)0)
#endif
#endif
