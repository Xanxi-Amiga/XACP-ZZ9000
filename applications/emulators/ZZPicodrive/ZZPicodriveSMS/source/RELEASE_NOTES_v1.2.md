# ZZPicoDriveSMS v1.2 - release notes (31 October 2026)

ZZPicoDriveSMS v1.2 is built on the ZZPicoDriveMD v1.2 platform code (launcher
and Core1), validated on an A4000/060, with the Master System specifics of the
Master System Edition and YM2413 FM sound.

## Display

- Core1 draws every emulated frame and publishes it when a display buffer
  is free. Occasional displayed-frame drops can occur if audio and video
  pacing cannot remain perfectly aligned.
- The 68k loop is locked to the ZZ9000 vertical blank (software PLL, deferred
  PAN acknowledge as a second edge indicator).
- PAL games: physical 640x480 output at 50.00 Hz. NTSC games: 640x480 at
  59.98 Hz. The P96 320x240 screen, its x2 scaler and the scanlines are kept.
  Flags `no50` / `no60`.
- The Master System picture (256 x 192 or 224) is centred in 320 x 240 with
  black borders.
- Quick start: the picture appears within a fraction of a second.

## Emulation timing and speed

- The Core1 program runs at the pace of the audio, with Paula as with AHI.
- With AHI, the next frame waits for the previous one to be shown (no frame
  overwritten by a burst); the audio keeps priority if it would run short.
- The Core1 stack is now in cached memory (it was left uncached by the MMU
  table): the emulation runs about 4 times faster, about 4 ms per frame with
  FM. Hot code (Z80, mode 4 video, sound, emu2413) compiled for speed.
- About one frame per minute may be skipped with AHI: the audio clock and the
  video clock differ slightly and the sound has priority.

## Audio

- PSG and **YM2413 FM** (emu2413 1.6.3, the engine of ZZVGMPlay 1.0), mixed
  with saturation at PicoDrive's level. GUI "FM sound" On/Off, Shell `fm` /
  `nofm`, `fm=` in the configuration. See [FM_GAMES.md](FM_GAMES.md).
- In the Japan region the console answers as a Japanese one on the I/O ports
  (TH/TR outputs read back 0, as on the 315-5297 I/O chip): games that use
  the FM unit only on a Japanese console (Wonder Boy III) now do.
- In the Japan region the audio selection of the Japanese console is emulated
  (port $F2: PSG only / FM only / both / none, as in Genesis Plus GX): games
  that play their music on both chips and select "FM only" (Wonder Boy in
  Monster Land, Wonder Boy III...) are heard with their FM soundtrack alone,
  not PSG music with FM drums.  Other regions: PSG and FM both heard.
- Paula plays in PAL or NTSC mode according to the region actually used.
- AHI task hardened; main loop at priority 6 during the game.

## Savestates and battery saves

- Saving and loading work with Paula and with AHI.
- Each savestate records its region and the FM setting; a state of the other
  region family (PAL / NTSC) or with the other FM setting is refused.
- A savestate is guaranteed only with the same region, audio output and FM
  setting it was saved with.
- The battery save (`ROM.srm`) is written at exit while the audio still runs.
- A `.srm` larger than 32 KB (Mega Drive game with the same name) is ignored
  and never overwritten.

## Launcher

- Default ROM drawer: *Set ROM drawer...* / *Clear*, saved in
  `ZZPicoDriveSMS.cfg`; the ROM requester opens there (or in the last drawer
  used). From the Shell a ROM name alone is also looked for in that drawer.
- Shell: region and flags in any order (`ZZPicoDriveSMS Rastan.sms eur ahi`).
- Clean exit and immediate relaunch; the mouse never quits; fault report if
  the Core1 program fails to start.
- Shell start: timing, audio, FM and CPU statistics at exit.

## Controls

- Button 1 / Button 2 / Pause; the second button of a DB9 pad is read on the
  POT line; Return is always Pause.

## Base

Emulation core: PicoDrive (notaz, irixxxx) with Cyclone 68000; emu2413 1.6.3.
Changes to PicoDrive are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Savestate format: PicoDrive's, plus a 9-byte region / FM tag at the end of the
file. Architecture details: `docs/ARCHITECTURE.md` in the source archive.
