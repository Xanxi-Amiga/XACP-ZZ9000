/* ZZScreensaverR: MatrixView behind the Core1 presenter's scene interface.
 * Copyright (C) 2026 Xanxi. GPL-2.0-or-later. See COPYING. No warranty.
 */
#include <stddef.h>
#include "zzgldemo.h"
#include "matrix_scene.h"
#include "matrixview.h"

const mx_preset mx_presets[MX_PRESETS] = {
    {80, 60, 1, "80 x 60"},
    {64, 48, 1, "64 x 48"},
    {40, 30, 1, "40 x 30"}
};

static u32 preset = 2, last_tick;
static double elapsed;

void zzgldemo_preset(u32 p) { preset = p; }
int zzgldemo_done(void) { return 0; }
int sw_needs_history(void) { return 0; }      /* every frame is drawn whole */

int zzgldemo_init(u32 pool, u32 pages, u32 zbuf, int fullclear)
{
    zzgl_config c;
    const mx_preset *p;
    int rc;
    (void)fullclear;
    if (preset < 1u || preset > MX_PRESETS) return RIXE_BADMODE;
    p = &mx_presets[preset - 1u];
    if (p->cols > MX_MAX_COLS || p->rows > MX_MAX_ROWS) return RIXE_BADMODE;
    c.pool_base = pool; c.pool_pages = pages; c.pool_cacheable = 1;
    c.zbuf = zbuf; c.width = MX_WIDTH; c.height = MX_HEIGHT; c.ztrick = 0;
    rc = zzgl_init(&c);
    if (rc) return rc;
#ifdef RIX_HOST_SIM
    mv_sys_reset(0x58414e58u);
#else
    mv_sys_reset(rix_now() | 1u);
#endif
    mv_gl_reset();
    mv_gl_grid(p->cols, p->rows, p->pixels);
    matrixview_matrix_resize(p->cols, p->rows);
    if (mv_sys_failed()) return RIXE_BADMODE;
    matrixview_init(MX_WIDTH, MX_HEIGHT);
    if (mv_gl_failed() || zzglGetError()) return RIXE_DESC;
    last_tick = 0; elapsed = 0.0;
    return 0;
}

int zzgldemo_render(u32 frame_no, u32 *tris_out)
{
    u32 now = rix_now();
    float dt = frame_no ? (float)rix_ticks_to_us(now - last_tick) / 1000000.0f : 1.0f / 60;
    last_tick = now;
    if (dt > 8.0f / 60) dt = 8.0f / 60;   /* bounded recovery after a stalled presenter */
#ifdef MX_FIXED_DT
    dt = 1.0f / 40;                       /* PC bench: the same rain whatever the frame took */
#endif
    elapsed += dt;
    matrixview_draw(elapsed, NULL);
    *tris_out = zzgl_tris_frame();
    return (mv_gl_failed() || zzglGetError()) ? RIXE_DESC : 0;
}
