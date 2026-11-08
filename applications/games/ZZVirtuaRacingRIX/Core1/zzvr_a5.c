/*
 * zzvr_a5.c - ZZVirtuaRacingRIX A5b: two patches of Virtua Racing Deluxe's SH-2
 * code (USA ROM, CRC32 7896B62E) that remove busy-wait loops the emulated SH-2s
 * run every frame, applied in SH-2 SDRAM once the game has put its code there
 * (checked after every frame: two compares; re-applied if the game reloads it).
 *
 * Found on the host test bench (SH-2 interpreter, instruction fetches counted
 * during the race demo, frames 1800-2400): 47 % of the slave's instructions and
 * 12 % of the master's are these two loops.  On the A4000 the SH-2 recompiler
 * is built without its loop detection (ZZPicoDrive32X v1.2 RC5: VR froze with
 * it), so the loops really run: the slave cost 2.9 ms and the master 2.7 ms of
 * each PicoFrame (A3b4m).
 *
 * P1 - the slave's command loop (06000592): it reads COMM4 (byte 20004024),
 *      and when there is no command waits 64 rounds of "nop; dt r7; bf" before
 *      reading again.  The wait is too long for PicoDrive's poll detection (two
 *      reads of the same register within 20 cycles), so the slave never sleeps.
 *      06000608 "mov #64,r7" becomes "bra 06000592" (its delay slot 0600060a is
 *      already a nop): the slave reads COMM4 back to back, PicoDrive sees the
 *      poll after a few reads and stops the slave until COMM4 is written.
 * P2 - the 32X VDP fill routine (060044F0, both SH-2s; it clears the 3D lines,
 *      192 fills of 160 words): after each fill it waits 125 rounds of
 *      "dt r7; bf" (the constant at 06004528) before reading the fill-busy bit.
 *      PicoDrive ends a fill after 3 + length SH-2 cycles and the routine reads
 *      the busy bit anyway: the constant becomes 1.
 *
 * Off with the launcher's flag noa5 (ZZVR_SH_HLE_CTRL bit 4).  ASCII only.
 */
#include <stdint.h>
#include "pico/pico_int.h"
#include "zzvr_shared.h"
#ifdef DRC_SH2
#include "cpu/sh2/compiler.h"
#endif

typedef uint32_t u32;
typedef uint16_t u16;

static volatile u32 *g_sh;
static int g_on;

static u16 sdram_w(u32 a) { return ((u16 *)Pico32xMem->sdram)[(a & 0x3fffeu) >> 1]; }
static void sdram_patch(u32 a, u16 v)
{
    ((u16 *)Pico32xMem->sdram)[(a & 0x3fffeu) >> 1] = v;
#ifdef DRC_SH2
    sh2_drc_wcheck_ram(a, 2, &ssh2);       /* drop the recompiled blocks over it */
#endif
}

void zzvr_a5_init(volatile u32 *sh, int hle_on)
{
    g_sh = sh;
    g_on = hle_on && !(sh[ZZVR_SH_HLE_CTRL] & 16u);
    sh[ZZVR_SH_A5_STATE] = g_on ? 0x41350000u : 0u;   /* 'A5' in the high half: on */
    sh[ZZVR_SH_A5_APPLIED] = 0;
}

/* once after every PicoFrame */
void zzvr_a5_frame(void)
{
    if (!g_on || !Pico32xMem) return;
    /* P1 */
    if (sdram_w(0x06000608) == 0xE740u && sdram_w(0x0600060A) == 0x0009u && sdram_w(0x0600060C) == 0x4710u &&
        sdram_w(0x0600060E) == 0x8BFCu && sdram_w(0x06000610) == 0xAFBFu &&
        sdram_w(0x06000592) == 0xD10Cu && sdram_w(0x06000594) == 0x6010u) {
        sdram_patch(0x06000608, 0xAFC3u);  /* bra 06000592 */
        g_sh[ZZVR_SH_A5_STATE] |= 1u;
        g_sh[ZZVR_SH_A5_APPLIED]++;
    }
    /* P2 */
    if (sdram_w(0x06004528) == 0x007Du && sdram_w(0x06004508) == 0x970Eu && sdram_w(0x0600450A) == 0x4710u &&
        sdram_w(0x0600450C) == 0x8BFDu) {
        sdram_patch(0x06004528, 0x0001u);
        g_sh[ZZVR_SH_A5_STATE] |= 2u;
        g_sh[ZZVR_SH_A5_APPLIED]++;
    }
}
