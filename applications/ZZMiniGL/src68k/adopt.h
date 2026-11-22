/* adopt.h - A8.27: MGLCreateContextFromWindow.  The geometry of a window the application opened itself
 * (SDL 1.2's MiniGL backend opens its screen and its window, then hands the window over), read from the
 * Intuition structures with no call to the system.  Included by platform.c and by the host harness, so
 * that the same arithmetic runs under the tests.
 *
 * struct Window (intuition/intuition.h):  +4 LeftEdge, +6 TopEdge, +8 Width, +10 Height (WORD),
 *   +24 Flags (ULONG), +46 WScreen, +50 RPort, +54 BorderLeft, +55 BorderTop, +56 BorderRight,
 *   +57 BorderBottom (BYTE), +86 UserPort.
 * struct Screen (intuition/screens.h):    +12 Width, +14 Height (WORD), +20 Flags (UWORD),
 *   +84 RastPort (its BitMap pointer at +88).
 * struct RastPort (graphics/rastport.h):  +0 Layer, +4 BitMap.
 *
 * The picture is the inner area of the window (without its borders).  Two ways to show it:
 *   direct  the frame is copied by Core1 straight into the screen's bitmap at (x, y).  Only for a window
 *           of a private custom screen (nothing else is drawn there), drawn into that screen's own
 *           bitmap, entirely inside it, at an even x (the copy starts on a 32-bit boundary);
 *   slow    p96WritePixelArray through the window's RastPort at (rx, ry), clipped by its layer: a window
 *           on the Workbench or on a public screen, or any case the direct path does not accept.
 */
#ifndef ZZ_ADOPT_H
#define ZZ_ADOPT_H
#define ZZ_WFLG_GIMMEZEROZERO 0x00000400u
#define ZZ_SCREENTYPE 0x000Fu
#define ZZ_CUSTOMSCREEN 0x000Fu
typedef struct ZZAdopt {
 Z w,h;            /* inner size: the size of the context */
 Z x,y;            /* inner area inside the screen's bitmap (direct path) */
 Z rx,ry;          /* inner area inside the window's RastPort (slow path) */
 Z direct;         /* 1: the direct path may be tried */
 Z why;            /* why not direct: 1 not a private custom screen, 2 another bitmap, 3 outside the screen, 4 odd x */
 Ptr screen,rastport,bitmap;
} ZZAdopt;
/* 0: not a usable window (null pointers, empty or absurd inner area) */
static int zz_adopt_geometry(const void *window,ZZAdopt *g){
 const zu8 *win=(const zu8 *)window,*scr,*rp;
 int left,top,ww,wh,bl,bt,br,bb,iw,ih,sw,sh,x,y;
 Z flags;Ptr sbm,wbm;
 if(!win||!g)return 0;
 scr=*(const zu8 *const *)(const void *)(win+46);rp=*(const zu8 *const *)(const void *)(win+50);
 if(!scr||!rp)return 0;
 left=*(const short *)(const void *)(win+4);top=*(const short *)(const void *)(win+6);
 ww=*(const short *)(const void *)(win+8);wh=*(const short *)(const void *)(win+10);
 flags=*(const Z *)(const void *)(win+24);
 bl=(signed char)win[54];bt=(signed char)win[55];br=(signed char)win[56];bb=(signed char)win[57];
 if(bl<0||bt<0||br<0||bb<0)return 0;
 iw=ww-bl-br;ih=wh-bt-bb;
 if(iw<1||ih<1||iw>4096||ih>4096)return 0;
 sw=*(const short *)(const void *)(scr+12);sh=*(const short *)(const void *)(scr+14);
 x=left+bl;y=top+bt;
 g->w=(Z)iw;g->h=(Z)ih;
 g->rx=(flags&ZZ_WFLG_GIMMEZEROZERO)?0u:(Z)bl;g->ry=(flags&ZZ_WFLG_GIMMEZEROZERO)?0u:(Z)bt;
 g->screen=(Ptr)(unsigned long)scr;g->rastport=(Ptr)(unsigned long)rp;
 sbm=*(Ptr const *)(const void *)(scr+88);wbm=*(Ptr const *)(const void *)(rp+4);
 g->bitmap=sbm;
 g->direct=0;g->x=g->y=0;
 if(((Z)*(const unsigned short *)(const void *)(scr+20)&ZZ_SCREENTYPE)!=ZZ_CUSTOMSCREEN)g->why=1;
 else if(!sbm||sbm!=wbm)g->why=2;
 else if(x<0||y<0||x+iw>sw||y+ih>sh)g->why=3;
 else if(x&1)g->why=4;
 else{g->why=0;g->direct=1;g->x=(Z)x;g->y=(Z)y;}
 return 1;
}
/* direct path: the address of the picture's first pixel, from the address of the bitmap's first pixel and
   its row length in bytes (16-bit pixels).  0: the rows are too short for the window (not that bitmap) */
static int zz_adopt_target(const ZZAdopt *g,Z width,Z row,Z *address){
 if(!g->direct||row<(g->x+width)*2u)return 0;
 *address+=g->y*row+g->x*2u;
 return 1;
}
#endif
