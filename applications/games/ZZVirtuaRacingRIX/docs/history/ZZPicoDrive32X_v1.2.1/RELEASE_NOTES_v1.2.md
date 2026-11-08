# ZZPicoDrive32X v1.2 - release notes (1 November 2026)

ZZPicoDrive32X v1.2 is built on the ZZPicoDriveMD v1.2 platform code (launcher
and Core1 publication model), validated on an A4000/060, with the 32X core of
the previous ZZPicoDrive32X build (PicoDrive with its SH-2 recompiler).

Measured on an A4000/060 with XX19d-RIX:

| Game | Region, audio | Frames displayed per second | Frames lost in game | Core1 time per frame (avg) |
| --- | --- | --- | --- | --- |
| Doom | NTSC, Paula | 59.8 | 0 | 7.6 ms |
| Virtua Racing Deluxe | NTSC, Paula | 59.8 | 0 (no freeze, 6.4 min + 20 min) | 9.7 ms |
| NBA Jam Tournament Edition | PAL, AHI | 49.6 | 0 | 8.8 ms |

Loading or saving a savestate (about 680 KB) holds the 68k for a few hundred
milliseconds: a few frames and a short audio gap at that moment only.

## Display

- Every emulated frame is displayed. The Core1 program draws every frame and,
  when the display buffer is not free yet, publishes the frame from its audio
  wait as soon as the 68k has taken the previous one.
- With AHI (which takes the sound 512 samples at a time), the next frame waits
  for the previous one to be shown: no frame is overwritten by a burst. The
  sound keeps priority if it would run short.
- The 68k loop is locked to the ZZ9000 vertical blank with a software PLL
  (timer.device sleep, edge detection, long-baseline period measurement), and
  uses the deferred-PAN acknowledge counter as a second edge indicator.
- PAL games: physical 640x480 output at 50.00 Hz. NTSC games: 640x480 at
  59.98 Hz. The P96 320x240 screen, its x2 scaler and the scanlines are kept.
  Flags `no50` / `no60`.
- Quick start: the Core1 program skips its diagnostic picture path as soon as
  the launcher enables the direct display; the picture appears within a
  fraction of a second.

## Emulation

- Same 32X emulation as the previous ZZPicoDrive32X build, with the loop
  optimisations of the SH-2 recompiler turned off (`LOOP_DETECTION=0`,
  `LOOP_OPTIMIZER=0`): Virtua Racing froze with both SH-2 processors waiting
  in a loop, a freeze also reported by PicoDrive users with the recompiler and
  not with the interpreter.
- Player 1 is a 6-button pad (X / Y / Z / Mode on keypad 4 / 5 / 6 / 0).

## Audio

- 32X PWM, YM2612 FM and PSG mixed on the ARM side, 16-bit stereo 22050 Hz.
- AHI task hardened; main loop at priority 6 during the game.
- Paula plays in PAL or NTSC mode according to the region the game actually
  runs in.

## Savestates and battery saves

- Saving and loading work with Paula and with AHI. A 32X savestate is about
  700 KB (up to 1.5 MB supported).
- Each savestate records its region; a state of the other region family
  (PAL / NTSC) is refused with a message.
- A savestate is guaranteed only with the same region and the same audio
  output it was saved with.
- The battery save (`ROM.srm`) is written at exit while the audio still runs.

## Launcher

- Default ROM drawer: *Set ROM drawer...* / *Clear*, saved in
  `ZZPicoDrive32X.cfg`; the ROM requester opens there (or in the last drawer
  used). From the Shell a ROM name alone is also looked for in that drawer.
- Shell: region and flags in any order (`ZZPicoDrive32X Doom.32x usa ahi`).
- 100 KiB stack for the launcher; clean stop of the Core1 program (data cache
  cleaned before the CPU is parked): immediate relaunch.
- Del = console reset (acknowledged once by the Core1 program).
- The mouse never quits the game (Shift+Esc quits).
- Core1 text line in the top border only with the `osd` flag.
- Fault report (ARM exception address and status) if the Core1 program fails
  to start; timing, audio and Core1 statistics at exit when started from the
  Shell.

## Base

Emulation core: PicoDrive (notaz, irixxxx; tree of VirtuaRacingDeluxeEnhanced)
with Cyclone 68000 and the SH-2 recompiler. Changes to PicoDrive are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Savestate format: PicoDrive's,
plus a 9-byte region tag at the end of the file. Development history:
`docs/history/`.
