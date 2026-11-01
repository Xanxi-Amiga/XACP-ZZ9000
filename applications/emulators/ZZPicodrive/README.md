# ZZPicoDrive for XACP / ZZ9000

ZZPicoDrive brings Sega console emulation to classic Amigas equipped with an
MNT ZZ9000 and XACP firmware. PicoDrive runs on the ZZ9000 ARM Cortex-A9
Core1, while AmigaOS handles the Workbench launcher, Picasso96 display,
controls, files and audio output.

## Current editions

| Edition | Emulated system | Features |
| --- | --- | --- |
| **[ZZPicoDriveMD 1.2](ZZPicodriveMD/README.md)** | Mega Drive / Genesis | Smooth PAL/NTSC gameplay, AHI or Paula audio, SRAM and savestates |
| **[ZZPicoDriveSMS 1.2](ZZPicodriveSMS/README.md)** | Master System | Smooth PAL/NTSC gameplay and Yamaha YM2413 FM sound |
| **[ZZPicoDrive32X 1.2](ZZPicodrive32X/README.md)** | Sega 32X | First public 32X edition, based on MD 1.2, with dual-SH-2 recompilation |

The three editions are separate Amiga applications. Each directory contains
a compiled Amiga distribution, corresponding sources, installation instructions
and third-party licence notices.

## Compatibility

- An MNT ZZ9000 running **XACP XX19c** or a later compatible firmware.
  **XX19d-RIX or a later compatible update is recommended.** Use the matching
  `zz9000.card` driver.
- Picasso96 and a suitable ZZ9000 RTG display mode.
- AHI is optional when using direct Paula audio.

For supported ROM formats, controls, video modes and detailed requirements,
consult the README for the edition you want to use.

## Previous versions

The original MD and SMS 1.1 releases remain available in the
**[archive/](archive/)** directory. The first public 32X edition is 1.2,
reflecting its shared platform with ZZPicoDriveMD 1.2.

## Credits and licensing

ZZ9000/AmigaOS ports and integration: Xanxi. PicoDrive by notaz, irixxxx
and contributors; Cyclone by its respective contributors. The 32X edition
uses a PicoDrive code base derived from PokaMilkTea's
VirtuaRacingDeluxeEnhanced.

PicoDrive's historical **non-commercial licence** applies. Refer to each
edition's `COPYING_PICODRIVE.txt` and `THIRD_PARTY_NOTICES.md` for details.
No commercial game ROMs are distributed.

[All XACP emulators](../README.md)
