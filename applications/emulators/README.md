
# Emulators

This directory contains emulator projects developed for classic Amigas equipped with an MNT ZZ9000 and XACP firmware.

For ZZPicoDrive, emulation runs on the ZZ9000 ARM Cortex-A9 Core1, while AmigaOS handles the Workbench launcher, Picasso96 display, input, files and audio output.

## ZZPicoDrive series

| Edition | System | Highlights |
| --- | --- | --- |
| **[ZZPicoDriveMD 1.2](ZZPicodrive/ZZPicodriveMD/README.md)** | Sega Mega Drive / Genesis | Smooth PAL/NTSC gameplay, AHI/Paula audio, SRAM and savestates |
| **[ZZPicoDriveSMS 1.2](ZZPicodrive/ZZPicodriveSMS/README.md)** | Sega Master System | Yamaha YM2413 FM sound, PAL/NTSC gameplay and savestates |
| **[ZZPicoDrive32X 1.2.1](ZZPicodrive/ZZPicodrive32X/README.md)** | Sega 32X | Dual-SH-2 recompilation, PAL/NTSC gameplay and six-button controller support |
| **[ZZPicoDriveMD Virtua Racing Edition 1.2](ZZPicodrive/ZZPicodriveVR/README.md)** | Mega Drive Virtua Racing | Dedicated original SVP cartridge emulation for European, US and Japanese versions |
| **[ZZPicoDriveMegaCD 1.2](ZZPicodrive/ZZPicodriveMegaCD/README.md)** | Sega Mega-CD / Sega CD | CHD disc images, CD audio, multi-disc support and Sega CD 32X games |

All five editions are compatible with **XACP XX19c**. **XX19d-RIX or a later compatible XACP firmware release is recommended**, together with the matching `zz9000.card` driver.

Individual application READMEs provide detailed CPU, display, audio and installation requirements.

Older MD and SMS releases are retained in [ZZPicodrive/archive/](ZZPicodrive/archive/).

## Other emulator projects

- **[ZZPPC](ZZPPC/)** — experimental PowerPC 32-bit / FPU execution on ZZ9000 Core1; a proof of concept, not a general PowerPC emulator.

## Distribution and licensing

Each ZZPicoDrive edition provides its own Amiga distribution, corresponding source code, installation instructions and third-party licence notices.

PicoDrive retains its historical non-commercial licence. No commercial game ROMs, disc images or Mega-CD BIOS files are included.