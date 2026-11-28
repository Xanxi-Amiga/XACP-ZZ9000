/*
 * zzr_jarek_shim.h - minimal bare-metal shim for the Jarek Burczynski YM2151 core.
 * Replaces FBNeo's driver.h/state.h. Only types and no-op savestate macros; no synthesis
 * equation is touched.
 * ASCII only.
 */
#ifndef ZZR_JAREK_SHIM_H
#define ZZR_JAREK_SHIM_H

typedef unsigned char  UINT8;
typedef signed char    INT8;
typedef unsigned short UINT16;
typedef signed short   INT16;
typedef unsigned int   UINT32;
typedef signed int     INT32;
/* FBNeo's port handler takes (offset, data). */
typedef void (*write8_handler)(UINT32 offset, UINT8 data);

/* FBNeo/MAME inline macro. */
#ifndef INLINE
#define INLINE static __inline__
#endif

/* Savestate registration is FBNeo/MAME infrastructure we do not need. */
#define state_save_register_UINT8(a,b,c,d,e)   do { } while (0)
#define state_save_register_INT8(a,b,c,d,e)    do { } while (0)
#define state_save_register_UINT16(a,b,c,d,e)  do { } while (0)
#define state_save_register_INT16(a,b,c,d,e)   do { } while (0)
#define state_save_register_UINT32(a,b,c,d,e)  do { } while (0)
#define state_save_register_INT32(a,b,c,d,e)   do { } while (0)
#define state_save_register_double(a,b,c,d,e)  do { } while (0)
#define state_save_register_func_postload(f)   do { } while (0)

/* Logging and FBNeo savestate helpers are not present on bare metal. */
#define logerror(...)        do { } while (0)
#define SCAN_VAR(x)          do { } while (0)
#define ACB_DRIVER_DATA      0

#endif
