/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2tgl_video.c - ZZQuake2TGL: GLimp_* backend of ref_gl.
 *
 * Replaces port_gl_sdl.c of quake2generic for the video part (no SDL,
 * no window, no context). TinyGL draws into its own 32-bit frame
 * (zzq2tgl_tgl.c); GLimp_EndFrame hands that frame to
 * zzq2_present32() (zzq2_glue.c), which copies it into the P96 buffer
 * the launcher granted, with the release triple-buffer handshake.
 *
 * The screen stays 640 x 480 (opened by the launcher). The engine may
 * render smaller: gl_mode 0 (320 x 240) and 1 (400 x 300) are drawn by
 * TinyGL at that size and scaled up when presented, for the benchmark
 * of the three resolutions. Any other mode falls back to 3 (640 x 480).
 *
 * ASCII only.
 */

#include "ref_gl/gl_local.h"
#include "zzq2_diag.h"
#include "zzq2tgl_version.h"

/* zzq2tgl_tgl.c */
extern int             zz_tgl_open(int w, int h);
extern const unsigned *zz_tgl_frame(int *w, int *h);
/* zzq2_glue.c */
extern void zzq2_crumb(unsigned int code);
extern void zzq2_present32(const unsigned *pixels, int w, int h);

int GLimp_Init(void *hinstance, void *wndproc)
{
    (void)hinstance; (void)wndproc;
    zzq2_crumb(ZZQ2_DIAG_SWIMP_INIT);
    return true;
}

int GLimp_SetMode(int *pwidth, int *pheight, int mode, qboolean fullscreen)
{
    int width = 0, height = 0;
    (void)fullscreen;
    zzq2_crumb(ZZQ2_DIAG_SWIMP_MODE);

    if (mode != 0 && mode != 1 && mode != 3) {
        ri.Con_Printf(PRINT_ALL, "ZZQuake2TGL: gl_mode %d not available, 3 used\n", mode);
        ri.Cvar_SetValue("gl_mode", 3);
        mode = 3;
    }
    if (!ri.Vid_GetModeInfo(&width, &height, mode))
        return rserr_invalid_mode;
    if (!zz_tgl_open(width, height)) {
        ri.Con_Printf(PRINT_ALL, "ZZQuake2TGL: TinyGL could not open %dx%d\n", width, height);
        return rserr_invalid_mode;
    }
#if defined(ZZQ2TGL_MODE) && ZZQ2TGL_MODE == 0
    ri.Con_Printf(PRINT_ALL, "ZZQuake2TGL %s: TinyGL %dx%d on Core1, shown at 320x240\n",
                  ZZQ2TGL_VERSION, width, height);
#else
    ri.Con_Printf(PRINT_ALL, "ZZQuake2TGL %s: TinyGL %dx%d on Core1, shown at 640x480\n",
                  ZZQ2TGL_VERSION, width, height);
#endif
    *pwidth = width;
    *pheight = height;
    ri.Vid_NewWindow(width, height);
    return rserr_ok;
}

void GLimp_Shutdown(void)
{
    /* the TinyGL context is kept: R_Shutdown is also used by vid_restart */
}

void GLimp_BeginFrame(float camera_separation)
{
    (void)camera_separation;
}

void GLimp_EndFrame(void)
{
    int w = 0, h = 0;
    const unsigned *f = zz_tgl_frame(&w, &h);
    if (f)
        zzq2_present32(f, w, h);
}

void GLimp_AppActivate(qboolean active)
{
    (void)active;
}
