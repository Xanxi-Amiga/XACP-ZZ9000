/*
 * zzvr_a6.c - ZZVirtuaRacingRIX A6a: the race picture without the SH-2s.
 *
 * During a race the two SH-2s only draw: the 68000 runs the game (physics, AI,
 * input, rules, Mega Drive sound) and sends a 2560-byte scene to the SH-2s each
 * tick (command 2, or 5 for the demo, in COMM0; the scene by DREQ to 0600C000).
 * The HLE (vrde/hle.c) rebuilds the same polygon list from that scene and
 * RasterIX draws it, so the SH-2s' own picture is computed and then thrown away.
 *
 * A6a patches the master's two race commands in SH-2 SDRAM (USA ROM, CRC32
 * 7896B62E; same method as zzvr_a5.c) so that they keep everything the 68000
 * depends on and skip the drawing:
 *   kept:    the DMA of the scene from the DREQ FIFO (06004448 / 06004480 and
 *            "ready for DREQ" in COMM1), the line-table effects (06004274), the
 *            2D blits into the frame buffer (06003CB4: the HUD's bitmaps, 32
 *            entries of the scene; the demo also its 6 text entries, 06004060),
 *            and "picture finished" (060043F0: COMM0 cleared, COMM1 bit 0).
 *   skipped: the frame buffer clears (the master's fill of 100 lines; the
 *            slave's 64 + 28 lines are never ordered), the slave's commands,
 *            the object matrices, the track sort, the geometry on both SH-2s,
 *            the HUD's polygons and the drawing (overlays A, B, C).
 * The slave is never commanded and sleeps in its idle loop (A5b's patch); the
 * master returns to its command loop (a COMM poll PicoDrive sleeps on).
 *
 * The 3D lines of the frame buffer (16..207, 160 words each at word address
 * 0x2000 + 0x100 * line) are cleared here instead, in the buffer the SH-2s draw
 * into, when a race scene has arrived (zzvr_a6_scene, from the DREQ hook: the
 * blits come after the DMA).  Index 0 is what the fills wrote: the Mega Drive
 * layer shows through it, as before.
 *
 * Patches (all four words are checked before writing, re-applied if the game
 * reloads its code):
 *   race, 06000CFC:  06000D06 "mov.w #3000,r1"      -> "bra 06000D10" (skips the fill)
 *                    06000D08 "mov.w #009f,r2"      -> nop (delay slot)
 *                    06000D1C "mov.l =0600441c,r0"  -> "bra 06000D44" (skips slave command 4 .. drawing)
 *                    06000D1E "jsr @r0"             -> nop
 *   demo, 06001924:  0600192E "mov.w #3000,r1"      -> "bra 06001938"
 *                    06001930 "mov.w #009f,r2"      -> nop
 *                    06001952 "mov.l =0600441c,r0"  -> "bra 06001988" (skips slave command 4 .. the demo's drawing)
 *                    06001954 "jsr @r0"             -> nop
 *
 * A6b: diagnostics only (the words at the two race patch addresses every frame, and the
 * master's commands to the slave while the 68000 asks for a race picture).
 *
 * Off with the launcher's flag noa6 (ZZVR_SH_HLE_CTRL bit 5).  ASCII only.
 */
#include <stdint.h>
#include <string.h>
#include "pico/pico_int.h"
#include "zzvr_shared.h"
#ifdef DRC_SH2
#include "cpu/sh2/compiler.h"
#endif

typedef uint32_t u32;
typedef uint16_t u16;

static volatile u32 *g_sh;
static int g_on;
static int g_race_patched, g_demo_patched;
static u32 g_scenes_seen, g_frames_since_scene, g_frame;

static u16 sdram_w(u32 a) { return ((u16 *)Pico32xMem->sdram)[(a & 0x3fffeu) >> 1]; }
static void sdram_patch(u32 a, u16 v)
{
    ((u16 *)Pico32xMem->sdram)[(a & 0x3fffeu) >> 1] = v;
#ifdef DRC_SH2
    sh2_drc_wcheck_ram(a, 2, &msh2);       /* drop the recompiled blocks over it */
#endif
}

extern int zzvr_rix_ok(void);   /* zzvr_rix.c */
void zzvr_a6_init(volatile u32 *sh, int hle_on)
{
    g_sh = sh;
    g_on = hle_on && !(sh[ZZVR_SH_HLE_CTRL] & 32u) && zzvr_rix_ok();   /* 1.0: only with RasterIX */
    g_race_patched = g_demo_patched = 0;
    g_scenes_seen = 0; g_frames_since_scene = 0; g_frame = 0;
    sh[ZZVR_SH_A6_STATE] = g_on ? 0x41360000u : 0u;   /* 'A6' in the high half: on */
    sh[ZZVR_SH_A6_APPLIED] = 0;
    sh[ZZVR_SH_A6_CLEARS] = 0;
    sh[ZZVR_SH_A6_TICK_SUM] = 0;
    sh[ZZVR_SH_A6_TICK_N] = 0;
    sh[ZZVR_SH_A6_TICK_3] = 0;
    sh[ZZVR_SH_A6_TICK_MAX] = 0;
    sh[ZZVR_SH_A6_SIG] = 0;
    sh[ZZVR_SH_A6_SLAVE_CMDS] = 0;
}

int zzvr_a6_on(void) { return g_on; }

/* A8g: RasterIX failed (zzvr_rix.c gpu_error): the SH-2s draw the race again. The
   original words go back where our patches still are; no more 3D lines cleared. */
static void sdram_restore(u32 a, u16 patched, u16 original)
{
    if (sdram_w(a) == patched) sdram_patch(a, original);
}
void zzvr_a6_off(void)
{
    if (!g_on || !Pico32xMem) { g_on = 0; return; }
    if (g_race_patched) {
        sdram_restore(0x06000D06, 0xA003u, 0x9124u); sdram_restore(0x06000D08, 0x0009u, 0x9224u);
        sdram_restore(0x06000D1C, 0xA012u, 0xD013u); sdram_restore(0x06000D1E, 0x0009u, 0x400Bu);
    }
    if (g_demo_patched) {
        sdram_restore(0x0600192E, 0xA003u, 0x9136u); sdram_restore(0x06001930, 0x0009u, 0x9236u);
        sdram_restore(0x06001952, 0xA019u, 0xD01Au); sdram_restore(0x06001954, 0x0009u, 0x400Bu);
    }
    g_race_patched = g_demo_patched = 0;
    g_on = 0;
    g_sh[ZZVR_SH_A6_STATE] |= 0x100u;      /* given back to the SH-2s */
}

/* once after every PicoFrame: the patches, and the tick lengths (frames between scenes) */
void zzvr_a6_frame(unsigned int scenes)
{
    if (!g_on || !Pico32xMem) return;
    g_frame++;
    if (scenes != g_scenes_seen) {
        if (g_scenes_seen != 0 && g_frames_since_scene <= 8u) {   /* consecutive ticks (hle.c LONGEST_TICK) */
            g_sh[ZZVR_SH_A6_TICK_SUM] += g_frames_since_scene;
            g_sh[ZZVR_SH_A6_TICK_N]++;
            if (g_frames_since_scene == 3u) g_sh[ZZVR_SH_A6_TICK_3]++;
            if (g_frames_since_scene > g_sh[ZZVR_SH_A6_TICK_MAX]) g_sh[ZZVR_SH_A6_TICK_MAX] = g_frames_since_scene;
        }
        g_scenes_seen = scenes;
        g_frames_since_scene = 0;
    }
    g_frames_since_scene++;
    g_sh[ZZVR_SH_A6_SIG] = ((u32)sdram_w(0x06000D06) << 16) | sdram_w(0x06000D1C);   /* A6b: what is there */

    /* race command 2 (06000CFC) */
    if (sdram_w(0x06000D06) == 0x9124u && sdram_w(0x06000D08) == 0x9224u && sdram_w(0x06000D0A) == 0xD015u &&
        sdram_w(0x06000D0C) == 0x400Bu && sdram_w(0x06000D0E) == 0xE364u && sdram_w(0x06000D10) == 0xD014u &&
        sdram_w(0x06000D1C) == 0xD013u && sdram_w(0x06000D1E) == 0x400Bu && sdram_w(0x06000D20) == 0xE104u &&
        sdram_w(0x06000D44) == 0xDE0Du && sdram_w(0x06000D4C) == 0xD00Du) {
        sdram_patch(0x06000D06, 0xA003u);  /* bra 06000D10 */
        sdram_patch(0x06000D08, 0x0009u);
        sdram_patch(0x06000D1C, 0xA012u);  /* bra 06000D44 */
        sdram_patch(0x06000D1E, 0x0009u);
        g_race_patched = 1;
        g_sh[ZZVR_SH_A6_STATE] |= 1u;
        g_sh[ZZVR_SH_A6_APPLIED]++;
    }
    /* demo command 5 (06001924) */
    if (sdram_w(0x0600192E) == 0x9136u && sdram_w(0x06001930) == 0x9236u && sdram_w(0x06001932) == 0xD01Eu &&
        sdram_w(0x06001938) == 0xD01Du && sdram_w(0x06001952) == 0xD01Au && sdram_w(0x06001954) == 0x400Bu &&
        sdram_w(0x06001956) == 0xE104u && sdram_w(0x06001988) == 0xDE14u && sdram_w(0x06001998) == 0xD014u) {
        sdram_patch(0x0600192E, 0xA003u);  /* bra 06001938 */
        sdram_patch(0x06001930, 0x0009u);
        sdram_patch(0x06001952, 0xA019u);  /* bra 06001988 */
        sdram_patch(0x06001954, 0x0009u);
        g_demo_patched = 1;
        g_sh[ZZVR_SH_A6_STATE] |= 2u;
        g_sh[ZZVR_SH_A6_APPLIED]++;
    }
}

/* A6b: the master commands the slave (a COMM4 write) while the 68000's command is a race
   picture: with the patches in effect this never happens */
void zzvr_a6_slave_command(int command)
{
    if (g_on && (command == 2 || command == 5)) g_sh[ZZVR_SH_A6_SLAVE_CMDS]++;
}

/* A race scene has just arrived in SH-2 SDRAM (the last DREQ word): clear the 3D lines
   of the frame buffer the SH-2s draw into, as their fills did. */
void zzvr_a6_scene(int command)
{
    u16 *dram;
    int line;
    if (!g_on || !Pico32xMem) return;
    if (command != 2 && command != 5) return;
    dram = Pico32xMem->dram[(Pico32x.vdp_regs[0x0a / 2] & P32XV_FS) ^ 1];
    for (line = 16; line < 208; line++)
        memset(&dram[0x2000 + 0x100 * line], 0, 160 * sizeof(u16));
    g_sh[ZZVR_SH_A6_CLEARS]++;
}
