/*
 * zzvr_hle.c - VirtuaRacingDeluxeEnhanced geometry reconstruction and
 * HUD tracking for ZZVirtuaRacingRIX 1.0.
 *
 * PicoDrive 32X hooks provide game commands, vertex lists and framebuffer
 * writes to the high-level emulation. The resulting polygon lists are sent
 * to the RasterIX renderer, with frame checks and diagnostic counters
 * available for development and troubleshooting.
 *
 * The current accelerated path bypasses the original SH-2 race drawing;
 * the emulated 32X renderer is available as a fallback.
 */
#include <stdint.h>
#include <string.h>

#include "pico/pico_int.h"
#include "zzvr_shared.h"
#include "vrde/hle.h"
#include "vrde/hud.h"
#include "vrde/sh2raster.h"
#include "vrde/hlecheck.h"
#include "vrde/zzvr_hle_diag.h"

typedef uint32_t u32;

/* A3: RasterIX (zzvr_rix.c) */
extern void zzvr_rix_init(volatile u32 *sh, int hle_on);
extern void zzvr_rix_picture(const hle_frame_t *f, int buffer);
extern void zzvr_rix_no_picture(void);
extern void zzvr_rix_reset(void);
/* A5b: busy-wait patches of the game's SH-2 code (zzvr_a5.c) */
extern void zzvr_a5_init(volatile u32 *sh, int hle_on);
extern void zzvr_a5_frame(void);
/* A6a: the race commands without the SH-2s' drawing (zzvr_a6.c) */
extern void zzvr_a6_init(volatile u32 *sh, int hle_on);
extern void zzvr_a6_frame(unsigned int scenes);
extern void zzvr_a6_scene(int command);
extern int zzvr_a6_on(void);
extern void zzvr_a6_slave_command(int command);
extern void zzvr_rix_set_fbref(int fbref);
/* A7a: an image every frame of a race (zzvr_rix.c) */
extern int zzvr_rix_direct(void);
extern void zzvr_rix_frame_direct(const hle_frame_t *f, int buffer);
extern void zzvr_rix_hold(void);
extern void zzvr_rix_set_haze(int on, float start, float end);
extern void zzvr_rix_set_sky(int mode);
#include "vrde/sky.h"
extern int hle_wheel_strobe;
extern u32 zzvr_pcm_fill_pairs(void);
extern int zzvr_rix_has_image(void);
/* A8a: the image governor.  Normally the pacing loop leaves about 1400 stereo pairs in the
   PCM ring at the start of a frame and PicoFrame adds about 370; when Core1 runs late the
   ring runs down.  Below this, the frame keeps the image on screen instead of drawing a
   new one (some 7 ms back): the images a second drop, never the sound. */
#define GOV_PAIRS 1300u
static int g_last_ibuilds = -1;

static volatile u32 *g_sh;
static int g_hle_on, g_check_on;
static int g_frame;
static int g_command;        /* the 68000's last command (COMM0 low byte) */

#if defined(__arm__)
static inline u32 cyc(void){ u32 v; __asm__ volatile("mrc p15,0,%0,c9,c13,0":"=r"(v)); return v; }
#define CYC_PER_US 666u
#else   /* host test bench: microseconds */
#include <time.h>
static inline u32 cyc(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (u32)(t.tv_sec*1000000u + t.tv_nsec/1000u); }
#define CYC_PER_US 1u
#endif

/* hle.c's two stderr messages */
void zzvr_hle_note(int what, int value)
{
    if (!g_sh) return;
    if (what == ZZVR_HLE_NOTE_VERTICES) { g_sh[ZZVR_SH_HLE_NOTES]++; g_sh[ZZVR_SH_HLE_NOTE_LAST] = 0x01000000u | ((u32)value & 0xFFFFu); }
    else                                 { g_sh[ZZVR_SH_HLE_NOTES]++; g_sh[ZZVR_SH_HLE_NOTE_LAST] = 0x02000000u | ((u32)value & 0xFFFFu); }
}

static void on_comm_write(int writer, unsigned int pc, unsigned int offset, unsigned int value, int size)
{
    (void)pc;
    if (writer == 0 && (offset & ~1u) == 0x20 && (size == 2 || (offset & 1)))
        g_command = (int)(size == 2 ? (value & 0xFFu) : value);
    if (writer == 1 && (offset & ~1u) == 0x24 && value != 0)
        zzvr_a6_slave_command(g_command);                  /* A6b: the slave commanded during a race picture */
    hle_on_comm_write(writer, offset, value, size);
}

/* The last DREQ word of a race scene builds the polygon list: timed */
static void on_dreq_write(int is_slave, unsigned int address, unsigned int value)
{
    u32 c0, us;
    int before = hle_scene_count();
    (void)is_slave; (void)value;
    c0 = cyc();
    hle_on_dreq_write(address);
    if (hle_scene_count() == before) return;          /* not a scene's last word */
    zzvr_a6_scene(g_command);                         /* A6a: the SH-2s' fills, done here */
    us = (cyc() - c0) / CYC_PER_US;
    g_sh[ZZVR_SH_HLE_BUILDS]++;
    g_sh[ZZVR_SH_HLE_BUILD_US_SUM] += us;
    if (us > g_sh[ZZVR_SH_HLE_BUILD_US_MAX]) g_sh[ZZVR_SH_HLE_BUILD_US_MAX] = us;
}

static void on_dram_write(int is_slave, unsigned int address, unsigned int value, int size)
{
    if (is_slave) return;
    hud_note_dram_write(is_slave, address, value, size);
}

void zzvr_hle_init(volatile u32 *sh)
{
    u32 ctrl;
    int i;
    g_sh = sh;
    ctrl = sh[ZZVR_SH_HLE_CTRL];
    for (i = ZZVR_SH_HLE_FIRST; i <= ZZVR_SH_HLE_LAST; i++)
        if (i != ZZVR_SH_HLE_CTRL) sh[i] = 0;
    for (i = ZZVR_SH_SH2_MS_US; i <= ZZVR_SH_SH2_SS_MAX; i++) sh[i] = 0;   /* A5a */
    sh[ZZVR_SH_HLE_CHK_LOWEST] = 10000u;              /* in 1/100 %: none yet */
    g_hle_on = !(ctrl & 1u);
    g_check_on = g_hle_on && !(ctrl & 2u);
    g_frame = 0;
    g_command = 0;
    hle_reset();
    if (g_hle_on) {
        vrde_comm_write_hook = on_comm_write;
        vrde_dreq_write_hook = on_dreq_write;
        zzvr_dram_write_hook = on_dram_write;
    } else {
        vrde_comm_write_hook = 0;
        vrde_dreq_write_hook = 0;
        zzvr_dram_write_hook = 0;
    }
    vrde_sh2_access_hook = 0;                         /* interpreter-only, not used */
    zzvr_rix_init(sh, g_hle_on);
    zzvr_a5_init(sh, g_hle_on);
    zzvr_a6_init(sh, g_hle_on);
    if (zzvr_a6_on()) g_check_on = 0;                 /* A6a: the SH-2s draw nothing to compare with */
    zzvr_rix_set_fbref(!zzvr_a6_on());                /* A6c: the frame buffer no longer says where the MD layer is in front */
    {   /* A8a: VRDE's far view (with its haze) and detailed opponents, as its front end
           sets them (apply_hle_settings); off with the check (the SH-2s draw neither) */
        u32 far = sh[ZZVR_SH_A8_FAR], tiles = sh[ZZVR_SH_A8_TILES], detail = sh[ZZVR_SH_A8_DETAIL];
        int far_on, haze_on, det_on, reach;
        if (far == 0u) far = 2u;
        if (tiles < 4u || tiles > 16u) tiles = 8u;
        far_on = (far == 2u || far == 3u) && !g_check_on;
        haze_on = far_on && far == 2u;
        det_on = (detail != 1u) && !g_check_on;
        reach = (int)tiles * 1024;
        if (reach < FAR_VIEW_NEAREST) reach = FAR_VIEW_NEAREST;
        if (reach > FAR_VIEW_FURTHEST) reach = FAR_VIEW_FURTHEST;
        hle_set_far_view(far_on);
        hle_set_far_view_distance(reach);
        hle_set_far_view_tiles((int)tiles);
        hle_set_detailed_opponents(det_on);
        {
            int hs = reach * 2 / 3, he = reach - 512;
            if (hs < 4096) hs = 4096;                    /* the original's own range stays clear */
            if (he > hs + 2400) he = hs + 2400;
            zzvr_rix_set_haze(haze_on, (float)hs, (float)he);
            sh[ZZVR_SH_A8_HAZE] = (u32)hs | ((u32)he << 16);
        }
        {   /* A8b: the sky (vrde/sky.c) and the wheels */
            u32 sm = sh[ZZVR_SH_A8_SKY_MODE];
            if (sm > 2u || g_check_on) sm = 1u;
            zzvr_rix_set_sky((int)sm);
            sky_set_blend(sm == 0u);
            hle_wheel_strobe = sh[ZZVR_SH_A8_WHEELS] != 1u;
            sh[ZZVR_SH_A8_SKY_FRAMES] = sh[ZZVR_SH_A8_SKY_OFF] = 0;
            sh[ZZVR_SH_A8_SKY_REBUILDS] = sh[ZZVR_SH_A8_SKY_BLENDROWS] = sh[ZZVR_SH_A8_SKY_PLANE] = 0;
        }
        sh[ZZVR_SH_A8_STATE] = 0x41380000u | (u32)(far_on ? (haze_on ? 1 : 2) : 0) | ((u32)det_on << 4) | (tiles << 8);
    }
    sh[ZZVR_SH_HLE_MAGIC] = g_hle_on ? (g_check_on ? 0x484C4543u /* 'HLEC' */ : 0x484C452Du /* 'HLE-' */) : 0x4F464621u /* 'OFF!' */;
}

/* After a savestate is loaded: forget the scenes (VRDE's load_state) */
void zzvr_hle_after_load(void)
{
    if (g_hle_on) { hle_reset(); zzvr_rix_reset(); }
}

static void check_picture(const hle_frame_t *f)
{
    static uint8_t image[SH2_SCREEN_WIDTH * SH2_SCREEN_HEIGHT];
    int i, compared, same;
    u32 c0 = cyc(), us, permyriad;

    memset(image, 0, sizeof(image));
    for (i = 0; i < f->count; i++)
        sh2_draw_polygon(image, &f->polygons[i], f->view_top, f->view_bottom);
    hlecheck_compare(f, image, &compared, &same);
    us = (cyc() - c0) / CYC_PER_US;
    g_sh[ZZVR_SH_HLE_CHK_US_SUM] += us;
    if (us > g_sh[ZZVR_SH_HLE_CHK_US_MAX]) g_sh[ZZVR_SH_HLE_CHK_US_MAX] = us;
    if (compared == 0) return;                        /* nothing drawn on either side */

    g_sh[ZZVR_SH_HLE_CHK_PICTURES]++;
    g_sh[ZZVR_SH_HLE_CHK_PIX] += (u32)compared;
    g_sh[ZZVR_SH_HLE_CHK_SAME] += (u32)same;
    permyriad = (u32)(((unsigned long long)same * 10000u) / (u32)compared);
    if (same == compared) g_sh[ZZVR_SH_HLE_CHK_IDENTICAL]++;
    else if (g_sh[ZZVR_SH_CHKLOG_N] < ZZVR_SH_CHKLOG_MAX) {   /* A3: the first ones not identical */
        int at = ZZVR_SH_CHKLOG_FIRST + 3 * (int)g_sh[ZZVR_SH_CHKLOG_N];
        g_sh[at] = (u32)g_frame;
        g_sh[at + 1] = ((u32)f->command & 0xFFu) | (((u32)f->view_top & 0xFFu) << 8) | (((u32)f->view_bottom & 0xFFu) << 16);
        g_sh[at + 2] = permyriad | ((u32)f->count << 16);
        g_sh[ZZVR_SH_CHKLOG_N]++;
    }
    if (permyriad < 9000u) g_sh[ZZVR_SH_HLE_CHK_BELOW90]++;
    if (permyriad < g_sh[ZZVR_SH_HLE_CHK_LOWEST]) {
        g_sh[ZZVR_SH_HLE_CHK_LOWEST] = permyriad;
        g_sh[ZZVR_SH_HLE_CHK_LOWEST_FRAME] = (u32)g_frame;
    }
}

/* A5a: the SH-2s' share of PicoFrame (pico/32x/32x.c run_sh2) */
extern unsigned int zzvr_sh2_cyc[2];

/* Once after every PicoFrame() */
/* A8c: the frame's work in two parts. zzvr_hle_frame_early, right after PicoFrame: the
   scene's bookkeeping and the interpolated list (Core1 alone, 3 to 4 ms), while RasterIX
   is still drawing the image sent at the end of the frame before; zzvr_rix_composite then
   finds it done instead of waiting for it (A8b waited 1.9 ms a frame on average).
   zzvr_hle_frame_end, after the publication: the list sent to RasterIX. */
static int g_early_done, g_early_governed, g_gov_run;
extern int zzvr_pace_waited(void);   /* zzvr_core1.c: the last pacing loop waited */
static const hle_frame_t *g_early_f, *g_early_fi;

void zzvr_hle_frame_early(void)
{
    const hle_frame_t *f;
    g_early_done = 1; g_early_f = NULL; g_early_fi = NULL; g_early_governed = 0;
    g_frame++;
    if (g_sh) {
        u32 ms = zzvr_sh2_cyc[0] / CYC_PER_US, ss = zzvr_sh2_cyc[1] / CYC_PER_US;
        zzvr_sh2_cyc[0] = 0; zzvr_sh2_cyc[1] = 0;
        if (g_frame > 1) {                      /* the first frame holds the start-up */
            g_sh[ZZVR_SH_SH2_MS_US] += ms; g_sh[ZZVR_SH_SH2_SS_US] += ss; g_sh[ZZVR_SH_SH2_FRAMES]++;
            if (ss > g_sh[ZZVR_SH_SH2_SS_MAX]) g_sh[ZZVR_SH_SH2_SS_MAX] = ss;
        }
    }
    if (!g_hle_on) return;
    zzvr_a5_frame();
    zzvr_a6_frame((unsigned int)hle_scene_count());
    f = hle_on_frame_end();
    g_early_f = f;
    g_sh[ZZVR_SH_HLE_SCENES] = (u32)hle_scene_count();
    hle_set_light_lists(zzvr_rix_direct());          /* A7b: no full list per scene, the interpolated ones only */
    if (zzvr_rix_direct() && g_sh[ZZVR_SH_A8_SKY_MODE] != 1u) {   /* A8b: the sky's scroll and cache */
        const sky_stats_t *ss;
        sky_frame_end(hle_scene_count());
        ss = sky_stats();
        g_sh[ZZVR_SH_A8_SKY_REBUILDS] = ss->rebuilds;
        g_sh[ZZVR_SH_A8_SKY_BLENDROWS] = ss->blend_rows_done;
        g_sh[ZZVR_SH_A8_SKY_PLANE] = ss->plane_w | (ss->plane_h << 16);
    }
    if (zzvr_rix_direct()) {
        /* A7a: an image at the end of every frame, the camera and the cars interpolated
           between the game's last two ticks (VRDE's hle_interpolated) */
        const hle_frame_t *on_screen = hle_shown();
        /* A8f: the governor holds the image only when Core1 itself is late (the pacing
           loop found the ring already low, no wait), and never twice in a row. A8e on the
           A4000: after a 51 ms stall of the 68k, frames waited for the 68k's PAN (deferred
           publication: the ring may then sink to 700 pairs before PicoFrame goes on), the
           ring stayed under GOV_PAIRS and the image was held for good. */
        if (on_screen && zzvr_rix_has_image() && g_gov_run < 1 && !zzvr_pace_waited()
            && zzvr_pcm_fill_pairs() < GOV_PAIRS) {
            g_sh[ZZVR_SH_A8_GOV]++;
            g_early_governed = 1;                         /* A8a: behind: no new image this frame */
            on_screen = NULL;
            g_gov_run++;
        } else if (on_screen) g_gov_run = 0;
        if (on_screen) {
            u32 c0 = cyc(), us, fpi2 = g_sh[ZZVR_SH_A7_FPI2];
            if (fpi2 < 2u || fpi2 > 8u) fpi2 = 2u;
            g_early_fi = hle_interpolated(0.0, on_screen, (double)fpi2 * 0.5);
            us = (cyc() - c0) / CYC_PER_US;
            g_sh[ZZVR_SH_A7_INTERP_US_SUM] += us;
            if (us > g_sh[ZZVR_SH_A7_INTERP_US_MAX]) g_sh[ZZVR_SH_A7_INTERP_US_MAX] = us;
        }
    }
}

void zzvr_hle_frame_end(void)
{
    const hle_frame_t *f;
    if (!g_early_done) zzvr_hle_frame_early();       /* the bench tools call this one only */
    g_early_done = 0;
    if (!g_hle_on) return;
    f = g_early_f;
    if (zzvr_rix_direct()) {
        const hle_frame_t *fi = g_early_fi;
        int governed = g_early_governed;
        if (governed) zzvr_rix_hold();
        if (fi && hle_interpolated_builds() == g_last_ibuilds) zzvr_rix_hold();   /* A7b: same list, same image */
        else if (fi) { g_last_ibuilds = hle_interpolated_builds(); zzvr_rix_frame_direct(fi, Pico32x.vdp_regs[0x0a / 2] & P32XV_FS); }
        else if (!governed) { g_last_ibuilds = -1; zzvr_rix_no_picture(); }
        if (!f) return;
    } else {
    if (!f) {
        if (!hle_shown()) zzvr_rix_no_picture();      /* no race picture on screen */
        return;
    }
    zzvr_rix_picture(f, Pico32x.vdp_regs[0x0a / 2] & P32XV_FS);   /* A3: drawn by RasterIX */
    }
    g_sh[ZZVR_SH_HLE_PICTURES]++;
    g_sh[ZZVR_SH_HLE_POLY_SUM] += (u32)f->count;
    if ((u32)f->count > g_sh[ZZVR_SH_HLE_POLY_MAX]) g_sh[ZZVR_SH_HLE_POLY_MAX] = (u32)f->count;
    g_sh[ZZVR_SH_HLE_CMD_LAST] = (u32)f->command;
    if (g_check_on) check_picture(f);
}
