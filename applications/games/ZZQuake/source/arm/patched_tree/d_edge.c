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
// d_edge.c

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

static int	miplevel;

float		scale_for_mip;
extern int			screenwidth;
int			ubasestep, errorterm, erroradjustup, erroradjustdown;
int			vstartscan;

// FIXME: should go away
extern void			R_RotateBmodel (void);
extern void			R_TransformFrustum (void);

vec3_t		transformed_modelorg;

/*
==============
D_DrawPoly

==============
*/
void D_DrawPoly (void)
{
// this driver takes spans, not polygons
}


/*
=============
D_MipLevelForScale
=============
*/
int D_MipLevelForScale (float scale)
{
	int		lmiplevel;

	if (scale >= d_scalemip[0] )
		lmiplevel = 0;
	else if (scale >= d_scalemip[1] )
		lmiplevel = 1;
	else if (scale >= d_scalemip[2] )
		lmiplevel = 2;
	else
		lmiplevel = 3;

	if (lmiplevel < d_minmip)
		lmiplevel = d_minmip;

	return lmiplevel;
}


/*
==============
D_DrawSolidSurface
==============
*/

// FIXME: clean this up

void D_DrawSolidSurface (surf_t *surf, int color)
{
	espan_t	*span;
	byte	*pdest;
	int		u, u2, pix;
	
	pix = (color<<24) | (color<<16) | (color<<8) | color;
	for (span=surf->spans ; span ; span=span->pnext)
	{
		pdest = (byte *)d_viewbuffer + screenwidth*span->v;
		u = span->u;
		u2 = span->u + span->count - 1;
		((byte *)pdest)[u] = pix;

		if (u2 - u < 8)
		{
			for (u++ ; u <= u2 ; u++)
				((byte *)pdest)[u] = pix;
		}
		else
		{
			for (u++ ; u & 3 ; u++)
				((byte *)pdest)[u] = pix;

			u2 -= 4;
			for ( ; u <= u2 ; u+=4)
				*(int *)((byte *)pdest + u) = pix;
			u2 += 4;
			for ( ; u <= u2 ; u++)
				((byte *)pdest)[u] = pix;
		}
	}
}


/*
==============
D_CalcGradients
==============
*/
void D_CalcGradients (msurface_t *pface)
{
	mplane_t	*pplane;
	float		mipscale;
	vec3_t		p_temp1;
	vec3_t		p_saxis, p_taxis;
	float		t;

	pplane = pface->plane;

	mipscale = 1.0 / (float)(1 << miplevel);

	TransformVector (pface->texinfo->vecs[0], p_saxis);
	TransformVector (pface->texinfo->vecs[1], p_taxis);

	t = xscaleinv * mipscale;
	d_sdivzstepu = p_saxis[0] * t;
	d_tdivzstepu = p_taxis[0] * t;

	t = yscaleinv * mipscale;
	d_sdivzstepv = -p_saxis[1] * t;
	d_tdivzstepv = -p_taxis[1] * t;

	d_sdivzorigin = p_saxis[2] * mipscale - xcenter * d_sdivzstepu -
			ycenter * d_sdivzstepv;
	d_tdivzorigin = p_taxis[2] * mipscale - xcenter * d_tdivzstepu -
			ycenter * d_tdivzstepv;

	VectorScale (transformed_modelorg, mipscale, p_temp1);

	t = 0x10000*mipscale;
	sadjust = ((fixed16_t)(DotProduct (p_temp1, p_saxis) * 0x10000 + 0.5)) -
			((pface->texturemins[0] << 16) >> miplevel)
			+ pface->texinfo->vecs[0][3]*t;
	tadjust = ((fixed16_t)(DotProduct (p_temp1, p_taxis) * 0x10000 + 0.5)) -
			((pface->texturemins[1] << 16) >> miplevel)
			+ pface->texinfo->vecs[1][3]*t;

//
// -1 (-epsilon) so we never wander off the edge of the texture
//
	bbextents = ((pface->extents[0] << 16) >> miplevel) - 1;
	bbextentt = ((pface->extents[1] << 16) >> miplevel) - 1;
}


/*
==============
D_DrawSurfaces
==============
*/
void D_DrawSurfaces (void)
{
	surf_t			*s;
	msurface_t		*pface;
	surfcache_t		*pcurrentcache;
	vec3_t			world_transformed_modelorg;
	vec3_t			local_modelorg;

	currententity = &cl_entities[0];
	TransformVector (modelorg, transformed_modelorg);
	VectorCopy (transformed_modelorg, world_transformed_modelorg);

// TODO: could preset a lot of this at mode set time
	if (r_drawflat.value)
	{
		for (s = &surfaces[1] ; s<surface_p ; s++)
		{
			if (!s->spans)
				continue;

			d_zistepu = s->d_zistepu;
			d_zistepv = s->d_zistepv;
			d_ziorigin = s->d_ziorigin;

			D_DrawSolidSurface (s, (intptr_t)s->data & 0xFF);
			{ ZZQ_S2(3); D_DrawZSpans (s->spans); ZZQ_S2_END(3); }
		}
	}
	else
	{
		for (s = &surfaces[1] ; s<surface_p ; s++)
		{
			if (!s->spans)
				continue;

			r_drawnpolycount++;

			d_zistepu = s->d_zistepu;
			d_zistepv = s->d_zistepv;
			d_ziorigin = s->d_ziorigin;

			if (s->flags & SURF_DRAWSKY)
			{
				ZZQ_S2(4);
				if (!r_skymade)
				{
					R_MakeSky ();
				}

				D_DrawSkyScans8 (s->spans);
				ZZQ_S2_END(4);
				zzq_s2_count(1, s->spans);
				{ ZZQ_S2(3); D_DrawZSpans (s->spans); ZZQ_S2_END(3); }
			}
			else if (s->flags & SURF_DRAWBACKGROUND)
			{
			// set up a gradient for the background surface that places it
			// effectively at infinity distance from the viewpoint
				d_zistepu = 0;
				d_zistepv = 0;
				d_ziorigin = -0.9;

				ZZQ_S2(6);
				D_DrawSolidSurface (s, (int)r_clearcolor.value & 0xFF);
				ZZQ_S2_END(6);
				zzq_s2_count(3, s->spans);
				{ ZZQ_S2(3); D_DrawZSpans (s->spans); ZZQ_S2_END(3); }
			}
			else if (s->flags & SURF_DRAWTURB)
			{
				pface = s->data;
				miplevel = 0;
				cacheblock = (pixel_t *)
						((byte *)pface->texinfo->texture +
						pface->texinfo->texture->offsets[0]);
				cachewidth = 64;

				if (s->insubmodel)
				{
				// FIXME: we don't want to do all this for every polygon!
				// TODO: store once at start of frame
					ZZQ_S2(5);
					currententity = s->entity;	//FIXME: make this passed in to
												// R_RotateBmodel ()
					VectorSubtract (r_origin, currententity->origin,
							local_modelorg);
					TransformVector (local_modelorg, transformed_modelorg);

					R_RotateBmodel ();	// FIXME: don't mess with the frustum,
										// make entity passed in
					ZZQ_S2_END(5);
					zzq_s2_count(4, s->spans);
				}

				ZZQ_S2(1);
				D_CalcGradients (pface);
				ZZQ_S2_END(1);
				ZZQ_S2(4);
				Turbulent8 (s->spans);
				ZZQ_S2_END(4);
				zzq_s2_count(2, s->spans);
				{ ZZQ_S2(3); D_DrawZSpans (s->spans); ZZQ_S2_END(3); }

				if (s->insubmodel)
				{
				//
				// restore the old drawing state
				// FIXME: we don't want to do this every time!
				// TODO: speed up
				//
					ZZQ_S2(5);
					currententity = &cl_entities[0];
					VectorCopy (world_transformed_modelorg,
								transformed_modelorg);
					VectorCopy (base_vpn, vpn);
					VectorCopy (base_vup, vup);
					VectorCopy (base_vright, vright);
					VectorCopy (base_modelorg, modelorg);
					R_TransformFrustum ();
					ZZQ_S2_END(5);
				}
			}
			else
			{
				if (s->insubmodel)
				{
				// FIXME: we don't want to do all this for every polygon!
				// TODO: store once at start of frame
					ZZQ_S2(5);
					currententity = s->entity;	//FIXME: make this passed in to
												// R_RotateBmodel ()
					VectorSubtract (r_origin, currententity->origin, local_modelorg);
					TransformVector (local_modelorg, transformed_modelorg);

					R_RotateBmodel ();	// FIXME: don't mess with the frustum,
										// make entity passed in
					ZZQ_S2_END(5);
					zzq_s2_count(4, s->spans);
				}

				pface = s->data;
				miplevel = D_MipLevelForScale (s->nearzi * scale_for_mip
				* pface->texinfo->mipadjust);

			// FIXME: make this passed in to D_CacheSurface
				ZZQ_S2(0);
				pcurrentcache = D_CacheSurface (pface, miplevel);
				ZZQ_S2_END(0);

				cacheblock = (pixel_t *)pcurrentcache->data;
				cachewidth = pcurrentcache->width;

				ZZQ_S2(1);
				D_CalcGradients (pface);
				ZZQ_S2_END(1);

				ZZQ_S2(2);
				{ extern volatile unsigned int *shared;
				  unsigned int _p = (unsigned int)d_drawspans;
				  shared[SH_DDS_BEFORE_CALL] = _p;
				  if (shared[SH_DDS_EXPECTED] && _p != shared[SH_DDS_EXPECTED])
				      shared[SH_DDS_MISMATCH]++; }
				(*d_drawspans) (s->spans);
				ZZQ_S2_END(2);
				zzq_s2_count(0, s->spans);

				{ ZZQ_S2(3); D_DrawZSpans (s->spans); ZZQ_S2_END(3); }

				if (s->insubmodel)
				{
				//
				// restore the old drawing state
				// FIXME: we don't want to do this every time!
				// TODO: speed up
				//
					ZZQ_S2(5);
					currententity = &cl_entities[0];
					VectorCopy (world_transformed_modelorg,
								transformed_modelorg);
					VectorCopy (base_vpn, vpn);
					VectorCopy (base_vup, vup);
					VectorCopy (base_vright, vright);
					VectorCopy (base_modelorg, modelorg);
					R_TransformFrustum ();
					ZZQ_S2_END(5);
				}
			}
		}
	}
}

