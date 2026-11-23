/* Reference model of the RasterIX edge walker for descriptor testing.
 * Based on the Rasterizer.v implementation by ToNi3141 (Copyright 2023).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef RIX_WALK_MODEL_H
#define RIX_WALK_MODEL_H

#define RIX_WALK_MIX( x, y ) (( (unsigned long long)(unsigned)( (y) * 2048 + (x) ) + 0x9E3779B97F4A7C15ull ) * 0xD6E8FEB86659FD93ull ^ ( (unsigned long long)(unsigned)( (y) * 2048 + (x) ) << 37 ))
typedef struct {
	unsigned clocks;      /* every step of the walker for this triangle */
	unsigned pixels;      /* steps that draw a pixel */
	unsigned lines;       /* lines walked */
	unsigned empty;       /* of those, lines with no pixel */
	unsigned far_clocks;  /* steps that draw nothing, on the lines where the walker ran to the end of the box (the third case above) */
	unsigned end_clocks;  /* steps on the lines before the first and after the last line that holds a pixel: what rix_box_fit = 2 takes off */
} rix_walk_t;

/* d: a triangle descriptor (rixdesc.c).  xres, yres: the frame (640, 481).
   sig, when not NULL: a signature of the set of pixels drawn (their sum under a mixing function,
   so that two walks draw the same pixels if and only if - to 2^-64 - their signatures and their
   pixel counts are equal, whatever the order). */
static void rix_walk_sig( const unsigned *d, unsigned xres, unsigned yres, rix_walk_t *o, unsigned long long *sig )
{
	enum { W_SEARCH_LEFT = 0, W_WALK_OUT, W_WALK, W_YINC, W_INIT, W_SEARCH_RIGHT };
	const int bsx = (int)( d[1] & 0xFFFFu ), bsy = (int)( d[1] >> 16 ), bex = (int)( d[2] & 0xFFFFu ), bey = (int)( d[2] >> 16 );
	int w0 = (int)d[3], w1 = (int)d[4], w2 = (int)d[5];
	const int ix0 = (int)d[6], ix1 = (int)d[7], ix2 = (int)d[8], iy0 = (int)d[9], iy1 = (int)d[10], iy2 = (int)d[11];
	int x = bsx, y = bsy, xs = bsx, s0 = w0, s1 = w1, s2 = w2, st = W_INIT, line_px = 0, line_clk = 0, far = 0, seen = 0;
	unsigned trail = 0u;
	(void)xres;
	o->clocks = 1u; o->pixels = 0u; o->lines = 0u; o->empty = 0u; o->far_clocks = 0u; o->end_clocks = 0u;      /* RASTERIZER_INIT */
	while( y < bey && y < (int)yres )
	{
		const int in = !(( w0 | w1 | w2 ) < 0 ) && x < bex && x >= bsx;
		int inc = 0, dec = 0, yinc = 0, push = 0, pop = 0;
		o->clocks++; line_clk++;
		switch( st )
		{
		case W_INIT: push = 1; st = W_SEARCH_LEFT; break;
		case W_SEARCH_LEFT:
			if( in ) { push = 1; inc = 1; st = W_WALK; o->pixels++; line_px++; if( sig ) *sig += RIX_WALK_MIX( x, y ); }
			else if( x == bex ) { dec = 1; st = W_SEARCH_RIGHT; far = 1; }
			else inc = 1;
			break;
		case W_SEARCH_RIGHT:
			if( in ) { dec = 1; st = W_WALK_OUT; }
			else if( x == bsx ) { push = 1; st = W_YINC; }
			else dec = 1;
			break;
		case W_WALK_OUT:
			if( in ) dec = 1; else { inc = 1; st = W_SEARCH_LEFT; }
			break;
		case W_YINC:
			yinc = 1; st = W_WALK_OUT;
			o->lines++; if( !line_px ) o->empty++;
			if( far ) o->far_clocks += (unsigned)( line_clk - line_px );
			if( line_px ) { seen = 1; trail = 0u; }
			else if( !seen ) o->end_clocks += (unsigned)line_clk;
			else trail += (unsigned)line_clk;
			line_px = 0; line_clk = 0; far = 0;
			break;
		default: /* W_WALK */
			if( !in ) { pop = 1; st = W_YINC; } else { inc = 1; o->pixels++; line_px++; if( sig ) *sig += RIX_WALK_MIX( x, y ); }
			break;
		}
		/* the commands, as the always block applies them (PUSH and POP take the values before the step) */
		if( push ) { xs = x; s0 = w0; s1 = w1; s2 = w2; }
		if( inc ) { x++; w0 += ix0; w1 += ix1; w2 += ix2; }
		if( dec ) { x--; w0 -= ix0; w1 -= ix1; w2 -= ix2; }
		if( yinc ) { y++; w0 += iy0; w1 += iy1; w2 += iy2; }
		if( pop ) { x = xs; w0 = s0; w1 = s1; w2 = s2; }
	}
	o->clocks++;                                           /* the last one, which flushes the pipeline */
	o->end_clocks += trail;
}
static void rix_walk( const unsigned *d, unsigned xres, unsigned yres, rix_walk_t *o ) { rix_walk_sig( d, xres, yres, o, (unsigned long long *)0 ); }

#endif
