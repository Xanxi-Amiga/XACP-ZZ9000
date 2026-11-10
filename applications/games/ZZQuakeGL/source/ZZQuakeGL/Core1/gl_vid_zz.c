/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * gl_vid_zz.c - ZZQuakeGL: GLQuake's video layer on the ZZ9000 Core1.
 *
 * Replaces gl_vidnt.c / gl_vidlinux.c of id's WinQuake for GLQuake: no
 * window, no context, no mouse or keyboard (the release platform feeds
 * Sys_SendKeyEvents and in_null.c).  The GPU draws into a 640x481 RGB565
 * colour buffer of the G4B window (Core1/rix/zzq2rix_gpu.c, vendored from
 * ZZQuake2RIX 0.10.3); GL_EndRendering closes the frame, waits for its
 * fence and hands the buffer to the presentation path (zzqgl_present.c),
 * which converts it into the P96 buffer the launcher granted, with the
 * release triple-buffer handshake.  Launcher ZZQuake640 unchanged.
 *
 * Modelled on gl_vidlinux.c (the smallest of id's GL video files) and on
 * zzq2rix_video.c (the Q2RIX GLimp: cvars rix_*, self-test, reports).
 *
 * The screen is 640 x 480, opened by the launcher before Core1 starts;
 * the console is at the same size (no -conwidth).
 *
 * ASCII only.
 */

#include "quakedef.h"
#include "zzquake_config.h"
#include "zzqgl_diag.h"
#include "zzqgl_version.h"

/* ---- zzq2rix_gpu.c (vendored, no Quake header on that side) ------ */
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
#if ZZQGL_VID_W > 800
extern int  zz_rix_fbprobe(char *report, int cap);
extern void zz_rix_guard_report(char *out);
extern unsigned int rix_texpool_free_pages(void);
static unsigned zzqgl_pool_min_free = 0xFFFFFFFFu;
#endif
extern void zz_rix_set_sync(int on);
extern void zz_rix_set_ring(int mode);
extern void zz_rix_set_overlap(int on);
/* ---- ZZGL / librixgpu ------------------------------------------- */
extern void zzgl_set_profile(int on);
extern void zzgl_set_deferred(int on);
extern void zzgl_set_tex_versioning(int on);
extern void zzgl_set_clear_flush(int on);
extern void rix_frame_set_pump_interval(unsigned n);
extern int  rix_fill_rule, rix_tex_wnorm;
/* ---- zzqgl_gl.c ------------------------------------------------- */
extern void zzqgl_gl_set_filter(int mode);
extern void zzqgl_gl_set_filter2d(int on);
extern void zzqgl_gl_set_texbias2d(int on);
extern void zzqgl_gl_set_lightmaps(unsigned first, unsigned count);
extern unsigned zzqgl_n_teximage, zzqgl_n_teximage_mip, zzqgl_n_texsub, zzqgl_n_texsmall;
extern unsigned zzqgl_n_polygon, zzqgl_n_depthfunc_other, zzqgl_n_texenv_other;
extern unsigned zzqgl_n_drawbuffer_front, zzqgl_n_fog;
/* ---- zzqgl_present.c -------------------------------------------- */
extern int zzq2_present_burst;
extern int zzqgl_present_special;
extern int zzqgl_present_must_show;
extern void zzqgl_present_show_stats(unsigned *waits, unsigned *timeouts);
/* ---- gl_screen.c ------------------------------------------------- */
extern qboolean scr_drawloading, scr_drawdialog;
/* ---- zzqgl_qgmain.c --------------------------------------------- */
extern void zzqgl_crumb(unsigned int code);
extern volatile unsigned int zzqgl_newmap_count;   /* R_NewMap calls (interposed), one per map load */
/* ---- release platform ------------------------------------------- */
extern volatile unsigned int *shared;
extern void QG_SetPalette(unsigned char palette[768]);

/* ---- what glquake.h expects from the video layer ----------------- */
unsigned short	d_8to16table[256];
unsigned		d_8to24table[256];
unsigned char	d_15to8table[65536];	/* GL_Upload8_EXT / GL_MipMap8Bit only (is8bit false): never filled */

int		texture_mode = GL_LINEAR;
int		texture_extension_number = 1;
float	gldepthmin, gldepthmax;

/* gl_ztrick: GLQuake's default, 1 (no depth clear, alternating depth ranges).  ZZQuakeGL 0.1
   started at 0 (explicit depth clear every frame); 1 measured on the A4000 (J4, 09/11/2026):
   43.2 -> 47.8 fps on timedemo demo1, e1m1 played without a defect.  "gl_ztrick 0" in the
   console takes effect at the next frame. */
cvar_t	gl_ztrick = {"gl_ztrick", "1"};
cvar_t	vid_mode = {"vid_mode", "0", false};

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

qboolean is8bit = false;
qboolean isPermedia = false;
qboolean gl_mtexable = false;
/* qglMTexCoord2fSGIS / qglSelectTextureSGIS: defined NULL in gl_rsurf.c */

#ifndef ZZQGL_VID_W                 /* 0.4: build_arm.sh ZZQGL_RES (320, 640, 800) */
#define ZZQGL_VID_W 640
#define ZZQGL_VID_H 480
#endif
int scr_width = ZZQGL_VID_W, scr_height = ZZQGL_VID_H;
static qboolean gl_opened;

/* gl_draw.c / gl_rmisc.c: 8-bit paletted texture uploads (3dfx extension): never here */
qboolean VID_Is8bit(void) { return is8bit; }

/* ---- ZZQuakeGL cvars (names of ZZQuake2RIX, same meanings) ------------
   0.1 (J2) ran everything direct: rix_defer 0, rix_overlap 0, rix_clearflush 0, gl_ztrick 0.
   0.2 defaults = the J4 measurements on the A4000 (09/11/2026, timedemo demo1, DEPTH=16,
   one change per timedemo): direct 34.2 fps; + rix_overlap 35.1; + rix_defer 42.1;
   + rix_clearflush 43.2; + gl_ztrick 47.8 (software ZZQuake640: 41.9); e1m1 played without
   a defect.  Each one goes back to 0 in the console (next frame) if a display defect shows.
   rix_texver stays 0: 36 lightmap sub-uploads per 1000 frames, the drains cost 47 us/frame. */
static cvar_t rix_present    = {"rix_present",    "1", true};   /* 1 burst read + table, 0 word path */
static cvar_t rix_fillrule   = {"rix_fillrule",   "0", false};
static cvar_t rix_wnorm      = {"rix_wnorm",      "0", false};
static cvar_t rix_prof       = {"rix_prof",       "0", false};
static cvar_t rix_defer      = {"rix_defer",      "1", false};
static cvar_t rix_sync       = {"rix_sync",       "0", false};
static cvar_t rix_texver     = {"rix_texver",     "0", false};
static cvar_t rix_overlap    = {"rix_overlap",    "1", false};
static cvar_t rix_clearflush = {"rix_clearflush", "1", false};
static cvar_t rix_pump       = {"rix_pump",       "32", false};
static cvar_t rix_ring       = {"rix_ring",       "1", true};    /* 1 = 32 x 16 KiB chunks (Q2RIX 0.9.3+), 0 = 8 x 64 KiB; start-up only */
static cvar_t rix_alpha      = {"rix_alpha",      "2", true};    /* 0 RGB565, 1 RGBA5551, 2 RGBA5551 if the self-test passed */
static cvar_t rix_filter     = {"rix_filter",     "2", true};    /* 0 nearest, 1 lightmaps linear, 2 as GLQuake asks */
static cvar_t rix_filter2d   = {"rix_filter2d",   "0", true};    /* 0 2D nearest, 1 2D as rix_filter */
static cvar_t rix_texbias2d  = {"rix_texbias2d",  "1", true};
static cvar_t rix_report     = {"rix_report",     "1", true};    /* 1 = write id1/rixtest.txt, rixstat.txt, rixtd.txt */

static void con_line(const char *line) { Con_Printf("%s\n", line); }

/* ---- small helpers ---------------------------------------------------- */
static void write_text(const char *name, const char *title, const char *text)
{
    char path[MAX_OSPATH];
    FILE *f;
    if (!rix_report.value) return;
    sprintf(path, "%s/%s", com_gamedir, name);
    f = fopen(path, "w");
    if (!f) { Con_Printf("ZZQuakeGL: could not write %s\n", path); return; }
    fprintf(f, "ZZQuakeGL %s %s\n", ZZQGL_VERSION, title);
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    Con_Printf("ZZQuakeGL: %s written\n", path);
}

static void print_lines(const char *rep)
{
    const char *p, *q;
    for (p = rep; *p; p = q) {
        char line[200]; int n = 0;
        for (q = p; *q && *q != '\n'; q++) if (n < (int)sizeof(line) - 1) line[n++] = *q;
        line[n] = 0;
        if (*q) q++;
        Con_Printf("%s\n", line);
    }
}

static void write_td(void)
{
    static char rep[33000];
    const char *t = zz_rix_td_text();
    int n = (int)strlen(t);
    if (n > (int)sizeof(rep) - 100) n = (int)sizeof(rep) - 100;
    memcpy(rep, t, n); rep[n] = 0;
    if (zz_rix_td_lost()) sprintf(rep + n, "(%u later reports not kept: buffer full)\n", zz_rix_td_lost());
    write_text("rixtd.txt", "timedemo reports (one per timedemo, in order)", rep);
}

/* ---- direct-rect, pause: nothing to do -------------------------------- */
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{ (void)x; (void)y; (void)pbitmap; (void)width; (void)height; }
void D_EndDirectRect (int x, int y, int width, int height)
{ (void)x; (void)y; (void)width; (void)height; }

/* ---- palette ----------------------------------------------------------- */
void VID_SetPalette (unsigned char *palette)
{
    byte     *pal;
    unsigned r, g, b, v;
    int      i;
    unsigned *table;

    /* 8 8 8 encoding, RGBA bytes in memory (what GL_Upload32 hands to glTexImage2D) */
    pal = palette;
    table = d_8to24table;
    for (i = 0; i < 256; i++) {
        r = pal[0]; g = pal[1]; b = pal[2];
        pal += 3;
        v = (255 << 24) + (r << 0) + (g << 8) + (b << 16);
        *table++ = v;
    }
    d_8to24table[255] &= 0xffffff;	/* 255 is transparent */
    /* d_15to8table: only the 8-bit paletted upload paths read it; never taken here */

    /* the release LUTs and SH_STATUS = PALETTE_OK, as the software blob does */
    QG_SetPalette(palette);
    zzqgl_crumb(ZZQGL_DIAG_PALETTE);
}

void VID_ShiftPalette (unsigned char *palette)
{
    /* GLQuake tints the view with R_PolyBlend (gl_polyblend 1); the gamma ramps of
       gl_vidnt.c need a hardware palette the ZZ9000 RTG screen does not have */
    (void)palette;
}

/* ---- GL state defaults (gl_vidlinux.c GL_Init, unchanged) ---------------- */
void GL_Init (void)
{
    gl_vendor = (const char *)glGetString (GL_VENDOR);
    Con_Printf ("GL_VENDOR: %s\n", gl_vendor);
    gl_renderer = (const char *)glGetString (GL_RENDERER);
    Con_Printf ("GL_RENDERER: %s\n", gl_renderer);
    gl_version = (const char *)glGetString (GL_VERSION);
    Con_Printf ("GL_VERSION: %s\n", gl_version);
    gl_extensions = (const char *)glGetString (GL_EXTENSIONS);
    Con_Printf ("GL_EXTENSIONS: %s\n", gl_extensions);

    /* no multitexture, no texture objects extension: GLQuake's single-TMU paths */
    gl_mtexable = false;

    glClearColor (1,0,0,0);
    glCullFace(GL_FRONT);
    glEnable(GL_TEXTURE_2D);

    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.666);

    glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
    glShadeModel (GL_FLAT);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    zzqgl_crumb(ZZQGL_DIAG_GL_INIT);
}

/* ---- 0.2: a first frame for the launcher's watchdog ------------------------
   The launcher ends the run after 5 s + 3 s without a frame (SH_RENDERS).  On the A4000,
   Host_Init (GPU open, self-test, some 130 texture uploads of the HUD and console, the
   mesh of the beam models) followed by the first map load reached that window on the
   first two runs of 0.1: the watchdog fired, the first frame arrived during the 3 s of
   grace.  A black frame is handed over here, through the normal path (begin, one quad,
   end: fence waited, presentation), as soon as the GPU is open and before anything
   slow; from the first file GLQuake loads after Draw_Init, the keepalive of
   zzqgl_present.c repeats it at most once a second until the console appears.
   Presented at once: the overlap of the frames (rix_overlap) is switched on afterwards,
   by apply_cvars.  The quad is in clip space (identity matrices after GL_Init), the
   depth test and the cull are still off (GLQuake enables them in R_SetupGL). */
static void early_frame (void)
{
    zz_rix_set_overlap(0);
    zz_rix_begin_frame();
    glDisable (GL_TEXTURE_2D);
    glColor4f (0, 0, 0, 1);
    glBegin (GL_QUADS);               /* a little beyond the clip volume: clipped to the whole viewport */
    glVertex3f (-1.1f, -1.1f, 0);
    glVertex3f ( 1.1f, -1.1f, 0);
    glVertex3f ( 1.1f,  1.1f, 0);
    glVertex3f (-1.1f,  1.1f, 0);
    glEnd ();
    glColor4f (1, 1, 1, 1);
    glEnable (GL_TEXTURE_2D);
    glFlush ();
    zzqgl_present_special = 1;
    zz_rix_end_frame();
    zzqgl_present_special = 0;
}

/* ---- the cvars, applied every frame (GL_BeginRendering) ----------------- */
static void apply_cvars (void)
{
    zzgl_set_profile(rix_prof.value != 0);
    zzgl_set_deferred(rix_defer.value != 0);
    zz_rix_set_sync(rix_sync.value != 0);
    zzgl_set_tex_versioning(rix_texver.value != 0);
    zz_rix_set_overlap(rix_overlap.value != 0);
    zzgl_set_clear_flush(rix_clearflush.value != 0);
    rix_frame_set_pump_interval((unsigned)(rix_pump.value > 0 ? rix_pump.value : 1));
    zzq2_present_burst = (rix_present.value != 0);
    rix_fill_rule = (rix_fillrule.value != 0);
    rix_tex_wnorm = (rix_wnorm.value != 0);
    zzqgl_gl_set_filter((int)rix_filter.value);
    zzqgl_gl_set_filter2d(rix_filter2d.value != 0);
    zzqgl_gl_set_texbias2d(rix_texbias2d.value != 0);
    {   /* lightmap texture numbers for the filter policy (gl_rsurf.c allocates MAX_LIGHTMAPS = 64 once) */
        extern int lightmap_textures;
        zzqgl_gl_set_lightmaps((unsigned)lightmap_textures, lightmap_textures ? 64u : 0u);
    }
}

/* ---- frame cycle --------------------------------------------------------- */
void GL_BeginRendering (int *x, int *y, int *width, int *height)
{
    *x = *y = 0;
    *width = scr_width;
    *height = scr_height;
    if (!gl_opened) return;
    apply_cvars();
    zz_rix_set_timedemo(cls.timedemo ? 1 : 0, (int)zzqgl_newmap_count);
    zz_rix_begin_frame();
}

void GL_EndRendering (void)
{
    if (!gl_opened) return;
    glFlush();
    if (scr_drawloading || scr_drawdialog) {
        /* 0.2: the loading plaque and the modal dialogs ("quit? y/n") are the last frame for a
           while.  With rix_overlap 1 a frame is shown at the end of the next one: this one is
           shown now (the pending frame first), and never dropped for want of a buffer
           (zzqgl_present_must_show).  apply_cvars puts the overlap back at the next frame. */
        zz_rix_set_overlap(0);
        zzqgl_present_must_show = 1;
    }
    zz_rix_end_frame();            /* frame fence, wait, presentation (zzqgl_present.c) */
    zzqgl_present_must_show = 0;
#if ZZQGL_VID_W > 800
    {   /* 0.5, 1024: the low-water mark of the 12 MiB texture pool (arbitration: measure it) */
        unsigned f = rix_texpool_free_pages();
        if (f < zzqgl_pool_min_free) zzqgl_pool_min_free = f;
    }
#endif
    if (zz_rix_td_take()) write_td();
}

/* ---- init / shutdown ------------------------------------------------------- */
static void Check_Gamma (unsigned char *pal)
{
    float f, inf;
    unsigned char palette[768];
    int i;
    float vid_gamma = 1.0;

    if ((i = COM_CheckParm("-gamma")) == 0)
        return;
    vid_gamma = Q_atof(com_argv[i+1]);
    if (vid_gamma == 1.0)
        return;
    for (i=0 ; i<768 ; i++) {
        f = pow ( (pal[i]+1)/256.0 , vid_gamma );
        inf = f*255 + 0.5;
        if (inf < 0) inf = 0;
        if (inf > 255) inf = 255;
        palette[i] = inf;
    }
    memcpy (pal, palette, sizeof(palette));
}

void VID_Init (unsigned char *palette)
{
    char why[96];
    char gldir[MAX_OSPATH];

    zzqgl_crumb(ZZQGL_DIAG_VID_INIT);
    ZZQGL_TL_MARK(ZZQGL_TL_VID_INIT);
    Cvar_RegisterVariable (&vid_mode);
    Cvar_RegisterVariable (&gl_ztrick);
    Cvar_RegisterVariable (&rix_present);
    Cvar_RegisterVariable (&rix_fillrule);
    Cvar_RegisterVariable (&rix_wnorm);
    Cvar_RegisterVariable (&rix_prof);
    Cvar_RegisterVariable (&rix_defer);
    Cvar_RegisterVariable (&rix_sync);
    Cvar_RegisterVariable (&rix_texver);
    Cvar_RegisterVariable (&rix_overlap);
    Cvar_RegisterVariable (&rix_clearflush);
    Cvar_RegisterVariable (&rix_pump);
    Cvar_RegisterVariable (&rix_ring);
    Cvar_RegisterVariable (&rix_alpha);
    Cvar_RegisterVariable (&rix_filter);
    Cvar_RegisterVariable (&rix_filter2d);
    Cvar_RegisterVariable (&rix_texbias2d);
    Cvar_RegisterVariable (&rix_report);

    vid.maxwarpwidth = 320;
    vid.maxwarpheight = 200;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

    scr_width = ZZQGL_VID_W;              /* 0.4: the size of this blob */
    scr_height = ZZQGL_VID_H;
    vid.conwidth = ZZQGL_VID_W;           /* GLQuake's default: the console at the screen size */
    vid.conheight = ZZQGL_VID_H;
    vid.width = vid.conwidth;
    vid.height = vid.conheight;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
    vid.numpages = 2;

    /* the GPU: MMU attributes, librixgpu ladder, chunk ring, ZZGL (zzq2rix_gpu.c; a failure
       ends the run through the release Sys_Error with the librixgpu code) */
    zz_rix_set_ring((int)rix_ring.value);
    why[0] = 0;
    if (!zz_rix_open(scr_width, scr_height, why, (int)sizeof(why)))
        Sys_Error("ZZQuakeGL: RasterIX could not open %dx%d: %s", scr_width, scr_height, why);
    gl_opened = true;
    zzqgl_crumb(ZZQGL_DIAG_GPU_OPEN);
    ZZQGL_TL_MARK(ZZQGL_TL_GPU_OPEN);

    GL_Init ();
    early_frame ();                 /* 0.2: the launcher's watchdog has its first frame */
    Con_Printf ("ZZQuakeGL %s: RasterIX G4C %dx%d (ZZGL %s, librixgpu %s)\n",
                ZZQGL_VERSION, scr_width, scr_height, ZZQGL_ZZGL_VERSION, ZZQGL_RIX_VERSION);
    zz_rix_console_report(con_line);

    /* texel self-test, once, before GLQuake uploads anything: console + id1/rixtest.txt */
    {
        static char rep[4096];
        static char mb[1024];
        int ok = zz_rix_textest(rep, (int)sizeof(rep));
        zz_rix_microbench(mb, (int)sizeof(mb));
        if (strlen(rep) + strlen(mb) < sizeof(rep)) strcat(rep, mb);
#if ZZQGL_VID_W > 800
        /* 0.5, 1024 x 768: proof of the layout (colour and Z buffers in the arena) before the game;
           a failure stops the run after rixtest.txt is written (arbitration of 10/11/2026) */
        {
            static char fp[1024];
            int fok = zz_rix_fbprobe(fp, (int)sizeof(fp));
            if (strlen(rep) + strlen(fp) < sizeof(rep)) strcat(rep, fp);
            print_lines(rep);
            write_text("rixtest.txt", "texel self-test", rep);
            if (!fok) Sys_Error("ZZQGL 1024: layout probe FAILED (id1/rixtest.txt)");
            GL_Init ();             /* the GL state of GLQuake's start, after the probe's own */
        }
#else
        print_lines(rep);
        write_text("rixtest.txt", "texel self-test", rep);
#endif
        Con_Printf("ZZQuakeGL: texel test %s\n", ok ? "ALL OK" : "MISMATCHES");
    }
    ZZQGL_TL_MARK(ZZQGL_TL_TESTS);
    /* alpha-keyed textures (console font, HUD, menu pics with index 255, particles, sprites,
       the sky's alpha layer) as RGBA5551 so that the alpha test cuts their transparent texels */
    {
        int mode = (int)rix_alpha.value, aok = zz_rix_alpha_ok(), on;
        on = (mode == 1) || (mode == 2 && aok > 0);
        zz_rix_set_alpha_textures(on);
        Con_Printf("ZZQuakeGL: rix_alpha %d -> %s (self-test %s)\n", mode,
                   on ? "alpha-keyed textures RGBA5551" : "RGB565 everywhere",
                   aok > 0 ? "passed" : aok == 0 ? "FAILED" : "not run");
    }
    apply_cvars();

    sprintf (gldir, "%s/glquake", com_gamedir);
    Sys_mkdir (gldir);

    Check_Gamma(palette);
    VID_SetPalette(palette);

    Con_SafePrintf ("Video mode %dx%d initialized.\n", scr_width, scr_height);
    vid.recalc_refdef = 1;				/* force a surface cache flush */
    ZZQGL_TL_MARK(ZZQGL_TL_VID_DONE);
}

/* 0.2: the init timeline (zzqgl_diag.h), in ms from QG_Create, at the end of rixstat.txt */
static void timeline_text (char *out, int cap)
{
    static const char *names[ZZQGL_TL_N] = {
        "QG_Create entered", "Host_Init called", "VID_Init entered", "GPU open (zz_rix_open)",
        "black frame accepted by the launcher", "texel self-test + micro-benchmark done", "VID_Init returned",
        "Host_Init returned", "first real frame handed over", "first map loaded (R_NewMap)",
        "first frame with the world handed over"
    };
    int i, n = 0;
    n += sprintf (out + n, "init timeline, ms from QG_Create (launcher watchdog: 5 s + 3 s without a frame):\n");
    for (i = 0; i < ZZQGL_TL_N && n < cap - 80; i++) {
        if (!zzqgl_tl[i]) { n += sprintf (out + n, "  %-40s -\n", names[i]); continue; }
        {
            unsigned d = zzqgl_tl[i] - zzqgl_tl[ZZQGL_TL_CREATE];
            n += sprintf (out + n, "  %-40s %u.%03u\n", names[i], d / 1000u, d % 1000u);
        }
    }
}

/* the GPU is closed by zzqgl_qgmain.c (end of run); here the reports of the run */
void VID_Shutdown (void)
{
    static char rep[5120];
    char extra[1024];
    if (!gl_opened) return;
    zz_rix_td_flush();
    if (zz_rix_td_take()) write_td();
    zz_rix_perf_report(rep, 4096);
    sprintf(extra, "gl: teximage %u (mip levels dropped %u, enlarged %u) texsub %u polygons %u "
                   "depthfunc-other %u texenv-other %u drawbuffer-front %u fog %u\n",
            zzqgl_n_teximage, zzqgl_n_teximage_mip, zzqgl_n_texsmall, zzqgl_n_texsub, zzqgl_n_polygon,
            zzqgl_n_depthfunc_other, zzqgl_n_texenv_other, zzqgl_n_drawbuffer_front, zzqgl_n_fog);
    if (strlen(rep) + strlen(extra) < sizeof(rep)) strcat(rep, extra);
    {
        unsigned w, t;
        zzqgl_present_show_stats(&w, &t);
        sprintf(extra, "plaque / dialog frames: waited for a buffer %u times, %u timeouts (100 ms)\n", w, t);
        if (strlen(rep) + strlen(extra) < sizeof(rep)) strcat(rep, extra);
    }
#if ZZQGL_VID_W > 800
    zz_rix_guard_report(extra);
    if (strlen(rep) + strlen(extra) < sizeof(rep)) strcat(rep, extra);
    sprintf(extra, "1024 texture pool: 6143 pages of 2 KiB, fewest free %u, free at exit %u"
                   " (0 = the pool was full: textures evicted and uploaded again)\n",
            zzqgl_pool_min_free == 0xFFFFFFFFu ? 0u : zzqgl_pool_min_free, rix_texpool_free_pages());
    if (strlen(rep) + strlen(extra) < sizeof(rep)) strcat(rep, extra);
#endif
    timeline_text(extra, (int)sizeof(extra));
    if (strlen(rep) + strlen(extra) < sizeof(rep)) strcat(rep, extra);
    if (rep[0]) write_text("rixstat.txt", "performance report", rep);
}
