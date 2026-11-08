/* sky.c - ZZVirtuaRacingRIX A8b: the race's sky (Mega Drive plane B), as
   VirtuaRacingDeluxeEnhanced (9f059e1) shows it:
   - plane B drawn by us from the VDP's memory, wider than the screen (layers.c
     draw_plane_b_wide; here the whole plane, once, into a cache);
   - moved sideways between the game's scroll steps with the interpolated camera
     (vrde.c note_sky_scroll / sky_offset_for: the older scene's scroll plus the
     difference to the newer's times camera_t), in half pixels at 640;
   - its A-B-A dither blended into gradients, in linear light (vrde.c skyblend_shader:
     a dither pixel has the same colour on both sides, one 9-bit step from its own,
     across or down; each dither pixel and the pixels around it within one step share
     their colours with tent weights 1 2 1), in integer arithmetic, a few rows a frame;
   - used only while plane B as we draw it is what the console shows (vrde.c
     check_plane_b: here on the FB3A lines of the 3D, the HUD's pixels excepted).
   Whole-screen scroll only (the races'), as VRDE. Same licence as the rest of this
   folder (LICENSE_VRDE.txt). ASCII only. */
#include <string.h>
#include <math.h>
#include "pico/pico_int.h"
#include "sky.h"

#define CACHE_PIXELS (512 * 512)
#define BLEND_ROWS_PER_FRAME 8
#define SCROLLS_KEPT 4

static uint16_t raw[CACHE_PIXELS];       /* plane B, rows of plane_w pixels, plane_h rows */
static uint16_t blended[CACHE_PIXELS];
static uint8_t dither[CACHE_PIXELS];
static int plane_w, plane_h, valid, blend_on = 1;
static int dither_rows_done, blend_rows_done;   /* rows 0..n-1 done (row h-1 first for dither) */
static uint32_t cache_key;
static uint64_t used_colours;            /* the palette entries plane B uses (the game cycles others) */
static sky_stats_t stats;

static int scrolls[SCROLLS_KEPT], scenes_seen, have_scrolls;
static int end_hscroll = -1, end_vscroll;       /* at the end of the last frame */
static int frame_hscroll = -1, frame_vscroll;   /* used by the frame just emulated */

void sky_set_blend(int on) { blend_on = on; }
const sky_stats_t *sky_stats(void) { return &stats; }

static int plane_cells(int size_bits) { return size_bits == 1 ? 64 : size_bits == 3 ? 128 : 32; }

static int plane_b_hscroll(void)
{
	int hscroll_table = (Pico.video.reg[13] & 0x3F) << 10;
	return PicoMem.vram[(hscroll_table + 2) / 2] & 0x3FF;
}

/* The cache's key: plane B's registers, map, the patterns it uses and the colours */
static uint32_t current_key(int w_cells, int h_cells)
{
	const unsigned char *reg = Pico.video.reg;
	int nametable = (reg[4] & 7) << 13;
	uint32_t h = 2166136261u ^ (uint32_t)(reg[4] & 7) ^ ((uint32_t)(reg[16]) << 8) ^ ((uint32_t)(reg[7] & 0x3F) << 16);
	int i, k;
	for (i = 0; i < 64; i++)   /* only the colours plane B shows (the race cycles 17-24 every frame) */
		if (used_colours & ((uint64_t)1 << i)) h = (h ^ Pico.est.HighPal[i]) * 16777619u;
	for (i = 0; i < w_cells * h_cells; i++) {
		uint16_t entry = PicoMem.vram[((nametable + i * 2) & 0xFFFF) / 2];
		const uint32_t *pattern = (const uint32_t *)&PicoMem.vram[((entry & 0x7FF) * 32) / 2];
		uint32_t p = entry;
		for (k = 0; k < 8; k++) p = p * 31u + pattern[k];
		h = (h ^ p) * 16777619u;
	}
	return h;
}

/* The whole plane into raw (layers.c draw_plane_b_wide, a cell at a time) */
static void draw_plane(int w_cells, int h_cells)
{
	const unsigned char *reg = Pico.video.reg;
	int nametable = (reg[4] & 7) << 13, background = reg[7] & 0x3F, cy, cx, y, x;
	const uint16_t *pal = Pico.est.HighPal;
	uint64_t used = (uint64_t)1 << background;
	for (cy = 0; cy < h_cells; cy++)
		for (cx = 0; cx < w_cells; cx++) {
			uint16_t entry = PicoMem.vram[((nametable + (cy * w_cells + cx) * 2) & 0xFFFF) / 2];
			int tile = entry & 0x7FF, colours = ((entry >> 13) & 3) * 16;
			for (y = 0; y < 8; y++) {
				int ty = entry & 0x1000 ? 7 - y : y;
				uint16_t *out = &raw[(cy * 8 + y) * plane_w + cx * 8];
				const uint16_t *line = &PicoMem.vram[(tile * 32 + ty * 4) / 2];
				uint32_t bits = ((uint32_t)line[0] << 16) | line[1];   /* pixel 0 in the high bits */
				for (x = 0; x < 8; x++) {
					int tx = entry & 0x0800 ? 7 - x : x;
					int pixel = (bits >> (28 - 4 * tx)) & 15, index = pixel ? colours + pixel : background;
					out[x] = pal[index];
					used |= (uint64_t)1 << index;
				}
			}
		}
	used_colours = used;
}

/* ---- the dither blended (skyblend_shader) ---- */
static uint16_t lin5[32], lin6[64];           /* 0..4095, linear light */
static uint8_t srgb5[4096], srgb6[4096];
static int luts_ready;

static double to_linear(double c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
static double to_srgb(double c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1 / 2.4) - 0.055; }
static void make_luts(void)
{
	int i;
	for (i = 0; i < 32; i++) lin5[i] = (uint16_t)lround(to_linear(i / 31.0) * 4095.0);
	for (i = 0; i < 64; i++) lin6[i] = (uint16_t)lround(to_linear(i / 63.0) * 4095.0);
	for (i = 0; i < 4096; i++) {
		double s = to_srgb(i / 4095.0);
		srgb5[i] = (uint8_t)lround(s * 31.0);
		srgb6[i] = (uint8_t)lround(s * 63.0);
	}
	luts_ready = 1;
}

static inline int one_step_apart(uint16_t a, uint16_t b)
{
	int dr = (a >> 11) - (b >> 11), dg = ((a >> 5) & 63) - ((b >> 5) & 63), db = (a & 31) - (b & 31);
	return dr <= 6 && dr >= -6 && dg <= 12 && dg >= -12 && db <= 6 && db >= -6;
}
static inline uint16_t at(int y, int x)
{
	return raw[(y & (plane_h - 1)) * plane_w + (x & (plane_w - 1))];
}
static void dither_row(int y)
{
	int x;
	for (x = 0; x < plane_w; x++) {
		uint16_t c = at(y, x), l = at(y, x - 1), r = at(y, x + 1), u = at(y - 1, x), d = at(y + 1, x);
		int across = l == r && l != c && one_step_apart(l, c);
		int down = u == d && u != c && one_step_apart(u, c);
		dither[y * plane_w + x] = (uint8_t)(across || down);
	}
}
static void blend_row(int y)
{
	int x, dx, dy;
	for (x = 0; x < plane_w; x++) {
		uint16_t c = at(y, x);
		int centre_dither = dither[y * plane_w + x];
		uint32_t sr = 0, sg = 0, sb = 0, total = 0;
		for (dy = -1; dy <= 1; dy++)
			for (dx = -1; dx <= 1; dx++) {
				int ny = (y + dy) & (plane_h - 1), nx = (x + dx) & (plane_w - 1);
				uint16_t n = raw[ny * plane_w + nx];
				uint32_t w;
				if ((dx || dy) && (!one_step_apart(n, c) || (!centre_dither && !dither[ny * plane_w + nx])))
					continue;
				w = (uint32_t)((2 - (dx < 0 ? -dx : dx)) * (2 - (dy < 0 ? -dy : dy)));
				sr += w * lin5[n >> 11]; sg += w * lin6[(n >> 5) & 63]; sb += w * lin5[n & 31];
				total += w;
			}
		blended[y * plane_w + x] = (uint16_t)(((uint32_t)srgb5[sr / total] << 11) | ((uint32_t)srgb6[sg / total] << 5) | srgb5[sb / total]);
	}
}

void sky_frame_end(int scene_count)
{
	const unsigned char *reg = Pico.video.reg;
	int w_cells, h_cells, n;
	uint32_t key;
	if (!PicoIn.AHW || (reg[11] & 3) != 0) { valid = 0; return; }   /* whole-screen scroll only */
	/* the scroll: the frame just emulated drew with the one at the end of the frame before */
	frame_hscroll = end_hscroll; frame_vscroll = end_vscroll;
	end_hscroll = plane_b_hscroll(); end_vscroll = PicoMem.vsram[1] & 0x3FF;
	if (scene_count != scenes_seen) {
		scenes_seen = scene_count;
		scrolls[scene_count % SCROLLS_KEPT] = end_hscroll;   /* vrde.c note_sky_scroll */
		if (have_scrolls < SCROLLS_KEPT) have_scrolls++;
	}
	/* the cache */
	w_cells = plane_cells(reg[16] & 3); h_cells = plane_cells((reg[16] >> 4) & 3);
	if (w_cells * 8 * h_cells * 8 > CACHE_PIXELS) { valid = 0; return; }
	key = current_key(w_cells, h_cells);
	if (!valid || key != cache_key || plane_w != w_cells * 8 || plane_h != h_cells * 8) {
		plane_w = w_cells * 8; plane_h = h_cells * 8;
		draw_plane(w_cells, h_cells);
		cache_key = current_key(w_cells, h_cells);   /* with the colours it now knows it uses */
		valid = 1;
		dither_rows_done = blend_rows_done = 0;
		stats.rebuilds++; stats.plane_w = (uint32_t)plane_w; stats.plane_h = (uint32_t)plane_h;
	}
	if (!blend_on) return;
	if (!luts_ready) make_luts();
	/* a few rows a frame: the dither map one row ahead of the blend (row h-1 first) */
	for (n = 0; n < BLEND_ROWS_PER_FRAME && blend_rows_done < plane_h; n++) {
		if (dither_rows_done == 0) { dither_row(plane_h - 1); dither_row(0); dither_rows_done = 1; }
		while (dither_rows_done < plane_h && dither_rows_done <= blend_rows_done + 1) dither_row(dither_rows_done++);
		blend_row(blend_rows_done++);
		stats.blend_rows_done++;
	}
}

static int scroll_difference(int d)
{
	d %= 1024;
	if (d >= 512) d -= 1024;
	if (d < -512) d += 1024;
	return d;
}

int sky_scroll_half(int scene, double camera_t, int *ok)
{
	*ok = 0;
	if (!valid || frame_hscroll < 0) return 0;
	if (have_scrolls >= 2 && scene >= 1 && scenes_seen - scene <= SCROLLS_KEPT - 2) {
		int older = scrolls[(scene - 1) % SCROLLS_KEPT], newer = scrolls[scene % SCROLLS_KEPT];
		double s = older + scroll_difference(newer - older) * camera_t;
		/* not one tick's turn away from the frame's own (vrde.c: at most 65 pixels) */
		if (fabs((double)scroll_difference((int)lround(s) - frame_hscroll)) < 102.0) {
			*ok = 1;
			return (int)lround(2.0 * s);
		}
	}
	*ok = 1;
	return 2 * frame_hscroll;
}

int sky_line_matches(int line, const uint16_t *row, const uint8_t *keep)
{
	int x, py, hs;
	const uint16_t *src;
	if (!valid || frame_hscroll < 0) return 0;
	py = (line + frame_vscroll) & (plane_h - 1);
	src = &raw[py * plane_w];
	hs = frame_hscroll;
	for (x = 0; x < 320; x++)
		if (!keep[x] && src[(x - hs) & (plane_w - 1)] != row[x]) return 0;
	return 1;
}

int sky_line_rows(int line, int scroll_half, uint32_t *row0, uint32_t *row1)
{
	int py, j0, k, mask;
	const uint16_t *src;
	if (!valid) return 0;
	py = (line + frame_vscroll) & (plane_h - 1);
	src = (blend_on && py < blend_rows_done) ? &blended[py * plane_w] : &raw[py * plane_w];
	mask = plane_w - 1;
	j0 = (-scroll_half) & (2 * plane_w - 1);   /* sky_line640: pixel X shows src[((X - s) & mask2) >> 1] */
	if (!(j0 & 1)) {
		int i = j0 >> 1;
		for (k = 0; k < 320; k += 4) {
			uint32_t a = src[(i + k) & mask], b = src[(i + k + 1) & mask], c = src[(i + k + 2) & mask], d = src[(i + k + 3) & mask];
			a |= a << 16; b |= b << 16; c |= c << 16; d |= d << 16;
			row0[k] = a; row0[k + 1] = b; row0[k + 2] = c; row0[k + 3] = d;
			if (row1) { row1[k] = a; row1[k + 1] = b; row1[k + 2] = c; row1[k + 3] = d; }
		}
	} else {
		int i = j0 >> 1;
		uint32_t prev = src[i & mask];
		for (k = 0; k < 320; k++) {
			uint32_t next = src[(i + k + 1) & mask], w = prev | (next << 16);
			row0[k] = w; if (row1) row1[k] = w;
			prev = next;
		}
	}
	return 1;
}

int sky_line640(int line, int scroll_half, uint16_t *out)
{
	int py, X, mask2;
	const uint16_t *src;
	if (!valid) return 0;
	py = (line + frame_vscroll) & (plane_h - 1);
	src = (blend_on && py < blend_rows_done) ? &blended[py * plane_w] : &raw[py * plane_w];
	mask2 = 2 * plane_w - 1;
	for (X = 0; X < 640; X++) out[X] = src[((X - scroll_half) & mask2) >> 1];
	return 1;
}
