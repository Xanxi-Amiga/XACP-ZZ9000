/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
/* ZZOpenLaraRIX A2: the OpenLara engine (Game::init / update / render) on Core1.
 *
 * One translation unit holds the whole engine (OpenLara is header-only), built for the _OS_ZZ
 * platform of core.h: FFP + SPLIT_BY_TILE, gapi/zz.h on ZZGL/librixgpu, no sound mixer yet.
 * Engine: BeWorld2018/OpenLara, branch MorphOS, commit 2c2bcd679553cac9f47573d2cf2cff82005fc2fd.
 *
 * This file provides what the engine expects from its platform:
 *   - the os* hooks of core.h (mutexes, clock, joystick, VR/fullscreen);
 *   - the entry points called by Core1/platform.c: ol_init (container, GPU, Game::init),
 *     ol_tick (input, Game::update, Game::render), ol_shutdown;
 *   - the key mapping from the launcher's key codes (quakegeneric K_*) to InputKey.
 *
 * Files come from the preloaded container through ol_stdio.h / ol_fs.c (force-included).
 * ASCII only.
 */
#define _OS_ZZ 1
#if defined(OL_GAPI_SW) || defined(OL_GAPI_TGL)
#define OL_SOFT 1   /* A2.3/A2.4: rendered by Core1 itself (gapi/sw.h or gapi/tgl.h), no GPU */
#endif

#include "ol_api.h"
#include "game.h"

extern "C" void ol_music_stats(unsigned*, unsigned*, unsigned*, unsigned*, unsigned*, unsigned*);   /* A2.19 */
extern "C" void ol_music_enable(int on);
extern "C" int ol_fs_exists(const char *path);   /* A2.20 */
extern "C" void ol_fs_loose_stats(unsigned*, unsigned*, unsigned*, unsigned*);
extern "C" void ol_fs_stream_stats(unsigned*, unsigned*, unsigned*, unsigned*);   /* A2.21 */
extern "C" void ol_fs_stream_pump(void);
extern "C" void ol_fs_stream_errors(unsigned*, unsigned*, unsigned*, unsigned*, unsigned*);   /* A2.22.2 */
extern "C" void ol_fs_read_at_stats(unsigned*, unsigned*, unsigned*);   /* A2.21.2: video read-ahead, once per frame */
#ifdef OL_SOFT
extern "C" {
void zzq2_present32(const unsigned *pixels, int w, int h);
void zzq2_present_rgb565_wh(const unsigned short *pixels, int w, int h);
void ol_screen_size(int *w, int *h);
void ol_fs_stats(unsigned*, unsigned*, unsigned*);
void ol_diag(unsigned stage);
}
#else
extern "C" {
#include "zzgl.h"
#include "rixframe.h"
int  zz_rix_open(int, int, char*, int);
void zz_rix_close(void);
void zz_rix_set_alpha_textures(int);
void zz_rix_set_ring(int);
void zz_rix_set_overlap(int);
void zz_rix_stats(unsigned*, unsigned*, unsigned*, unsigned*, unsigned*, unsigned*);
void ol_fs_stats(unsigned*, unsigned*, unsigned*);
void ol_diag(unsigned stage);
void ol_screen_size(int *w, int *h);
}
#ifdef OL_RIX320
#define OL_RIX_W 320   /* A2.6: zzopenlara_rix320.bin, real 320 x 240 screen (Core1/zzq2rix_gpu.c) */
#define OL_RIX_H 240
#elif defined(OL_RIX800)
#define OL_RIX_W 800   /* A2.15: zzopenlara_rix800.bin, test build, real 800 x 600 screen */
#define OL_RIX_H 600
#else
#define OL_RIX_W 640
#define OL_RIX_H 480
#endif
#endif

/* identity of the game blob: launcher/ZZOpenLara.c refuses a zzopenlara.bin without it (the A1 room
   viewer blob would otherwise run silently with the A2 container, whose first entry is the level) */
#if defined(OL_GAPI_SW)
extern "C" __attribute__((used, section(".rodata.ol_ident"))) const char ol_blob_ident[] = "ZZOPENLARA-GAME-A2 SW";
#elif defined(OL_GAPI_TGL)
extern "C" __attribute__((used, section(".rodata.ol_ident"))) const char ol_blob_ident[] = "ZZOPENLARA-GAME-A2 TGL";
#elif defined(OL_RIX320)
extern "C" __attribute__((used, section(".rodata.ol_ident"))) const char ol_blob_ident[] = "ZZOPENLARA-GAME-A2 RIX320";
#elif defined(OL_RIX800)
extern "C" __attribute__((used, section(".rodata.ol_ident"))) const char ol_blob_ident[] = "ZZOPENLARA-GAME-A2 RIX800";
#else
extern "C" __attribute__((used, section(".rodata.ol_ident"))) const char ol_blob_ident[] = "ZZOPENLARA-GAME-A2";
#endif

#ifdef OL_SOFT
/* ---- A2.3 software renderers: presentation and the statistics the RIX backend gives otherwise -- */
static unsigned g_pres_frames, g_pres_last_us;
static unsigned long long g_pres_tot_us;

static void soft_present() {
    unsigned t0 = ol_clock_us();
#if defined(OL_GAPI_SW)
    zzq2_present_rgb565_wh((const unsigned short*)GAPI::swColor, Core::width, Core::height);
#else
    zzq2_present32(GAPI::tglPixels(), Core::width, Core::height);
#endif
    g_pres_last_us = ol_clock_us() - t0;
    g_pres_tot_us += g_pres_last_us;
    g_pres_frames++;
}

/* same outputs as zz_rix_stats: frames, GPU wait (none), max, present average (us), binds, free pages */
static void zz_rix_stats(unsigned *nf, unsigned *gu, unsigned *gm, unsigned *pu, unsigned *binds, unsigned *freep) {
    *nf = g_pres_frames; *gu = 0; *gm = 0;
    *pu = g_pres_frames ? (unsigned)(g_pres_tot_us / g_pres_frames) : 0u;
    *binds = 0; *freep = 0;
}
#endif

/* A2.24: the measurement report (olstat_<level>.txt) is written only with STATS=1 (release: off; TOUR= still
   writes its oltour_ report) */
static int g_cfg_stats = 0;

#ifdef OL_GAPI_SW
/* ---- A2.12: measurement of the software renderer, PROGDIR:data/olstat_<level>.txt ------------------
   Per frame (global-timer ticks of gapi/sw.h, 333 per us): clears, transform + lighting, rasterization;
   the counters of the culling before lighting; memcpy / memset / memmove of our code (Core1/ol_memprof.c) */
extern "C" {
int  ol_stat_write(const char *name, const char *text, unsigned len);
extern unsigned ol_mem_calls[3];
extern unsigned long long ol_mem_bytes[3], ol_mem_ticks[3];
}
struct SwAcc { unsigned long long frames, period, upd, rnd, clr, xf, ras, pres, snd, prims, cdist, cback, cclip, vin, vxf, vlit, spans, pix, clipped, light; };
static SwAcc g_sw_all, g_sw_vb[3];
static unsigned g_sw_hist[26], g_sw_last_end;
static unsigned g_sw_pres_last, g_sw_snd_last;

static void sw_frame(unsigned upd_us, unsigned rnd_us) {
    unsigned now = ol_clock_us();
    unsigned period = g_sw_last_end ? now - g_sw_last_end : 0u;
    g_sw_last_end = now;
    unsigned long long v[20] = { 1, period, upd_us, rnd_us, GAPI::swTClear / 333u, GAPI::swTXform / 333u, GAPI::swTRaster / 333u,
        g_sw_pres_last, g_sw_snd_last, GAPI::swPrims, GAPI::swCullDist, GAPI::swCullBack, GAPI::swCullClip,
        GAPI::swVertsIn, GAPI::swVertsXf, GAPI::swVertsLit, GAPI::swSpans, GAPI::swPixels, GAPI::swClipped, GAPI::swTLight / 333u };
    GAPI::swTClear = GAPI::swTXform = GAPI::swTRaster = GAPI::swTLight = 0;
    GAPI::swPrims = GAPI::swCullDist = GAPI::swCullBack = GAPI::swCullClip = 0;
    GAPI::swVertsIn = GAPI::swVertsXf = GAPI::swVertsLit = GAPI::swSpans = GAPI::swPixels = GAPI::swClipped = 0;
    if (!period) return;   /* first frame: no period */
    int vb = period <= 16810u ? 0 : (period <= 33620u ? 1 : 2);
    unsigned long long *a = &g_sw_all.frames, *b = &g_sw_vb[vb].frames;
    for (int k = 0; k < 20; k++) { a[k] += v[k]; b[k] += v[k]; }
    unsigned bin = period / 2000u; if (bin > 25u) bin = 25u;
    g_sw_hist[bin]++;
}

static char g_sw_rep[6144];
static unsigned g_sw_rep_n;
static void sws(const char *s) { while (*s && g_sw_rep_n < sizeof(g_sw_rep) - 1) g_sw_rep[g_sw_rep_n++] = *s++; }
static void swu(unsigned long long v) { char b[24]; int n = 0; do { b[n++] = char('0' + v % 10); v /= 10; } while (v); while (n) { char s[2] = { b[--n], 0 }; sws(s); } }
static void swms(unsigned long long us_tot, unsigned long long n) {   /* ms per frame, one decimal */
    unsigned long long x = n ? (us_tot * 10ull + n / 2) / (n * 1000ull) : 0; swu(x / 10); sws("."); swu(x % 10);
}
static void sw_block(const char *title, const SwAcc &s) {
    unsigned long long n = s.frames ? s.frames : 1;
    sws(title); swu(s.frames); sws(" frames, period "); swms(s.period, n); sws(" ms\n");
    sws("  ms per frame   upd "); swms(s.upd, n); sws("   rnd "); swms(s.rnd, n); sws("   present "); swms(s.pres, n); sws("   snd "); swms(s.snd, n); sws("\n");
    sws("  rnd = clear "); swms(s.clr, n); sws(" + transform/lighting "); swms(s.xf, n); sws(" (lighting "); swms(s.light, n); sws(") + raster "); swms(s.ras, n);
    unsigned long long rest = s.rnd > s.clr + s.xf + s.ras ? s.rnd - s.clr - s.xf - s.ras : 0;
    sws(" + engine/rest "); swms(rest, n); sws("\n");
    sws("  per frame      primitives "); swu(s.prims / n); sws(" (out of range "); swu(s.cdist / n); sws(", facing away "); swu(s.cback / n);
    sws(", outside the clip rect "); swu(s.cclip / n); sws(")  vertex refs "); swu(s.vin / n); sws(" projected "); swu(s.vxf / n);
    sws(" lit "); swu(s.vlit / n); sws("  spans "); swu(s.spans / n); sws(" pixels "); swu(s.pix / n);
    sws("  clipped "); swu(s.clipped / n); sws("\n");
#ifdef ZZQ2RIX_HOST
    if (&s == &g_sw_all) { sws("  bench: pixels past the depth test "); swu(GAPI::swHostPass / n); sws(", written "); swu(GAPI::swHostWrite / n); sws("\n"); }
#endif
}
static void sw_report(const char *level) {
    if (!g_cfg_stats) { printf("OL STAT off (STATS=1 writes olstat_<level>.txt)\n"); return; }
    g_sw_rep_n = 0;
    sws(ol_blob_ident); sws(" (ZZOpenLara 1.0, software) measurement report\nlevel "); sws(level); sws("  screen ");
    swu(Core::width); sws("x"); swu(Core::height); sws("  SWRAST="); swu(GAPI::swRast); sws(" SWDIST="); swu((unsigned)(GAPI::swMaxDist / 1024.0f + 0.5f)); sws("\nperiod = from the end of one frame to the end of the next (P96 copy included)\n\n");
    sw_block("all frames: ", g_sw_all);
    sws("\n"); sw_block("frames of 1 VBlank (<= 16.8 ms): ", g_sw_vb[0]);
    sw_block("frames of 2 VBlanks (16.8 - 33.6 ms): ", g_sw_vb[1]);
    sw_block("frames of 3 VBlanks or more (> 33.6 ms): ", g_sw_vb[2]);
    sws("\nframe period histogram (2 ms bins):\n");
    for (int k = 0; k < 26; k++) if (g_sw_hist[k]) { sws("  "); swu(k * 2); sws(k == 25 ? "+    ms " : "-"); if (k < 25) { swu(k * 2 + 2); sws(" ms "); } sws("  "); swu(g_sw_hist[k]); sws("\n"); }
    static const char *mn[3] = { "memcpy ", "memset ", "memmove" };
    sws("\nmemcpy / memset / memmove of our code (release byte loops), whole run:\n");
    for (int k = 0; k < 3; k++) {
        sws("  "); sws(mn[k]); sws("  calls "); swu(ol_mem_calls[k]); sws("  KiB "); swu(ol_mem_bytes[k] / 1024u);
        sws("  ms "); swu(ol_mem_ticks[k] / 333000ull); sws("\n");
    }
    char fname[64];
    int k = 0; const char *l = level, *pre = "olstat_";
    while (*pre) fname[k++] = *pre++;
    while (*l && *l != '.' && k < 52) fname[k++] = *l++;
    fname[k++] = '.'; fname[k++] = 't'; fname[k++] = 'x'; fname[k++] = 't'; fname[k] = 0;
    int ok = ol_stat_write(fname, g_sw_rep, g_sw_rep_n);
    printf("OL STAT %s %u bytes %s\n", fname, g_sw_rep_n, ok ? "written" : "NOT WRITTEN");
}
#endif

/* ---- A2.5: sound effects ------------------------------------------------------------------- */
/* A2.6: the engine's mixer (Sound::fill) runs at 22050 Hz (sound.h, SND_RATE for _OS_ZZ), the rate of the
   release PCM ring (Core1/ol_snd.c) and of the TR1 samples: written as is. Sound::fill sizes its work
   buffers on its first call, so it is always called with OL_MIX_FRAMES. Pumped once per tick, up to the
   ring's fill target (186 ms). */
extern "C" {
int  ol_pcm_open(void);
int  ol_pcm_room(void);
void ol_pcm_write(const short *lr, int n);
unsigned ol_pcm_underruns(void);
}
#define OL_MIX_FRAMES 512
typedef char ol_frame_is_lr16[sizeof(Sound::Frame) == 4 ? 1 : -1];
static Sound::Frame g_mix[OL_MIX_FRAMES];
static int g_snd_on;
static unsigned g_snd_us, g_snd_chunks;       /* over the sample second */
static unsigned long long g_snd_tot_us;

#ifdef ZZQ2RIX_HOST
#include <time.h>
static unsigned long long g_host_snd_ns, g_host_snd_frames;   /* host bench: real time inside Sound::fill */
#endif
extern "C" void ol_pcm_set_fill(unsigned bytes);   /* A2.21.2 */
static void snd_pump() {
    if (g_snd_on <= 0) return;
#if OL_VIDEO_AUDIOCLOCK
    ol_pcm_set_fill((inventory && inventory->video) ? 49152u : 16384u);   /* A2.21.3 (prepared, off): 0.56 s ahead during a film */
#endif
    unsigned t0 = ol_clock_us();
    int room = ol_pcm_room(), n = 0;
    while (room >= OL_MIX_FRAMES && n < 16) {
#ifdef ZZQ2RIX_HOST
        { struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
        Sound::fill(g_mix, OL_MIX_FRAMES);
        clock_gettime(CLOCK_MONOTONIC, &b); g_host_snd_ns += (unsigned long long)((b.tv_sec - a.tv_sec) * 1000000000LL + (b.tv_nsec - a.tv_nsec)); g_host_snd_frames += OL_MIX_FRAMES; }
#else
        Sound::fill(g_mix, OL_MIX_FRAMES);
#endif
        ol_pcm_write((const short*)g_mix, OL_MIX_FRAMES);   /* Frame = { int16 L, R }: interleaved */
        room -= OL_MIX_FRAMES;
        n++;
    }
    unsigned dt = ol_clock_us() - t0;
    g_snd_us += dt;
    g_snd_tot_us += dt;
    g_snd_chunks += (unsigned)n;
}

/* ---- OS hooks (core.h) ------------------------------------------------------------------- */

/* one thread on Core1: the mutex object is a non-null token, lock/unlock do nothing */
void* osMutexInit()           { return (void*)1; }
void  osMutexFree(void *obj)  { (void)obj; }
void  osMutexLock(void *obj)  { (void)obj; }
void  osMutexUnlock(void *obj){ (void)obj; }

/* the global timer in microseconds wraps every 71 minutes (32 bits): keep a 64-bit total */
static unsigned     g_last_us;
static unsigned long long g_total_us;
static int          g_clock_init;

static unsigned long long ol_total_us() {
    unsigned now = ol_clock_us();
    if (!g_clock_init) { g_last_us = now; g_clock_init = 1; }
    g_total_us += (unsigned)(now - g_last_us);
    g_last_us = now;
    return g_total_us;
}

int osGetTimeMS() {
    return (int)(ol_total_us() / 1000ull);
}

bool osJoyReady(int index)                       { (void)index; return false; }
void osJoyVibrate(int index, float L, float R)   { (void)index; (void)L; (void)R; }

/* ---- options from the container (zzopenlara.cfg, "KEY=VALUE" lines) ---------------------- */

struct OLConfig {
    char level[64];
    int  defer, overlap, clearflush, texver, ring, pump, alphatex, filter, quitkey, cull;
};
static OLConfig g_cfg;
#ifndef OL_SOFT
static int g_cfg_sync, g_cfg_prof;   /* A2.7: zzopenlara.cfg RIXSYNC= / RIXPROF= */
static int g_cfg_wnorm = 1;          /* A2.16: RIXWNORM= (librixgpu rix_tex_wnorm), 1 by default */
static int g_cfg_desc = 1;           /* A2.17: RIXDESC= (librixgpu rix_desc_delta), 1 by default */
static int g_cfg_preclear = 1;       /* A2.13: RIXPRECLEAR= ; A2.14: 1 by default (10A on the A4000: 17.5 -> 14.8 ms per frame) */
static int g_tour_fpv;               /* A2.9: zzopenlara.cfg TOUR=n, frames per view of the room tour */
#endif

static void cfg_defaults() {
    memset(&g_cfg, 0, sizeof g_cfg);
    strcpy(g_cfg.level, "LEVEL1.PHD");
    g_cfg.defer      = 1;    /* ZZQuake2RIX 0.9.4 defaults, validated on the A4000 (16/10) */
    g_cfg.overlap    = 1;
    g_cfg.clearflush = 1;
    g_cfg.texver     = 1;
    g_cfg.ring       = 1;
    g_cfg.pump       = 32;
    g_cfg.alphatex   = 1;    /* RGBA5551 for alpha-keyed tiles (A1 ran with it) */
    g_cfg.filter     = 1;    /* 1 = the TMU's bilinear filter, 0 = nearest */
    g_cfg.quitkey    = 144;  /* F10 (quakegeneric K_F1 + 9): leave at once, outside the inventory */
    g_cfg.cull       = 1;
#ifdef OL_GAPI_TGL
    g_cfg.filter     = 0;    /* TinyGL: bilinear = four texels per pixel on Core1, FILTER=1 to try it */
#endif
}

static void cfg_apply(const char *key, const char *val) {
    int n = atoi(val);
    if (!strcmp(key, "LEVEL"))           { strncpy(g_cfg.level, val, sizeof(g_cfg.level) - 1); }
    else if (!strcmp(key, "RIXDEFER"))   g_cfg.defer = n;
    else if (!strcmp(key, "RIXOVERLAP")) g_cfg.overlap = n;
    else if (!strcmp(key, "RIXCLEARFLUSH")) g_cfg.clearflush = n;
    else if (!strcmp(key, "RIXTEXVER"))  g_cfg.texver = n;
    else if (!strcmp(key, "RIXRING"))    g_cfg.ring = n;
    else if (!strcmp(key, "RIXPUMP"))    g_cfg.pump = n;
    else if (!strcmp(key, "ALPHATEX"))   g_cfg.alphatex = n;
    else if (!strcmp(key, "FILTER"))     g_cfg.filter = n;
    else if (!strcmp(key, "QUITKEY"))    g_cfg.quitkey = n;
    else if (!strcmp(key, "CULL"))       g_cfg.cull = n;
    else if (!strcmp(key, "STATS"))      g_cfg_stats = n;   /* A2.24: 1 = olstat_<level>.txt at the end of the run */
#ifdef OL_GAPI_TGL
    else if (!strcmp(key, "TGLSUBPIX"))  GAPI::tglSubpix = n;  /* A2.18: vertices to the sub-pixel (1, default) or on whole pixels (0) */
    else if (!strcmp(key, "TGLPERSP"))   GAPI::tglPersp = n;   /* A2.18: textures divided by 1/w (1, default) or by the depth (0) */
#endif
#ifdef OL_GAPI_SW
    else if (!strcmp(key, "SWRAST"))     GAPI::swRast = n;   /* A2.13: 1 = clipping, sub-pixel, perspective; 0 = the A2.12 rasterizer */
    else if (!strcmp(key, "SWDIST"))     { int b = n < 8 ? 8 : (n > 40 ? 40 : n); GAPI::swMaxDist = float(b) * 1024.0f; GAPI::swFogStart = float(b) * 1024.0f * 0.6f; }   /* A2.13: draw distance, blocks (fog from 60 %); A2.14: 30 by default */
#endif
#ifndef OL_SOFT
    else if (!strcmp(key, "RIXSYNC"))    g_cfg_sync = n;   /* A2.7: GPU time (every chunk waited) */
    else if (!strcmp(key, "RIXPROF"))    g_cfg_prof = n;   /* A2.7: zzglEnd split (descriptors) */
    else if (!strcmp(key, "RIXPRECLEAR")) g_cfg_preclear = n;   /* A2.13: colour clear by Core1 after the P96 copy */
    else if (!strcmp(key, "RIXZTRICK"))  GAPI::zTrick = n;   /* A2.14: no depth clear, alternating depth halves; A2.16: 1 by default */
    else if (!strcmp(key, "RIXWNORM"))   g_cfg_wnorm = n;    /* A2.16: texture planes with 1/w normalised per triangle */
    else if (!strcmp(key, "RIXDESC"))    g_cfg_desc = n;     /* A2.17: descriptor planes from vertex differences */
    else if (!strcmp(key, "RIXLITCACHE")) GAPI::litCache = n; /* A2.17: lit vertices of static meshes kept */
    else if (!strcmp(key, "RIXSORT"))    GAPI::sortOn = n; /* A2.8: opaque pass sorted by texture (A2.10: 2 = rooms too) */
    else if (!strcmp(key, "SCISCULL"))   GAPI::scisCull = n;   /* A2.11: triangles outside the portal rectangle dropped */
    else if (!strcmp(key, "DRAWDIST"))   GAPI::drawDist = n < 0 ? 0 : (n > 64 ? 64 : n);   /* A2.11: draw distance, blocks */
    else if (!strcmp(key, "ZNEAR"))      GAPI::zNear = n < 8 ? 8 : (n > 512 ? 512 : n);   /* A2.10: camera near plane */
    else if (!strcmp(key, "TOUR"))       g_tour_fpv = n < 0 ? 0 : (n > 60 ? 60 : n);   /* A2.9: room tour */
#endif
}

static void cfg_read() {
    unsigned size = 0;
    const char *p = (const char*)ol_fs_find("zzopenlara.cfg", &size);
    char key[32], val[64];
    unsigned i = 0;
    cfg_defaults();
    if (!p) return;
    while (i < size) {
        unsigned k = 0, v = 0;
        while (i < size && (p[i] == ' ' || p[i] == '\r' || p[i] == '\n' || p[i] == '\t')) i++;
        while (i < size && p[i] != '=' && p[i] != '\n' && p[i] != '\r' && k < sizeof(key) - 1) key[k++] = p[i++];
        key[k] = 0;
        if (i < size && p[i] == '=') {
            i++;
            while (i < size && p[i] != '\n' && p[i] != '\r' && v < sizeof(val) - 1) val[v++] = p[i++];
        }
        val[v] = 0;
        while (v && (val[v - 1] == ' ' || val[v - 1] == '\t')) val[--v] = 0;
        if (k) cfg_apply(key, val);
        while (i < size && p[i] != '\n') i++;
    }
}

/* ---- input --------------------------------------------------------------------------------- */

/* quakegeneric key codes (launcher/zzquake_config.h QK_*, quake_headers/quakekeys.h K_*) */
static InputKey mapKey(int key) {
    if (key >= 'a' && key <= 'z') return InputKey(ikA + (key - 'a'));
    if (key >= 'A' && key <= 'Z') return InputKey(ikA + (key - 'A'));
    if (key >= '0' && key <= '9') return InputKey(ik0 + (key - '0'));
    if (key >= 135 && key <= 146) return InputKey(ikF1 + (key - 135));
    switch (key) {
        case 128 : return ikUp;
        case 129 : return ikDown;
        case 130 : return ikLeft;
        case 131 : return ikRight;
        case 132 : return ikAlt;
        case 133 : return ikCtrl;
        case 134 : return ikShift;
        case 27  : return ikEscape;
        case 13  : return ikEnter;
        case 32  : return ikSpace;
        case 9   : return ikTab;
        case 127 : return ikBack;
        case 148 : return ikDel;
        case 147 : return ikIns;
        case 149 : return ikNext;
        case 150 : return ikPrev;
        case 151 : return ikHome;
        case 152 : return ikEnd;
        case 200 : return ikMouseL;
        case 201 : return ikMouseR;
        case 202 : return ikMouseM;
        case '-' : return ikMinus;
        case '=' : case '+' : return ikPlus;
        case '[' : return ikLSB;
        case ']' : return ikRSB;
        case '/' : return ikSlash;
        case '\\': return ikBSlash;
        case ',' : return ikComma;
        case '.' : return ikDot;
        case '`' : return ikTilda;
        case ';' : return ikColon;
        case '\'': return ikApos;
        default  : return ikNone;
    }
}

static vec2 g_mouse_pos;

static void pumpInput() {
    int down, key;
    while (ol_key(&down, &key)) {
        if (down && key == g_cfg.quitkey) {
            Core::quit();
            continue;
        }
        InputKey k = mapKey(key);
        if (k != ikNone) Input::setDown(k, down != 0);
    }
    int mx, my;
    ol_mouse(&mx, &my);
    if (mx || my) {
        g_mouse_pos += vec2(float(mx), float(my));
        Input::setPos(ikMouseL, g_mouse_pos);
        Input::setPos(ikMouseR, g_mouse_pos);
    }
}

/* ---- statistics ---------------------------------------------------------------------------- */

static unsigned g_frames, g_sample_frames, g_sample_time, g_fps10, g_first_frame;
static unsigned g_update_us, g_render_us;   /* accumulated over the sample second */
static unsigned long long g_tot_update_us, g_tot_render_us;   /* A2.2: whole session */
static unsigned g_min_fps10 = 0xFFFFFFFFu, g_first_tick;

static void publishStats() {
    unsigned now = ol_clock_us();
    if ((unsigned)(now - g_sample_time) < 1000000u) return;
    unsigned dt = now - g_sample_time;
    unsigned n = g_frames - g_sample_frames;
    g_fps10 = (unsigned)((double)n * 10000000.0 / (double)dt);
    if (g_fps10 < g_min_fps10) g_min_fps10 = g_fps10;
    unsigned nf, gu, gm, pu, binds, freep, om, os, miss;
    zz_rix_stats(&nf, &gu, &gm, &pu, &binds, &freep);
    ol_fs_stats(&om, &os, &miss);
#ifndef OL_SOFT
    const zzgl_stats *st = zzgl_get_stats();
#endif
    int room = -1, px = 0, py = 0, pz = 0, hp = 0;
    if (Game::level && Game::level->players[0]) {
        Lara *lara = (Lara*)Game::level->players[0];
        room = lara->getRoomIndex();
        px = int(lara->pos.x); py = int(lara->pos.y); pz = int(lara->pos.z);
        hp = int(lara->health);
    }
#ifdef ZZQ2RIX_HOST
    if (Game::level && Game::level->players[0]) {
        Lara *lara = (Lara*)Game::level->players[0];
        printf("HOST LARA state=%d anim=%d input=%x stand=%d up=%d st_up=%d st_right=%d inv=%d angle=%.2f\n",
               lara->state, lara->animation.index, lara->input, lara->stand, Input::down[ikUp], Input::state[0][cUp], Input::state[0][cRight],
               inventory->isActive(), lara->angle.y);
        if (lara->camera) {
            Camera *cam = (Camera*)lara->camera;
            printf("HOST CAMERA eye=%.1f,%.1f,%.1f target=%.1f,%.1f,%.1f fov=%.1f aspect=%.3f\n",
                   cam->eye.pos.x, cam->eye.pos.y, cam->eye.pos.z, cam->target.pos.x, cam->target.pos.y, cam->target.pos.z, cam->fov, cam->aspect);
        }
    }
#endif
    /* room first: the launcher keeps only the first 60 characters of the last line */
    printf("OL room=%d fps10=%u upd=%u rnd=%u gpu=%u pres=%u tris=%u pos=%d,%d,%d hp=%d frames=%u fence=%u binds=%u dips=%u batches=%u files=%u/%u/%u snd=%u/%u/%u\n",
           room, g_fps10, n ? g_update_us / n : 0u, n ? g_render_us / n : 0u, gu, pu,
           (unsigned)Core::stats.tris, px, py, pz, hp, g_frames, nf, binds,
           (unsigned)Core::stats.dips, GAPI::dipBatches, om, os, miss,
           n ? g_snd_us / n : 0u, g_snd_chunks, g_snd_on > 0 ? ol_pcm_underruns() : 0u);
#ifndef OL_SOFT
    (void)st;
#endif
    g_sample_frames = g_frames;
    g_sample_time = now;
    g_update_us = g_render_us = 0;
    g_snd_us = g_snd_chunks = 0;
    GAPI::dipBatches = 0;
    GAPI::dipVerts = 0;
}

#ifndef OL_SOFT
/* ---- A2.7: measurement report (RasterIX blobs), PROGDIR:data/olstat.txt at the end of the run -----
   Every rendered frame is split into (microseconds, global timer):
     upd     Game::update
     rnd     Game::render, made of
       beg     beginFrame (zz_rix_begin_frame)
       dip     GAPI::DIP: the engine's draw calls, made of
         batch   zzglBegin .. zzglEnd: flush (state + binds) + zend (zzglEnd: transform kept, clip,
                 descriptors) + vert (DIP's per-vertex lighting and the zzglColor/TexCoord/Vertex calls)
         setup   dip - batch: matrices, light positions, per call
       endf    endFrame (zz_rix_end_frame): swap + present (RIXOVERLAP=1: fence wait of frame N-1 +
               RGB565 -> P96 copy) + gpu (fence wait, RIXOVERLAP=0 / RIXSYNC=1)
       eng     rnd - beg - dip - endf: the engine's own work in render (rooms and portals, entities,
               animation, sorting, state)
     snd     the sound mixer of the tick
     mem     memcpy + memset + memmove of our code (release byte loops), spread over the above
   Totals for the run, by frame period (one, two, three or more VBlanks of 16.81 ms) and by Lara's
   room. Counts: binds, triangles in/out, vertices, batches, DIP calls. */
struct OLAcc {
    unsigned long long frames, wall, upd, rnd, beg, dip, batch, flush, bind, zend, endf, swap, pres, pwait, pcopy, gpu, upl, snd, mem, membytes,
                       binds, tin, tout, verts, begins, dips, culled, clipped, sgpu;
};
#define OL_MAX_ROOMS 512
static OLAcc g_acc_all, g_acc_vb[3], g_acc_room[OL_MAX_ROOMS];
static unsigned g_hist[26];          /* frame period, 2 ms bins, last = 50 ms and more */
static unsigned g_prof_seq;
static unsigned long long g_mem_t0[3], g_mem_b0[3];
extern "C" {
extern unsigned ol_mem_calls[3];
extern unsigned long long ol_mem_bytes[3], ol_mem_ticks[3];
u32 rix_ticks_to_us(u32 dt);
u32 rix_timer_hz(void);
void zz_rix_set_sync(int on);
void zz_rix_set_preclear(int on);
extern int rix_tex_wnorm;   /* librixgpu/rixdesc.c (1.2.3) */
extern int rix_desc_delta;  /* librixgpu/rixdesc.c (1.5) */
u32  zz_rix_preclear_frames(void);
u32  zz_rix_preclear_us(void);
void zz_rix_perf_report(char *out, int cap);
int  ol_stat_write(const char *name, const char *text, unsigned len);
#include "zzq2rix_olprof.h"
}

static void acc_add(OLAcc &a, const OLAcc &f) {
    unsigned long long *d = (unsigned long long*)&a;
    const unsigned long long *s = (const unsigned long long*)&f;
    for (unsigned i = 0; i < sizeof(OLAcc) / sizeof(unsigned long long); i++) d[i] += s[i];
}

/* after a rendered frame and the sound of its tick */
static void prof_frame(unsigned upd_us, unsigned rnd_us, unsigned snd_us, int room) {
    const zz_ol_frame *fr = zz_rix_last_frame();
    OLAcc f;
    memset(&f, 0, sizeof f);
    unsigned long long mt = 0, mb = 0;
    for (int k = 0; k < 3; k++) {
        mt += ol_mem_ticks[k] - g_mem_t0[k]; mb += ol_mem_bytes[k] - g_mem_b0[k];
        g_mem_t0[k] = ol_mem_ticks[k]; g_mem_b0[k] = ol_mem_bytes[k];
    }
    unsigned beg = rix_ticks_to_us(GAPI::beginTicks), dip = rix_ticks_to_us(GAPI::dipTicks), endf = rix_ticks_to_us(GAPI::endTicks);
    unsigned dips = GAPI::dipCalls;
    GAPI::dipLitTotal += GAPI::dipLit; GAPI::dipLit = 0;
    GAPI::dipCulledTotal += GAPI::dipCulled; GAPI::dipCulled = 0;
    GAPI::dipFarTotal += GAPI::dipFar; GAPI::dipFar = 0;   /* A2.11 */
    GAPI::dipScisTotal += GAPI::dipScis; GAPI::dipScis = 0;
    GAPI::beginTicks = GAPI::dipTicks = GAPI::endTicks = GAPI::dipCalls = 0;
    if (fr->seq == g_prof_seq) return;   /* nothing was drawn: no frame closed by the backend */
    g_prof_seq = fr->seq;
    f.frames = 1; f.wall = fr->wall_us; f.upd = upd_us; f.rnd = rnd_us; f.beg = beg; f.dip = dip; f.batch = fr->batch_us;
    f.flush = fr->flush_us; f.bind = fr->bind_us; f.zend = fr->end_us; f.endf = endf; f.swap = fr->swap_us; f.pres = fr->present_us;
    f.pwait = fr->pwait_us; f.pcopy = fr->pcopy_us; f.gpu = fr->gpu_us; f.upl = fr->upload_us; f.snd = snd_us;
    f.mem = mt < 0xFFFFFFFFull ? rix_ticks_to_us((u32)mt) : 0; f.membytes = mb; f.binds = fr->binds; f.tin = fr->tris_in; f.tout = fr->tris_out; f.verts = fr->verts;
    f.begins = fr->begins; f.dips = dips; f.culled = fr->culled; f.clipped = fr->clipped; f.sgpu = fr->sync_gpu_us;
    acc_add(g_acc_all, f);
    if (fr->wall_us) {
        int vb = fr->wall_us <= 16810u ? 0 : (fr->wall_us <= 33620u ? 1 : 2);
        acc_add(g_acc_vb[vb], f);
        unsigned h = fr->wall_us / 2000u;
        g_hist[h > 25u ? 25u : h]++;
    }
    if (room >= 0 && room < OL_MAX_ROOMS) acc_add(g_acc_room[room], f);
}

/* the report text */
static char g_rep[49152];
static unsigned g_rep_n;
static void rs(const char *s) { while (*s && g_rep_n < sizeof(g_rep) - 1) g_rep[g_rep_n++] = *s++; g_rep[g_rep_n] = 0; }
static void ru(unsigned long long v, int width = 0) {
    char t[24]; int k = 0;
    do { t[k++] = char('0' + v % 10); v /= 10; } while (v && k < 23);
    for (int i = k; i < width; i++) rs(" ");
    char o[24]; for (int i = 0; i < k; i++) o[i] = t[k - 1 - i]; o[k] = 0; rs(o);
}
/* microseconds as milliseconds with one decimal, right-aligned */
static void rms(unsigned long long us, int width) {
    unsigned long long d = (us + 50) / 100;
    char t[32]; int k = 0;
    t[k++] = char('0' + d % 10); t[k++] = '.'; d /= 10;
    do { t[k++] = char('0' + d % 10); d /= 10; } while (d && k < 30);
    for (int i = k; i < width; i++) rs(" ");
    char o[32]; for (int i = 0; i < k; i++) o[i] = t[k - 1 - i]; o[k] = 0; rs(o);
}
static unsigned long long sub0(unsigned long long a, unsigned long long b) { return a > b ? a - b : 0; }

static void rep_split(const char *title, const OLAcc &a) {
    unsigned long long n = a.frames ? a.frames : 1;
    rs(title); rs(": "); ru(a.frames); rs(" frames");
    if (a.frames) { rs(", period "); rms(a.wall / n, 0); rs(" ms"); }
    rs("\n");
    if (!a.frames) return;
    unsigned long long eng = sub0(a.rnd, a.beg + a.dip + a.endf), setup = sub0(a.dip, a.batch), vert = sub0(a.batch, a.flush + a.zend);
    rs("  ms per frame   upd "); rms(a.upd / n, 0); rs("   rnd "); rms(a.rnd / n, 0); rs("   snd "); rms(a.snd / n, 0); rs("   (mem "); rms(a.mem / n, 0); rs(" inside)\n");
    rs("  rnd = eng "); rms(eng / n, 0); rs(" + beg "); rms(a.beg / n, 0); rs(" + dip "); rms(a.dip / n, 0); rs(" + endf "); rms(a.endf / n, 0); rs("\n");
    rs("  dip = setup "); rms(setup / n, 0); rs(" + batches "); rms(a.batch / n, 0); rs(" (flush "); rms(a.flush / n, 0); rs(" of which binds "); rms(a.bind / n, 0);
    rs(", zzglEnd "); rms(a.zend / n, 0); rs(", vertices "); rms(vert / n, 0); rs(")\n");
    rs("  endf = swap "); rms(a.swap / n, 0); rs(" + present "); rms(a.pres / n, 0); rs(" (fence wait N-1 "); rms(a.pwait / n, 0); rs(", P96 copy "); rms(a.pcopy / n, 0);
    rs(") + gpu "); rms(a.gpu / n, 0); rs("   uploads "); rms(a.upl / n, 0); rs("\n");
    rs("  per frame      dips "); ru(a.dips / n); rs("  batches "); ru(a.begins / n); rs("  binds "); ru(a.binds / n); rs("  verts "); ru(a.verts / n);
    rs("  tris in "); ru(a.tin / n); rs(" out "); ru(a.tout / n); rs(" culled "); ru(a.culled / n); rs(" clipped "); ru(a.clipped / n);
    rs("  mem KiB "); ru(a.membytes / n / 1024); rs("\n");
    if (a.sgpu) { rs("  RIXSYNC=1: GPU busy "); rms(a.sgpu / n, 0); rs(" ms per frame\n"); }
}

/* ---- A2.9: room tour (TOUR=n) -----------------------------------------------------------------------
   Measures a whole level without playing it: the camera (the engine's own spectator camera) is put in
   every room of the level in turn, at eye height above the walkable sector nearest the room's centre,
   and looks in 8 directions (45 degrees apart, level), n frames each. Lara stays where she is (her
   health is kept full so that the enemies near her cannot end the run). The report goes to
   oltour_<level>.txt (one file per level) with every room, the frame time distribution and the 12
   heaviest views; the run ends by itself after the last room. Keys: only QUITKEY (F10). */
enum { TOUR_VIEWS = 8, TOUR_WORST = 12 };
struct TourWorst { unsigned wall; int room, view; unsigned binds, tris, dips; };
static TourWorst g_tw[TOUR_WORST];
static int g_tour_room = -1, g_tour_view, g_tour_frame, g_tour_done;
static vec3 g_tour_pos;

static vec3 tour_pos(int r) {
    TR::Room &room = Game::level->level.rooms[r];
    vec3 c = room.getCenter();
    int best = -1; float bd = 1e30f;
    for (int sx = 0; sx < room.xSectors; sx++)
        for (int sz = 0; sz < room.zSectors; sz++) {
            const TR::Room::Sector &s = room.sectors[sx * room.zSectors + sz];
            if (s.floor == -127 || s.ceiling >= s.floor) continue;   /* wall or no space */
            float x = room.info.x + sx * 1024.0f + 512.0f, z = room.info.z + sz * 1024.0f + 512.0f;
            float d = (x - c.x) * (x - c.x) + (z - c.z) * (z - c.z);
            if (d < bd) { bd = d; best = sx * room.zSectors + sz; }
        }
    if (best < 0) return c;
    const TR::Room::Sector &s = room.sectors[best];
    float x = room.info.x + (best / room.zSectors) * 1024.0f + 512.0f, z = room.info.z + (best % room.zSectors) * 1024.0f + 512.0f;
    float floorY = s.floor * 256.0f, ceilY = s.ceiling * 256.0f;
    float y = floorY - 768.0f;                     /* about Lara's eye height */
    if (y < ceilY + 128.0f) y = (floorY + ceilY) * 0.5f;
    return vec3(x, y, z);
}

static Camera *tour_camera() { return Game::level ? (Camera*)Game::level->camera : NULL; }
#ifdef ZZQ2RIX_HOST
#include <valgrind/callgrind.h>
/* bench only: OL_CG_ROOM=n, the room tour measured under callgrind for the views of room n only */
static void tour_cg() {
    static int on = 0;
    const char *e = getenv("OL_CG_ROOM");
    if (!e) return;
    int want = (g_tour_room == atoi(e)) && !g_tour_done;
    if (want && !on) { CALLGRIND_START_INSTRUMENTATION; on = 1; }
    if (!want && on) { CALLGRIND_STOP_INSTRUMENTATION; on = 0; }
}
#endif

/* before Game::update: the camera of the current view */
static void tour_before_update() {
    if (!g_tour_fpv || g_tour_done || !Game::level) return;
    if (g_tour_room < 0) {
        g_tour_room = 0; g_tour_view = 0; g_tour_frame = 0;
#ifdef ZZQ2RIX_HOST
        { const char *e = getenv("OL_TOUR_FROM"); if (e && atoi(e) > 0 && atoi(e) < Game::level->level.roomsCount) g_tour_room = atoi(e); }   /* bench only */
#endif
        g_tour_pos = tour_pos(g_tour_room);
        printf("OL TOUR %d rooms, %d views per room, %d frames per view\n", Game::level->level.roomsCount, TOUR_VIEWS, g_tour_fpv);
    }
#ifdef ZZQ2RIX_HOST
    tour_cg();
#endif
    if (Game::level->players[0]) ((Lara*)Game::level->players[0])->health = LARA_MAX_HEALTH;
    Camera *cam = tour_camera();
    if (!cam) return;
    cam->spectator = true;
    cam->specPos = cam->specPosSmooth = g_tour_pos;
    cam->specRot = cam->specRotSmooth = vec3(0.0f, g_tour_view * (2.0f * PI / TOUR_VIEWS), PI);
    cam->specRoom = (int16)g_tour_room;
}

/* after Game::update: the room is the tour's (overlapping alternate rooms) */
static void tour_after_update() {
    if (!g_tour_fpv || g_tour_done || g_tour_room < 0) return;
    Camera *cam = tour_camera();
    if (cam) cam->specRoom = (int16)g_tour_room;
}

/* after a rendered frame: worst views, then the next frame / view / room */
static void tour_frame_done() {
    if (!g_tour_fpv || g_tour_done || g_tour_room < 0) return;
    const zz_ol_frame *fr = zz_rix_last_frame();
    if (g_tour_frame > 0 && fr->wall_us) {        /* the first frame of a view also carries the jump */
        int k = TOUR_WORST - 1;
        if (fr->wall_us > g_tw[k].wall) {
            while (k > 0 && fr->wall_us > g_tw[k - 1].wall) { g_tw[k] = g_tw[k - 1]; k--; }
            g_tw[k].wall = fr->wall_us; g_tw[k].room = g_tour_room; g_tw[k].view = g_tour_view;
            g_tw[k].binds = fr->binds; g_tw[k].tris = fr->tris_in; g_tw[k].dips = 0;
        }
    }
    if (++g_tour_frame < g_tour_fpv) return;
    g_tour_frame = 0;
    if (++g_tour_view < TOUR_VIEWS) return;
    g_tour_view = 0;
    if (++g_tour_room >= Game::level->level.roomsCount) {
        g_tour_done = 1;
        printf("OL TOUR done\n");
        Core::quit();
        return;
    }
    g_tour_pos = tour_pos(g_tour_room);
}

static void prof_report() {
    if (!g_cfg_stats && !g_tour_fpv) { printf("OL STAT off (STATS=1 writes olstat_<level>.txt)\n"); return; }
    g_rep_n = 0; g_rep[0] = 0;
    rs(ol_blob_ident); rs(" (ZZOpenLara 1.0, RasterIX) measurement report\n");
    if (g_tour_fpv) { rs("ROOM TOUR: every room, "); ru(TOUR_VIEWS); rs(" views, "); ru(g_tour_fpv); rs(" frames per view"); rs(g_tour_done ? " (complete)\n" : " (STOPPED BEFORE THE END)\n"); }
    rs("level "); rs(g_cfg.level); rs("  screen "); ru(OL_RIX_W); rs("x"); ru(OL_RIX_H);
    rs("  RIXSYNC="); ru(g_cfg_sync); rs(" RIXPROF="); ru(g_cfg_prof); rs(" RIXSORT="); ru(GAPI::sortOn); rs(" ZNEAR="); ru(GAPI::zNear); rs(" DRAWDIST="); ru(GAPI::drawDist); rs(" RIXOVERLAP="); ru(g_cfg.overlap); rs(" RIXDEFER="); ru(g_cfg.defer); rs(" RIXPRECLEAR="); ru(g_cfg_preclear); rs(" RIXZTRICK="); ru(GAPI::zTrick); rs(" RIXWNORM="); ru(g_cfg_wnorm); rs(" RIXDESC="); ru(g_cfg_desc); rs(" RIXLITCACHE="); ru(GAPI::litCache);
    rs(" sound "); rs(g_snd_on > 0 ? "on" : "off"); rs("\n");
    {   unsigned mt, mb, ml, mlb, mms, mf;   /* A2.19: tracks of PROGDIR:data/ol_music.pak */
        ol_music_stats(&mt, &mb, &ml, &mlb, &mms, &mf);
        {   unsigned lo, lb, lm, lms;   /* A2.20: files read whole from PROGDIR:data/ (levels, cutscenes, pictures) */
            ol_fs_loose_stats(&lo, &lb, &lm, &lms);
            {   unsigned so, ss, sb, sw;   /* A2.21.2: videos streamed by 32 KiB blocks, 1 MB ahead */
                ol_fs_stream_stats(&so, &ss, &sb, &sw);
                rs("videos streamed: "); ru(so); rs(" opened, blocks read "); ru(sb); rs(" in the background + "); ru(ss);
                rs(" waited for at once, "); ru(sw); rs(" times the player waited for a background block\n");
                {   unsigned er, eb, ebb, ebf, es, rq, rf, rus;   /* A2.22.2: all 0 but "dropped" (a level or music read cancels a block in flight) */
                    ol_fs_stream_errors(&er, &eb, &ebb, &ebf, &es);
                    ol_fs_read_at_stats(&rq, &rf, &rus);
                    rs("video read errors: retried "); ru(er); rs(", still bad "); ru(eb); rs(", short background blocks "); ru(ebb);
                    rs(", background requests dropped or failed "); ru(ebf); rs(", short reads given to the player "); ru(es);
                    rs(" | file requests "); ru(rq); rs(", failed "); ru(rf); rs("\n"); } }
            rs("files of PROGDIR:data/ read whole: "); ru(lo); rs(" ("); ru(lb); rs(" bytes, "); ru(lms); rs(" ms), not found "); ru(lm); rs("\n"); }
        rs("music: "); ru(mt); rs(" tracks in ol_music.pak, "); ru(mb); rs(" bytes in memory for this level, ");
        ru(ml); rs(" tracks read in all ("); ru(mlb); rs(" bytes, "); ru(mms); rs(" ms), "); ru(mf); rs(" failures\n"); }
    rs("times in ms per frame (average). eng = engine work in Game::render outside the draw calls;\n"
       "dip = draw calls (setup + batches); endf = end of frame (swap, presentation, GPU waits).\n\n");
    rep_split("all frames", g_acc_all);
    rs("\n");
    rep_split("frames of 1 VBlank (<= 16.8 ms)", g_acc_vb[0]);
    rep_split("frames of 2 VBlanks (16.8 - 33.6 ms)", g_acc_vb[1]);
    rep_split("frames of 3 VBlanks or more (> 33.6 ms)", g_acc_vb[2]);
    rs("\nframe period histogram (2 ms bins):\n");
    for (int i = 0; i < 26; i++) {
        if (!g_hist[i]) continue;
        rs("  "); ru(i * 2, 2); rs(i == 25 ? "+    ms " : "-"); if (i < 25) { ru(i * 2 + 2, 2); rs(" ms "); }
        ru(g_hist[i], 6); rs("\n");
    }
    if (g_tour_fpv) {
        rs("\nheaviest views (frame period, ms; room, view = direction 0..7 by 45 degrees):\n");
        for (int k = 0; k < TOUR_WORST && g_tw[k].wall; k++) {
            rs("  "); rms(g_tw[k].wall, 6); rs("  room "); ru(g_tw[k].room, 3); rs(" view "); ru(g_tw[k].view);
            rs("  binds "); ru(g_tw[k].binds, 3); rs("  tris "); ru(g_tw[k].tris, 5); rs("\n");
        }
    }
    /* rooms: the 16 where Lara spent the most frames (tour: every room, in order) */
    rs(g_tour_fpv ? "\nby room (camera in the room, 8 directions), ms per frame:\n" : "\nby Lara's room (the 16 with the most frames), ms per frame:\n");
    rs("  room frames period   rnd   eng setup  vert  zend flush  endf   mem | dips batch binds verts  tris\n");
    static unsigned char used[OL_MAX_ROOMS];
    memset(used, 0, sizeof used);
    for (int k = 0; k < (g_tour_fpv ? OL_MAX_ROOMS : 16); k++) {
        int best = -1;
        if (g_tour_fpv) { if (g_acc_room[k].frames) best = k; else continue; }
        else for (int r = 0; r < OL_MAX_ROOMS; r++)
            if (!used[r] && g_acc_room[r].frames && (best < 0 || g_acc_room[r].frames > g_acc_room[best].frames)) best = r;
        if (best < 0) break;
        used[best] = 1;
        const OLAcc &a = g_acc_room[best];
        unsigned long long n = a.frames;
        ru(best, 6); ru(a.frames, 7); rms(a.wall / n, 7); rms(a.rnd / n, 6); rms(sub0(a.rnd, a.beg + a.dip + a.endf) / n, 6);
        rms(sub0(a.dip, a.batch) / n, 6); rms(sub0(a.batch, a.flush + a.zend) / n, 6); rms(a.zend / n, 6); rms(a.flush / n, 6);
        rms(a.endf / n, 6); rms(a.mem / n, 6); rs(" |"); ru(a.dips / n, 5); ru(a.begins / n, 6); ru(a.binds / n, 6); ru(a.verts / n, 6); ru(a.tin / n, 6); rs("\n");
    }
    if (g_acc_all.verts) { rs("\nvertices lit once per draw call (A2.9): "); ru(GAPI::dipLitTotal / (g_acc_all.frames ? g_acc_all.frames : 1)); rs(" per frame for "); ru(g_acc_all.verts / g_acc_all.frames); rs(" sent ("); ru(GAPI::litCacheHits / (g_acc_all.frames ? g_acc_all.frames : 1)); rs(" per frame taken lit from the cache, RIXLITCACHE="); ru(GAPI::litCache); rs("); triangles dropped before lighting (facing away) "); ru(GAPI::dipCulledTotal / (g_acc_all.frames ? g_acc_all.frames : 1)); rs(" per frame; beyond DRAWDIST="); ru(GAPI::drawDist); rs(" "); ru(GAPI::dipFarTotal / (g_acc_all.frames ? g_acc_all.frames : 1)); rs(" per frame; outside the scissor (SCISCULL="); ru(GAPI::scisCull); rs(") "); ru(GAPI::dipScisTotal / (g_acc_all.frames ? g_acc_all.frames : 1)); rs(" per frame\n"); }
    /* A2.13: the clears */
    if (zzgl_clr_n[0] && g_acc_all.frames) {
        rs("\nclears measured alone (RIXSYNC=1, full screen, each in its own waited chunk), GPU us per frame: colour ");
        ru(zzgl_clr_us[0] / g_acc_all.frames); rs(" (");  ru(zzgl_clr_us[0] / zzgl_clr_n[0]); rs(" each), depth ");
        ru(zzgl_clr_us[1] / g_acc_all.frames); rs(" ("); ru(zzgl_clr_us[1] / zzgl_clr_n[1]); rs(" each); clears per frame x100 ");
        ru(zzgl_clr_n[0] * 100ull / g_acc_all.frames); rs("\n");
    }
    if (GAPI::zTrick) { rs("\nRIXZTRICK=1: depth clears skipped "); ru(GAPI::zTrickSkipped); rs(", done by the GPU "); ru(GAPI::zTrickCleared); rs("\n"); }
    if (g_cfg_preclear && g_acc_all.frames) {
        unsigned pf = zz_rix_preclear_frames();
        rs("\nRIXPRECLEAR=1: buffers filled with black by Core1 "); ru(pf); rs(", colour clears skipped "); ru(zzgl_preclear_hits);
        rs(" ("); ru(zzgl_preclear_hits * 100ull / g_acc_all.frames); rs(" % of the frames); fill time us each ");
        ru(pf ? zz_rix_preclear_us() / pf : 0u); rs(" (inside endf / present)\n");
    }
    rs("\nsorted opaque pass (RIXSORT="); ru(GAPI::sortOn); rs("): flushes "); ru(GAPI::sortFlushes); rs(", draws sent from the queue "); ru(GAPI::sortItemsSent);
    if (g_acc_all.frames) { rs(" ("); ru(GAPI::sortItemsSent / g_acc_all.frames); rs(" per frame)"); }
    rs("\n");
    rs("\nmemcpy / memset / memmove of our code (release byte loops), whole run:\n");
    static const char *const mn[3] = { "  memcpy ", "  memset ", "  memmove" };
    for (int k = 0; k < 3; k++) {
        unsigned long long hz = rix_timer_hz() ? rix_timer_hz() : 1u, us = ol_mem_ticks[k] * 1000ull / (hz / 1000ull ? hz / 1000ull : 1ull);
        rs(mn[k]); rs("  calls "); ru(ol_mem_calls[k]); rs("  KiB "); ru(ol_mem_bytes[k] / 1024u); rs("  ms "); ru(us / 1000u);
        if (ol_mem_bytes[k]) { rs("  ns per byte "); ru(us * 1000ull / ol_mem_bytes[k]); }
        rs("\n");
    }
    rs("\nbackend report (ZZQuake2RIX 0.9.5 format; categories = OpenLara's):\n");
    static char brep[16384];
    zz_rix_perf_report(brep, (int)sizeof brep);
    rs(brep);
    if (g_rep_n >= sizeof(g_rep) - 1) rs("\n(report truncated)\n");
    char fname[64];
    {   /* oltour_<level without extension>.txt; A2.11: olstat_<level>.txt (one per level, not overwritten
           by the next level's run) */
        int k = 0; const char *l = g_cfg.level;
        const char *pre = g_tour_fpv ? "oltour_" : "olstat_";
        while (*pre) fname[k++] = *pre++;
        while (*l && *l != '.' && k < 52) fname[k++] = *l++;
        fname[k++] = '.'; fname[k++] = 't'; fname[k++] = 'x'; fname[k++] = 't'; fname[k] = 0;
    }
    int ok = ol_stat_write(fname, g_rep, g_rep_n);
    printf("OL STAT %s %u bytes %s\n", fname, g_rep_n, ok ? "written" : "NOT WRITTEN");
}
#endif

/* ---- entry points (Core1/platform.c) ------------------------------------------------------- */

extern "C" void ol_init(const void *data, unsigned size, const char *name) {
    (void)name;
    if (!ol_fs_init(data, size)) ol_fail("E202 PACK", (int)size, 0);
    ol_diag(0x01);

    cfg_read();
    {
        unsigned fsz = 0;
        /* A2.20: the level may be in the container (a file given to the launcher) or in PROGDIR:data/ */
        if (!ol_fs_find(g_cfg.level, &fsz) && !ol_fs_exists(g_cfg.level)) ol_fail("E209 LEVEL NOT FOUND", ol_fs_count(), 0);
        printf("%s: %s (%u bytes), %d files, defer=%d overlap=%d clearflush=%d texver=%d ring=%d pump=%d alphatex=%d filter=%d cull=%d\n",
               ol_blob_ident, g_cfg.level, fsz, ol_fs_count(), g_cfg.defer, g_cfg.overlap, g_cfg.clearflush, g_cfg.texver, g_cfg.ring, g_cfg.pump, g_cfg.alphatex, g_cfg.filter, g_cfg.cull);
    }

#ifdef OL_SOFT
    /* no GPU: the picture is drawn by Core1 at the size of the screen the launcher opened (640x480,
       or a real 320x240 screen with RES=320), and copied 1:1 by present.c */
    cacheDir[0] = saveDir[0] = contentDir[0] = 0;
    Core::x = Core::y = 0;
    {
        int sw = 0, sh = 0;
        ol_screen_size(&sw, &sh);
        if (!((sw == 320 && sh == 240) || (sw == 640 && sh == 480))) ol_fail("E311 SCREEN SIZE", sw, sh);
        Core::width  = sw;
        Core::height = sh;
    }
    printf("OL RENDER %s %dx%d\n",
  #ifdef OL_GAPI_SW
           "SW",
  #else
           "TGL",
  #endif
           Core::width, Core::height);
  #ifdef OL_GAPI_TGL
    GAPI::cullEnabled = g_cfg.cull;
    GAPI::tglFilter   = g_cfg.filter != 0;
  #endif
    ol_diag(0x02);
    ol_diag(0x03);
#else
    /* the GPU: the 0.9.5 backend, with the transport settings validated by ZZQuake2RIX */
    {
        char why[96];
        zz_rix_set_ring(g_cfg.ring);
    #if defined(OL_RIX320) || defined(OL_RIX800)
        {   /* the screen the launcher opened must be this blob's (A2.15: checked before any GPU access) */
            int sw = 0, sh = 0;
            ol_screen_size(&sw, &sh);
            if (sw != OL_RIX_W || sh != OL_RIX_H) ol_fail("E311 SCREEN SIZE", sw, sh);
        }
    #endif
        if (!zz_rix_open(OL_RIX_W, OL_RIX_H, why, sizeof why)) ol_fail("E300 GPU OPEN", 0, 0);
        zzgl_set_deferred(g_cfg.defer);
        zzgl_set_tex_versioning(g_cfg.texver);
        zzgl_set_clear_flush(g_cfg.clearflush);
        zz_rix_set_overlap(g_cfg.overlap);
        rix_frame_set_pump_interval((unsigned)(g_cfg.pump > 0 ? g_cfg.pump : 1));
        zz_rix_set_alpha_textures(g_cfg.alphatex);
        GAPI::cullEnabled = g_cfg.cull;
        zz_rix_set_sync(g_cfg_sync);          /* A2.7 */
        zzgl_set_clear_split(g_cfg_sync);     /* A2.13: SYNC mode measures the colour and depth clears apart */
        zz_rix_set_preclear(g_cfg_preclear);  /* A2.13 */
        rix_tex_wnorm = g_cfg_wnorm;          /* A2.16 */
        rix_desc_delta = g_cfg_desc;          /* A2.17 */
        zzgl_set_profile(g_cfg_prof);
        printf("OL PROF RIXSYNC=%d RIXPROF=%d RIXSORT=%d report olstat_<level>.txt\n", g_cfg_sync, g_cfg_prof, GAPI::sortOn);
    }
    ol_diag(0x02);

    cacheDir[0] = saveDir[0] = contentDir[0] = 0;
    Core::x = Core::y = 0;
    Core::width  = OL_RIX_W;
    Core::height = OL_RIX_H;

    ol_diag(0x03);
#endif
    g_snd_on = ol_pcm_open();        /* A2.19: before the level, which reads its music only with the sound on */
    ol_music_enable(g_snd_on > 0);
    Game::init(g_cfg.level);
    if (Core::isQuit) ol_fail("E240 GAME INIT", 0, 0);
    Core::settings.detail.setFilter(g_cfg.filter ? Core::Settings::MEDIUM : Core::Settings::LOW);
    if (Game::level) Game::level->atlasRooms->setFilterQuality(Core::settings.detail.filter);
    ol_diag(0x04);

    g_sample_time = ol_clock_us();
#ifdef ZZQ2RIX_HOST
    /* host bench only: textured / colored faces of Lara's meshes */
    if (Game::level) {
        TR::Level &L = Game::level->level;
        for (int m = 0; m < L.modelsCount; m++) {
            if (L.models[m].type != TR::Entity::LARA) continue;
            int tex = 0, col = 0;
            for (int k = 0; k < L.models[m].mCount; k++) {
                int idx = L.getMeshByID(L.models[m].mStart + k);
                if (idx < 0) continue;
                TR::Mesh &mesh = L.meshes[idx];
                for (int f = 0; f < mesh.fCount; f++) { if (mesh.faces[f].colored) col++; else tex++; }
            }
            printf("HOST LARA meshes=%d textured faces=%d colored faces=%d\n", L.models[m].mCount, tex, col);
        }
    }
#endif
    printf("OL SOUND %s, %d channels max, reverb %d\n", g_snd_on > 0 ? "on (mixer and PCM ring 22050 Hz)" : "off (launcher NOSOUND)",
           SND_CHANNELS_MAX, Core::settings.audio.reverb ? 1 : 0);
    printf("OL INIT OK level=%s\n", g_cfg.level);
}

#ifdef ZZQ2RIX_HOST
/* host bench only (OL_AUTOPILOT=n in the environment): hold forward; when Lara has not moved for 25
   frames, release and turn left or right for a random time, then forward again. Counts rooms visited. */
#include <stdlib.h>
static void autopilot() {
    static int on = -1, stuck, turning, turnKey, frame, jumpHold, wander;
    static vec3 last;
    static unsigned char seen[4096];
    static int nseen;
    if (on < 0) { const char *e = getenv("OL_AUTOPILOT"); on = e ? 1 : 0; if (e) srand(atoi(e)); }
    if (!on || !Game::level || !Game::level->players[0]) return;
    Lara *lara = (Lara*)Game::level->players[0];
    TR::Level &L = Game::level->level;
    int r = lara->getRoomIndex();
    if (r >= 0 && r < 4096 && !seen[r]) { seen[r] = 1; nseen++; printf("HOST ROOM ENTER %d (rooms visited %d) frame %d pos=%d,%d,%d\n", r, nseen, frame, int(lara->pos.x), int(lara->pos.y), int(lara->pos.z)); }
    frame++;
    if (jumpHold > 0 && --jumpHold == 0) Input::setDown(ikAlt, false);
    if (turning > 0) {
        if (--turning == 0) { Input::setDown(InputKey(turnKey), false); Input::setDown(ikUp, true); stuck = 0; wander = 60; }
        return;
    }
    if ((lara->pos - last).length() < 2.0f) stuck++; else stuck = 0;
    last = lara->pos;
    if (stuck > 25) {
        Input::setDown(ikUp, false);
        Input::setDown(ikLeft, false); Input::setDown(ikRight, false);
        if (rand() % 3 == 0) { Input::setDown(ikAlt, true); jumpHold = 5; Input::setDown(ikUp, true); stuck = 0; return; }
        turnKey = (rand() & 1) ? ikLeft : ikRight;
        Input::setDown(InputKey(turnKey), true);
        turning = 10 + rand() % 40;
        return;
    }
    if (wander > 0) { wander--; Input::setDown(ikUp, true); return; }
    /* steer to the nearest portal leading to a room not visited yet (else any portal) */
    float best = 1e30f; vec3 target; int found = 0;
    if (r >= 0 && r < L.roomsCount) {
        TR::Room &room = L.rooms[r];
        for (int pass = 0; pass < 2 && !found; pass++)
            for (int k = 0; k < room.portalsCount; k++) {
                TR::Room::Portal &pt = room.portals[k];
                if (pass == 0 && seen[pt.roomIndex]) continue;
                vec3 c(0.0f);
                for (int v = 0; v < 4; v++) c += vec3(float(pt.vertices[v].x), float(pt.vertices[v].y), float(pt.vertices[v].z));
                c *= 0.25f; c.x += room.info.x; c.z += room.info.z;
                float d = (c - lara->pos).length();
                if (d < best) { best = d; target = c; found = 1; }
            }
    }
    Input::setDown(ikLeft, false); Input::setDown(ikRight, false);
    if (!found) { Input::setDown(ikUp, true); return; }
    float want = atan2f(target.x - lara->pos.x, target.z - lara->pos.z);
    float diff = want - lara->angle.y;
    while (diff >  PI) diff -= 2.0f * PI;
    while (diff < -PI) diff += 2.0f * PI;
    if (diff > 0.25f) { Input::setDown(ikRight, true); Input::setDown(ikUp, fabsf(diff) < 1.0f); }
    else if (diff < -0.25f) { Input::setDown(ikLeft, true); Input::setDown(ikUp, fabsf(diff) < 1.0f); }
    else Input::setDown(ikUp, true);
}
#endif

extern "C" void ol_tick() {
    unsigned t0 = ol_clock_us(), t1 = 0, t2 = 0;
    int rendered = 0;
    pumpInput();
#ifdef ZZQ2RIX_HOST
    autopilot();
    {   /* A2.19 host bench: OL_TEST_TRACKS="frame:track[:b];..." asks the engine for a track as a trigger would
           (b = background, as the secret jingle and the voices of Natla and the skater) */
        static int frame;
        const char *e = getenv("OL_TEST_TRACKS");
        frame++;
        while (e && *e && Game::level) {
            int f = 0, trk = 0; char bg = 0;
            if (sscanf(e, "%d:%d:%c", &f, &trk, &bg) < 2) break;
            if (f == frame) { printf("HOST TRACK frame %d: playTrack(%d%s)\n", f, trk, bg == 'b' ? ", background" : ""); ((Level*)Game::level)->playTrack((uint8)trk, bg == 'b'); }
            e = strchr(e, ';'); if (e) e++;
        }
    }
#endif
    if (Core::isQuit) { ol_stop(); return; }
#ifndef OL_SOFT
    tour_before_update();
#endif
    if (Game::update()) {
#ifndef OL_SOFT
        tour_after_update();
#endif
        t1 = ol_clock_us();
        Game::render();
        t2 = ol_clock_us();
#ifdef OL_SOFT
        soft_present();   /* outside r: its time is p of the statistics */
#endif
        g_update_us += (unsigned)(t1 - t0);
        g_render_us += (unsigned)(t2 - t1);
        g_tot_update_us += (unsigned)(t1 - t0);
        g_tot_render_us += (unsigned)(t2 - t1);
        g_frames++;
        rendered = 1;
        if (!g_first_frame) { g_first_frame = 1; g_first_tick = t2; ol_diag(0x10); }
        publishStats();
    }
#if !defined(OL_SOFT) || defined(OL_GAPI_SW)
    unsigned ts = ol_clock_us();
#endif
    ol_fs_stream_pump();
    snd_pump();
#ifdef OL_GAPI_SW
    if (rendered) {   /* A2.12 */
        g_sw_pres_last = g_pres_last_us;
        g_sw_snd_last = ol_clock_us() - ts;
        sw_frame(t1 - t0, t2 - t1);
    }
#endif
#ifndef OL_SOFT
    if (rendered) {
        int room = -1;
        if (Game::level && Game::level->players[0]) room = ((Lara*)Game::level->players[0])->getRoomIndex();
        if (g_tour_fpv && g_tour_room >= 0) room = g_tour_room;
        prof_frame(t1 - t0, t2 - t1, ol_clock_us() - ts, room);
        tour_frame_done();
    }
#endif
    if (Core::isQuit) ol_stop();
}

/* A2.2: one line for the whole session, the last printf of the run (the launcher keeps 60 characters):
   n = frames, f = average fps, m = lowest 1-second sample, u / r = ARM ms per frame in Game::update /
   Game::render (lists built, or the whole picture in SW/TGL), g = residual GPU wait, p = P96 copy,
   s (A2.5) = sound mixing, all ms per frame. A2.5: "fps=" / "min=" shortened to "f=" / "m=" for s. */
static void session_summary() {
    unsigned now = ol_clock_us(), n = g_frames ? g_frames : 1u, el = (unsigned)(now - g_first_tick);
    unsigned nf, gu, gm, pu, binds, freep;
    unsigned fps10 = el ? (unsigned)((double)g_frames * 10000000.0 / (double)el) : 0u;
    unsigned u100 = (unsigned)(g_tot_update_us / n / 100ull), r100 = (unsigned)(g_tot_render_us / n / 100ull);
    zz_rix_stats(&nf, &gu, &gm, &pu, &binds, &freep);
    unsigned s100 = (unsigned)(g_snd_tot_us / n / 100ull);
    printf("OL END n=%u f=%u.%u m=%u u=%u.%u r=%u.%u g=%u.%u p=%u.%u s=%u.%u\n",
           g_frames, fps10 / 10, fps10 % 10, g_min_fps10 == 0xFFFFFFFFu ? 0u : g_min_fps10 / 10,
           u100 / 10, u100 % 10, r100 / 10, r100 % 10, gu / 1000, (gu / 100) % 10, pu / 1000, (pu / 100) % 10,
           s100 / 10, s100 % 10);
}

extern "C" void ol_shutdown() {
#ifdef ZZQ2RIX_HOST
#ifndef OL_SOFT
    printf("HOST LIT plain %llu lights %llu water %llu both %llu\n", GAPI::hostLit[0], GAPI::hostLit[1], GAPI::hostLit[2], GAPI::hostLit[3]);
#endif
    if (g_host_snd_frames) printf("HOST SND %.3f ms of Sound::fill per second of sound (%llu mixer frames)\n", g_host_snd_ns / 1e6 / (g_host_snd_frames / (double)(OL_MIX_FRAMES == 512 ? 22050 : 44100)), g_host_snd_frames);
#endif
#ifndef OL_SOFT
    prof_report();       /* A2.7: before OL END, which stays the last printf of the run */
#endif
#ifdef OL_GAPI_SW
    sw_report(g_cfg.level);   /* A2.12 */
#endif
    session_summary();
    ol_diag(0x20);
    Game::deinit();
#ifndef OL_SOFT
    zz_rix_close();
#endif
    ol_diag(0x21);
}
