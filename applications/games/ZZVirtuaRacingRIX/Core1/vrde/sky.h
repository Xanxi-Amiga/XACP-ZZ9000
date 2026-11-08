/* sky.h - ZZVirtuaRacingRIX A8b: the race's sky (Mega Drive plane B) as
   VirtuaRacingDeluxeEnhanced shows it: drawn by us from the VDP's memory (layers.c
   draw_plane_b_wide), moved sideways with the interpolated camera (vrde.c
   sky_offset_for) and its A-B-A dither blended into gradients (vrde.c
   skyblend_shader, here in integer arithmetic on Core1).
   Same licence as the rest of this folder (LICENSE_VRDE.txt). ASCII only. */
#ifndef VRDE_SKY_H
#define VRDE_SKY_H

#include <stdint.h>

/* After each emulated frame: notes plane B's scroll for the scene sent in it, and checks
   the sky's cache (plane B's tiles, map and colours) */
void sky_frame_end(int scene_count);

/* Plane B's scroll in half pixels for an image built from scene `scene` at camera_t
   (hle_frame_t): between the older scene's scroll and the newer's; *ok = 0 when unknown */
int sky_scroll_half(int scene, double camera_t, int *ok);

/* Whether plane B as we draw it is what FB3A shows on console line `line` (FB3A row
   `row`, keep[x] != 0 for the pixels that are not the Mega Drive layer): 1 = same */
int sky_line_matches(int line, const uint16_t *row, const uint8_t *keep);

/* One console line of the sky at 640 pixels: out receives 640 RGB565 pixels (each
   console pixel twice, moved by scroll_half half pixels), blended where the cache is
   blended. Returns 0 without a cache. */
int sky_line640(int line, int scroll_half, uint16_t *out);

/* A8g: the same line straight into two rows of 320 words (GPU pixel order, pixel 2k in the
   low half), each console pixel twice; one pass, no 640-pixel copy (A8h: row1 may be NULL). Returns 0
   without a cache. */
int sky_line_rows(int line, int scroll_half, uint32_t *row0, uint32_t *row1);

/* Settings: blend the dither (1, default) or not */
void sky_set_blend(int on);

/* Diagnostics */
typedef struct {
	uint32_t rebuilds, blend_rows_done, plane_w, plane_h;
	uint32_t key_changes;
} sky_stats_t;
const sky_stats_t *sky_stats(void);

#endif
