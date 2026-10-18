# ZZVGMPlay v1.0 - Debug menu and shutdown validation

The AmigaOS 3.x player window now displays a clean `Playing` / `Paused` state by default. Playback counters have moved behind the **Debug > Show WRITES / UND** checkmarked menu item. Selecting the menu item toggles real-time counter display; it is off at program startup.

The counters retain their original meanings: `WRITES` is sourced from the shared Core1 `VSH_OPL_WRITES` mailbox word, `UND` counts the 68k audio buffer underruns, and `loop` is the Core1 loop counter. No Core1 firmware, synthesis engine, PCM mixer, transport or filesystem code was changed for this feature.

This uses a nonmodal GadTools menu toggle rather than opening a dialog while audio is running; the AHI-driven audio loop continues to work normally.

## Shutdown regression check on real hardware

A single illegal-address exception was reported after closing a Moonwalker session; subsequent closes have not reproduced it. This does not establish root cause or prove that the previous cleanup fixes resolved it. The exit-trace candidate is retained in this package. Before tagging v1.0, run at least the following on a real A4000 + ZZ9000:

1. Launch and exit with no playback.
2. Play Moonwalker, stop manually, then close the player.
3. Let a Moonwalker track complete, then close the player.
4. Repeat start/stop/next/close several times with the playlist.
5. Toggle Debug > Show WRITES / UND during playback, confirm counters update, then toggle it off.
6. Check `T:ZZVGMPlay_exit.log` (or `RAM:ZZVGMPlay_exit.log`) when investigating shutdown failures.

If the crash recurs, save the exit trace and the exception PC/stack. The source build is intended to run fully offline under MSYS by calling `sh build_all.sh` from the package root; the ARM/68k cross-compile and hardware results are not certified by host-side tests.
