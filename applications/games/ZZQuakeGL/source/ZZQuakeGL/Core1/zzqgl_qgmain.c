/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzqgl_qgmain.c - ZZQuakeGL: the fork point between the UNCHANGED
 * ZZQuake640 release platform and the GLQuake engine.
 *
 * The release zzquake_main.c is compiled as it is.  It still calls
 *
 *     QG_Create(argc, argv);          <- the fork point
 *     while (zzq_running) QG_Tick(dt);
 *     Host_Shutdown();
 *
 * The release provided QG_Create / QG_Tick in zzquake_qgmain.c, which
 * also held the small zzq_* helpers the patched engine and the release
 * platform call.  This file replaces zzquake_qgmain.c (the ONLY release
 * Core1 file not compiled): the same helpers, the same hunk (malloc
 * from the release heap at 0x07000000, size SH_HUNK_MB), the same
 * command line, plus what the GL blob needs before Host_Init:
 *
 *   - the layout checks of ZZQuake2RIX (zzq2_glue.c): .glbss inside the
 *     arena, every pak the launcher announced below the GPU reservation
 *     (0x3D800000), nothing written before the checks pass;
 *   - the clearing of .glbss (zzqgl.ld), which the release bootstrap
 *     does not know;
 *   - the 8-byte stack trampoline of ZZQuake2 0.8.1 around Host_Init and
 *     Host_Frame (the release enters Core1 with SP = 4 mod 8: every
 *     double passed through "..." was read 4 bytes off: timedemo "0.0
 *     fps", Cvar_SetValue of non-integer values);
 *   - the breadcrumbs Q1_ENTER / Q1_INIT_OK (zzqgl_diag.h) in the
 *     existing SH_DIAG slot and the "last printf" slots; Q1_FIRST_FRAME is
 *     in zzqgl_present.c;
 *   - the link-time interposers (build_arm.sh --wrap) that give the GPU
 *     adapter its draw categories and map-load count, and close the GPU
 *     after the engine's Host_Shutdown.
 *
 * Nothing before the QG_Create call is touched: entry stub, BSS clear,
 * vectors, MMU, shared slots, preload handshake and pak check are the
 * release code.  ASCII only.
 */

#include <stdlib.h>
#include "quakedef.h"
#include "zzquake_config.h"
#include "zzqgl_diag.h"
#ifndef ZZQGL_VID_W                 /* 0.4: build_arm.sh ZZQGL_RES (320, 640, 800) */
#define ZZQGL_VID_W 640
#define ZZQGL_VID_H 480
#endif
#include "zzqgl_version.h"

typedef unsigned int u32;
extern volatile u32 *shared;
extern volatile int  zzq_running;
extern int  zzq_printf(const char *fmt, ...);
extern void zz_rix_close(void);                 /* zzq2rix_gpu.c */

/* ---- linker script (zzqgl.ld) ------------------------------------- */
#ifndef ZZQGL_HOST
extern u32 _glbss_start, _glbss_end;
#endif

/* ---- the GPU reservation (zzq2rix_gpu.c, RIXQ2_RESERVE_LO) ---------- */
#define ZZQGL_GPU_RESERVE_LO 0x3D800000u

/* ------------------------------------------------------------------
   8-byte stack alignment for the engine (ZZQuake2 0.8.1, unchanged)
   ------------------------------------------------------------------ */
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
static void host_init_aligned(quakeparms_t *parms)
{ zzq2_call_aligned((void *)Host_Init, (void *)parms, (void *)0); }
/* Host_Frame takes a float: passed through a static, the trampoline carries integer arguments only */
static float g_frame_time;
static void host_frame_thunk(void *a0, void *a1) { (void)a0; (void)a1; Host_Frame(g_frame_time); }
static void host_frame_aligned(float t)
{ g_frame_time = t; zzq2_call_aligned((void *)host_frame_thunk, (void *)0, (void *)0); }
#else
#define host_init_aligned  Host_Init
#define host_frame_aligned Host_Frame
#endif

/* ------------------------------------------------------------------
   Breadcrumbs
   ------------------------------------------------------------------ */
static int g_init_done;

static void diag(u32 code)
{
    shared[SH_DIAG] = code;
    __asm__ volatile("dsb" ::: "memory");
}

/* init-time breadcrumb: silent once Host_Init has returned, so that Q1_FIRST_FRAME is never overwritten */
void zzqgl_crumb(unsigned int code)
{
    if (!g_init_done) diag((u32)code);
}

/* a gate: the code in SH_DIAG and its name in the "last printf" slots the launcher prints */
static void gate(u32 code, const char *name)
{
    zzq_printf("%s\n", name);
    diag(code);
}

/* ------------------------------------------------------------------
   Helpers the release platform and the patched engine expect
   (zzquake_qgmain.c of ZZQuake640, taken as they are)
   ------------------------------------------------------------------ */
int zzq_alias_stage = 0;

void zzq_publish_engine_state(void)
{
    shared[SH_SV_ACTIVE]    = (u32)sv.active;
    shared[SH_DEMOPLAYBACK] = (u32)cls.demoplayback;
    shared[SH_DEMONUM]      = (u32)cls.demonum;
    /* r_maxsurfs / r_maxedges / r_cnumsurfs belong to the software renderer */
    shared[SH_CV_MAXSURFS]  = 0;
    shared[SH_CV_MAXEDGES]  = 0;
    shared[SH_CV_CNUMSURFS] = 0;
}

double zzq_min_frametime(void)
{
    u32 f = shared[SH_ENGINE_MAXFPS];
    if (f == 0) return 0.0;
    return 1.0 / (double)f;
}

void zzq_note_command(const char *text)
{
    int i, b = 0, k;
    shared[SH_CMD_COUNT]++;
    for (i = 0; i < SH_LAST_CMD_LEN; i++) {
        u32 w = 0;
        for (k = 0; k < 4; k++) {
            unsigned char ch = 0;
            if (text && text[b] && text[b] != '\n') { ch = (unsigned char)text[b]; b++; }
            w |= ((u32)ch) << (k * 8);
        }
        shared[SH_LAST_CMD + i] = w;
    }
}

void zzq_surfcache_got(int bytes)
{
    shared[SH_SURFCACHE_GOT] = (u32)bytes;
}

void zzq_timedemo_result(int frames, float seconds)
{
    long ms = (long)(seconds * 1000.0f);
    if (ms < 1) ms = 1;
    shared[SH_TD_FRAMES]  = (u32)frames;
    shared[SH_TD_TIME_MS] = (u32)ms;
    shared[SH_TD_FPS_X100]= (u32)(((long)frames * 100000L) / ms);
    shared[SH_TD_DONE]    = 1;
}

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

void zzq_ftoa(char *dst, int cap, float v)
{
    zzq_ftoa_prec(dst, cap, v, 6);
}

void zzq_hunk_fail(const char *name, int raw, int adj,
                   int low, int high, int total)
{
    int i, b = 0, k;
    for (i = 0; i < 4; i++) {
        u32 w = 0;
        for (k = 0; k < 4; k++) {
            unsigned char c = 0;
            if (name && name[b]) { c = (unsigned char)name[b]; b++; }
            w |= ((u32)c) << (k * 8);
        }
        shared[SH_HUNKF_NAME + i] = w;
    }
    shared[SH_HUNKF_RAW]   = (u32)raw;
    shared[SH_HUNKF_ADJ]   = (u32)adj;
    shared[SH_HUNKF_LOW]   = (u32)low;
    shared[SH_HUNKF_HIGH]  = (u32)high;
    shared[SH_HUNKF_SIZE]  = (u32)total;
    shared[SH_ALIAS_STAGE] = (u32)zzq_alias_stage;
}

int zzq_cvarfix_enabled(void)
{
    return (int)shared[SH_ENABLE_CVARFIX];
}

void zzq_cvar_snapshot(int which)
{
    (void)which;        /* software renderer cvars: nothing to snapshot in the GL blob */
}

void zzq_float_printf_test(void)
{
    char buf[32];
    int i, n;
    for (i = 0; i < 32; i++) buf[i] = (char)0xA5;
    n = sprintf(buf, "%f", 800.0);
    shared[SH_FTEST_RET] = (u32)n;
    buf[31] = 0;
    for (i = 0; i < 4; i++)
        shared[SH_FTEST_STR + i] =
            ((u32)(unsigned char)buf[i*4+0])       |
            ((u32)(unsigned char)buf[i*4+1] << 8)  |
            ((u32)(unsigned char)buf[i*4+2] << 16) |
            ((u32)(unsigned char)buf[i*4+3] << 24);
    shared[SH_FTEST_BACK] = (u32)(int)Q_atof(buf);
}

/* ------------------------------------------------------------------
   Interposers (build_arm.sh --wrap): draw categories and map loads for
   the GPU adapter's reports, GPU closed after the engine's shutdown.
   Measurement and ordering only: the engine functions run unchanged.
   ------------------------------------------------------------------ */
volatile int zzq2_rix_phase;                 /* read by zzqgl_gl.c (zz_category): 1 world, 2 bmodels, 4 water */
volatile u32 zzq2_world_count;               /* read by zzq2rix_gpu.c (timedemo segments) */
volatile unsigned int zzqgl_newmap_count;    /* read by gl_vid_zz.c */
unsigned int zzqgl_tl[ZZQGL_TL_N];           /* 0.2: init timeline (zzqgl_diag.h), written in rixstat.txt */

extern void __real_R_DrawWorld(void);
extern void __real_R_DrawBrushModel(entity_t *e);
extern void __real_R_DrawWaterSurfaces(void);
extern void __real_R_NewMap(void);
extern void __real_Host_Shutdown(void);

void __wrap_R_DrawWorld(void) { zzq2_world_count++; zzq2_rix_phase = 1; __real_R_DrawWorld(); zzq2_rix_phase = 0; }
void __wrap_R_DrawBrushModel(entity_t *e) { zzq2_rix_phase = 2; __real_R_DrawBrushModel(e); zzq2_rix_phase = 0; }
/* R_DrawAliasModel is called inside gl_rmain.c (same object: a link-time wrap cannot see it): alias
   models are counted under "other 3D" in the reports */
void __wrap_R_DrawWaterSurfaces(void) { zzq2_rix_phase = 4; __real_R_DrawWaterSurfaces(); zzq2_rix_phase = 0; }
void __wrap_R_NewMap(void) { zzqgl_newmap_count++; __real_R_NewMap(); ZZQGL_TL_MARK(ZZQGL_TL_NEWMAP); }

/* 0.2: SCR_ModalMessage ("quit? y/n", "new game?") draws its dialog once, then spins on
   Sys_SendKeyEvents with key_count = -1 until y, n or Escape, without a frame; the launcher
   ends the run after 5 s + 3 s without a frame.  The release Sys_SendKeyEvents (platform.c,
   unchanged) runs first; while a modal wait is on, the dialog frame is handed over again at
   most once a second (zzqgl_present_keepalive). */
extern void __real_Sys_SendKeyEvents(void);
extern int key_count;                        /* keys.c: -1 during a modal wait, -2 in Con_NotifyBox */
extern void zzqgl_present_keepalive(void);
void __wrap_Sys_SendKeyEvents(void)
{
    __real_Sys_SendKeyEvents();
    if (key_count < 0) zzqgl_present_keepalive();
}

/* the engine's Host_Shutdown (config.cfg written, sound and video shut down), then the GPU
   idle (last fence waited) and the release MMU attributes back; same for "quit" and for the
   68k STOP command (the release main loop calls Host_Shutdown in both cases) */
void __wrap_Host_Shutdown(void)
{
    __real_Host_Shutdown();
    zz_rix_close();
    diag(shared[SH_EXIT_REASON] == ZZQ_EXIT_CMD_STOP ? ZZQGL_DIAG_STOP : ZZQGL_DIAG_QUIT);
}

/* ------------------------------------------------------------------
   THE FORK POINT
   ------------------------------------------------------------------ */
static u32 g_pack_end;
unsigned zzq2_last_pak_end(void) { return g_pack_end; }

/* one pack announced by the launcher: [addr, addr + size) must lie in the arena */
static int pack_span(u32 addr, u32 size, u32 *end)
{
    if (addr < ZZQ_PACK_ARENA_BASE || addr > ZZQ_PACK_ARENA_END ||
        size > ZZQ_PACK_ARENA_END - addr) {
        Sys_Error("ZZQGL layout: pak 0x%08x + %u outside arena", addr, size);
        return 0;
    }
    if (addr + size > *end) *end = addr + size;
    return 1;
}

#define ZZQ_ARGV_MAX 12
static char *zzq_argv[ZZQ_ARGV_MAX];

void QG_Create(int argc, char *argv[])
{
    static quakeparms_t parms;
    u32 pre, bss_lo, bss_hi, pack_end;
    int m;

    /* ---------------- gate : Q1_ENTER ---------------- */
    zzqgl_tl[ZZQGL_TL_CREATE] = zzq_us_now();
    if (!zzqgl_tl[ZZQGL_TL_CREATE]) zzqgl_tl[ZZQGL_TL_CREATE] = 1u;   /* 0 means "not reached" */
    pre = shared[SH_PRELOAD_STATE];
    gate((pre == ZZQ_PRELOAD_START) ? ZZQGL_DIAG_ENTER : ZZQGL_DIAG_ENTER_NOPRE, "Q1_ENTER");

    /* the screen the launcher opened must be the size of this blob (the presentation writes
       ZZQGL_VID_W x ZZQGL_VID_H pixels; 0.4: 320, 640 or 800, build_arm.sh ZZQGL_RES) */
    if (shared[SH_FB_WIDTH] != (u32)ZZQGL_VID_W || shared[SH_FB_HEIGHT] != (u32)ZZQGL_VID_H) {
        Sys_Error("ZZQGL: screen %ux%u but blob %ux%u: wrong blob",
                  shared[SH_FB_WIDTH], shared[SH_FB_HEIGHT], (u32)ZZQGL_VID_W, (u32)ZZQGL_VID_H);
        return;
    }

    /* ---------------- arena layout (checked before anything is written) ----------------
         packs   placed by the launcher from ZZQ_PACK_ARENA_BASE
         GPU     0x3D800000 .. 0x3EC00000 (zzq2rix_gpu.c)
         .glbss  _glbss_start .. _glbss_end (zzqgl.ld), below ZZQ_PACK_ARENA_END */
#ifndef ZZQGL_HOST
    bss_lo = (u32)(unsigned long)&_glbss_start;
    bss_hi = (u32)(unsigned long)&_glbss_end;
    if (bss_lo < ZZQ_PACK_ARENA_BASE || bss_hi <= bss_lo || bss_hi > ZZQ_PACK_ARENA_END) {
        Sys_Error("ZZQGL layout: BSS 0x%08x..0x%08x outside arena", bss_lo, bss_hi);
        return;
    }
#else
    bss_lo = bss_hi = 0;   /* PC bench (host_q1.c): no .glbss, the C runtime zeroes the statics */
#endif
    pack_end = ZZQ_PACK_ARENA_BASE;
    if (!pack_span(shared[SH_PACK0_ADDR], shared[SH_PACK0_SIZE], &pack_end)) return;
    if (shared[SH_PACK_COUNT] > 1 && shared[SH_PACK1_SIZE] &&
        !pack_span(shared[SH_PACK1_ADDR], shared[SH_PACK1_SIZE], &pack_end)) return;
    if (shared[SH_PACK_COUNT] > 2 && shared[SH_PACK2_SIZE] &&
        !pack_span(shared[SH_PACK2_ADDR], shared[SH_PACK2_SIZE], &pack_end)) return;
    for (m = 0; m < (int)shared[SH_MOD_COUNT] && m < ZZQ_MODPACK_MAX; m++)
        if (!pack_span(shared[SH_MOD_ADDR + m], shared[SH_MOD_SIZE + m], &pack_end)) return;
    if (pack_end > ZZQGL_GPU_RESERVE_LO) {
        Sys_Error("ZZQGL layout: pak ends 0x%08x, GPU reservation at 0x%08x",
                  pack_end, ZZQGL_GPU_RESERVE_LO);
        return;
    }
    g_pack_end = pack_end;
    diag(ZZQGL_DIAG_LAYOUT_OK);

    /* the engine, GL, ZZGL and librixgpu statics: .glbss lives in the arena, which the release
       MMU setup mapped WB/WA long before this point; the packs end below the GPU reservation,
       so nothing preloaded is overwritten */
#ifndef ZZQGL_HOST
    {
        u32 *p = &_glbss_start;
        while (p < &_glbss_end) *p++ = 0;
    }
#endif
    diag(ZZQGL_DIAG_BSS_CLEARED);

    /* ---------------- the hunk, as the release QG_Create ----------------
       SH_HUNK_MB from the launcher (HUNK=n); 0 = default.  The GL renderer has no surface
       cache and keeps its lightmaps in .glbss, so its hunk need is at most the software
       one; the default is raised to 12 MiB (release: 8) with the same 14 MiB cap, inside
       the release's 16 MiB heap at 0x07000000. */
    {
        u32 mb = shared[SH_HUNK_MB];
        if (mb == 0) mb = 12;
        if (mb > 14) mb = 14;
        parms.memsize = (int)(mb * 1024u * 1024u);
    }
    { extern void zzq_diag_stage_d(void); zzq_diag_stage_d(); }
    parms.membase = malloc(parms.memsize);
    parms.basedir = ".";
    shared[SH_HUNK_SIZE] = (u32)parms.memsize;
    shared[SH_HUNK_BASE] = (u32)(unsigned long)parms.membase;
    if (!parms.membase) {
        shared[SH_ERROR] = ZZQ_FATAL_OUT_OF_MEM;
        Sys_Error("ZZQGL: hunk malloc failed (%d bytes)", parms.memsize);
        return;
    }

    /* the command line of the release: game mode switches from zzquake_main.c, +timedemo demo1 */
    {
        int an = 0, ai;
        for (ai = 0; ai < argc && an < ZZQ_ARGV_MAX - 4; ai++)
            zzq_argv[an++] = argv[ai];
        if (an == 0) zzq_argv[an++] = "zzquake";
        if (shared[SH_ENABLE_TIMEDEMO]) {
            zzq_argv[an++] = "+timedemo";
            zzq_argv[an++] = "demo1";
        }
        zzq_argv[an] = 0; argc = an; argv = zzq_argv;
    }
    COM_InitArgv(argc, argv);
    parms.argc = com_argc;
    parms.argv = com_argv;

    zzq_float_printf_test();
    diag(ZZQGL_DIAG_INIT_CALL);
    ZZQGL_TL_MARK(ZZQGL_TL_INIT_CALL);
    host_init_aligned(&parms);

    /* ---------------- gate : Q1_INIT_OK ---------------- */
    ZZQGL_TL_MARK(ZZQGL_TL_INIT_OK);
    g_init_done = 1;
    gate(ZZQGL_DIAG_INIT_OK, "Q1_INIT_OK");
}

void QG_Tick(double duration)
{
    static int first_tick_done;
    if (!first_tick_done) {
        /* 0.3: QG_Create has returned (status QG_CREATE_OK): the launcher publishes its first
           render buffer from here on; the black frame of VID_Init is offered again at once,
           before the first Host_Frame (map load) */
        first_tick_done = 1;
        zzqgl_present_keepalive();
    }
    host_frame_aligned((float)duration);
}
