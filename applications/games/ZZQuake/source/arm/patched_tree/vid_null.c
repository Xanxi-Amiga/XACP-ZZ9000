/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// vid_null.c -- null video driver to aid porting efforts

#include "quakedef.h"

/* ---- ZZQuake diagnostic hooks (injected, see zzq_patch_engine.py) */
#include "zzquake_config.h"   /* SH_LG_* diagnostics */
extern void zzq_hunk_fail(const char *name, int raw, int adj,
                          int low, int high, int total);
extern int  zzq_alias_stage;
extern int  zzq_cvarfix_enabled(void);
extern void zzq_cvar_snapshot(int which);
extern void zzq_ftoa(char *dst, int cap, float v);
extern void zzq_ftoa_prec(char *dst, int cap, float v, int prec);
extern void zzq_timedemo_result(int frames, float seconds);
extern void zzq_note_command(const char *text);
extern double zzq_min_frametime(void);
extern void zzq_ph_begin(int p);
extern void zzq_ph_end(int p);
#define ZZQ_PH(p)     zzq_ph_begin(p)
#define ZZQ_PH_END(p) zzq_ph_end(p)
extern void zzq_s2_begin(int p);
extern void zzq_s2_end(int p);
extern void zzq_s2_count(int kind, void *spans);
#ifdef ZZQ_PRODUCTION
/* Production blob: level-2 probes are disabled.
   Level-1 profiling remains available at negligible cost. */
#define ZZQ_S2(p)     ((void)0)
#define ZZQ_S2_END(p) ((void)0)
/* zzq_s2_count remains declared normally and becomes a no-op
   in the production platform implementation. */
#else
#define ZZQ_S2(p)     zzq_s2_begin(p)
#define ZZQ_S2_END(p) zzq_s2_end(p)
#endif
extern void zzq_surfcache_got(int bytes);
/* ---- end ZZQuake hooks ---- */

#include "d_local.h"

#include "quakegeneric.h"

viddef_t	vid;				// global video state

#define	BASEWIDTH	320
#define	BASEHEIGHT	240

byte	vid_buffer[BASEWIDTH*BASEHEIGHT];
short	zbuffer[BASEWIDTH*BASEHEIGHT];
byte	*surfcache;
size_t	surfcache_size;

void	VID_SetPalette (unsigned char *palette)
{
	// quake generic
	QG_SetPalette(palette);
}

void	VID_ShiftPalette (unsigned char *palette)
{
	// quake generic
	QG_SetPalette(palette);
}

void	VID_Init (unsigned char *palette)
{
	vid.width = vid.conwidth = BASEWIDTH;
	vid.height = vid.conheight = BASEHEIGHT;
	/* Warp limits describe the buffer, not the display. */
	vid.maxwarpwidth  = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.aspect = 1.0;
	vid.numpages = 1;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
	vid.buffer = vid.conbuffer = vid_buffer;
	vid.rowbytes = vid.conrowbytes = BASEWIDTH;
	
	d_pzbuffer = zbuffer;

	surfcache_size = D_SurfaceCacheForRes(BASEWIDTH, BASEHEIGHT);
	surfcache = malloc(surfcache_size);
	zzq_surfcache_got (surfcache ? (int)surfcache_size : 0);
	if (!surfcache)
		Sys_Error ("ZZQ: surface cache malloc failed (%d bytes)",
		           (int)surfcache_size);
	D_InitCaches (surfcache, surfcache_size);

	// quake generic
	QG_Init();
}

void	VID_Shutdown (void)
{
		free(surfcache);
}

void	VID_Update (vrect_t *rects)
{
	// quake generic
	QG_DrawFrame(vid.buffer);
}

/*
================
D_BeginDirectRect
================
*/
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
}


/*
================
D_EndDirectRect
================
*/
void D_EndDirectRect (int x, int y, int width, int height)
{
}


