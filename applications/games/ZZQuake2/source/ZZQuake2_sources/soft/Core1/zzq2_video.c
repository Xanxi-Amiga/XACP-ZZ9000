/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2_video.c - ZZQuake2 A0: SWimp_* backend of ref_soft.
 *
 * This is the Quake II side of the video adapter. It contains no
 * framebuffer code at all: every pixel that reaches the screen goes
 * through the release QG_DrawFrame() of ZZQuake640 (8-bit indexed to
 * RGB32 or RGB565, P96 pitch, cache clean, frame-ready handshake,
 * triple buffering), reached through zzq2_present() in zzq2_glue.c.
 *
 * Fixed mode, the size of the screen the launcher opened: 640 x 480
 * (sw_mode 3 of Quake II) by default; 320 x 240 (sw_mode 0) in the
 * 0.7 blob zzq2_320.bin and 800 x 600 (sw_mode 4) in zzq2_800.bin,
 * built with ZZQ2_RES=320 / 800 (build_arm.sh, which passes
 * ZZQ2_VID_W, ZZQ2_VID_H and ZZQ2_SW_MODE).
 *
 * ASCII only.
 */

#include "ref_soft/r_local.h"
#include "zzq2_diag.h"

#include "zzq2_res.h"          /* ZZQ2_VID_W, ZZQ2_VID_H, ZZQ2_SW_MODE */

/* zzq2_glue.c */
extern void  zzq2_crumb(unsigned int code);
extern void  zzq2_set_palette_rgbx(const unsigned char *pal4);
extern void *zzq2_video_buffer(void);
extern void  zzq2_present(void *pixels);

int SWimp_Init(void *hInstance, void *wndProc)
{
    (void)hInstance; (void)wndProc;
    zzq2_crumb(ZZQ2_DIAG_SWIMP_INIT);
    return true;
}

/*
** The screen was opened by the launcher before Core1 started, and its
** size cannot change. Whatever mode is asked for, the answer is
** ZZQ2_VID_W x ZZQ2_VID_H; sw_mode is put back so the video menu tells the
** truth. ref_soft clears sw_mode->modified after a successful return,
** so this cannot loop.
*/
rserr_t SWimp_SetMode(int *pwidth, int *pheight, int mode, qboolean fullscreen)
{
    (void)fullscreen;
    zzq2_crumb(ZZQ2_DIAG_SWIMP_MODE);

    if (mode != ZZQ2_SW_MODE) {
        ri.Con_Printf(PRINT_ALL,
                      "ZZQuake2: sw_mode %d not available, %dx%d kept\n",
                      mode, ZZQ2_VID_W, ZZQ2_VID_H);
        ri.Cvar_SetValue("sw_mode", ZZQ2_SW_MODE);
    }

    vid.buffer   = (pixel_t *)zzq2_video_buffer();
    vid.rowbytes = ZZQ2_VID_W;

    *pwidth  = ZZQ2_VID_W;
    *pheight = ZZQ2_VID_H;

    /* let the client know the size of the "window" */
    ri.Vid_NewWindow(ZZQ2_VID_W, ZZQ2_VID_H);

    return rserr_ok;
}

/*
** palette: 256 entries of 4 bytes, R G B x (ref_soft passes
** sw_state.currentpalette). A null pointer means "set it again".
*/
void SWimp_SetPalette(const unsigned char *palette)
{
    zzq2_crumb(ZZQ2_DIAG_SWIMP_PAL);
    if (!palette)
        palette = (const unsigned char *)sw_state.currentpalette;
    zzq2_set_palette_rgbx(palette);
}

void SWimp_BeginFrame(float camera_separation)
{
    (void)camera_separation;
}

void SWimp_EndFrame(void)
{
    zzq2_present(vid.buffer);
}

void SWimp_AppActivate(qboolean active)
{
    (void)active;
}

/* The frame buffer is kept: R_Shutdown is also used for vid_restart. */
void SWimp_Shutdown(void)
{
}
