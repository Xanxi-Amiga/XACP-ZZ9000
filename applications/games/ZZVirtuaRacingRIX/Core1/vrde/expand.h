/* expand.h - VRDE's expand_polygon (vrde.c), for ZZVirtuaRacingRIX's RasterIX path.
   Same licence as the rest of this folder (LICENSE_VRDE.txt). */
#ifndef VRDE_EXPAND_H
#define VRDE_EXPAND_H

/* ZZVirtuaRacingRIX A8a: a, the vertex's alpha (the haze), copied along as VRDE copies
   its vertices' other fields */
typedef struct { float x, y, a; } vrde_xy;

/* How far exact polygons are pushed out, in console pixels: VRDE's
   fminf(EXPAND / scale_x, EXPAND_MOST) at the console's resolution (scale 1) */
#define VRDE_EXPAND_CONSOLE 0.375f

/* Moves a polygon's edges outwards by `distance` (console pixels); writes up to
   2 * count vertices to out and returns how many (see expand.c) */
int vrde_expand_polygon(const vrde_xy *v, int count, float distance, vrde_xy *out);

#endif
