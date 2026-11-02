# ZZPicoDrive32X 1.2 — Sega 32X Edition

**ZZPicoDrive32X 1.2 is the first public Sega 32X edition of ZZPicoDrive**, 
built on the smooth video, audio and AmigaOS integration of **ZZPicoDriveMD 1.2**.
It runs Sega 32X games on classic Amigas equipped with an **MNT ZZ9000**.

PicoDrive, including the dynamic recompiler for both SH-2 processors, runs on
ZZ9000 ARM Cortex-A9 Core1. The Amiga 68k manages the Workbench launcher,
Picasso96 display, controls, file access, audio output and save files.

## Highlights

- **Smooth Sega 32X gameplay**, with excellent frame rates approaching
  **49.9 FPS in PAL** or **59.9 FPS in NTSC**, depending on the game and setup.
- **Real PAL 50 Hz and NTSC 59.98 Hz video output**, synchronised to the
  ZZ9000 display for fluid scrolling.
- **Fast dual-SH-2 emulation**, using PicoDrive's ARM recompiler with a
  stability fix for Virtua Racing Deluxe.
- **AHI or direct Paula DMA audio**, including YM2612 FM, PSG and 32X PWM.
- **Savestates and SRAM**, keyboard and gamepad controls, a configurable
  default ROM drawer, and clean exit/relaunch.
- Workbench icon and complete corresponding 68k/ARM source code.

See [RELEASE_NOTES_v1.2.md](RELEASE_NOTES_v1.2.md) for an overview of this
first release.

## Requirements

- Amiga with MNT ZZ9000 and a 68020 or better; validated on A4000/060.
- **XACP firmware XX19c or later**, with its matching `zz9000.card`.
  **XX19d-RIX or a later compatible firmware update is recommended.**
- Picasso96 with a ZZ9000 **320x240, 32-bit** screen mode.
- A monitor that accepts **640x480 at 50 Hz (PAL)** and
  **59.98 Hz (NTSC)**.
- AHI only if using the AHI audio backend. Paula playback works without AHI.

ZZPicoDrive32X requires XACP interfaces and is not compatible with the
standard MNT ZZ9000 firmware.

## Supported ROM formats

Sega 32X cartridges in **`.32x` or `.bin`** format, up to 4 MB.
Compressed ZIP files are not supported; extract the cartridge image first.
No game ROMs or ROM-derived data are distributed.

## Starting a game

Copy the supplied `ZZPicoDrive32X` drawer to your Amiga, then double-click
**ZZPicoDrive32X** in Workbench. Select a 32X ROM, region, audio backend and
controller, then choose **Start**.

The launcher can remember a default ROM drawer through **Set ROM drawer...**
and **Clear**; the preference is stored in `ZZPicoDrive32X.cfg`.

## Audio

- **Paula**: direct DMA playback on AUD0/AUD1, without AHI.
- **AHI**: 16-bit stereo output through AHI-compatible hardware.
- **None**: muted output.

32X PWM, YM2612 FM and PSG are generated and mixed on ARM Core1.

## Controls

The default keyboard mapping is:

| Key | Action |
| --- | --- |
| Cursor keys | Direction pad |
| Keypad 1 / 2 / 3 | A / B / C |
| Keypad 4 / 5 / 6 | X / Y / Z |
| Keypad 0 | Mode |
| Return | Start (player 1) |
| Tab | Start (player 2) |
| Del | Console reset |
| Shift + Esc | Quit |
| F1–F5 | Load savestate slot 1–5 |
| Shift + F1–F5 | Save savestate slot 1–5 |

Supports DB9 joystick or pad, CD32 pad and USB pad through Poseidon/
`lowlevel.library`. Two-player controls are supported.

## Savestates

Savestates are stored next to the ROM (`ROM.st1`–`ROM.st5`), and battery
saves as `ROM.srm`.

**Load savestates with the same region and audio backend (AHI or Paula) used
when saving.** A state from the other PAL/NTSC region family is refused.

## Installation and sources

See [INSTALL.md](INSTALL.md) for setup instructions.

The complete corresponding source code is provided separately as
`ZZPicoDrive32X_v1.2_release_source_PUBLIC.zip` and in the
[GitHub repository](https://github.com/Xanxi-Amiga/XACP-ZZ9000) under
`applications/emulators/ZZPicodrive/ZZPicodrive32X/source/`.
It contains the 68k launcher, ARM Core1 code, PicoDrive, Cyclone, build
scripts and third-party licence notices.

## Credits and licence

ZZ9000 / AmigaOS port, launcher, Core1 integration and validation: **Xanxi**.

PicoDrive by **notaz, irixxxx and contributors**; Cyclone 68000 by
**Dave Haywood, notaz and irixxxx**. The PicoDrive code base is taken from
**PokaMilkTea's VirtuaRacingDeluxeEnhanced**.

**PicoDrive's historical non-commercial licence applies.** See
[COPYING_PICODRIVE.txt](COPYING_PICODRIVE.txt) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

SEGA and Sega 32X are trademarks of SEGA. This is an independent project.
