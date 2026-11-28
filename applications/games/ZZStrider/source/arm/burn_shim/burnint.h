#ifndef ZZS_BURNINT_H
#define ZZS_BURNINT_H
/*
 * burnint.h - ZZStrider replacement for the FB Alpha "burn" framework header.
 *
 * The CPS-1 files in ../cps1 come from FB Alpha 2012 (libretro/fbalpha2012_cps1). They expect a
 * whole multi-game framework behind this header: driver lists, ROM loaders, save states, cheats,
 * sound mixers, a zip frontend. None of that is imported. This header declares only what those
 * files really use, and zzs_burn.cpp implements it on top of the ZZRastan Core1 infrastructure.
 *
 * Original ZZStrider code (interface names follow FB Alpha so that the CPS-1 files compile
 * unchanged). ASCII only.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef __cdecl
#define __cdecl
#endif
#undef  __fastcall
#define __fastcall

typedef unsigned char       UINT8;
typedef signed char         INT8;
typedef unsigned short      UINT16;
typedef signed short        INT16;
typedef unsigned int        UINT32;
typedef signed int          INT32;
__extension__ typedef unsigned long long UINT64;
__extension__ typedef long long          INT64;

/* The Cortex-A9 is little-endian, like the x86 hosts FB Alpha was written for. */
#define BURN_ENDIAN_SWAP_INT8(x)   x
#define BURN_ENDIAN_SWAP_INT16(x)  x
#define BURN_ENDIAN_SWAP_INT32(x)  x
#define BURN_ENDIAN_SWAP_INT64(x)  x

/* --- drawing target: the private INDEX16 frame (384 x 224 palette indices) --- */
extern UINT8  *pBurnDraw;
extern INT32   nBurnPitch;
extern INT32   nBurnBpp;
extern UINT8   nBurnLayer;
extern UINT32  nCurrentFrame;
extern INT32   nMaxPlayers;

inline static INT32 GetCurrentFrame(void) { return (INT32)nCurrentFrame; }

#define PutPix(pPix, c) (*((UINT16 *)(pPix)) = (UINT16)(c))

/* --- memory: bump allocator in the private arena, never freed --- */
UINT8 *BurnMalloc(INT32 size);
void   _BurnFree(void *ptr);
#define BurnFree(x) do { _BurnFree(x); x = NULL; } while (0)

/* --- save states / cheats / hiscores: not part of ZZStrider. Kept as no-ops so that the
       FB Alpha files that mention them (cps_mem.cpp) stay byte-identical to upstream. --- */
struct BurnArea { void *Data; UINT32 nLen; INT32 nAddress; char *szName; };
inline static INT32 BurnAcb(struct BurnArea *pba) { (void)pba; return 0; }
#define ACB_READ         (1 << 0)
#define ACB_WRITE        (1 << 1)
#define ACB_MEMORY_ROM   (1 << 2)
#define ACB_NVRAM        (1 << 3)
#define ACB_MEMCARD      (1 << 4)
#define ACB_MEMORY_RAM   (1 << 5)
#define ACB_DRIVER_DATA  (1 << 6)
#define SCAN_VAR(x)      do { } while (0)

/* --- driver description structures: only named by declarations in cps.h --- */
struct BurnRomInfo   { char szName[100]; UINT32 nLen; UINT32 nCrc; UINT32 nType; };
struct BurnInputInfo { char *szName; UINT8 nType; UINT8 *pVal; char *szInfo; };
struct BurnDIPInfo   { INT32 nInput; UINT8 nFlags; UINT8 nMask; UINT8 nSetting; char *szText; };

/* --- sound routing constants referenced by cps.h declarations --- */
#define BURN_SND_ROUTE_LEFT   1
#define BURN_SND_ROUTE_RIGHT  2
#define BURN_SND_ROUTE_BOTH   (BURN_SND_ROUTE_LEFT | BURN_SND_ROUTE_RIGHT)

#endif /* ZZS_BURNINT_H */
