# ZZPicoDriveMD v1.2

**ZZPicoDriveMD** is a Sega Mega Drive / Genesis emulator for classic Amiga
systems equipped with an **MNT ZZ9000**.

The emulation core (PicoDrive) runs on the ZZ9000 ARM Core1 and renders
directly into ZZ9000 RTG memory. The Amiga 68k side handles the launcher, file
access, display flips, input, audio output, SRAM and savestates, and stays
almost idle.

This is the **Genesis / Mega Drive Edition** of ZZPicoDrive.

## What is new in v1.2

- **Per-frame rendering with vblank-synchronised display.** The measured
  display rates are 49.5-49.8 FPS for PAL and 59.0-59.8 FPS for NTSC,
  depending on the audio backend. A small number of frames can be missed
  during host-system stalls or audio-clock drift.
- **Real 50 Hz output for PAL games** and **59.98 Hz output for NTSC games**,
  with the 320x240 screen, its x2 scaler and the scanlines kept. Fluid
  scrolling, no judder.
- **Paula and AHI at the same level**: the display stays synchronised with
  either backend.
- **Savestates work with Paula and with AHI**, and a savestate of the other
  region (PAL / NTSC) is refused instead of breaking the game.
- **Clean exit and immediate relaunch** without resetting the ZZ9000.
- **The mouse never quits the game** (Shift+Esc quits).
- **Cacheable ARM stack**: the Core1 stack memory section is mapped
  write-back/write-allocate instead of strongly ordered.
- **Default ROM drawer** remembered in the configuration.

See [RELEASE_NOTES_v1.2.md](RELEASE_NOTES_v1.2.md) for the details.

## Requirements

- Amiga with MNT ZZ9000 and a 68020 or better (tested on A4000/060).
- XACP firmware **XX19c or later**, with its matching `zz9000.card`. The
  current firmware (XX19d-RIX at the time of this release, or any later
  update) is recommended; v1.2 was validated on XX19d-RIX and also works with
  XX19c.
- Picasso96 with a ZZ9000 **320x240, 32-bit** screen mode.
- AHI, only for the AHI audio backend. The Paula backend needs nothing.
- A monitor or TV that accepts 640x480 at 50 Hz and at 59.98 Hz (most do;
  see `no50` / `no60` below if yours does not).

ZZPicoDriveMD is not compatible with the official MNT firmware or with other
ZZ9000 firmware branches: it needs specific XACP Core1, memory and display
interfaces.

## ROM formats

Supported: `.bin`, `.gen`, `.md`. Not supported: `.zip`, `.smd` (decompress or
convert them first).

No ROM is included. Only use ROM files you are legally entitled to use.

## Starting

From Workbench: double-click **ZZPicoDriveMD**, choose the ROM, region, audio
backend and controller, then **Start**.

**ROM drawer**: *Set ROM drawer...* chooses the drawer the ROM requester always
opens in (shown on the *ROM dir* line); *Clear* goes back to the last drawer
used.  Both are saved at once in `ZZPicoDriveMD.cfg` (`romdir=`, `lastdir=`).
If the drawer's volume is not mounted, the requester opens normally and the
setting is kept.  From the Shell, a ROM name alone that is not in the current
directory is looked for in the ROM drawer.

From the Shell:

```text
ZZPicoDriveMD <rom> [region] [flags]

  ZZPicoDriveMD Sonic.bin
  ZZPicoDriveMD Sonic.bin eur paula
  ZZPicoDriveMD Sonic.bin usa ahi kbd
```

- Region and flags are keywords and can be given in any order (upper or
  lower case). The Core1 program embedded in ZZPicoDriveMD is used; the form
  with `-` in second position (`Sonic.bin - eur paula`) also works. Any other
  word is taken as an external Core1 blob file (developers only).
- `region`: `auto` (default), `usa`, `jap`, `eur`, `jappal`.
- `flags`:
  - audio: `ahi`, `paula`, `silence`
  - input: `kbd`, `cd32`, `db9`, `usb`, `port=N`, `2p`
  - display: `no50` (PAL games stay on the 60 Hz screen mode),
    `no60` (NTSC games use the plain 59.5 Hz screen mode), `osd`
  - diagnostics only: `pri0`, `norecheck`, `nosettle`, `tof`, `gate`, `inputlog`

Started from the Shell, ZZPicoDriveMD prints timing and audio statistics when
it exits (frames emulated and displayed, audio underruns, exit reason).

## Audio

ZZPicoDriveMD produces 16-bit stereo PCM at 22050 Hz; YM2612 FM and PSG are
emulated on the ARM side.

- **Paula**: direct DMA playback on AUD0/AUD1, no AHI needed.
- **AHI**: 16-bit stereo through AHI (sound cards, mixing with other programs).
- **None**: no sound.

## Controls

| Key | Mega Drive |
| --- | --- |
| Cursor keys | D-pad |
| Keypad 1 / 2 / 3 | A / B / C |
| Keypad 4 / 5 / 6 | X / Y / Z |
| Keypad 0 | Mode |
| Return | Start (player 1) |
| Tab | Start (player 2) |

Controllers: DB9 joystick or pad, CD32 pad, USB pad through Poseidon
(lowlevel.library). Two players with two controllers on different ports.

## Hotkeys

| Key | Action |
| --- | --- |
| Shift + Esc | Quit |
| Del | Console reset |
| F1 - F5 | Load savestate slot 1 - 5 |
| Shift + F1 - F5 | Save savestate slot 1 - 5 |

## Savestates

Savestates are stored next to the ROM as `ROM.st1` ... `ROM.st5`; battery
saves as `ROM.srm`.

**A savestate is guaranteed only when it is loaded with the same region and
the same audio output (AHI / Paula) it was saved with. In any other case it is
not guaranteed.** A savestate saved in the other region family (PAL / NTSC) is
refused by the launcher with a message.

## Installation

See [INSTALL.md](INSTALL.md).

## Source code and building

The complete source is published in [`source/`](source/): 68k launcher
(`amiga/`), ARM Core1 program (`Core1/`), PicoDrive and Cyclone
(`third_party/`), and build scripts.
See [source/BUILD.md](source/BUILD.md).

## Credits

ZZ9000 / AmigaOS port, launcher, ARM Core1 integration, audio, video, input,
save handling and real-hardware validation: **Xanxi**.

Emulation core: **PicoDrive by notaz and contributors**; Cyclone 68000 by
Dave Haywood, notaz and irixxxx.

See [COPYING_PICODRIVE.txt](COPYING_PICODRIVE.txt) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Sega, Mega Drive and Genesis are trademarks of SEGA. This project is not
affiliated with SEGA.
