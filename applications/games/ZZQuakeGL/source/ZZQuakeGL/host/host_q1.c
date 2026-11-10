/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * host_q1.c - ZZQuakeGL test bench on the PC (Linux, 32-bit build): the
 * engine tree of the blob (quakegeneric + id GL files, patched copies),
 * the GL binding (zzqgl_gl.c), the video layer (gl_vid_zz.c), the fork
 * point (zzqgl_qgmain.c, ZZQGL_HOST) and ZZGL/librixgpu run against the
 * behavioural GPU model of ZZGear (sim_rix.c, -DRIX_HOST_SIM
 * -DSIM_RIX_NO_APP), on the real pak0.pak.  The model fills every
 * triangle with a hash colour of its geometry (SIM_TMU=1: textured,
 * REPLACE only; no depth, no blending) and refuses every transport rule
 * violation (FSC deadlocks, rule 11, ring hazards, scissored memsets...).
 * What it proves: the whole call path from the engine to the display
 * lists, the frame cycle, the bind traffic, the geometry on screen, the
 * lightmap uploads.  Not part of the blob.
 *
 *   host_q1 <basedir> <outdir> <script>
 *     basedir: holds id1/pak0.pak
 *     script: "<frame>:<command>;..."; "shot <name>" writes <outdir>/<name>.ppm
 *             from the GPU's colour buffer after that frame; "quit" ends.
 *
 * This file stands in for the release platform (zzquake_main.c,
 * zzquake_platform.c, zzquake_fs_mem.c) and for zzqgl_present.c.
 * ASCII only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include "quakedef.h"
#include "zzquake_config.h"

typedef unsigned int u32;
typedef unsigned char u8;

/* ---- the release shared block, simulated ------------------------------ */
static u32 shared_mem[4096];
volatile u32 *shared = shared_mem;
volatile int zzq_running = 1;

extern void QG_Create(int argc, char *argv[]);
extern void QG_Tick(double duration);
extern void Cbuf_AddText(char *text);
extern void sim_model_reset(void);
extern int  sim_model_violations(void);
extern double sim_model_now_us(void);
extern unsigned sim_model_pixels(void);
extern unsigned sim_model_fifo_words(void);
extern int sim_model_tmu;
extern void zz_rix_stats(unsigned *, unsigned *, unsigned *, unsigned *, unsigned *, unsigned *);
extern void zz_rix_close(void);

static const char *g_out;
static char g_shot[128];
static int g_now_ms = 1000;
static unsigned g_frames, g_presented, g_dropped;

/* ---- release platform stand-ins ---------------------------------------- */
u32 zzq_us_now(void) { return (u32)sim_model_now_us(); }
double Sys_FloatTime(void) { return g_now_ms / 1000.0; }
void zzq_diag_stage_d(void) { }
void dcache_clean_range(u32 start, u32 len) { (void)start; (void)len; }
void zzq_ph_frame(u32 work_us) { (void)work_us; }
void zzq_s2_frame(u32 work_us) { (void)work_us; }
void zzq_s2_begin(int p) { (void)p; }
void zzq_s2_end(int p) { (void)p; }
void zzq_publish_stats(void) { }
void __real_zzq_publish_stats(void) { }
void __wrap_zzq_publish_stats(void) { }
void zzq_record_file_req(const char *path) { (void)path; }
void zzq_fatal_end(u32 code) { printf("zzq_fatal_end(%u)\n", code); exit(3); }

int zzq_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("[zzq] ");
    vprintf(fmt, ap);
    va_end(ap);
    return 0;
}

void Sys_Error (char *error, ...)
{
    va_list argptr;
    printf("Sys_Error: ");
    va_start(argptr, error);
    vprintf(error, argptr);
    va_end(argptr);
    printf("\n");
    exit(2);
}

void Sys_Printf (char *fmt, ...)
{
    va_list argptr;
    va_start(argptr, fmt);
    vprintf(fmt, argptr);
    va_end(argptr);
}

void Sys_Quit (void) { zzq_running = 0; }
char *Sys_ConsoleInput (void) { return NULL; }
void Sys_Sleep (void) { }
void Sys_SendKeyEvents (void) { }
void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length) { (void)startaddr; (void)length; }
void Sys_HighFPPrecision (void) { }
void Sys_LowFPPrecision (void) { }
void Sys_SetFPCW (void) { }
void Sys_DebugLog (char *file, char *fmt, ...) { (void)file; (void)fmt; }
int  QG_GetKey(int *down, int *key) { (void)down; (void)key; return 0; }
void QG_GetMouseMove(int *x, int *y) { *x = 0; *y = 0; }
void QG_SetPalette(unsigned char palette[768]) { (void)palette; shared[SH_PALETTE_COUNT]++; }
void QG_GetJoyAxes(float *axes) { int i; for (i = 0; i < 8; i++) axes[i] = 0.0f; }
int zzgl_test_force_busy;   /* ZZGL host hook (ZZGL_FORCE_BUSY of the Q2 bench): off */
qboolean isDedicated = false;

/* ---- file system (sys_null.c of quakegeneric, stdio) ------------------ */
#define MAX_HANDLES 10
static FILE *sys_handles[MAX_HANDLES];
static int findhandle (void)
{
    int i;
    for (i=1 ; i<MAX_HANDLES ; i++)
        if (!sys_handles[i])
            return i;
    Sys_Error ("out of handles");
    return -1;
}
static int filelength (FILE *f)
{
    int pos, end;
    pos = ftell (f);
    fseek (f, 0, SEEK_END);
    end = ftell (f);
    fseek (f, pos, SEEK_SET);
    return end;
}
int Sys_FileOpenRead (char *path, int *hndl)
{
    FILE *f;
    int i;
    i = findhandle ();
    f = fopen(path, "rb");
    if (!f) { *hndl = -1; return -1; }
    sys_handles[i] = f;
    *hndl = i;
    return filelength(f);
}
int Sys_FileOpenWrite (char *path)
{
    FILE *f;
    int i;
    i = findhandle ();
    f = fopen(path, "wb");
    if (!f) { printf("Sys_FileOpenWrite: %s: %s\n", path, strerror(errno)); return -1; }
    sys_handles[i] = f;
    return i;
}
void Sys_FileClose (int handle) { if (handle > 0) { fclose (sys_handles[handle]); sys_handles[handle] = NULL; } }
void Sys_FileSeek (int handle, int position) { fseek (sys_handles[handle], position, SEEK_SET); }
int Sys_FileRead (int handle, void *dest, int count) { return fread (dest, 1, count, sys_handles[handle]); }
int Sys_FileWrite (int handle, void *data, int count) { return fwrite (data, 1, count, sys_handles[handle]); }
int Sys_FileTime (char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return -1;
}
void Sys_mkdir (char *path) { mkdir(path, 0777); }

/* ---- presentation (zzqgl_present.c on the target) ----------------------- */
int zzq2_present_burst = 1;
void (*zzq2_present_hook)(void);
static unsigned g_keepalive;
void zzqgl_present_keepalive(void) { g_keepalive++; }
int zzqgl_present_special;   /* 0.2: 1 = the early black frame of VID_Init (gl_vid_zz.c) */
int zzqgl_present_must_show; /* 0.2: plaque / dialog frame (no buffer wait in the model) */
static unsigned g_must_show;
void zzqgl_present_show_stats(unsigned *waits, unsigned *timeouts) { *waits = g_must_show; *timeouts = 0; }
#ifndef ZZQGL_VID_W                 /* 0.4: build_host_q1.sh ZZQGL_RES */
#define ZZQGL_VID_W 640
#define ZZQGL_VID_H 480
#endif
#define VW ZZQGL_VID_W
#define VH ZZQGL_VID_H
static unsigned g_holes_frames, g_holes_max, g_holes_max_frame;
void zzq2_present_rgb565(const unsigned short *pixels, unsigned pitch_bytes)
{
    char path[512];
    FILE *f;
    int x, y;
    g_presented++;
    if (zzqgl_present_must_show) { g_must_show++; printf("plaque/dialog frame handed over at once (frame %u)\n", g_frames); }
    if (zzq2_present_hook) { int k; for (k = 0; k < 30; k++) zzq2_present_hook(); }
    {   /* pixels this frame did not write: the buffer was filled with 0xF81F after its previous presentation */
        unsigned holes = 0, black = 0;
        for (y = 0; y < VH; y++) {
            const unsigned short *row = (const unsigned short *)((const unsigned char *)pixels + (unsigned)y * pitch_bytes);
            for (x = 0; x < VW; x++) { if (row[x] == 0xF81Fu) holes++; if (row[x] == 0u) black++; }
        }
        if (g_presented > 2u) { if (holes) g_holes_frames++; if (holes > g_holes_max) { g_holes_max = holes; g_holes_max_frame = g_frames; } }
        if (zzqgl_present_special == 1) {
            printf("early frame presented from VID_Init: %u of %u pixels black, %u unwritten\n", black, VW * VH, holes);
            snprintf(g_shot, sizeof(g_shot), "early");
        }
    }
    if (g_shot[0]) {
        snprintf(path, sizeof(path), "%s/%s.ppm", g_out, g_shot);
        f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", VW, VH);
            for (y = 0; y < VH; y++) {
                const unsigned short *row = (const unsigned short *)((const unsigned char *)pixels + (unsigned)y * pitch_bytes);
                for (x = 0; x < VW; x++) {
                    unsigned v = row[x], r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
                    fputc((int)((r << 3) | (r >> 2)), f); fputc((int)((g << 2) | (g >> 4)), f); fputc((int)((b << 3) | (b >> 2)), f);
                }
            }
            fclose(f);
        }
        printf("shot %s (frame %u, violations so far %d)\n", path, g_frames, sim_model_violations());
        {
            extern const void *zzgl_get_stats(void);
            const unsigned *st = (const unsigned *)zzgl_get_stats();
            if (st) printf("   zzgl frame: begins %u verts %u tris_in %u tris_out %u binds %u (+%u skipped) uploads %u/%u clipped %u culled %u state %u errors %u\n",
                           st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[7], st[8], st[9], st[10], st[11]);
        }
        g_shot[0] = 0;
    }
    { int yy, xx; for (yy = 0; yy < VH; yy++) { unsigned short *row = (unsigned short *)((unsigned char *)(unsigned long)pixels + (unsigned)yy * pitch_bytes); for (xx = 0; xx < VW; xx++) row[xx] = 0xF81Fu; } }
}

/* ---- cost of the alias model meshing (shareware: the ms2 cache is never read) ---- */
#include <time.h>
extern void __real_GL_MakeAliasModelDisplayLists(void *m, void *hdr);
static double g_mesh_s; static int g_mesh_n;
void __wrap_GL_MakeAliasModelDisplayLists(void *m, void *hdr)
{
    clock_t t0 = clock();
    __real_GL_MakeAliasModelDisplayLists(m, hdr);
    g_mesh_s += (double)(clock() - t0) / CLOCKS_PER_SEC; g_mesh_n++;
}
/* ---- main ---------------------------------------------------------------- */
int main(int argc, char **argv)
{
    char *q1argv[16];
    int q1argc = 0, frame;
    char script[4096], *p;
    char basedir[512];
    if (argc < 4) { fprintf(stderr, "usage: host_q1 <basedir> <outdir> <script>\n"); return 1; }
    g_out = argv[2];
    sim_model_tmu = getenv("SIM_TMU") ? atoi(getenv("SIM_TMU")) : 1;
    mkdir(g_out, 0777);
    sim_model_reset();
    snprintf(basedir, sizeof(basedir), "%s", argv[1]);

    /* what the launcher leaves in the shared block */
    shared[SH_FB_WIDTH] = VW; shared[SH_FB_HEIGHT] = VH; shared[SH_FB_BPP] = 4;
    shared[SH_TRIPLE] = 1;
    shared[SH_PRELOAD_STATE] = ZZQ_PRELOAD_START;
    shared[SH_PACK_COUNT] = 1; shared[SH_PACK0_ADDR] = 0x30000000u; shared[SH_PACK0_SIZE] = 18689235u;
    shared[SH_HUNK_MB] = getenv("HUNK_MB") ? (u32)atoi(getenv("HUNK_MB")) : 0u;
    shared[SH_PCM_ENABLE] = 0;

    q1argv[q1argc++] = "zzquake";
    q1argv[q1argc++] = "-basedir"; q1argv[q1argc++] = basedir;
    q1argv[q1argc++] = "-nosound";
    if (getenv("Q1_TIMEDEMO")) { q1argv[q1argc++] = "+timedemo"; q1argv[q1argc++] = "demo1"; }
    q1argv[q1argc] = NULL;
    snprintf(script, sizeof(script), "%s", argv[3]);

    QG_Create(q1argc, q1argv);
    for (frame = 1; frame < 1000000 && zzq_running; frame++) {
        p = script;
        while (p && *p) {
            char *semi = strchr(p, ';');
            char item[256];
            int at;
            size_t len = semi ? (size_t)(semi - p) : strlen(p);
            if (len >= sizeof(item)) len = sizeof(item) - 1;
            memcpy(item, p, len); item[len] = 0;
            at = atoi(item);
            if (at == frame) {
                char *cmd = strchr(item, ':');
                if (cmd) {
                    cmd++;
                    if (!strncmp(cmd, "shot ", 5)) snprintf(g_shot, sizeof(g_shot), "%s", cmd + 5);
                    else if (!strcmp(cmd, "quit")) {
                        unsigned fr, ga, gm, pa, b, pf;
                        zz_rix_stats(&fr, &ga, &gm, &pa, &b, &pf);
                        printf("end: %u frames, %u presented, violations %d, GPU pixels %u, FIFO words %u, model time %.1f ms, rix frames %u (stray x100000), pool free %u pages\n",
                               g_frames, g_presented, sim_model_violations(), sim_model_pixels(), sim_model_fifo_words(), sim_model_now_us() / 1000.0, fr, pf);
                        { extern void Host_Shutdown(void); Host_Shutdown(); }
                        printf("holes: %u presented frames left pixels unwritten (max %u at frame %u)\n", g_holes_frames, g_holes_max, g_holes_max_frame);
                        printf("cpu: alias meshing %.3f s for %d models (x86 host, this run)\n", g_mesh_s, g_mesh_n);
                        printf("keepalive hook calls (Draw_BeginDisc): %u\n", g_keepalive);
                        return sim_model_violations() ? 2 : 0;
                    }
                    else { Cbuf_AddText(cmd); Cbuf_AddText("\n"); }
                }
            }
            p = semi ? semi + 1 : NULL;
        }
        g_now_ms += 16;
        QG_Tick(0.016);
        g_frames++;
    }
    (void)g_dropped;
    return 0;
}
