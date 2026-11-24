/* ZZScreensaverR scene glue. Copyright (C) 2026 Xanxi. GPL-2.0-or-later. */
#ifndef ZZSM_MATRIX_SCENE_H
#define ZZSM_MATRIX_SCENE_H
#include <stdint.h>

#define MX_WIDTH 640
#define MX_HEIGHT 480
#define MX_PRESETS 3
#define MX_MAX_COLS 128
#define MX_MAX_ROWS 96

typedef struct { int cols, rows; int pixels; const char *name; } mx_preset;   /* pixels: Core1 writes the cells itself */
extern const mx_preset mx_presets[MX_PRESETS];

/* mv_sys.c: the scene's memory and random numbers */
void mv_sys_reset(uint32_t seed);
int mv_sys_failed(void);            /* 1 when an allocation did not fit the static pool */

/* mv_gl.c: MatrixView's GL calls on ZZGL */
void mv_gl_reset(void);
int mv_gl_failed(void);             /* 1 when the scene asked for something the adapter cannot give */
unsigned mv_gl_quads_in(void);      /* quads the scene issued in the last frame */
unsigned mv_gl_quads_drawn(void);   /* of which drawn (the others were black: nothing to add) */
void mv_gl_grid(int cols, int rows, int pixels); /* the character grid, before matrixview_init(): cells may then go
                                       the direct way, or be written as pixels by Core1 when pixels is not 0 */
unsigned mv_gl_quads_pixels(void);  /* of the drawn quads of the last frame, those written as pixels */
unsigned mv_gl_quads_lost(void);    /* quads dropped since the reset because a pixel frame kept too many for RasterIX */
int mv_gl_pixels_on(void);          /* the pixel path passed its checks on this grid */
unsigned mv_gl_quads_direct(void);  /* of the drawn quads of the last frame, those sent as ready-made descriptors */
unsigned mv_gl_cells_direct(void);  /* grid cells that passed the check for the direct way (0 = not used on this grid) */

/* the presenter's question: copy the previous frame before drawing? (name kept from the presenter) */
int sw_needs_history(void);

#endif
