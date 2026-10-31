# ZZPicoDriveSMS v1.2

**ZZPicoDriveSMS** is a Sega Master System emulator for classic Amiga systems
equipped with an **MNT ZZ9000**.

The emulation core (PicoDrive) runs on the ZZ9000 ARM Core1 and renders
directly into ZZ9000 RTG memory. The Amiga 68k side handles the launcher, file
access, display flips, input, audio output, SRAM and savestates, and stays
almost idle.

This is the **Master System Edition** of ZZPicoDrive, with **YM2413 FM sound**.

## What is new in v1.2

v1.2 brings to the Master System Edition everything validated in
ZZPicoDriveMD v1.2:

- **Smooth video presentation** near 50 frames per second for PAL games
  and 60 for NTSC games, with both audio backends. Rare frames may be
  skipped when the audio and video clocks differ.
- **Real 50 Hz output for PAL games** and **59.98 Hz output for NTSC games**,
  with the 320x240 screen, its x2 scaler and the scanlines kept.
- **The emulation follows the audio clock** (it no longer drifts against the
  sound): no audio gaps, no slow drift of the picture.
- **Paula and AHI at the same level.**
- **Savestates work with Paula and with AHI**; a savestate of the other
  region (PAL / NTSC) is refused instead of breaking the game.
- **Clean exit and immediate relaunch** without resetting the ZZ9000.
- **The mouse never quits the game** (Shift+Esc quits).
- **YM2413 FM sound**: the FM Sound Unit of the Japanese Master System /
  Mark III, mixed with the PSG; option On/Off, FM state in the savestates.
  See [FM_GAMES.md](FM_GAMES.md) for the games that use it.
- **Default ROM drawer** remembered in the configuration.
- **Corrected Cortex-A9 stack mapping**: the Core1 stack is cached;
  measured emulation time is about 4 ms per frame with FM on the tested system.

See [RELEASE_NOTES_v1.2.md](RELEASE_NOTES_v1.2.md) for the details.

## Requirements

- Amiga with MNT ZZ9000 and a 68020 or better.
- XACP firmware **XX19c or later**, with its matching `zz9000.card`. The
  current firmware (XX19d-RIX at the time of this release, or any later
  update) is recommended.
- Picasso96 with a ZZ9000 **320x240, 32-bit** screen mode.
- AHI, only for the AHI audio backend. The Paula backend needs nothing.
- A monitor or TV that accepts 640x480 at 50 Hz and at 59.98 Hz (most do;
  see `no50` / `no60` below if yours does not).

ZZPicoDriveSMS is not compatible with the official MNT firmware or with other
ZZ9000 firmware branches: it needs specific XACP Core1, memory and display
interfaces.

## ROM formats

Supported: `.sms`, `.bin`. Not supported: `.zip` (decompress first).

No ROM is included. Only use ROM files you are legally entitled to use.

## Starting

From Workbench: double-click **ZZPicoDriveSMS**, choose the ROM, region, audio
backend and controller, then **Start**.

**ROM drawer**: *Set ROM drawer...* chooses the drawer the ROM requester always
opens in (shown on the *ROM dir* line); *Clear* goes back to the last drawer
used.  Both are saved at once in `ZZPicoDriveSMS.cfg` (`romdir=`, `lastdir=`).
If the drawer's volume is not mounted, the requester opens normally and the
setting is kept.  From the Shell, a ROM name alone that is not in the current
directory is looked for in the ROM drawer.

From the Shell:

```text
ZZPicoDriveSMS <rom> [region] [flags]

  ZZPicoDriveSMS Alex.sms
  ZZPicoDriveSMS Alex.sms eur paula
  ZZPicoDriveSMS Alex.sms usa ahi kbd
  ZZPicoDriveSMS PhantasyStar.sms jap ahi fm
```

- Region and flags are keywords and can be given in any order (upper or
  lower case). The Core1 program embedded in ZZPicoDriveSMS is used; the old
  form with `-` in second position (`Alex.sms - eur paula`) still works.
  Any other word is taken as an external Core1 blob file (developers only).
- `region`: `auto` (default), `usa`, `jap`, `eur`.
- `flags`:
  - audio: `ahi`, `paula`, `silence`, `fm` (default), `nofm`
  - input: `kbd`, `cd32`, `db9`, `usb`, `port=N`, `2p`
  - display: `no50` (PAL games stay on the 60 Hz screen mode),
    `no60` (NTSC games use the plain 59.5 Hz screen mode), `osd`
  - diagnostics only: `pri0`, `norecheck`, `nosettle`, `tof`, `gate`, `inputlog`

Started from the Shell, ZZPicoDriveSMS prints timing and audio statistics when
it exits.

## Audio

16-bit stereo PCM at 22050 Hz; the PSG and the YM2413 FM unit are emulated on
the ARM side and mixed into one stream.

**FM sound (YM2413)**: On (default) exposes the FM unit; the game decides to
use it. Off = PSG only, as on a Master System without FM. GUI "FM sound",
Shell `fm` / `nofm`. PSG and FM are mixed at PicoDrive's level.

In the Japan region the audio selection of the Japanese console is emulated:
a game that selects "FM only" mutes the PSG (many Sega games play their music
on both chips and let the console choose; Wonder Boy in Monster Land then
plays its full FM soundtrack instead of PSG music with FM drums). In the other
regions PSG and FM are always both heard.

Some games use the FM unit in any region (Rastan, Golvellius...); others only
on a Japanese console (Wonder Boy III: The Dragon's Trap) - start them with
the Japan region (`jap`). Phantasy Star and Ys have FM music only in their
Japanese versions. Full list: [FM_GAMES.md](FM_GAMES.md).

- **Paula**: direct DMA playback on AUD0/AUD1, no AHI needed.
- **AHI**: 16-bit stereo through AHI.
- **None**: no sound.

## Controls

| Key | Master System |
| --- | --- |
| Cursor keys | D-pad |
| Keypad 1 / 2 | Button 1 / Button 2 |
| Return | Pause (console button) |

Controllers: DB9 joystick or pad (second button read on the POT line), CD32
pad (Blue = Button 1, Red = Button 2, Start = Pause), USB pad through
Poseidon (lowlevel.library). Two players with two controllers on different
ports. Return always works as Pause, even with a controller.

## Hotkeys

| Key | Action |
| --- | --- |
| Shift + Esc | Quit |
| Del | Console reset |
| F1 - F5 | Load savestate slot 1 - 5 |
| Shift + F1 - F5 | Save savestate slot 1 - 5 |

## Savestates

Savestates are stored next to the ROM as `ROM.st1` ... `ROM.st5`; battery
saves as `ROM.srm`. A `.srm` file larger than 32 KB (a Mega Drive game with
the same name, `Sonic.bin` -> `Sonic.srm`) is ignored and never overwritten.

**A savestate is guaranteed only when it is loaded with the same region, the
same audio output (AHI / Paula) and the same FM setting it was saved with. In
any other case it is not guaranteed.** A savestate saved in the other region
family (PAL / NTSC) or with the other FM setting is refused by the launcher
with a message.

## Installation

See [INSTALL.md](INSTALL.md).

## Source code and building

The complete source is included: 68k launcher (`amiga/`), ARM Core1 program
(`Core1/`), PicoDrive, Cyclone and emu2413 (`third_party/`), and the build
scripts (`sh build_all.sh`). See [BUILD.md](BUILD.md).

## Credits

ZZ9000 / AmigaOS port, launcher, ARM Core1 integration, audio, video, input,
save handling and real-hardware validation: **Xanxi**.

Emulation core: **PicoDrive by notaz and contributors**; Cyclone 68000 by
Dave Haywood, notaz and irixxxx; emu2413 (YM2413) by Mitsutaka Okazaki.

See [COPYING_PICODRIVE.txt](COPYING_PICODRIVE.txt) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Sega and Master System are trademarks of SEGA. This project is not affiliated
with SEGA.
