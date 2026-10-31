# ZZPicoDriveSMS 1.2 — Master System Edition

Sega Master System emulation on classic Amiga computers equipped with an MNT ZZ9000 and XACP firmware.
PicoDrive runs on the ZZ9000 ARM Cortex-A9 Core1; the Amiga 68k handles Workbench, ROM files,
input, Picasso96 presentation, audio output and saves.

## Version 1.2

- Yamaha YM2413 FM Sound Unit emulation using emu2413 1.6.3, selectable in the launcher
- PAL 50 Hz and NTSC 59.98 Hz output, with improved audio/video synchronization
- AHI and direct Paula DMA backends; SMS PSG and FM mixed on Core1
- Savestate corrections, remembered ROM directory and improved start/exit handling
- Corrected cached ARM stack mapping for Cortex-A9 performance

## Files

- [`ZZPicoDriveSMS_v1.2_Amiga.zip`](ZZPicoDriveSMS_v1.2_Amiga.zip): compiled Amiga executable,
  Workbench icon and operating instructions; the ARM program is embedded
- [`source/`](source/): complete corresponding 68k and ARM source code and third-party sources
- [`source/BUILD.md`](source/BUILD.md): offline MSYS build instructions (run `sh build_all.sh`
  from inside `source/`)
- [`INSTALL.md`](INSTALL.md): installation and firmware requirements
- [`FM_GAMES.md`](FM_GAMES.md): FM-compatible games
- [`RELEASE_NOTES_v1.2.md`](RELEASE_NOTES_v1.2.md): changes in this version
- [`source/docs/ARCHITECTURE.md`](source/docs/ARCHITECTURE.md): architecture and technical notes

## Requirements

Amiga 68020 or newer with ZZ9000, XACP XX19c or later with the matching `zz9000.card`,
Picasso96 320x240 32-bit mode, a suitable monitor, and AHI if the AHI output is selected.
This application does not run on stock MNT firmware without the XACP interfaces.

ROM files are not included. Supported file types: `.sms`, `.bin` (decompress ZIP files first).

## Credits and licenses

ZZ9000 / AmigaOS port and integration: Xanxi.
PicoDrive: notaz and contributors; Cyclone: its respective contributors;
emu2413: Mitsutaka Okazaki.
See [`COPYING_PICODRIVE.txt`](COPYING_PICODRIVE.txt),
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and the source tree for
complete third-party terms. PicoDrive's historical license conditions apply.
