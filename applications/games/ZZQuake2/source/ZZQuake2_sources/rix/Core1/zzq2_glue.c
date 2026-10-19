/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2_glue.c - ZZQuake2 A0: the thin adapter between the UNCHANGED
 * ZZQuake640 release platform and quake2generic.
 *
 * ZZQuake2TGL: this copy is built with -DZZQ2_TGL. Three blocks differ
 * from ZZQuake2 A0, each under #ifdef ZZQ2_TGL: the renderer profile
 * interposers (ref_gl functions instead of ref_soft ones), the 32-bit
 * presentation zzq2_present32() (TinyGL draws RGB, not palette
 * indices), and the command line given to Quake II (gl_mode).
 *
 * ZZQuake2RIX: built with -DZZQ2_RIX. Same ref_gl interposers and the
 * same presentation block (the GPU's RGB565 buffer is the source:
 * zzq2_present_rgb565), the Quake II heap limited to 0x3D800000 so that
 * the 20 MiB under .q2bss belong to the GPU (ZZQUAKE2RIX_MEMORY_MAP.md),
 * the pak end published for the GPU side (zzq2_last_pak_end), the GPU
 * closed and the MMU attributes restored at the end of the run.
 * ZZQuake2RIX 0.10: the save games, the screen size guard and the frame
 * size of ZZQuake2 A0 0.7.1 (zzq2_save.c, zzq2_res.h), and the RGB565
 * presentation at ZZQ2_VID_W x ZZQ2_VID_H (640x480, 320x240, 800x600).
 *
 * The release zzquake_main.c is compiled as it is. It still calls
 *
 *     QG_Create(argc, argv);          <- the fork point
 *     while (zzq_running) QG_Tick(dt);
 *     Host_Shutdown();
 *
 * Those three names used to be provided by zzquake_qgmain.c and the
 * Quake I engine. Here they are provided by this file and lead to
 * Quake2_Init / Quake2_Frame. Nothing before the QG_Create call is
 * touched: entry stub, BSS clear, vectors, MMU, shared slots, preload
 * handshake and pak check are the release code.
 *
 * Everything this file needs from the platform is taken from the
 * release functions, called, never copied:
 *     QG_GetKey        key queue reader          (zzquake_platform.c)
 *     QG_GetMouseMove  mouse totals -> deltas    (zzquake_platform.c)
 *     QG_SetPalette    RGB32 + RGB565 LUTs       (zzquake_platform.c)
 *     QG_DrawFrame     convert, clean, handshake (zzquake_platform.c)
 *     Sys_FloatTime    Cortex-A9 Global Timer    (zzquake_platform.c)
 *     zzq_fopen ...    memory filesystem         (zzquake_fs_mem.c)
 *     zzq_fatal_end    exit path                 (zzquake_main.c)
 *
 * No Quake II header is included here on purpose: this file sees the
 * release side only. The Quake II side of the video backend is in
 * zzq2_video.c.
 *
 * ASCII only.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "zzquake_config.h"
#include "zzq2_diag.h"
#include "zzq2_res.h"

typedef unsigned int  u32;
typedef unsigned char u8;

/* ---- release platform (ZZQuake640), unchanged ------------------- */
extern volatile u32 *shared;
extern volatile int  zzq_running;
extern void   zzq_fatal_end(u32 code);
extern void   zzq_diag_stage_d(void);
extern u32    zzq_us_now(void);
extern double Sys_FloatTime(void);
extern int    QG_GetKey(int *down, int *key);
extern void   QG_GetMouseMove(int *x, int *y);
extern void   QG_SetPalette(unsigned char palette[768]);
extern void   QG_DrawFrame(void *pixels);
extern void   zzq_publish_stats(void);
extern int    zzq_printf(const char *fmt, ...);
extern FILE  *zzq_fopen(const char *path, const char *mode);
extern int    zzq_fclose(FILE *f);
extern size_t zzq_fread(void *dst, size_t sz, size_t n, FILE *f);
extern size_t zzq_fwrite(const void *src, size_t sz, size_t n, FILE *f);
extern void   Sys_Error(char *error, ...);

/* ---- zzq2_save.c (A0 0.6, RIX 0.10) ------------------------------- */
extern FILE  *zzq2_save_fopen(const char *path, const char *mode);

/* ---- quake2generic ---------------------------------------------- */
extern void Quake2_Init(int argc, char **argv);
extern void zzq2_lock_video(int gl);   /* gen/vid_menu_zz.c (zzq2_patch_vidmenu.py) */
extern void Quake2_Frame(int msec);
extern void Quake2_SendKey(int key, int down);
extern int  Com_ServerState(void);

/* ---- 8-byte stack alignment for the engine (0.8.1) ----------------
   The release enters Core1 with SP = 0x05ffff4c, 4 bytes off the
   8-byte alignment the AAPCS requires at every public call. GCC's
   va_arg(ap, double) rounds the argument pointer to an ABSOLUTE
   multiple of 8, so on that stack every double passed through "..."
   is read 4 bytes off: the timedemo line printed "661 frames, 0.0
   seconds: 0.0 fps", and Cvar_SetValue() of any non-integer value
   (menu sliders) stored a wrong string. Reproduced off-board with this
   toolchain's newlib vsprintf: wrong from SP = 4 mod 8, right from an
   aligned SP, right again through the trampoline below.
   zzq2_call_aligned(fn, a0, a1) calls fn(a0, a1) with SP rounded down
   to 8 bytes and puts the caller's SP back on return. The three entry
   points of the engine (Init, Frame, SendKey) go through it; nothing
   in the engine or in the release changes. */
#if defined(__arm__)
extern void zzq2_call_aligned(void *fn, void *a0, void *a1);
__asm__(
"\t.section .text.zzq2_call_aligned,\"ax\",%progbits\n"
"\t.arm\n"
"\t.align 2\n"
"\t.global zzq2_call_aligned\n"
"\t.type zzq2_call_aligned, %function\n"
"zzq2_call_aligned:\n"
"\tpush {r4, lr}\n"
"\tmov  r4, sp\n"
"\tbic  r12, r4, #7\n"
"\tmov  sp, r12\n"
"\tmov  r12, r0\n"
"\tmov  r0, r1\n"
"\tmov  r1, r2\n"
"\tblx  r12\n"
"\tmov  sp, r4\n"
"\tpop  {r4, pc}\n"
"\t.size zzq2_call_aligned, .-zzq2_call_aligned\n"
"\t.text\n");
static void q2_init_aligned(int argc, char **argv)
{ zzq2_call_aligned((void *)Quake2_Init, (void *)argc, (void *)argv); }
static void q2_frame_aligned(int msec)
{ zzq2_call_aligned((void *)Quake2_Frame, (void *)msec, (void *)0); }
static void q2_sendkey_aligned(int key, int down)
{ zzq2_call_aligned((void *)Quake2_SendKey, (void *)key, (void *)down); }
#else
#define q2_init_aligned    Quake2_Init
#define q2_frame_aligned   Quake2_Frame
#define q2_sendkey_aligned Quake2_SendKey
#endif

/* ---- zzq2_mem.c -------------------------------------------------- */
extern void zzq2_arena_init(u32 lo, u32 hi);
extern u32  zzq2_arena_lo(void);
extern u32  zzq2_arena_hi(void);
extern u32  zzq2_arena_peak(void);
extern u32  zzq2_arena_top(void);
extern void *zzq2_malloc(size_t size);

/* ---- arena layout ------------------------------------------------- */
/* Least amount of memory that must be left to the engine between the
   end of the pak and .q2bss. Same value as ZZQ2_HEAP_MIN of the
   launcher (launcher/zzq2_patch_launcher.py); build_launcher.sh checks
   that the two agree. 32 MiB: ref_soft reserves 16 MiB in one piece
   for every map it loads, on top of what is already allocated. */
#define ZZQ2_HEAP_MIN  0x02000000u

/* ---- linker script (zzq2a0.ld) ----------------------------------- */
extern u32 _q2bss_start, _q2bss_end;

#ifdef ZZQ2_RIX
/* ZZQuake2RIX: the engine heap stops where the GPU reservation starts
   (ZZQUAKE2RIX_MEMORY_MAP.md: pool 0x3D800000, ring 0x3E800000, Z,
   bind lists, up to .q2bss at 0x3EC00000) */
#define ZZQ2RIX_HEAP_LIMIT 0x3D800000u
static u32 g_pack_end;
unsigned zzq2_last_pak_end(void) { return g_pack_end; }
#endif

/* ------------------------------------------------------------------
   Symbols the release files expect from the Quake I engine.
   They are only read by diagnostic code.
   ------------------------------------------------------------------ */

/* QG_DrawFrame waits inside a modal dialog when this is set. Quake II
   has no SCR_ModalMessage, so it stays 0: the gate never blocks. */
int scr_drawdialog = 0;

/* zzq_wad_probe() (called by the release Sys_Printf) reports
   "gfx.wad not loaded" when these are null, and returns. */
unsigned char *wad_base     = 0;
int            wad_numlumps = 0;
void          *wad_lumps    = 0;

/* Called by the release fd_read() on every large pak read.
   The three slots keep their release meaning. SH_SV_ACTIVE is the
   boolean "a local server is running" (sv.active under Quake I).
   Quake II has no client-side demo playback flag and no demo loop
   index: the two other slots are written 0, as an idle Quake I. */
void zzq_publish_engine_state(void)
{
    shared[SH_SV_ACTIVE]    = Com_ServerState() ? 1u : 0u;
    shared[SH_DEMOPLAYBACK] = 0;
    shared[SH_DEMONUM]      = 0;
}

/* Used by the release zzq_fprintf() for %f and %g.
   Taken as it is from zzquake_qgmain.c (ZZQuake640 release). */
void zzq_ftoa_prec(char *dst, int cap, float v, int prec)
{
    int neg = 0, i = 0, j, n, k;
    long ip, fp, scale = 1;
    char tmp[16];
    if (cap < 4) { if (cap > 0) dst[0] = 0; return; }
    if (prec < 0) prec = 0;
    if (prec > 6) prec = 6;
    for (k = 0; k < prec; k++) scale *= 10;
    if (v != v) { dst[0]='n'; dst[1]='a'; dst[2]='n'; dst[3]=0; return; }
    if (v < 0.0f) { neg = 1; v = -v; }
    if (v > 2000000000.0f) v = 2000000000.0f;
    ip = (long)v;
    fp = (long)((v - (float)ip) * (float)scale + 0.5f);
    if (fp >= scale) { fp = 0; ip++; }
    if (neg) dst[i++] = '-';
    n = 0;
    if (ip == 0) tmp[n++] = '0';
    while (ip > 0 && n < 15) { tmp[n++] = (char)('0' + (ip % 10)); ip /= 10; }
    for (j = n - 1; j >= 0 && i < cap - 2; j--) dst[i++] = tmp[j];
    if (prec > 0 && i < cap - 2) {
        dst[i++] = '.';
        for (j = prec - 1; j >= 0 && i < cap - 1; j--) {
            long d = fp;
            for (k = 0; k < j; k++) d /= 10;
            dst[i++] = (char)('0' + (d % 10));
        }
    }
    dst[i] = 0;
}

/* ------------------------------------------------------------------
   Breadcrumbs
   ------------------------------------------------------------------ */

static int g_init_done;       /* Quake2_Init() has returned          */
static int g_first_endframe;  /* SWimp_EndFrame() has run once       */
static int g_first_present;   /* a frame passed the release gate     */

static void q2_diag(u32 code)
{
    shared[SH_DIAG] = code;
    __asm__ volatile("dsb" ::: "memory");
}

/* Init-time breadcrumb: silent once Quake2_Init() has returned, so
   that Q2_FIRST_FRAME is never overwritten by a later mode change. */
void zzq2_crumb(unsigned int code)
{
    if (!g_init_done) q2_diag((u32)code);
}

/* A gate: the code in SH_DIAG and its name in the "last printf"
   slots, which the launcher prints with any fault report. */
static void q2_gate(u32 code, const char *name)
{
    zzq_printf("%s\n", name);
    q2_diag(code);
}

/* ------------------------------------------------------------------
   Quake II heap: failure report and HEAP_PEAK
   ------------------------------------------------------------------ */

/* Called by zzq2_mem.c when a request cannot be served below
   HEAP_LIMIT. Nothing has been handed out. Does not return.

   The request goes into the "failed allocation" fields of the release
   (zzq_hunk_fail of zzquake_qgmain.c: name, size asked, size rounded,
   bytes in use, total), which the launcher prints in its fault report
   under "ALLOCATION QUI ECHOUE". Same fields, same meaning. Quake II
   has no "high hunk": that field is written 0. The peak is in the
   message; the launcher shows 60 characters of it. */
void zzq2_heap_fail(const char *what, u32 asked, u32 rounded,
                    u32 used, u32 peak, u32 total)
{
    int i, b = 0, k;
    for (i = 0; i < 4; i++) {
        u32 w = 0;
        for (k = 0; k < 4; k++) {
            unsigned char c = 0;
            if (what && what[b]) { c = (unsigned char)what[b]; b++; }
            w |= ((u32)c) << (k * 8);
        }
        shared[SH_HUNKF_NAME + i] = w;
    }
    shared[SH_HUNKF_RAW]  = asked;
    shared[SH_HUNKF_ADJ]  = rounded;
    shared[SH_HUNKF_LOW]  = used;
    shared[SH_HUNKF_HIGH] = 0;
    shared[SH_HUNKF_SIZE] = total;
    Sys_Error("ZZQ2 heap: %s %u fails, peak %u/%u K",
              what ? what : "?", asked, peak >> 10, total >> 10);
    for (;;) { __asm__ volatile("wfe"); }
}

/* ------------------------------------------------------------------
   Where the frame time goes. Measurement only: nothing here changes
   what the engine does.

   Two engine functions are interposed at link time (build_arm.sh:
   --wrap=SV_Frame --wrap=SCR_UpdateScreen; the engine sources are not
   touched). Together with the time spent in zzq2_present() they split
   a frame into:
       sv    SV_Frame          server: game logic, or reading a demo
       draw  SCR_UpdateScreen  the whole refresh (3D view, HUD,
                               console), WITHOUT the presentation
       show  zzq2_present      release QG_DrawFrame: conversion, cache
                               clean, handshake
       all   the period from one presented frame to the next; what is
             left after sv, draw and show is the client (network
             parsing, prediction, input)
   A frame is counted when SCR_UpdateScreen returns after having
   presented an image. Frames longer than 250 ms (map loads) are left
   out of the averages.
   ------------------------------------------------------------------ */
extern void __real_SV_Frame(int msec);
extern void __real_SCR_UpdateScreen(void);

static u32 g_tm_sv, g_tm_ref, g_tm_show;      /* current frame, us   */
static int g_tm_shown;                        /* an image went out   */
static u32 g_tm_last;                         /* end of last frame   */
static int g_tm_have;
static unsigned long long g_ts_all, g_ts_sv, g_ts_ref, g_ts_show;
static u32 g_ts_n;

void __wrap_SV_Frame(int msec)
{
    u32 t0 = zzq_us_now();
    __real_SV_Frame(msec);
    g_tm_sv += zzq_us_now() - t0;
}

void __wrap_SCR_UpdateScreen(void)
{
    u32 t0 = zzq_us_now(), now;
    __real_SCR_UpdateScreen();
    now = zzq_us_now();
    g_tm_ref += now - t0;
    if (!g_tm_shown) return;

    /* an image was presented during this refresh: close the frame */
    if (g_tm_have) {
        u32 period = now - g_tm_last;
        if (period < 250000u && g_tm_ref >= g_tm_show) {
            g_ts_all  += period;
            g_ts_sv   += g_tm_sv;
            g_ts_ref  += g_tm_ref - g_tm_show;
            g_ts_show += g_tm_show;
            g_ts_n++;
        }
    }
    g_tm_last = now;
    g_tm_have = 1;
    g_tm_sv = g_tm_ref = g_tm_show = 0;
    g_tm_shown = 0;
}

/* ------------------------------------------------------------------
   Where the RENDERER time goes: the release profiler, as it is.

   zzquake_platform.c of the release holds a renderer profiler
   (zzq_ph_begin/end, zzq_s2_begin/end). The release QG_DrawFrame
   closes it once per presented frame, zzq_publish_stats() publishes it
   in the release slots SH_PROF_* / SH_S2_*, and the launcher prints it
   under "profil du renderer". In ZZQuake640 the calls sit in the
   Quake I renderer; a Quake II blob never made them, hence the zeros.

   Here the SAME release functions are called around the Quake II
   functions that do the same work, by link-time interposition again
   (build_arm.sh, --wrap). Same phases, same numbers, same meaning:

     phase 1 WORLD   R_RenderWorld          as in the release
     phase 2 BRUSH   R_DrawSolidClippedSubmodelPolygons and
                     R_DrawSubmodelPolygons (the release brackets the
                     whole R_DrawBEntitiesOnList, which is the loop
                     around these two)
     phase 3 SCAN    R_ScanEdges            as in the release
     phase 5 ENT     R_AliasDrawModel, R_DrawSprite (the release
                     brackets the whole R_DrawEntitiesOnList)
     phase 7 PARTIC  R_DrawParticles        as in the release
     step  0 CACHE   D_CacheSurface         as in the release
     step  2 TEX     D_DrawSpans16          as in the release
     step  3 Z       D_DrawZSpans           as in the release
     step  4 SPECIAL Turbulent8, NonTurbulent8 (warping and flowing
                     surfaces; the Quake II sky is ordinary TEX)

   Left at zero because the call is inside one object file and cannot
   be interposed: phase 0 SETUP, phase 4 SURF (it is the sum of the
   steps), phase 6 VIEWMDL (Quake II draws the weapon as an ordinary
   entity: it is in ENT), step 1 GRAD, step 5 SUBMODL, step 6 OTHER.

   Measurement only. To be removed with the two wrappers above when it
   has served.
   ------------------------------------------------------------------ */
extern void zzq_ph_begin(int p);
extern void zzq_ph_end(int p);
extern void zzq_s2_begin(int p);
extern void zzq_s2_end(int p);

#if defined(ZZQ2_TGL) || defined(ZZQ2_RIX)
/* ref_gl: the same release phases around the ref_gl functions that
   do the same work, where they can be interposed (called from another
   object file):
     phase 1 WORLD   R_DrawWorld (gl_rsurf.c; includes the lightmap
                     pass R_BlendLightmaps of the world)
     phase 2 BRUSH   R_DrawBrushModel (gl_rsurf.c)
     phase 5 ENT     R_DrawAliasModel (gl_mesh.c)
     step  2 TEX     TinyGL triangle fill, measured inside TinyGL
                     (clip.c, gl_draw_triangle_fill) when the blob is
                     built with ZZQ2TGL_RASTER_PROFILE
     step  4 SPECIAL R_DrawAlphaSurfaces (water, glass)
   R_DrawParticles and R_DrawSpriteModel are called from inside
   gl_rmain.c and cannot be interposed: they stay in draw. */
extern void __real_R_DrawWorld(void);
extern void __real_R_DrawBrushModel(void *e);
extern void __real_R_DrawAliasModel(void *e);
extern void __real_R_DrawAlphaSurfaces(void);

/* 0.8.2: the same four calls also tell the RasterIX side which part of the frame each draw batch
   belongs to (zzq2_rix_phase, read by zzq2rix_qgl.c) and whether the frame drew the world
   (zzq2_world_count, read by zzq2rix_gpu.c to cut the timedemo reports).  Measurement only. */
volatile int zzq2_rix_phase;
volatile u32 zzq2_world_count;

void __wrap_R_DrawWorld(void)
{ zzq2_world_count++; zzq2_rix_phase = 1; zzq_ph_begin(1); __real_R_DrawWorld(); zzq_ph_end(1); zzq2_rix_phase = 0; }

void __wrap_R_DrawBrushModel(void *e)
{ zzq2_rix_phase = 2; zzq_ph_begin(2); __real_R_DrawBrushModel(e); zzq_ph_end(2); zzq2_rix_phase = 0; }

void __wrap_R_DrawAliasModel(void *e)
{ zzq2_rix_phase = 5; zzq_ph_begin(5); __real_R_DrawAliasModel(e); zzq_ph_end(5); zzq2_rix_phase = 0; }

void __wrap_R_DrawAlphaSurfaces(void)
{ zzq2_rix_phase = 4; zzq_s2_begin(4); __real_R_DrawAlphaSurfaces(); zzq_s2_end(4); zzq2_rix_phase = 0; }

#ifdef ZZQ2_TGL
/* called by the patched TinyGL clip.c (ZZQ2TGL_RASTER_PROFILE) */
void zzq2tgl_raster_begin(void) { zzq_s2_begin(2); }
void zzq2tgl_raster_end(void)   { zzq_s2_end(2); }
#endif
/* ZZQuake2RIX: step 2 TEX brackets the wait for the GPU frame fence (zzq2rix_gpu.c) */
#else
extern void  __real_R_RenderWorld(void);
extern void  __real_R_ScanEdges(void);
extern void  __real_R_DrawSolidClippedSubmodelPolygons(void *pmodel, void *topnode);
extern void  __real_R_DrawSubmodelPolygons(void *pmodel, int clipflags, void *topnode);
extern void  __real_R_AliasDrawModel(void);
extern void  __real_R_DrawSprite(void);
extern void  __real_R_DrawParticles(void);
extern void *__real_D_CacheSurface(void *surface, int miplevel);
extern void  __real_D_DrawSpans16(void *pspan);
extern void  __real_D_DrawZSpans(void *pspan);
extern void  __real_Turbulent8(void *pspan);
extern void  __real_NonTurbulent8(void *pspan);

void __wrap_R_RenderWorld(void)
{ zzq_ph_begin(1); __real_R_RenderWorld(); zzq_ph_end(1); }

void __wrap_R_DrawSolidClippedSubmodelPolygons(void *pmodel, void *topnode)
{ zzq_ph_begin(2); __real_R_DrawSolidClippedSubmodelPolygons(pmodel, topnode); zzq_ph_end(2); }

void __wrap_R_DrawSubmodelPolygons(void *pmodel, int clipflags, void *topnode)
{ zzq_ph_begin(2); __real_R_DrawSubmodelPolygons(pmodel, clipflags, topnode); zzq_ph_end(2); }

void __wrap_R_ScanEdges(void)
{ zzq_ph_begin(3); __real_R_ScanEdges(); zzq_ph_end(3); }

void __wrap_R_AliasDrawModel(void)
{ zzq_ph_begin(5); __real_R_AliasDrawModel(); zzq_ph_end(5); }

void __wrap_R_DrawSprite(void)
{ zzq_ph_begin(5); __real_R_DrawSprite(); zzq_ph_end(5); }

void __wrap_R_DrawParticles(void)
{ zzq_ph_begin(7); __real_R_DrawParticles(); zzq_ph_end(7); }

void *__wrap_D_CacheSurface(void *surface, int miplevel)
{
    void *r;
    zzq_s2_begin(0);
    r = __real_D_CacheSurface(surface, miplevel);
    zzq_s2_end(0);
    return r;
}

void __wrap_D_DrawSpans16(void *pspan)
{ zzq_s2_begin(2); __real_D_DrawSpans16(pspan); zzq_s2_end(2); }

void __wrap_D_DrawZSpans(void *pspan)
{ zzq_s2_begin(3); __real_D_DrawZSpans(pspan); zzq_s2_end(3); }

void __wrap_Turbulent8(void *pspan)
{ zzq_s2_begin(4); __real_Turbulent8(pspan); zzq_s2_end(4); }

void __wrap_NonTurbulent8(void *pspan)
{ zzq_s2_begin(4); __real_NonTurbulent8(pspan); zzq_s2_end(4); }
#endif

/* How Core1 and the level 2 cache are configured when Quake II runs.
   READ ONLY: nothing is written. The release never touches these, so
   they are what the firmware left:
     ACTLR  auxiliary control register of this core (bit 1: L2 prefetch
            hint, bit 2: L1 data prefetch, bit 6: SMP)
     L2     control (bit 0: enabled) and auxiliary control of the PL310
            level 2 cache of the Zynq (0xF8F02000; that megabyte is
            mapped by the release table, the Global Timer the release
            reads is in it). */
#define ZZQ2_L2C_CTRL (*(volatile u32 *)0xF8F02100u)
#define ZZQ2_L2C_AUX  (*(volatile u32 *)0xF8F02104u)

static u32 q2_read_actlr(void)
{
    u32 v;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 1" : "=r"(v));
    return v;
}

/* End of a session, through the release printf: HEAP_PEAK, the
   averages above in microseconds per frame, and the cache
   configuration. The launcher shows this line as "last printf"
   (60 characters) in the block it prints after an engine Quit:
       pk <KiB>K all <us> draw <us> show <us> A<hex3> L<hex1> X<hex8>
   A = ACTLR bits 0..11, L = L2 control bits 0..3, X = L2 auxiliary
   control. 58 characters with the values seen on the hardware. */
#ifdef ZZQ2_RIX
extern void zz_rix_buffers(unsigned *lo, unsigned *hi);
extern void zz_rix_stats(u32 *frames, u32 *gpu_us_avg, u32 *gpu_us_max, u32 *present_us_avg, u32 *binds_last, u32 *pool_pages_free);
extern void zz_rix_close(void);
#endif
static void q2_heap_report(void)
{
    u32 n = g_ts_n ? g_ts_n : 1u;
#ifdef ZZQ2_RIX
    /* RIX: the GPU numbers instead of the cache configuration (known):
         gpu  = submit->fence wait per presented frame (us, average / max)
         b    = texture binds of the last frame, pf = free pool pages */
    {
        u32 fr, gavg, gmax, pavg, binds, pf;
        zz_rix_stats(&fr, &gavg, &gmax, &pavg, &binds, &pf);
        zzq_printf("pk %uK all %u draw %u show %u gpu %u/%u b %u pf %u\n",
                   zzq2_arena_peak() >> 10,
                   (u32)(g_ts_all / n), (u32)(g_ts_ref / n), (u32)(g_ts_show / n),
                   gavg, gmax, binds, pf);
        return;
    }
#endif
    zzq_printf("pk %uK all %u draw %u show %u A%03x L%x X%08x\n",
               zzq2_arena_peak() >> 10,
               (u32)(g_ts_all / n), (u32)(g_ts_ref / n),
               (u32)(g_ts_show / n),
               q2_read_actlr() & 0xFFFu, ZZQ2_L2C_CTRL & 0xFu,
               ZZQ2_L2C_AUX);
}

/* ------------------------------------------------------------------
   Files
   ------------------------------------------------------------------ */

/* -1 when path is not ".../pakN.pak" (any case), else N. Same suffix
   test as pak_index() of the release filesystem. */
static int q2_pak_number(const char *p)
{
    static const char pat[8] = { 'p','a','k', 0, '.','p','a','k' };
    int l = 0, i;
    const char *s;
    if (!p) return -1;
    while (p[l]) l++;
    if (l < 8) return -1;
    s = p + l - 8;
    for (i = 0; i < 8; i++) {
        char c = s[i];
        if (i == 3) { if (c < '0' || c > '9') return -1; continue; }
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != pat[i]) return -1;
    }
    return s[3] - '0';
}

/* 1 when the path contains the directory component dir ("/name/"). */
static int q2_has_dir(const char *p, const char *dir)
{
    int i, j;
    if (!p) return 0;
    for (i = 0; p[i]; i++) {
        for (j = 0; dir[j]; j++) {
            char a = p[i + j];
            if (a == '\\') a = '/';
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (a != dir[j]) break;
        }
        if (!dir[j]) return 1;
    }
    return 0;
}

static int g_first_fopen;

/* fopen(path, "a"). The release file layer opens "a" exactly like
   "w": a new, empty file. Quake II writes config.cfg in two passes,
   the key bindings with "w" and then the archived variables with "a"
   (Cvar_WriteVariables); the second pass used to replace the first.
   Append is therefore done here: what the file already holds is read
   back through the release reader and written first into the new
   file. Both handles are release handles; the release file layer is
   not modified. A file that does not exist yet is simply created. */
static FILE *q2_open_append(const char *path)
{
    unsigned char buf[2048];
    size_t got;
    FILE *r, *w;

    r = zzq_fopen(path, "r");
    w = zzq_fopen(path, "w");
    if (r && w) {
        while ((got = zzq_fread(buf, 1, sizeof(buf), r)) > 0) {
            if (zzq_fwrite(buf, 1, got, w) != got) break;
        }
    }
    if (r) zzq_fclose(r);
    return w;
}

FILE *zzq2_fopen(const char *path, const char *mode)
{
    if (!g_first_fopen) { g_first_fopen = 1; zzq2_crumb(ZZQ2_DIAG_FIRST_FOPEN); }

    /* A0: pak0.pak only. FS_AddGameDirectory probes pak0..pak9; the
       nine others are refused here instead of being asked to the 68k,
       which would hand back the first 512 KiB of a larger file. */
    if (q2_pak_number(path) > 0) return 0;

    /* A0 0.6 (RIX 0.10): save games. The release file service keeps
       the base name only, so "save/current/base1.sav" and
       "save/save0/base1.sav" would be one and the same file. Every path
       under a save directory is given its own flat name by zzq2_save.c
       (sv_<dir>_<file>); what it cannot name is refused.
       config.cfg is not concerned. */
    if (q2_has_dir(path, "/save/")) return zzq2_save_fopen(path, mode);

    if (mode && mode[0] == 'a') return q2_open_append(path);

    return zzq_fopen(path, mode);
}

int zzq2_rename(const char *oldn, const char *newn)
{
    (void)oldn; (void)newn;
    return -1;
}

/* newlib time() -> _gettimeofday. Only the save-game comment and the
   func_clock entity ask for wall-clock time. */
int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (tv) {
        double t = Sys_FloatTime();
        tv->tv_sec  = (time_t)(946684800L + (long)t);    /* 2000-01-01 + uptime */
        tv->tv_usec = (suseconds_t)((t - (double)(long)t) * 1000000.0);
    }
    return 0;
}

/* ------------------------------------------------------------------
   quake2generic platform functions (quakegeneric.h of Quake II)
   ------------------------------------------------------------------ */

/* Milliseconds since the first call, from the release Global Timer
   routine. Sys_FloatTime() reads the full 64-bit counter; the brief's
   zzq_us_now()/1000 is the same clock truncated to 32 bits of
   microseconds, which wraps every 71 min 35 s of CARD uptime and
   would make Quake II's absolute time jump backwards mid-game. */
int QG_Milliseconds(void)
{
    static int    have_base;
    static double base;
    double t = Sys_FloatTime();
    if (!have_base) {
        base = t;
        have_base = 1;
        zzq2_crumb(ZZQ2_DIAG_FIRST_TIME);
    }
    return (int)((t - base) * 1000.0);
}

void QG_GetMouseDiff(int *dx, int *dy)
{
    int x = 0, y = 0;
    QG_GetMouseMove(&x, &y);          /* release: SH_MOUSE_TOT_X/Y deltas */
    if (dx) *dx = x;
    if (dy) *dy = y;
}

void QG_CaptureMouse(void) { }
void QG_ReleaseMouse(void) { }
void QG_Mkdir(const char *path) { (void)path; }

/* ------------------------------------------------------------------
   Video, release side. Called by zzq2_video.c (SWimp_*).
   ------------------------------------------------------------------ */

static unsigned char g_pal768[768];
static int g_pal_valid;

/* Quake II hands 256 entries of 4 bytes (R, G, B, unused). The
   release LUT builder wants 256 x 3. Only the stride changes; the
   RGB32 and byte-swapped RGB565 tables are built by QG_SetPalette. */
void zzq2_set_palette_rgbx(const unsigned char *pal4)
{
    int i;
    if (!pal4) return;
    for (i = 0; i < 256; i++) {
        g_pal768[i*3+0] = pal4[i*4+0];
        g_pal768[i*3+1] = pal4[i*4+1];
        g_pal768[i*3+2] = pal4[i*4+2];
    }
    g_pal_valid = 1;
    QG_SetPalette(g_pal768);
}

/* The 8-bit frame Quake II draws into: ZZQ2_VID_W x ZZQ2_VID_H,
   ZZQ2_VID_W bytes per row, exactly what the release QG_DrawFrame
   reads. Allocated once (ref_soft only: unused by the GL builds). */
void *zzq2_video_buffer(void)
{
    static void *buf;
    if (!buf) {
        buf = zzq2_malloc((u32)ZZQ2_VID_W * ZZQ2_VID_H);
        if (!buf) Sys_Error("ZZQ2: no memory for the frame");
        memset(buf, 0, (u32)ZZQ2_VID_W * ZZQ2_VID_H);
    }
    return buf;
}

/* SWimp_EndFrame: one call into the release presentation path. */
void zzq2_present(void *pixels)
{
    u32 f0, t0;

    if (!g_first_endframe) {
        g_first_endframe = 1;
        q2_gate(ZZQ2_DIAG_FIRST_FRAME, "Q2_FIRST_FRAME");
    }
    if (!pixels) return;

    f0 = shared[SH_FRAME];
    t0 = zzq_us_now();
    QG_DrawFrame(pixels);
    if (shared[SH_FRAME] != f0) {
        /* The frame went through. Call the release zzq_publish_stats()
           now; the release QG_DrawFrame calls it once every 32 frames.
           Same function, same slots, same values: only the cadence
           differs, so that the release counters (SH_RENDERS among
           them) are current after every presented frame, whatever
           the frame rate. */
        zzq_publish_stats();
        if (!g_first_present) {
            g_first_present = 1;
            q2_diag(ZZQ2_DIAG_PRESENTED);
        }
        g_tm_shown = 1;
    }
    g_tm_show += zzq_us_now() - t0;
}

#if defined(ZZQ2_TGL) || defined(ZZQ2_RIX)
/* ------------------------------------------------------------------
   ZZQuake2TGL: 32-bit presentation.  ZZQuake2RIX: the same path with
   the GPU's RGB565 buffer as the source (zzq2_present_rgb565 below).

   TinyGL draws 0x00RRGGBB pixels (the top byte may hold texel alpha
   and is cleared here). The release QG_DrawFrame only converts 8-bit
   palette indices, so its triple-buffer path is reproduced here, slot
   for slot, with the same protocol (launcher unchanged):
     SH_RENDER_SEQ new          -> a buffer is granted, else the frame
                                   is dropped (the engine never waits)
     SH_RENDER_FB               -> the ARM address of that buffer,
                                   checked like the release does
     SH_FB_USED                 <- the buffer written
     copy (scaled when the engine renders below 640x480), cache clean
     SH_FRAME++, SH_RENDER_DONE <- seq, SH_FRAME_READY <- seq
   The release keeps its triple-buffer counters in static variables
   that only its QG_DrawFrame updates; they are kept here and written
   into the same slots after zzq_publish_stats(), so that the
   launcher statistics and its "CORE1 FIGE" watchdog (SH_RENDERS) see
   the frames. Only the triple-buffer mode of the launcher (default,
   3 buffers obtained) is supported.
   ------------------------------------------------------------------ */
extern void dcache_clean_range(u32 start, u32 len);
extern void zzq_ph_frame(u32 work_us);
extern void zzq_s2_frame(u32 work_us);

static u32 t32_last_seq, t32_pass, t32_block, t32_rejects;
static u32 t32_renders, t32_heavy, t32_work_t0, t32_work_last, t32_work_max;
static u32 t32_fps_peak, t32_last_us;
static u32 t32_hist[ZZQ_HIST_BINS];
static unsigned short t32_xmap[640], t32_ymap[480];
static int t32_mw, t32_mh;

/* The release zzq_publish_stats() writes its own triple-buffer
   counters, which stay at zero here. It is interposed (build_arm.sh,
   --wrap=zzq_publish_stats): every call, including the last one made
   by the release exit path zzq_end_wfe(), is followed by the counters
   of zzq2_present32(). */
static void t32_overwrite(void);
extern void __real_zzq_publish_stats(void);
void __wrap_zzq_publish_stats(void)
{
    __real_zzq_publish_stats();
    t32_overwrite();
}

static void t32_publish(void)
{
    zzq_publish_stats();
}

static void t32_overwrite(void)
{
    int h;
    shared[SH_WORK_US]       = t32_work_last;
    shared[SH_WORK_MAX]      = t32_work_max;
    shared[SH_FPS_PEAK_X100] = t32_fps_peak;
    shared[SH_GATE_PASS]     = t32_pass;
    shared[SH_GATE_BLOCK]    = t32_block;
    shared[SH_FB_REJECTS]    = t32_rejects;
    shared[SH_HEAVY_FRAMES]  = t32_heavy;
    shared[SH_RENDERS]       = t32_renders;
    for (h = 0; h < ZZQ_HIST_BINS; h++) shared[SH_HIST_BASE + h] = t32_hist[h];
}

/* RGB565 (little-endian halves, the GPU's buffer) -> the P96 buffer:
   32-bit 0x00RRGGBB (5/6/5 bits replicated into 8) or 16-bit big-endian
   RGB565 (a byte swap).  src_pitch in bytes (640x481 buffers: 1280). */
/* 0.3: zz_present (zzq2rix_video.c, archived): 1 = burst path below (default), 0 = the 0.2
   word-by-word path, kept for comparison in the same run */
int zzq2_present_burst = 1;
/* 0.10 (RIX): ZZQ2_VID_W x ZZQ2_VID_H pixels (640 x 480, 320 x 240, 800 x 600), the size of the screen
   and of the blob; src points at the top line shown, src_pitch the GPU buffer's line */
#define T565_W ((u32)ZZQ2_VID_W)
#define T565_H ((u32)ZZQ2_VID_H)
static u32 t565_row[ZZQ2_VID_W / 2] __attribute__((aligned(32)));   /* one source line, cached */
static u32 t565_lut_hi[256], t565_lut_lo[256];             /* RGB565 byte -> 0x00RRGGBB part */
static int t565_lut_ok;

/* n32 blocks of 32 bytes from the GPU's non-cacheable buffer into cached memory: LDM of eight
   registers is one burst on the AXI port, where the 0.2 loop issued one uncached word read per
   two pixels (153600 per frame, measured 25.7 ms of "show" on the A4000, 14/10).  Integer
   registers only: no NEON in the Quake blob (the release's Core1 stack is not 8-byte aligned). */
static void nc_burst_copy(u32 *dst, const u32 *src, u32 n32)
{
#if defined(__arm__)
    __asm__ volatile(
        "1:\n\t"
        "ldmia %1!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "stmia %0!, {r3, r4, r5, r6, r8, r9, r10, r12}\n\t"
        "subs  %2, %2, #1\n\t"
        "bne   1b\n\t"
        : "+r"(dst), "+r"(src), "+r"(n32)
        :
        : "r3", "r4", "r5", "r6", "r8", "r9", "r10", "r12", "cc", "memory");
#else
    u32 i;
    for (i = 0; i < n32 * 8u; i++) dst[i] = src[i];
#endif
}

/* RGB565 -> 0x00RRGGBB with the same bit replication as the 0.2 loop, split by source byte:
   high byte = R5 G5..G3, low byte = G2..G0 B5; the fields never overlap, so OR of two lookups */
static void t565_lut_init(void)
{
    u32 i;
    for (i = 0; i < 256u; i++) {
        u32 r = i >> 3, gh = i & 7u, gl = i >> 5, b = i & 31u;
        t565_lut_hi[i] = (((r << 3) | (r >> 2)) << 16) | (((gh << 5) | (gh >> 1)) << 8);
        t565_lut_lo[i] = ((gl << 2) << 8) | ((b << 3) | (b >> 2));
    }
    t565_lut_ok = 1;
}

/* 0.9.3: called every 16 lines of the conversion when set (zzq2rix_gpu.c, rix_overlap 1: the GPU is still
   drawing the next frame, its queue is pumped meanwhile) */
void (*zzq2_present_hook)(void);
static void t565_copy_burst(u32 fb, u32 pitch, const unsigned short *src, u32 src_pitch, int bpp16)
{
    int x, y;
    if (!t565_lut_ok) t565_lut_init();
    for (y = 0; y < (int)T565_H; y++) {
        if (zzq2_present_hook && (y & 15) == 0) zzq2_present_hook();
        const u32 *row = (const u32 *)((const u8 *)src + (u32)y * src_pitch);
        const u32 *r2 = t565_row;
        nc_burst_copy(t565_row, row, T565_W * 2u / 32u);
        if (bpp16) {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < (int)(T565_W / 2u); x++) {
                u32 v = r2[x];   /* two pixels: byte-swap each halfword (P96 16-bit is big-endian) */
                d[x] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
            }
        } else {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < (int)(T565_W / 2u); x++) {
                u32 v = r2[x];
                d[2 * x]     = t565_lut_hi[(v >> 8) & 0xFFu]  | t565_lut_lo[v & 0xFFu];
                d[2 * x + 1] = t565_lut_hi[v >> 24]           | t565_lut_lo[(v >> 16) & 0xFFu];
            }
        }
    }
}

static void t565_copy(u32 fb, u32 pitch, const unsigned short *src, u32 src_pitch, int bpp16)
{
    int x, y;
    if (zzq2_present_burst && ((u32)src & 31u) == 0u && (src_pitch & 31u) == 0u) {
        t565_copy_burst(fb, pitch, src, src_pitch, bpp16);
        return;
    }
    for (y = 0; y < (int)T565_H; y++) {
        const unsigned short *row = (const unsigned short *)((const u8 *)src + (u32)y * src_pitch);
        if (zzq2_present_hook && (y & 15) == 0) zzq2_present_hook();
        if (bpp16) {
            unsigned short *d = (unsigned short *)(fb + (u32)y * pitch);
            for (x = 0; x < (int)T565_W; x++) {
                u32 v = row[x];
                d[x] = (unsigned short)(((v >> 8) | (v << 8)) & 0xFFFFu);
            }
        } else {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < (int)T565_W; x += 2) {
                u32 v2 = *(const u32 *)(row + x);         /* two texels, one read of the NC buffer */
                u32 v0 = v2 & 0xFFFFu, v1 = v2 >> 16;
                u32 r0 = (v0 >> 11) & 31u, g0 = (v0 >> 5) & 63u, b0 = v0 & 31u;
                u32 r1 = (v1 >> 11) & 31u, g1 = (v1 >> 5) & 63u, b1 = v1 & 31u;
                d[x]     = (((r0 << 3) | (r0 >> 2)) << 16) | (((g0 << 2) | (g0 >> 4)) << 8) | ((b0 << 3) | (b0 >> 2));
                d[x + 1] = (((r1 << 3) | (r1 >> 2)) << 16) | (((g1 << 2) | (g1 >> 4)) << 8) | ((b1 << 3) | (b1 >> 2));
            }
        }
    }
}

static void t32_copy(u32 fb, u32 pitch, const u32 *src, int w, int h, int bpp16)
{
    int x, y;
    if (w != t32_mw || h != t32_mh) {
        for (x = 0; x < 640; x++) t32_xmap[x] = (unsigned short)((x * w) / 640);
        for (y = 0; y < 480; y++) t32_ymap[y] = (unsigned short)((y * h) / 480);
        t32_mw = w; t32_mh = h;
    }
    for (y = 0; y < 480; y++) {
        const u32 *row = src + (u32)t32_ymap[y] * (u32)w;
        if (bpp16) {
            unsigned short *d = (unsigned short *)(fb + (u32)y * pitch);
            for (x = 0; x < 640; x++) {
                u32 p = (w == 640) ? row[x] : row[t32_xmap[x]];
                u32 v = ((p >> 8) & 0xF800u) | ((p >> 5) & 0x07E0u) | ((p >> 3) & 0x001Fu);
                d[x] = (unsigned short)(((v >> 8) | (v << 8)) & 0xFFFFu);
            }
        } else if (w == 640) {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < 640; x += 4) {
                d[x + 0] = row[x + 0] & 0x00FFFFFFu;
                d[x + 1] = row[x + 1] & 0x00FFFFFFu;
                d[x + 2] = row[x + 2] & 0x00FFFFFFu;
                d[x + 3] = row[x + 3] & 0x00FFFFFFu;
            }
        } else {
            u32 *d = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < 640; x++) d[x] = row[t32_xmap[x]] & 0x00FFFFFFu;
        }
    }
}

/* src565 = 0: pixels is w x h of 0x00RRGGBB (TinyGL); src565 = 1: pixels
   is the GPU's 640-wide RGB565 buffer, pitch565 bytes per line */
static void present_common(const void *pixels, int w, int h, int src565, u32 pitch565)
{
    u32 fb, pitch, seq, t0, now, wk;
    int bpp16;

    if (!g_first_endframe) {
        g_first_endframe = 1;
        q2_gate(ZZQ2_DIAG_FIRST_FRAME, "Q2_FIRST_FRAME");
    }
    if (!pixels || w <= 0 || h <= 0 || w > ZZQ2_VID_W || h > ZZQ2_VID_H) return;
    if (!shared[SH_TRIPLE])
        Sys_Error("ZZQ2GL: the launcher did not obtain 3 buffers");
    t0 = zzq_us_now();
    if (!t32_work_t0) t32_work_t0 = t0;

    seq = shared[SH_RENDER_SEQ];
    if (seq == t32_last_seq) {          /* no buffer free: dropped */
        t32_block++;
        g_tm_show += zzq_us_now() - t0;
        return;
    }
    t32_last_seq = seq;
    t32_pass++;
    fb = shared[SH_RENDER_FB];
    if (fb < 0x00200000u || fb > 0x08000000u) {
        shared[SH_FB_LAST_BAD] = fb;
        t32_rejects++;
        g_tm_show += zzq_us_now() - t0;
        return;
    }
    shared[SH_FB_USED] = fb;
    bpp16 = (shared[SH_FB_BPP] == 2u);
    pitch = shared[SH_FB_PITCH];
    if (pitch == 0) pitch = (u32)ZZQ2_VID_W * (bpp16 ? 2u : 4u);
#ifdef ZZQ2_RIX
    {   /* 0.10: P96 places the screen buffers where it wants in the board memory; one of them on the GPU's
           colour buffers (window 0x06600000.., 800 x 600: up to 0x067D5940) would be drawn by both sides.
           Never seen at 640 x 480; checked for every size, before anything is written. */
        unsigned lo, hi;
        zz_rix_buffers(&lo, &hi);
        if (fb < hi && fb + (u32)ZZQ2_VID_H * pitch > lo)
            Sys_Error("RIX: P96 buffer %08x over GPU buffers %08x-%08x", fb, lo, hi);
    }
#endif

    if (src565) t565_copy(fb, pitch, (const unsigned short *)pixels, pitch565, bpp16);
    else t32_copy(fb, pitch, (const u32 *)pixels, w, h, bpp16);
    shared[SH_CONVERT_US] = zzq_us_now() - t0;
    dcache_clean_range(fb, (u32)ZZQ2_VID_H * pitch);
    shared[SH_FRAME]++;

    now = zzq_us_now();
    if (t32_last_us) {
        u32 d = now - t32_last_us;
        if (d > 0 && d < 1000000u) {
            u32 fps100 = 100000000u / d;
            if (fps100 > t32_fps_peak) t32_fps_peak = fps100;
        }
    }
    t32_last_us = now;

    wk = now - t32_work_t0;
    t32_work_last = wk;
    if (wk > t32_work_max) t32_work_max = wk;
    t32_renders++;
    if (wk > 16810u) t32_heavy++;
    zzq_ph_frame(wk);
    zzq_s2_frame(wk);
    {
        u32 bin = wk / 2000u;
        if (bin >= ZZQ_HIST_BINS) bin = ZZQ_HIST_BINS - 1u;
        t32_hist[bin]++;
    }
    t32_publish();
    shared[SH_RENDER_DONE] = seq;
    __asm__ volatile("dsb sy" ::: "memory");
    shared[SH_FRAME_READY] = seq;
    __asm__ volatile("dsb sy" ::: "memory");
    t32_work_t0 = zzq_us_now();

    if (!g_first_present) {
        g_first_present = 1;
        q2_diag(ZZQ2_DIAG_PRESENTED);
    }
    g_tm_shown = 1;
    g_tm_show += zzq_us_now() - t0;
}

void zzq2_present32(const unsigned *pixels, int w, int h)
{
    present_common(pixels, w, h, 0, 0u);
}

/* ZZQuake2RIX (zzq2rix_gpu.c): the GPU's RGB565 buffer after its frame fence, from the top line shown
   (0.10: ZZQ2_VID_W x ZZQ2_VID_H; the 320 x 240 picture sits in a 640 x 481 buffer, pitch 1280) */
void zzq2_present_rgb565(const unsigned short *pixels, unsigned pitch_bytes)
{
    present_common(pixels, ZZQ2_VID_W, ZZQ2_VID_H, 1, pitch_bytes);
}
#endif

/* ------------------------------------------------------------------
   End of run
   ------------------------------------------------------------------ */

/* Engine "quit" (Com_Quit has already shut the server and the client
   down and written config.cfg). Same exit path as the release:
   zzq_fatal_end() publishes the statistics, sets STOPPED, cleans the
   framebuffer and either returns to the firmware or parks in WFE.
   SH_ERROR stays 0: this is not a fault. No _Exit(). */
void zzq2_quit(void)
{
#ifdef ZZQ2_RIX
    zz_rix_close();                 /* GPU idle (last fence waited), release attributes back */
#endif
    q2_heap_report();
    q2_diag(ZZQ2_DIAG_QUIT);
    shared[SH_EXIT_REASON] = ZZQ_EXIT_QG_QUIT;
    zzq_running = 0;
    shared[SH_STATUS] = ZZQ_QUIT_REQUESTED;
    zzq_fatal_end(0);
    for (;;) { __asm__ volatile("wfe"); }
}

/* Called by the release main loop when it ends on the 68k STOP
   command. The launcher no longer serves file requests at that
   point, so nothing is written; memory is reclaimed by the next
   launch (BSS, .q2bss, heap and arena all start from scratch). */
void Host_Shutdown(void)
{
#ifdef ZZQ2_RIX
    zz_rix_close();
#endif
    q2_heap_report();
    q2_diag(ZZQ2_DIAG_STOP);
}

/* ------------------------------------------------------------------
   THE FORK POINT
   ------------------------------------------------------------------ */

static int g_old_ms;

/* One pack announced by the launcher: [addr, addr + size) must lie in
   the arena. Raises *end to its end. 0 after Sys_Error. */
static int q2_pack_span(u32 addr, u32 size, u32 *end)
{
    if (addr < ZZQ_PACK_ARENA_BASE || addr > ZZQ_PACK_ARENA_END ||
        size > ZZQ_PACK_ARENA_END - addr) {
        Sys_Error("ZZQ2 layout: pak 0x%08x + %u outside arena", addr, size);
        return 0;
    }
    if (addr + size > *end) *end = addr + size;
    return 1;
}

void QG_Create(int argc, char *argv[])
{
    /* ZZQ2_SURFCACHE is only defined by a variant build
       (build_arm.sh, ZZQ2_VARIANT=sc8): the size of the surface cache
       of ref_soft in bytes, through its own cvar. Without it Quake II
       computes the size itself (1516032 bytes at 640x480). */
#ifdef ZZQ2_SURFCACHE
#define ZZQ2_STR2(x) #x
#define ZZQ2_STR(x)  ZZQ2_STR2(x)
    static char *q2argv[] = { "quake2", "+set", "sw_mode", "3",
                              "+set", "sw_surfcacheoverride",
                              ZZQ2_STR(ZZQ2_SURFCACHE), 0 };
#define ZZQ2_ARGC 7
#elif defined(ZZQ2_TGL)
    /* ZZQ2TGL_MODE: gl_mode of the build (3 = 640x480, 1 = 400x300,
       0 = 320x240; build_arm.sh). */
#define ZZQ2_STR2(x) #x
#define ZZQ2_STR(x)  ZZQ2_STR2(x)
    static char *q2argv[] = { "quake2", "+set", "gl_mode",
                              ZZQ2_STR(ZZQ2TGL_MODE), 0 };
#define ZZQ2_ARGC 4
#elif defined(ZZQ2_RIX)
    /* ZZQ2_VID_W x ZZQ2_VID_H on the GPU (0.10: 640x480 gl_mode 3,
       320x240 gl_mode 0, 800x600 gl_mode 4: the numbers of sw_mode, same
       mode table); the sky, flashes and particles as ref_gl draws them
       by default */
    static char *q2argv[] = { "quake2", "+set", "gl_mode", ZZQ2_SW_MODE_S, 0 };
#define ZZQ2_ARGC 4
#else
    static char *q2argv[] = { "quake2", "+set", "sw_mode", "3", 0 };
#define ZZQ2_ARGC 4
#endif
    u32 pre, pack_end, bss_lo, bss_hi, lo, hi;
    int m;

    (void)argc; (void)argv;     /* the release argv only carries the
                                   Quake I game mode switches */

    /* ---------------- gate B : Q2_ENTER ---------------- */
    /* When execution gets here, the release code has already run the
       preload handshake (READY, chunks, START) and pak_check(). The
       state the launcher left is recorded in the gate itself. */
    pre = shared[SH_PRELOAD_STATE];
    q2_gate((pre == ZZQ_PRELOAD_START) ? ZZQ2_DIAG_ENTER
                                       : ZZQ2_DIAG_ENTER_NOPRE,
            "Q2_ENTER");

    /* Same call the release QG_Create makes first: blob checksums and
       malloc state "D", taken before the first malloc. */
    zzq_diag_stage_d();

    /* A0 0.7 (RIX 0.10): the screen the launcher opened must be the
       size this blob draws (the release presentation writes
       ZZQ2_VID_W x ZZQ2_VID_H pixels whatever the screen is: a 640 blob
       on a 320 screen would write past the buffers). zzq2rix.bin =
       640x480, zzq2rix_320.bin = 320x240, zzq2rix_800.bin = 800x600
       (launcher options 320, 800). */
    if (shared[SH_FB_WIDTH] != ZZQ2_VID_W || shared[SH_FB_HEIGHT] != ZZQ2_VID_H) {
        /* under 64 characters: the launcher shows SH_LAST_ERR_MSG */
        Sys_Error("ZZQ2: screen %ux%u but blob %ux%u: wrong blob",
                  shared[SH_FB_WIDTH], shared[SH_FB_HEIGHT],
                  (u32)ZZQ2_VID_W, (u32)ZZQ2_VID_H);
        return;
    }

    /* ---------------- arena layout ----------------
         packs   placed by the launcher from ZZQ_PACK_ARENA_BASE
         heap    HEAP_START .. HEAP_LIMIT, lowest address first
                 HEAP_START = align_up(real end of the packs, 1 MiB)
                 HEAP_LIMIT = start of .q2bss
         .q2bss  _q2bss_start .. _q2bss_end (zzq2a0.ld), below
                 ZZQ_PACK_ARENA_END
       The three zones are checked against each other BEFORE anything
       is written: nothing has been cleared or allocated when one of
       the Sys_Error below ends the run. */
    bss_lo = (u32)(unsigned long)&_q2bss_start;
    bss_hi = (u32)(unsigned long)&_q2bss_end;

    /* 1. .q2bss inside the arena */
    if (bss_lo < ZZQ_PACK_ARENA_BASE || bss_hi <= bss_lo ||
        bss_hi > ZZQ_PACK_ARENA_END) {
        Sys_Error("ZZQ2 layout: BSS 0x%08x..0x%08x outside arena",
                  bss_lo, bss_hi);
        return;
    }

    /* 2. every pack the launcher announced inside the arena; the real
          end of the highest one. Same slots as the release filesystem. */
    pack_end = ZZQ_PACK_ARENA_BASE;
    if (!q2_pack_span(shared[SH_PACK0_ADDR], shared[SH_PACK0_SIZE], &pack_end))
        return;
    if (shared[SH_PACK_COUNT] > 1 && shared[SH_PACK1_SIZE] &&
        !q2_pack_span(shared[SH_PACK1_ADDR], shared[SH_PACK1_SIZE], &pack_end))
        return;
    if (shared[SH_PACK_COUNT] > 2 && shared[SH_PACK2_SIZE] &&
        !q2_pack_span(shared[SH_PACK2_ADDR], shared[SH_PACK2_SIZE], &pack_end))
        return;
    for (m = 0; m < (int)shared[SH_MOD_COUNT] && m < ZZQ_MODPACK_MAX; m++) {
        if (!q2_pack_span(shared[SH_MOD_ADDR + m], shared[SH_MOD_SIZE + m],
                          &pack_end))
            return;
    }

    /* 3. pak against .q2bss */
    if (pack_end > bss_lo) {
        Sys_Error("ZZQ2 layout: pak ends 0x%08x, BSS starts 0x%08x",
                  pack_end, bss_lo);
        return;
    }

    /* 4. heap between the two. pack_end <= bss_lo < 0x3F800000, so the
          rounding cannot wrap. */
    lo = (pack_end + 0x000FFFFFu) & 0xFFF00000u;     /* HEAP_START */
    hi = bss_lo;                                     /* HEAP_LIMIT */
#ifdef ZZQ2_RIX
    /* the 20 MiB under .q2bss (0x3D800000..0x3EC00000) are the GPU's:
       texture pool, chunk ring, Z-buffer, bind lists (zzq2rix_gpu.c).
       The one allocator of the engine is bounded here, before its first
       allocation; the GPU side checks the pak end against the same
       limit before it writes anything there. */
    if (hi > ZZQ2RIX_HEAP_LIMIT) hi = ZZQ2RIX_HEAP_LIMIT;
    g_pack_end = pack_end;
    if (pack_end + 0x00100000u > hi) {
        Sys_Error("ZZQ2 layout: pak ends 0x%08x, GPU reservation at 0x%08x",
                  pack_end, hi);
        return;
    }
#endif
    if (lo < pack_end || lo > hi || hi - lo < ZZQ2_HEAP_MIN) {
        Sys_Error("ZZQ2 layout: heap 0x%08x..0x%08x under %u MiB",
                  lo, hi, ZZQ2_HEAP_MIN >> 20);
        return;
    }

    /* Clear the Quake II BSS. The release bootstrap clears .bss only;
       .q2bss lives in the arena, which the release MMU setup mapped
       WB/WA a long time before this point. The packs and the heap end
       below it (checked just above), so nothing preloaded is
       overwritten. */
    {
        u32 *p = &_q2bss_start;
        while (p < &_q2bss_end) *p++ = 0;
    }
    q2_diag(ZZQ2_DIAG_BSS_CLEARED);

    zzq2_arena_init(lo, hi);
    /* 5. what the allocator kept: still above the pak, still below
          .q2bss */
    if (zzq2_arena_lo() < pack_end || zzq2_arena_hi() > bss_lo ||
        zzq2_arena_hi() <= zzq2_arena_lo()) {
        Sys_Error("ZZQ2 layout: heap 0x%08x..0x%08x not between pak/BSS",
                  zzq2_arena_lo(), zzq2_arena_hi());
        return;
    }

    /* The release QG_Create publishes, at this very point, the base
       address and the size in bytes of the memory handed to the engine
       (the launcher prints them as "hunk base=... size=...", base 0
       meaning "not allocated"). Same two slots, same meaning, written
       once: the pool Quake II allocates from. */
    shared[SH_HUNK_SIZE] = zzq2_arena_hi() - zzq2_arena_lo();
    shared[SH_HUNK_BASE] = zzq2_arena_lo();
    q2_diag(ZZQ2_DIAG_ARENA_READY);

    q2_diag(ZZQ2_DIAG_INIT_CALL);
    q2_init_aligned(ZZQ2_ARGC, q2argv);
    /* 0.10.3 (A0 0.8.5): the renderer and the screen size of the blob can
       no longer be changed in game (patched other/vid_menu.c) */
    zzq2_lock_video(1);                 /* ref_gl: the OpenGL menu */

    /* ---------------- gate C : Q2_INIT_OK ---------------- */
    g_init_done = 1;
    g_old_ms = QG_Milliseconds();
    q2_gate(ZZQ2_DIAG_INIT_OK, "Q2_INIT_OK");
}

/* ------------------------------------------------------------------
   One turn of the release main loop (gate D).
   ------------------------------------------------------------------ */
void QG_Tick(double duration)
{
    static int pal_resent;
    int down, key, now, msec;

    (void)duration;             /* Quake II wants integer milliseconds */

    /* The release sets SH_STATUS to PALETTE_OK in QG_SetPalette, but
       only once QG_Create has returned. ref_soft sets its palette
       during Quake2_Init and may never set it again, so hand the same
       palette to the release once more: SH_STATUS then reaches 0x06
       as it does under Quake I. (0x07 and 0x08 are set by the release
       QG_DrawFrame in its double-buffer path only; with the default
       triple buffering the status stays at 0x06, release behaviour.) */
    if (!pal_resent) {
        pal_resent = 1;
        if (g_pal_valid) QG_SetPalette(g_pal768);
    }

    /* Drain the XACP key queue with the release reader. Same queue,
       same codes; Quake II wants letters in lower case (keys.h) and
       applies Shift itself. */
    while (QG_GetKey(&down, &key)) {
        if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
        q2_sendkey_aligned(key, down);
    }

    now  = QG_Milliseconds();
    msec = now - g_old_ms;
    if (msec > 0)
        q2_frame_aligned(msec);
    g_old_ms = now;
}
