/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2rix_video.c - ZZQuake2RIX: GLimp_* backend of ref_gl.
 *
 * Replaces port_gl_sdl.c of quake2generic for the video part (no SDL,
 * no window, no context).  The GPU draws into a 640x481 RGB565 colour
 * buffer of the G4B window (zzq2rix_gpu.c); GLimp_EndFrame closes the
 * frame, waits for its fence and hands the buffer to the release
 * presentation path (zzq2_present_rgb565, zzq2_glue.c), which converts
 * it into the P96 buffer the launcher granted, with the release
 * triple-buffer handshake.  Launcher unchanged (ZZQuake2_A0).
 *
 * The screen is opened by the launcher and the GPU renders at its size,
 * one blob per size (0.10, zzq2_res.h): 640 x 480 gl_mode 3 (zzq2rix.bin),
 * 320 x 240 gl_mode 0 (zzq2rix_320.bin), 800 x 600 gl_mode 4
 * (zzq2rix_800.bin); any other mode falls back to the blob's.
 *
 * ASCII only.
 */

#include "ref_gl/gl_local.h"
#include "zzq2_diag.h"
#include "zzq2rix_version.h"
#include "zzq2_res.h"

/* zzq2rix_gpu.c (no Quake II header on that side) */
extern int  zz_rix_open(int w, int h, char *why, int why_cap);
extern void zz_rix_begin_frame(void);
extern void zz_rix_end_frame(void);
extern void zz_rix_console_report(void (*out)(const char *line));
extern void zz_rix_set_alpha_textures(int on);
extern int  zz_rix_textest(char *report, int cap);
extern int  zz_rix_alpha_ok(void);
extern void zz_rix_perf_report(char *out, int cap);
extern void zz_rix_set_timedemo(int on, int regseq);
extern int  zz_rix_td_take(void);
extern const char *zz_rix_td_text(void);
extern unsigned zz_rix_td_lost(void);
extern void zz_rix_td_flush(void);
extern void zz_rix_microbench(char *out, int cap);
extern void zzgl_set_profile(int on);
extern void zzgl_set_deferred(int on);
extern void zz_rix_set_sync(int on);
extern void zz_rix_set_ring(int mode);
extern void zzgl_set_tex_versioning(int on);
extern void zz_rix_set_overlap(int on);
extern void zzgl_set_clear_flush(int on);
extern void rix_frame_set_pump_interval(unsigned n);
/* zzq2_glue.c */
extern void zzq2_crumb(unsigned int code);

static void con_line(const char *line) { ri.Con_Printf(PRINT_ALL, "%s\n", line); }

/* zzq2_glue.c: 1 = burst presentation path (0.3), 0 = the 0.2 word-by-word path */
extern int zzq2_present_burst;
static cvar_t *zz_present;
/* librixgpu 1.2.3 switches, read every frame (not archived: each run starts from the defaults) */
extern int rix_fill_rule, rix_tex_wnorm;
static cvar_t *zz_fillrule, *zz_wnorm;
/* 0.8.2: the engine's timedemo cvar, read every frame (zzq2rix_gpu.c cuts a report per timedemo) */
static cvar_t *zz_timedemo;
/* 0.8.3: rix_prof 1 splits zzglEnd's time in the reports (two more timer reads per triangle) */
static cvar_t *zz_prof;
/* 0.9: rix_defer 1 = deferred submission (librixgpu 1.3), from the next frame; 0 = direct (default) */
static cvar_t *zz_defer;
/* 0.9.1: rix_sync 1 = every chunk and bind waited at once (librixgpu SYNC, the ladder's mode): slow, but
   Core1's waits are then the GPU's own time, per frame and per category; 0 = off (default) */
static cvar_t *zz_sync;
/* 0.9.2: rix_texver 1 = lightmap updates by new page versions instead of draining the deferred queue (default 0);
   rix_ring 1 = the chunk ring as 32 x 16 KiB instead of 8 x 64 KiB (archived, read at start-up only) */
static cvar_t *zz_texver;
/* 0.9.3: rix_overlap 1 = frame N-1 presented while the GPU draws N (default 0); rix_clearflush 1 = the depth
   clear submitted at once (default 0); rix_pump n = deferred queue pumped every n triangles (default 32) */
static cvar_t *zz_overlap, *zz_clearflush, *zz_pump;

/* 0.8.2: baseq2/rixtd.txt, rewritten after each timedemo with the reports of all those of the run */
static void zz_write_td(void)
{
    char path[128];
    const char *t = zz_rix_td_text();
    FILE *f;
    Com_sprintf(path, sizeof(path), "%s/rixtd.txt", ri.FS_Gamedir());
    f = fopen(path, "w");
    if (!f) { ri.Con_Printf(PRINT_ALL, "ZZQuake2RIX: could not write %s\n", path); return; }
    fprintf(f, "ZZQuake2RIX %s timedemo reports (one per timedemo, in order)\n", ZZQ2RIX_VERSION);
    fwrite(t, 1, strlen(t), f);
    if (zz_rix_td_lost()) fprintf(f, "(%u later reports not kept: buffer full)\n", zz_rix_td_lost());
    fclose(f);
    ri.Con_Printf(PRINT_ALL, "ZZQuake2RIX: timedemo report written to %s\n", path);
}

int GLimp_Init(void *hinstance, void *wndproc)
{
    (void)hinstance; (void)wndproc;
    zzq2_crumb(ZZQ2_DIAG_SWIMP_INIT);
    return true;
}

int GLimp_SetMode(int *pwidth, int *pheight, int mode, qboolean fullscreen)
{
    int width = 0, height = 0;
    char why[96];
    (void)fullscreen;
    zzq2_crumb(ZZQ2_DIAG_SWIMP_MODE);

    if (mode != ZZQ2_SW_MODE) {   /* 0.10: the mode of this blob (same table as sw_mode) */
        ri.Con_Printf(PRINT_ALL, "ZZQuake2RIX: gl_mode %d not available, %d used\n", mode, ZZQ2_SW_MODE);
        ri.Cvar_SetValue("gl_mode", ZZQ2_SW_MODE);
        mode = ZZQ2_SW_MODE;
    }
    if (!ri.Vid_GetModeInfo(&width, &height, mode))
        return rserr_invalid_mode;
    /* zz_present (archived, 0.3): 1 = GPU buffer read by 32-byte bursts + table conversion
       (default), 0 = the 0.2 path (one uncached word per two pixels); read every frame */
    zz_present = ri.Cvar_Get("zz_present", "1", CVAR_ARCHIVE);
    zz_fillrule = ri.Cvar_Get("rix_fillrule", "0", 0);
    zz_wnorm = ri.Cvar_Get("rix_wnorm", "0", 0);
    zz_timedemo = ri.Cvar_Get("timedemo", "0", 0);
    zz_prof = ri.Cvar_Get("rix_prof", "0", 0);
    zz_defer = ri.Cvar_Get("rix_defer", "1", 0);       /* 0.9.3: on by default (validated 0.9 .. 0.9.2) */
    zz_sync = ri.Cvar_Get("rix_sync", "0", 0);
    zz_texver = ri.Cvar_Get("rix_texver", "1", 0);     /* 0.9.3: on by default */
    zz_overlap = ri.Cvar_Get("rix_overlap", "1", 0);       /* 0.9.4: on by default (validated 0.9.3, 16/10) */
    zz_clearflush = ri.Cvar_Get("rix_clearflush", "1", 0); /* 0.9.4: on by default */
    zz_pump = ri.Cvar_Get("rix_pump", "32", 0);
    {   /* 0.9.5: ref_gl's gl_ztrick (no depth clear: the depth range alternates between the two halves every
           frame), set once per run at the first mode set (R_Register has created it before R_SetMode).
           Validated on Amiga 4000, 16/10/2026: 33.4 fps in timedemo d1..  "gl_ztrick 0" in the console switches it off
           for the rest of the run; "+set rix_ztrick 0" keeps ref_gl's default from the start. */
        static int ztrick_done;
        cvar_t *zt = ri.Cvar_Get("rix_ztrick", "1", 0);
        if (!ztrick_done) {
            ztrick_done = 1;
            if (zt->value != 0.0f) ri.Cvar_Set("gl_ztrick", "1");
        }
    }
    {
        cvar_t *rr = ri.Cvar_Get("rix_ring", "1", CVAR_ARCHIVE);   /* 0.9.3: 32 x 16 KiB by default */
        zz_rix_set_ring((int)rr->value);   /* used by the first zz_rix_open only (the GPU context is kept across vid_restart) */
    }
    ri.Con_Printf(PRINT_ALL, "ZZQuake2RIX: zz_present %d (%s)\n", (int)zz_present->value,
                  zz_present->value != 0.0f ? "burst read + table" : "0.2 word path");
    why[0] = 0;
    if (!zz_rix_open(width, height, why, (int)sizeof(why))) {
        ri.Con_Printf(PRINT_ALL, "ZZQuake2RIX: RasterIX could not open %dx%d: %s\n", width, height, why);
        return rserr_invalid_mode;
    }
    ri.Con_Printf(PRINT_ALL, "ZZQuake2RIX %s: RasterIX G4C %dx%d (ZZGL 0.7.6, librixgpu 1.4.0)\n",
                  ZZQ2RIX_VERSION, width, height);
    zz_rix_console_report(con_line);
    /* 0.6: texel self-test, once per run (the first GLimp_SetMode), before ref_gl uploads anything:
       console + baseq2/rixtest.txt (the file the 68k writes like config.cfg) */
    {
        static int done;
        static char rep[4096];
        if (!done) {
            char path[128], *p, *q;
            FILE *f;
            int ok;
            done = 1;
            ok = zz_rix_textest(rep, (int)sizeof(rep));
            /* 0.8.3: the start-up micro-benchmark, GPU idle after the self-test */
            {
                static char mb[1024];
                zz_rix_microbench(mb, (int)sizeof(mb));
                if (strlen(rep) + strlen(mb) < sizeof(rep)) strcat(rep, mb);
            }
            for (p = rep; *p; p = q) {
                char line[200]; int n = 0;
                for (q = p; *q && *q != '\n'; q++) if (n < (int)sizeof(line) - 1) line[n++] = *q;
                line[n] = 0;
                if (*q) q++;
                ri.Con_Printf(PRINT_ALL, "%s\n", line);
            }
            Com_sprintf(path, sizeof(path), "%s/rixtest.txt", ri.FS_Gamedir());
            f = fopen(path, "w");
            if (f) {
                fprintf(f, "ZZQuake2RIX %s texel self-test\n%s", ZZQ2RIX_VERSION, rep);
                fclose(f);
                ri.Con_Printf(PRINT_ALL, "ZZQuake2RIX: texel test %s, written to %s\n", ok ? "ALL OK" : "MISMATCHES", path);
            } else ri.Con_Printf(PRINT_ALL, "ZZQuake2RIX: could not write %s\n", path);
        }
    }
    /* rix_alpha (archived, 0.7; replaces zz_alpha, whose "0" sits in existing config.cfg files):
       alpha-keyed textures (pics, console font, crosshair, sprites, grates) as RGBA5551 so that the
       alpha test cuts their transparent texels.  ref_gl gives those texels the colour of a
       neighbour (alpha 0) to avoid filtering fringes: in RGB565 they were drawn - fat letters,
       a blob of a crosshair, a black square around the menu's logo (0.1 .. 0.6).
         0 = RGB565 everywhere, 1 = RGBA5551, 2 = RGBA5551 if the start-up self-test passed (default).
       Read here so that "rix_alpha n" + "vid_restart" re-uploads every texture in the new format. */
    {
        cvar_t *ra = ri.Cvar_Get("rix_alpha", "2", CVAR_ARCHIVE);
        int mode = (int)ra->value, aok = zz_rix_alpha_ok(), on;
        on = (mode == 1) || (mode == 2 && aok > 0);
        zz_rix_set_alpha_textures(on);
        ri.Con_Printf(PRINT_ALL, "ZZQuake2RIX: rix_alpha %d -> %s (self-test %s)\n", mode,
                      on ? "alpha-keyed textures RGBA5551" : "RGB565 everywhere",
                      aok > 0 ? "passed" : aok == 0 ? "FAILED" : "not run");
    }
    *pwidth = width;
    *pheight = height;
    ri.Vid_NewWindow(width, height);
    return rserr_ok;
}

void GLimp_Shutdown(void)
{
    /* the GPU context is kept: R_Shutdown is also used by vid_restart.
       0.7: the performance report of the run so far goes to baseq2/rixstat.txt (quit and vid_restart
       come through here; the file is rewritten each time) */
    static char rep[4096];
    char path[128];
    FILE *f;
    zz_rix_td_flush();
    if (zz_rix_td_take()) zz_write_td();
    zz_rix_perf_report(rep, (int)sizeof(rep));
    if (!rep[0]) return;
    Com_sprintf(path, sizeof(path), "%s/rixstat.txt", ri.FS_Gamedir());
    f = fopen(path, "w");
    if (f) { fprintf(f, "ZZQuake2RIX %s performance report\n%s", ZZQ2RIX_VERSION, rep); fclose(f); }
}

void GLimp_BeginFrame(float camera_separation)
{
    (void)camera_separation;
    /* registration_sequence (gl_model.c): one more at every map load (R_BeginRegistration) */
    zz_rix_set_timedemo(zz_timedemo && zz_timedemo->value != 0.0f, registration_sequence);
    zzgl_set_profile(zz_prof && zz_prof->value != 0.0f);
    zzgl_set_deferred(zz_defer && zz_defer->value != 0.0f);
    zz_rix_set_sync(zz_sync && zz_sync->value != 0.0f);
    zzgl_set_tex_versioning(zz_texver && zz_texver->value != 0.0f);
    zz_rix_set_overlap(zz_overlap && zz_overlap->value != 0.0f);
    zzgl_set_clear_flush(zz_clearflush && zz_clearflush->value != 0.0f);
    if (zz_pump) rix_frame_set_pump_interval((unsigned)(zz_pump->value > 0.0f ? zz_pump->value : 1.0f));
    zz_rix_begin_frame();
}

void GLimp_EndFrame(void)
{
    zzq2_present_burst = (zz_present == 0 || zz_present->value != 0.0f);
    if (zz_fillrule) rix_fill_rule = (zz_fillrule->value != 0.0f);
    if (zz_wnorm) rix_tex_wnorm = (zz_wnorm->value != 0.0f);
    zz_rix_end_frame();
    if (zz_rix_td_take()) zz_write_td();
}

void GLimp_AppActivate(qboolean active)
{
    (void)active;
}
