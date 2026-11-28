#include "cps.h"

/* CPS (palette)
 *
 * ZZStrider adaptation of FB Alpha 2012 cps_pal.cpp. What changed and why:
 *
 *  1. INDEX16 pipeline (kept from ZZRastan). FB Alpha stores ready-to-plot host colours in
 *     CpsPal[] and its tile plotters write them straight to the screen. Here CpsPal[] is an
 *     IDENTITY table (CpsPal[i] == i): the unmodified plotters therefore write 12-bit palette
 *     INDICES into the private INDEX16 frame, and the real colours live in ZzsPalARGB[], used
 *     once per rendered frame by the final INDEX16 -> Picasso96 conversion (zzs_video.c).
 *     The index layout is FB Alpha's: entry (0x200 * page) + (offset ^ 15).
 *
 *  2. Page skipping follows MAME cps1_v.cpp (cps1_build_palette) and FBNeo: a page disabled in
 *     the CPS-B palette control register is skipped in gfxram, "but only if we have already
 *     copied at least one page". FB Alpha 2012 never skipped.
 *
 * The brightness formula is unchanged (identical in FB Alpha, FBNeo and MAME).
 */

UINT32* CpsPal = NULL;					/* IDENTITY: palette index -> palette index */
UINT32* ZzsPalARGB = NULL;				/* palette index -> 0xFFRRGGBB */
UINT32  ZzsPalUpdates = 0;				/* diagnostics */
UINT32  ZzsPalNonBlack = 0;
UINT32  ZzsProfPalCycles = 0;			/* PMU cycles spent rebuilding the colour table */
extern "C" UINT32 zzs_pmu_cycles(void);
INT32 nCpsPalCtrlReg;
INT32 bCpsUpdatePalEveryFrame = 0;	/* Some of the hacks need this as they don't write to CpsReg 0x0a */

#define BIT(x,n) 		(((x)>>(n))&1)

INT32 CpsPalInit()
{
   INT32 i;
   INT32 nLen = 0xc00 * sizeof(UINT32);
   CpsPal = (UINT32*)BurnMalloc(nLen);
   if (!CpsPal)
      return 1;
   /* 0x1000 entries: zzs_video.c masks the index to 12 bits, the top quarter stays black */
   ZzsPalARGB = (UINT32*)BurnMalloc(0x1000 * sizeof(UINT32));
   if (!ZzsPalARGB)
      return 1;

   for (i = 0; i < 0xc00; i++)
      CpsPal[i] = (UINT32)i;
   for (i = 0; i < 0x1000; i++)
      ZzsPalARGB[i] = 0xFF000000u;		/* MAME: palette_device::BLACK */
   ZzsPalUpdates = 0;
   ZzsPalNonBlack = 0;

   return 0;
}

INT32 CpsPalExit()
{
	BurnFree(CpsPal);
	BurnFree(ZzsPalARGB);
	return 0;
}

/* Update the colour table with the new palette at pNewPal (length 0xc00 words) */
INT32 CpsPalUpdate(UINT8* pNewPal)
{
   INT32 nPage;
   INT32 nCtrl = CpsReg[nCpsPalCtrlReg];
   UINT16 *PaletteBase = (UINT16*)pNewPal;
   UINT16 *PaletteRAM = PaletteBase;
   UINT32 nNonBlack = 0;
   UINT32 t0 = zzs_pmu_cycles();

   for (nPage = 0; nPage < 6; nPage++)
   {
      if (BIT(nCtrl, nPage))
      {
         INT32 Offset;
         for (Offset = 0; Offset < 0x200; ++Offset)
         {
            INT32 Palette = BURN_ENDIAN_SWAP_INT16(*(PaletteRAM++));
            INT32 Bright = 0x0f + ((Palette >> 12) << 1);
            INT32      r = ((Palette >> 8) & 0x0f) * 0x11 * Bright / 0x2d;
            INT32      g = ((Palette >> 4) & 0x0f) * 0x11 * Bright / 0x2d;
            INT32      b = ((Palette >> 0) & 0x0f) * 0x11 * Bright / 0x2d;

            ZzsPalARGB[(0x200 * nPage) + (Offset ^ 15)] = 0xFF000000u | ((UINT32)r << 16) | ((UINT32)g << 8) | (UINT32)b;
            if (r | g | b) nNonBlack++;
         }
      }
      else
      {
         /* MAME/FBNeo: skip the page in gfxram, but only once a page has been copied */
         if (PaletteRAM != PaletteBase)
            PaletteRAM += 0x200;
      }
   }

   ZzsPalUpdates++;
   ZzsPalNonBlack = nNonBlack;
   ZzsProfPalCycles += zzs_pmu_cycles() - t0;

   return 0;
}
