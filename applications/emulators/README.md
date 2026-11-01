# Emulators

This directory contains emulator projects developed for classic Amigas with
an MNT ZZ9000 and XACP firmware. For the ZZPicoDrive editions, emulation
runs on the ZZ9000 ARM Cortex-A9 Core1 while AmigaOS handles the Workbench
launcher, Picasso96 display, input, files and audio output.

## ZZPicoDrive series

| Edition | System | Highlights |
| --- | --- | --- |
| **[ZZPicoDriveMD 1.2](ZZPicodrive/ZZPicodriveMD/README.md)** | Sega Mega Drive / Genesis | Smooth PAL 50 Hz and NTSC 60 Hz gameplay, AHI/Paula audio, SRAM and savestates |
| **[ZZPicoDriveSMS 1.2](ZZPicodrive/ZZPicodriveSMS/README.md)** | Sega Master System | Smooth PAL/NTSC gameplay, Yamaha YM2413 FM audio, AHI/Paula and savestates |
| **[ZZPicoDrive32X 1.2](ZZPicodrive/ZZPicodrive32X/README.md)** | Sega 32X | First public 32X edition, based on ZZPicoDriveMD 1.2; dual-SH-2 recompilation, smooth PAL/NTSC gameplay, AHI/Paula and savestates |

The ZZPicoDrive editions are compatible with **XACP XX19c**. **XX19d-RIX
or a later compatible firmware update is recommended**, together with the
matching `zz9000.card` driver. Individual application READMEs provide full
requirements and installation instructions.

Older MD and SMS releases are kept in [ZZPicodrive/archive/](ZZPicodrive/archive/).

## Other emulator projects

- **[ZZPPC](ZZPPC/)** — experimental PowerPC 32-bit / FPU execution on
  ZZ9000 Core1; a proof of concept, not a general PowerPC emulator.

## Distribution and licensing

Each edition has its own Amiga distribution, corresponding source code,
installation notes and third-party licence notices. PicoDrive retains its
historical non-commercial licence. No commercial game ROMs are included.
