# ZZPicoDriveMD v1.2 - release notes (31 October 2026)

## Display

- Core1 renders every emulated frame. The display presents each completed
  frame when a buffer and vertical blank are available; a small number may
  still be missed during system stalls. The Core1 program draws every frame and,
  when the display buffer is not free yet, publishes the frame from its audio
  wait as soon as the 68k has taken the previous one.
- The 68k loop is locked to the ZZ9000 vertical blank with a software PLL
  (timer.device sleep, edge detection, long-baseline period measurement), and
  uses the deferred-PAN acknowledge counter as a second edge indicator, so a
  short vblank pulse hidden by system activity no longer costs a frame.
- PAL games: physical 640x480 output at 50.00 Hz (625 lines). NTSC games:
  640x480 at 59.98 Hz (521 lines); the plain 640x480 screen mode of the ZZ9000
  runs at 59.52 Hz and forced one frame skip every 2.5 s. The P96 320x240
  screen, its x2 scaler and the scanlines are kept. Flags `no50` / `no60`.
- Publication starts once the Core1 program runs at one frame per vblank: no
  frame is lost at start-up.

Firmware: XACP XX19c or later (current firmware recommended). Measured on
an A4000/060 with XX19d-RIX, frames displayed per second:

| Game region | Paula | AHI |
| --- | --- | --- |
| PAL (50 Hz output) | 49.8 | 49.5 |
| NTSC (59.98 Hz output) | 59.8 | 59.0 |

In a 240 s run, 14442 frames emulated and 14427 displayed; the few frames
skipped coincide with short system stalls of the 68k.

## Audio

- AHI task hardened: completion errors, short and abnormally fast completions
  are detected; after repeated failures the request is no longer resent and the
  program leaves cleanly instead of spinning.
- The main loop runs at priority 6 during the game so that the AHI task does
  not delay the display.
- Paula plays in PAL or NTSC mode according to the region the game actually
  runs in (with the AUTO region it always used PAL mode, which gave underruns
  at the start of NTSC games).

## Savestates

- Saving and loading work with Paula and with AHI (the launcher keeps the
  audio running while it waits for the Core1 program).
- Each savestate records its region; a state of the other region family
  (PAL / NTSC) is refused with a message.
- F1 - F5 ignore keyboard auto-repeat.
- The battery save (`ROM.srm`) is written at exit while the audio still runs
  (written after the audio stop, it could end with `SRAM: save TIMEOUT`).
- A savestate is guaranteed only with the same region and the same audio
  output it was saved with.

## Stability

- 100 KiB stack for the launcher (the Shell default of 4 KiB caused crashes on
  exit).
- Clean stop of the Core1 program (cache cleaned, CPU parked by the firmware):
  ZZPicoDriveMD can be relaunched immediately.
- A missing display acknowledge no longer ends the game: the flip is re-armed.
- The mouse no longer quits the game.
- Shell: region and flags can be given in any order
  (`ZZPicoDriveMD Sonic.bin eur paula`); the form with `-` still works.
- Fault report (ARM exception address and status) if the Core1 program fails
  to start.

## Unchanged

Emulation core: PicoDrive (notaz) with Cyclone 68000, same revisions as the
previous ZZPicoDriveMD release. Savestate format: PicoDrive's, plus a 9-byte
region tag at the end of the file.

## Core1 memory and audio improvements

From the ZZPicoDriveSMS v1.2 work, measured on the A4000:

- **Core1 stack cached.**  The firmware starts CPU1 with its stack at
  0x06000000 and the blob keeps it, but the blob's MMU table left that
  section strongly ordered, penalising stack accesses. Version 1.2
  maps that section write-back/write-allocate. This same correction was
  benchmarked in ZZPicoDriveSMS; the corresponding speed-up has not been
  independently measured for the Mega Drive edition. The launcher log
  identifies the extended Core1 build with `sub=2`.
- **AHI: no frame overwritten by a burst.**  AHI takes the audio 512 samples
  at a time; with a fast frame the Core1 could produce two frames within a
  few milliseconds and the second replaced the first before it was shown.
  The next frame now waits for the previous one to be shown, unless the
  audio would run short.
- **Quick start with AHI.**  The start-up wait accepts the uneven frame rhythm
  of AHI (7 to 9 frames over 8 vblanks) instead of running to its 3 s limit;
  the Core1 skips its old diagnostic path during that wait.
- **Default ROM drawer**: *Set ROM drawer...* / *Clear* in the launcher,
  saved in `ZZPicoDriveMD.cfg`; from the Shell a ROM name alone is also looked
  for in that drawer.
- About one frame per minute may still be skipped with AHI: the audio clock
  and the video clock differ slightly and the sound has priority.
