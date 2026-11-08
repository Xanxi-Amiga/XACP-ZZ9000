/* expand.c - expand_polygon() of VirtuaRacingDeluxeEnhanced's vrde.c (9f059e1),
   unchanged but for the vertex type (x and y only: the other fields of VRDE's
   hle_vertex_t are copied along unchanged there, and set by the caller here) and
   (ZZVirtuaRacingRIX A8g) the neighbours' indices taken without a modulo (the
   Cortex-A9 has no integer divider).
   Same licence as the rest of this folder (LICENSE_VRDE.txt). ASCII only.

   VRDE's note: exact vertices leave hairline cracks where the models' neighbouring
   polygons do not quite meet (the SH2s' rounded vertices close them, and their fill
   covers more than the GPU's anyway). At the console's resolution 0.375 pixel makes
   the GPU's picture closest to the SH2s' (94.9% of the pixels, against 92.8%
   unexpanded and 94.7% with the SH2s' own vertices). */
#include <math.h>
#include <string.h>
#include "expand.h"

/* Moves a polygon's edges outwards by `distance` (console pixels), keeping their
   directions: each vertex goes to where its two edges' moved lines cross. A sharp
   corner, where the lines cross more than 2 * distance away, is cut off instead:
   it becomes two vertices, one on each moved edge, so that the edges stay straight.
   Writes up to 2 * count vertices to out and returns how many; degenerate polygons
   stay as they are. */
int vrde_expand_polygon(const vrde_xy *v, int count, float distance, vrde_xy *out)
{
	float area = 0;
	for (int i = 0; i < count; i++) {
		const vrde_xy *a = &v[i], *b = &v[i + 1 == count ? 0 : i + 1];   /* A8g: no division (A9) */
		area += a->x * b->y - b->x * a->y;
	}
	if (fabsf(area) < 1e-3f) {
		memcpy(out, v, count * sizeof(*v));
		return count;
	}
	float outwards = area > 0 ? 1 : -1;   /* (dy, -dx) points out of a positive-area polygon */

	float normal_x[4], normal_y[4];       /* each edge's outward unit normal */
	for (int i = 0; i < count; i++) {
		const vrde_xy *a = &v[i], *b = &v[i + 1 == count ? 0 : i + 1];   /* A8g: no division (A9) */
		float dx = b->x - a->x, dy = b->y - a->y, length = sqrtf(dx * dx + dy * dy);
		normal_x[i] = length > 0 ? outwards * dy / length : 0;
		normal_y[i] = length > 0 ? -outwards * dx / length : 0;
	}
	int moved = 0;
	for (int i = 0; i < count; i++) {
		int before = i == 0 ? count - 1 : i - 1;
		float dot = 1 + normal_x[before] * normal_x[i] + normal_y[before] * normal_y[i];
		if (dot > 0.5f) {
			/* the corner's offset m solves m . n1 = m . n2 = distance */
			float scale = distance / dot;
			out[moved] = v[i];
			out[moved].x += (normal_x[before] + normal_x[i]) * scale;
			out[moved].y += (normal_y[before] + normal_y[i]) * scale;
			moved++;
			continue;
		}
		out[moved] = v[i];
		out[moved].x += normal_x[before] * distance;
		out[moved].y += normal_y[before] * distance;
		moved++;
		out[moved] = v[i];
		out[moved].x += normal_x[i] * distance;
		out[moved].y += normal_y[i] * distance;
		moved++;
	}
	return moved;
}
