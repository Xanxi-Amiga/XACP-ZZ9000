# ZZPicoDrive32X v1.2 - test builds (31 October - 1 November 2026)

| Build | Change | A4000 result |
| --- | --- | --- |
| RC1 | ZZPicoDriveMD v1.2 launcher with the 32X names; Core1 with deferred publication, AHI back-pressure, quick start, L1 clean; PicoDrive upstream 26683b2 (PWM channel control) | Doom 59.6 FPS once the vblank PLL locked; Virtua Racing froze in the demo after one race |
| RC2 | vblank PLL lock tried up to 5 times, failure reason printed | (launcher only) |
| RC3 | 26683b2 removed (PicoDrive tree identical to rc1) | Doom 59.8 FPS; Virtua Racing froze in a corner (3rd lap) and in the demo: both SH-2 stuck, Mega Drive side running |
| RC4 | launcher writes ARM_RUN = 0 before loading (a previous Core1 program that did not stop cleanly left stale L2 lines read back by the new blob); a blob file given and not found is an error | statistics correct after the old rc1 launcher |
| RC5 | SH-2 recompiler built with LOOP_DETECTION = 0, LOOP_OPTIMIZER = 0; SH-2 wait state in the Shell log | Virtua Racing: no freeze (race + 20 min demo, then 6.4 min with a reset), 9.7 ms/frame; Doom 7.6 ms/frame; NBA Jam TE PAL 49.6 FPS; savestates OK = v1.2 |

The Virtua Racing freeze is also reported by PicoDrive users with the
recompiler (libretro forum "PicoDrive Issues", 2024-2025; workaround: disable
the recompiler); the PC project VirtuaRacingDeluxeEnhanced runs the SH-2
interpreter.  Disabling only the loop optimisations keeps the recompiler's
speed on the Cortex-A9.
