#include "cps.h"
/* CPS - Draw
 *
 * ZZStrider adaptation of FB Alpha 2012 cps_draw.cpp. Every change is marked "ZZS:".
 *
 *  ZZS-1  Cps1Layers read Draw[i+1] with i == 3 when the sprites are the bottom layer: an
 *         out-of-bounds read of Draw[4]. Guarded as in FBNeo (i + 1 < 4).
 *  ZZS-2  Star field rewritten after MAME cps1_v.cpp (cps1_render_stars), the hardware
 *         reference: a star exists when (col & 0x1f) != 0x0f, its colour cycles with period
 *         15 or 16 depending on bit 7, the 0xa00 field is drawn before the 0x800 one, and the
 *         colour is a DIRECT hardware palette entry (FB Alpha indexed its (offset ^ 15)
 *         table with it, so its stars cycled in the opposite direction to MAME's).
 *  ZZS-3  Scroll 2 / Scroll 3 are displayed only if bits 2 / 3 of the CPS-A video control
 *         register are set, as in MAME (cps1_get_video_base). FB Alpha only looked at the
 *         CPS-B layer control register.
 *  ZZS-4  Instrumentation only: sprite time, star count, layer order.
 */

extern "C" UINT32 zzs_pmu_cycles(void);		/* ZZS-4 */
UINT32 ZzsProfObjCycles = 0;					/* ZZS-4: PMU cycles spent in the sprite pass */
UINT32 ZzsStarsDrawn = 0;						/* ZZS-4 */
UINT32 ZzsLayerOrder = 0;						/* ZZS-4: Draw[0..3], 4 bits each, 0xF = none */
UINT32 ZzsDrawMask = 0;							/* ZZS-4 */

UINT8 CpsRecalcPal = 0;			/* Flag - If it is 1, recalc the whole palette */

INT32 nStartline, nEndline;
INT32 nRasterline[MAX_RASTER + 2];

INT32 nCpsLcReg = 0;						/* Address of layer controller register */
INT32 CpsLayEn[6] = {0, 0, 0, 0, 0, 0};	/* bits for layer enable */
INT32 MaskAddr[4] = {0, 0, 0, 0};

INT32 CpsLayer1XOffs = 0;
INT32 CpsLayer2XOffs = 0;
INT32 CpsLayer3XOffs = 0;
INT32 CpsLayer1YOffs = 0;
INT32 CpsLayer2YOffs = 0;
INT32 CpsLayer3YOffs = 0;

INT32 Cps1DisableBgHi = 0;
INT32 CpsDisableRowScroll = 0;

INT32 Cps1OverrideLayers = 0;
INT32 nCps1Layers[4] = { -1, -1, -1, -1 };
INT32 nCps1LayerOffs[3] = { -1, -1, -1 };

static void Cps1Layers(void);

typedef INT32  (*CpsObjDrawDoFn)(INT32,INT32);
typedef INT32  (*CpsScrXDrawDoFn)(UINT8 *,INT32,INT32);
typedef void (*CpsLayersDoFn)(void);
typedef INT32  (*CpsrPrepareDoFn)(void);
typedef INT32  (*CpsrRenderDoFn)(void);

CpsObjDrawDoFn  CpsObjDrawDoX;
CpsScrXDrawDoFn CpsScr1DrawDoX;
CpsScrXDrawDoFn CpsScr3DrawDoX;
CpsLayersDoFn   CpsLayersDoX;
CpsrPrepareDoFn CpsrPrepareDoX;
CpsrRenderDoFn  CpsrRenderDoX;

void DrawFnInit(void)
{
   CpsLayersDoX   = Cps1Layers;
   CpsScr1DrawDoX = Cps1Scr1Draw;
   CpsScr3DrawDoX = Cps1Scr3Draw;
   CpsObjDrawDoX  = Cps1ObjDraw;
   CpsrPrepareDoX = Cps1rPrepare;
   CpsrRenderDoX  = Cps1rRender;
}

static INT32 DrawScroll1(INT32 i)
{
	/* Draw Scroll 1 */
	INT32 nOff, nScrX, nScrY;
	UINT8 *Find;

	nOff = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[i] + 0x02)));
	if (Cps1OverrideLayers && nCps1LayerOffs[0] != -1)
		nOff = BURN_ENDIAN_SWAP_INT16(nCps1LayerOffs[0]);

	/* Get scroll coordinates */
	nScrX = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[i] + 0x0c))); /* Scroll 1 X */
	nScrY = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[i] + 0x0e))); /* Scroll 1 Y */

	nScrX += 0x40;

	nScrX += CpsLayer1XOffs;
	nScrY += 0x10;
	nScrY += CpsLayer1YOffs;
	nOff <<= 8;
	nOff &= 0xffc000;
	Find = CpsFindGfxRam(nOff, 0x4000);
	if (!Find)
		return 1;
	CpsScr1DrawDoX(Find, nScrX, nScrY);
	return 0;
}

static INT32 DrawScroll2Init(INT32 i)
{
	/* Draw Scroll 2 */
	INT32 nScr2Off; INT32 n;

	nScr2Off = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[i] + 0x04)));
	if (Cps1OverrideLayers && nCps1LayerOffs[1] != -1)
		nScr2Off = BURN_ENDIAN_SWAP_INT16(nCps1LayerOffs[1]);

	/* Get scroll coordinates */
	nCpsrScrX= BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[i] + 0x10))); /* Scroll 2 X */
	nCpsrScrY= BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[i] + 0x12))); /* Scroll 2 Ytess */

	/* Get row scroll information */
	n = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[i] + 0x22)));

	nScr2Off <<= 8;

	nCpsrScrX += 0x40;

	nCpsrScrX += CpsLayer2XOffs;
	nCpsrScrX &= 0x03FF;

	nCpsrScrY += 0x10;
	nCpsrScrY += CpsLayer2YOffs;
	nCpsrScrY &= 0x03FF;

	nScr2Off &= 0xFFC000;
	CpsrBase = CpsFindGfxRam(nScr2Off, 0x4000);
	if (!CpsrBase)
		return 1;

	CpsrRows = NULL;

	if ((n & 1) && !CpsDisableRowScroll)
   {
      /* Find row scroll table: */
      INT32 nTab = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[i] + 0x08)));
      INT32 nStart = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[i] + 0x20)));

      nTab <<= 8;
      nTab &= 0xFFF800; /* Vampire - Row scroll effect in VS screen background */

      CpsrRows = (UINT16 *)CpsFindGfxRam(nTab, 0x0800);

      /* Find start offset */
      nCpsrRowStart = nStart + 16;
   }

	CpsrPrepareDoX();
	return 0;
}

inline static INT32 DrawScroll2Exit(void)
{
	CpsrBase = NULL;
	nCpsrScrX = 0;
	nCpsrScrY = 0;
	CpsrRows = NULL;
	return 0;
}

inline static INT32 DrawScroll2Do(void)
{
	if (!CpsrBase)
		return 1;
	CpsrRenderDoX();
	return 0;
}

static INT32 DrawScroll3(INT32 i)
{
	/* Draw Scroll 3 */
	INT32 nScrX, nScrY;
	UINT8 *Find;
	INT32 nOff = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[i] + 0x06)));

	if (Cps1OverrideLayers && nCps1LayerOffs[2] != -1)
		nOff = BURN_ENDIAN_SWAP_INT16(nCps1LayerOffs[2]);

	/* Get scroll coordinates */
	nScrX = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[i] + 0x14))); /* Scroll 3 X */
	nScrY = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[i] + 0x16))); /* Scroll 3 Y */

	nScrX += 0x40;

	nScrX += CpsLayer3XOffs;
	nScrY += 0x10;
	nScrY += CpsLayer3YOffs;

	nOff <<= 8;
	nOff &= 0xffc000;
	Find=CpsFindGfxRam(nOff, 0x4000);
	if (!Find)
		return 1;
	CpsScr3DrawDoX(Find, nScrX, nScrY);
	return 0;
}

/* ZZS-2: star field after MAME cps1_render_stars(). nLayer 0 = CPS-A "stars 1" registers
 * (0x18/0x1a), ROM byte 8*n, palette 0x800; nLayer 1 = "stars 2" (0x1c/0x1e), ROM byte 8*n+4,
 * palette 0xa00. CpsStar holds those two byte streams (see zzs_cps_load.cpp). */
static INT32 DrawStar(INT32 nLayer)
{
	INT32 nStar, nStarXPos, nStarYPos, nStarColour, nCnt;
	UINT8* pStar = CpsStar + (nLayer << 12);
	UINT16* pDraw = (UINT16*)pBurnDraw;
	INT32 nScrX = BURN_ENDIAN_SWAP_INT16(*((UINT16*)(CpsSaveReg[0] + 0x18 + (nLayer << 2))));
	INT32 nScrY = BURN_ENDIAN_SWAP_INT16(*((UINT16*)(CpsSaveReg[0] + 0x1A + (nLayer << 2))));
	INT32 nFrame16 = GetCurrentFrame() >> 4;

	if (!CpsStar)
		return 1;

	for (nStar = 0; nStar < 0x1000; nStar++)
   {
      nStarColour = pStar[nStar];

      if ((nStarColour & 0x1F) != 0x0F)
      {
         nStarXPos = (((nStar >> 8) << 5) - nScrX + (nStarColour & 0x1F) - 64) & 0x01FF;
         nStarYPos = ((nStar & 0xFF) - nScrY - 16) & 0xFF;

         if (nStarXPos < 384 && nStarYPos < 224)
         {
            nCnt = nFrame16 % ((nStarColour & 0x80) ? 15 : 16);
            nStarColour = ((nStarColour & 0xE0) >> 1) + nCnt;
            /* hardware entry 0x800 + layer*0x200 + colour, in FB Alpha's (offset ^ 15) layout */
            pDraw[(384 * nStarYPos) + nStarXPos] = (UINT16)(0x0800 + (nLayer << 9) + (nStarColour ^ 15));
            ZzsStarsDrawn++;
         }
      }
   }

	return 0;
}

static void Cps1Layers(void)
{
   INT32 Draw[4]={-1,-1,-1,-1};
   INT32 i=0;
   INT32 nDrawMask=1; /* Sprites always on */
   INT32 LayerCont = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[0] + nCpsLcReg)));
   /* Get correct bits from Layer Controller */
   if (LayerCont & CpsLayEn[1])
      nDrawMask|=2;
   if (LayerCont & CpsLayEn[2])
      nDrawMask|=4;
   if (LayerCont & CpsLayEn[3])
      nDrawMask|=8;
   /* ZZS-3: MAME enables scroll 2 / scroll 3 only with video control bits 2 / 3 */
   {
      INT32 VideoCont = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsSaveReg[0] + 0x22)));
      if (!(VideoCont & 0x04))
         nDrawMask &= ~4;
      if (!(VideoCont & 0x08))
         nDrawMask &= ~8;
   }
   nDrawMask&=nBurnLayer;   /* User choice of layers to display */

   /* Layer control: */
   Draw[0]=(LayerCont>>12)&3; /* top layer */
   Draw[1]=(LayerCont>>10)&3;
   Draw[2]=(LayerCont>> 8)&3;
   Draw[3]=(LayerCont>> 6)&3; /* bottom layer (most covered up) */

   if (Cps1OverrideLayers)
   {
      nDrawMask = 1;
      Draw[0] = nCps1Layers[0];
      Draw[1] = nCps1Layers[1];
      Draw[2] = nCps1Layers[2];
      Draw[3] = nCps1Layers[3];
      if (Draw[1] != -1)
         nDrawMask |= 2;
      if (Draw[2] != -1)
         nDrawMask |= 4;
      if (Draw[3] != -1)
         nDrawMask |= 8;
      nDrawMask &= nBurnLayer;
   }

   /* Check for repeated layers and if there are any, the lower layer is omitted */
#define CRP(a,b) if (Draw[a]==Draw[b]) Draw[b]=-1;
   CRP(0,1) CRP(0,2) CRP(0,3) CRP(1,2) CRP(1,3) CRP(2,3)
#undef CRP

   /* ZZS-4 */
   ZzsLayerOrder = ((UINT32)(Draw[0] & 15)) | ((UINT32)(Draw[1] & 15) << 4) | ((UINT32)(Draw[2] & 15) << 8) | ((UINT32)(Draw[3] & 15) << 12);
   ZzsDrawMask = (UINT32)nDrawMask;
   ZzsStarsDrawn = 0;

   /* ZZS-2: MAME order - the 0xa00 field (stars 2) first, then the 0x800 field (stars 1) */
      for (i = 1; i >= 0; i--)
      {
         if (LayerCont & CpsLayEn[4 + i])
            DrawStar(i);
      }

   /* prepare layer 2 */
   DrawScroll2Init(0);

   /* draw layers, bottom -> top */
   for (i=3;i>=0;i--)
   {
      INT32 n=Draw[i]; /* Find out which layer to draw */

      if (n==0)
      {
         if (nDrawMask & 1)
         {
            UINT32 t0 = zzs_pmu_cycles();			/* ZZS-4 */
            CpsObjDrawDoX(0,7);
            ZzsProfObjCycles += zzs_pmu_cycles() - t0;
         }

         if (!Cps1DisableBgHi && i + 1 < 4)			/* ZZS-1 */
         {
            nBgHi=1;
            switch (Draw[i+1])
            {
               case 1:
                  if (nDrawMask & 2)
                     DrawScroll1(0);
                  break;
               case 2:
                  if (nDrawMask & 4)
                     DrawScroll2Do();
                  break;
               case 3:
                  if (nDrawMask & 8)
                     DrawScroll3(0);
                  break;
            }
            nBgHi=0;
         }
      }

      /* Then Draw the scroll layer on top */
      switch (n)
      {
         case 1:
            if (nDrawMask & 2)
               DrawScroll1(0);
            break;
         case 2:
            if (nDrawMask & 4)
               DrawScroll2Do();
            break;
         case 3:
            if (nDrawMask & 8)
               DrawScroll3(0);
            break;
      }
   }

   DrawScroll2Exit();
}

void CpsClearScreen(void)
{
   INT32 i;
   UINT32* pClear = (UINT32*)pBurnDraw;
   UINT32 nColour = CpsPal[0xbff ^ 15] | CpsPal[0xbff ^ 15] << 16;
   for (i = 0; i < 384 * 224 / 16; i++)
   {
      *pClear++ = nColour;
      *pClear++ = nColour;
      *pClear++ = nColour;
      *pClear++ = nColour;
      *pClear++ = nColour;
      *pClear++ = nColour;
      *pClear++ = nColour;
      *pClear++ = nColour;
   }
}

static void DoDraw(INT32 Recalc)
{
	CtvReady(); /* Point to correct tile drawing functions */

	if (bCpsUpdatePalEveryFrame)
      GetPalette(0, 6);
	if (Recalc || bCpsUpdatePalEveryFrame)
      CpsPalUpdate(CpsSavePal);		/* recalc whole palette if needed */
	
	CpsClearScreen();

	CpsLayersDoX();
}

INT32 CpsDraw(void)
{
	DoDraw(CpsRecalcPal);

	CpsRecalcPal = 0;
	return 0;
}

INT32 CpsRedraw(void)
{
	DoDraw(1);

	CpsRecalcPal = 0;
	return 0;
}
