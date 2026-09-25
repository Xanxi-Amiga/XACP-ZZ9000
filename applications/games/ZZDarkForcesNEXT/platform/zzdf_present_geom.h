 




































#ifndef ZZDF_PRESENT_GEOM_H
#define ZZDF_PRESENT_GEOM_H

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Nothing in the mode table can ask for more than 2x (the widest
   published area is 640 and the narrowest engine image is 320).
   The cap is here so that a future mode cannot silently turn a
   tiny image into a wall of 8x8 blocks. */
#define ZZDF_PRESENT_MAX_SCALE 4u

typedef struct
{
	unsigned scale;      /* integer magnification, >= 1            */
	unsigned dw, dh;     /* presented size, always <= out          */
	unsigned sw, sh;     /* source pixels actually consumed        */
	unsigned ox, oy;     /* top-left of the image inside the area  */
	int      border;     /* non-zero when the image does not fill  */
} zzdf_present_geom;

static void zzdf_present_geom_compute(unsigned ew, unsigned eh,
                                      unsigned outW, unsigned outH,
                                      zzdf_present_geom *g)
{
	unsigned scale = 1u, dw, dh;

	g->scale = 1u; g->dw = g->dh = 0u; g->sw = g->sh = 0u;
	g->ox = g->oy = 0u; g->border = 0;
	if (!ew || !eh || !outW || !outH) { return; }

	if (ew <= outW && eh <= outH)
	{
		unsigned sx = outW / ew;
		unsigned sy = outH / eh;
		scale = (sx < sy) ? sx : sy;
		if (scale < 1u) { scale = 1u; }
		if (scale > ZZDF_PRESENT_MAX_SCALE) { scale = ZZDF_PRESENT_MAX_SCALE; }
	}

	dw = ew * scale;
	dh = eh * scale;
	/* An engine image LARGER than the published area must never be
	   written past the buffer. It is a bug when it happens, and the
	   caller reports it, but the clamp comes first. */
	if (dw > outW) { dw = outW; }
	if (dh > outH) { dh = outH; }

	g->scale  = scale;
	g->dw     = dw;
	g->dh     = dh;
	g->sw     = dw / scale;
	g->sh     = dh / scale;
	g->ox     = (outW - dw) / 2u;
	g->oy     = (outH - dh) / 2u;
	g->border = (dw != outW || dh != outH) ? 1 : 0;
}

 










static void zzdf_present_blit(const unsigned char *src, unsigned ew,
                              const unsigned *pal,
                              unsigned char *fb, unsigned pitch,
                              unsigned outW, unsigned outH,
                              const zzdf_present_geom *g)
{
	/* EVERY field is copied into a local first. dst writes into the
	   framebuffer and g is a pointer: the compiler cannot prove they
	   do not alias, so left as g->scale / g->sw the loop bounds are
	   reloaded from memory on every single iteration. */
	const unsigned scale = g->scale;
	const unsigned sw = g->sw, sh = g->sh;
	const unsigned dw = g->dw, dh = g->dh;
	const unsigned ox = g->ox, oy = g->oy;
	unsigned y, x, k, srcY, rep;
	unsigned char *dstRow;

	if (!src || !pal || !fb || !sw || !sh) { return; }

	/* The border is cleared every frame: the three buffers rotate and
	   each carries its own stale content, so a menu would otherwise
	   sit inside a frame of the previous mission's pixels. */
	if (g->border)
	{
		unsigned char *p = fb;
		for (y = 0; y < outH; y++, p += pitch)
		{
			if (y < oy || y >= oy + dh)
			{
				memset(p, 0, (size_t)outW * 4u);
			}
			else
			{
				if (ox) { memset(p, 0, (size_t)ox * 4u); }
				if (ox + dw < outW)
					memset(p + (size_t)(ox + dw) * 4u, 0,
					       (size_t)(outW - ox - dw) * 4u);
			}
		}
	}

	dstRow = fb + (size_t)oy * pitch + (size_t)ox * 4u;

	if (scale == 1u)
	{
		 


		for (y = 0; y < sh; y++)
		{
			unsigned *dst = (unsigned*)dstRow;
			const unsigned char *sp = src;
			x = 0;
			for (; x + 8u <= sw; x += 8u)
			{
				dst[0] = pal[sp[0]]; dst[1] = pal[sp[1]];
				dst[2] = pal[sp[2]]; dst[3] = pal[sp[3]];
				dst[4] = pal[sp[4]]; dst[5] = pal[sp[5]];
				dst[6] = pal[sp[6]]; dst[7] = pal[sp[7]];
				dst += 8; sp += 8;
			}
			for (; x < sw; x++) { *dst++ = pal[*sp++]; }
			src += ew;
			dstRow += pitch;
		}
		return;
	}

	 














	srcY = 0; rep = 0;
	for (y = 0; y < dh; y++)
	{
		unsigned *dst = (unsigned*)dstRow;
		const unsigned char *sp = src + (size_t)srcY * ew;

		if (scale == 2u)
		{
			x = 0;
			for (; x + 4u <= sw; x += 4u)
			{
				unsigned c0 = pal[sp[0]], c1 = pal[sp[1]];
				unsigned c2 = pal[sp[2]], c3 = pal[sp[3]];
				dst[0] = c0; dst[1] = c0;
				dst[2] = c1; dst[3] = c1;
				dst[4] = c2; dst[5] = c2;
				dst[6] = c3; dst[7] = c3;
				dst += 8; sp += 4;
			}
			for (; x < sw; x++)
			{
				unsigned c = pal[*sp++];
				dst[0] = c; dst[1] = c; dst += 2;
			}
		}
		else
		{
			for (x = 0; x < sw; x++)
			{
				unsigned c = pal[sp[x]];
				for (k = 0; k < scale; k++) { *dst++ = c; }
			}
		}

		dstRow += pitch;
		if (++rep == scale) { rep = 0; srcY++; }
	}
}

#ifdef __cplusplus
}
#endif

#endif /* ZZDF_PRESENT_GEOM_H */
