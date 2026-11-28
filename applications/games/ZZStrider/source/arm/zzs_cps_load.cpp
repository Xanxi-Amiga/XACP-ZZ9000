/*
 * zzs_cps_load.cpp - CPS-1 board data: replaces FB Alpha's cps.cpp for ZZStrider.
 *
 * FB Alpha's cps.cpp is mostly ROM loaders for some sixty games and bootlegs, all built on a
 * zip frontend. ZZStrider receives its ROMs through zzs_rom.c, which already decodes them into
 * the layout CpsInit() would have produced. What remains here is the set of board globals the
 * engine expects, and CpsInit() reduced to its bookkeeping.
 *
 * Derived from FB Alpha 2012 cps.cpp (globals, graphics mask computation).
 * ASCII only.
 */
#include "cps.h"
#include "zzs.h"
#include "zzs_game.h"

INT32 Cps = 0;							// 1 = CPS1, 2 = CPS2, 3 = CPS Changer
INT32 Cps1Qs = 0;
INT32 Cps1DisablePSnd = 0;				// Disables the Z80 as well
INT32 Cps2DisableQSnd = 0;				// Disables the Z80 as well

INT32 nCPS68KClockspeed = 0;
INT32 nCpsCycles = 0;					// 68K Cycles per frame
INT32 nCpsZ80Cycles;

UINT8 *CpsGfx =NULL; UINT32 nCpsGfxLen =0; // All the graphics
UINT8 *CpsRom =NULL; UINT32 nCpsRomLen =0; // Program Rom (as in rom)
UINT8 *CpsCode=NULL; UINT32 nCpsCodeLen=0; // Program Rom (decrypted)
UINT8 *CpsZRom=NULL; UINT32 nCpsZRomLen=0; // Z80 Roms
INT8 *CpsQSam=NULL; UINT32 nCpsQSamLen=0;	// QSound Sample Roms
UINT8 *CpsAd  =NULL; UINT32 nCpsAdLen  =0; // ADPCM Data
UINT8 *CpsStar=NULL;
UINT32 nCpsGfxScroll[4]={0,0,0,0}; // Offset to Scroll tiles
UINT32 nCpsGfxMask=0;	  // Address mask

INT32 CpsInit(void)
{
	INT32 i;

	Cps = 1;
	Cps1Qs = 0;
	Cps1DisablePSnd = 0;

	/* Exact cycles per frame from the crystals (FB Alpha derives them from a rounded FPS). */
	nCPS68KClockspeed = ZZS_M68K_CLOCK;
	nCpsCycles = ZZS_M68K_CYCLES_FRAME;
	nCpsZ80Cycles = (INT32)(((UINT64)ZZS_Z80_CLOCK * ZZS_FRAME_PIXELS) / ZZS_PIXEL_CLOCK);

	/* The decoded ROM store filled by zzs_rom.c. FB Alpha adds 0x2000 to the graphics length
	   for games with a star field (GameHasStars) and keeps the star bytes at its very end. */
	CpsGfx = g_zzs_gfx;  nCpsGfxLen = ZZS_GFX_SIZE + (ZZS_HAS_STARS ? ZZS_STAR_SIZE : 0);
	CpsRom = g_zzs_prg;  nCpsRomLen = ZZS_PRG_SIZE;
	CpsCode = NULL;      nCpsCodeLen = 0;
	CpsZRom = g_zzs_z80; nCpsZRomLen = ZZS_Z80_SIZE;
	CpsQSam = NULL;      nCpsQSamLen = 0;
	CpsAd = g_zzs_oki;   nCpsAdLen = ZZS_OKI_SIZE;
	CpsStar = ZZS_HAS_STARS ? (CpsGfx + nCpsGfxLen - ZZS_STAR_SIZE) : NULL;

	/* Create Gfx addr mask */
	for (i = 0; i < 31; i++)
	{
		if ((1 << i) >= (INT32)nCpsGfxLen)
			break;
	}
	nCpsGfxMask = (1 << i) - 1;

	/* Offset to Scroll tiles (CPS-1: none) */
	nCpsGfxScroll[1] = nCpsGfxScroll[2] = nCpsGfxScroll[3] = 0;

	CpsReset = 0; Cpi01A = Cpi01C = Cpi01E = 0;		  /* blank other inputs */

	/* B-board: CPS-B id (register layout) and graphics mapper PAL */
	SetCpsBId(ZZS_CPS_B_ID, ZZS_HAS_STARS);
	SetGfxMapper(ZZS_GFX_MAPPER);

	return 0;
}
