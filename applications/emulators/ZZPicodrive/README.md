
# ZZPicoDrive for XACP / ZZ9000

ZZPicoDrive brings Sega console emulation to classic Amigas equipped with an MNT ZZ9000 and XACP firmware.

PicoDrive runs on the ZZ9000 ARM Cortex-A9 Core1, while AmigaOS handles the Workbench launcher, Picasso96 display, controls, file access and audio output.

## Current editions

| Edition | Emulated system | Features |
| --- | --- | --- |
| **[ZZPicoDriveMD 1.2](ZZPicodriveMD/README.md)** | Mega Drive / Genesis | Smooth PAL/NTSC gameplay, Paula or AHI audio, SRAM and savestates |
| **[ZZPicoDriveSMS 1.2](ZZPicodriveSMS/README.md)** | Master System | PAL/NTSC gameplay and Yamaha YM2413 FM sound |
| **[ZZPicoDrive32X 1.2.1](ZZPicodrive32X/README.md)** | Sega 32X | Dual-SH-2 recompilation, smooth PAL/NTSC gameplay and six-button controller support |
| **[ZZPicoDriveMD Virtua Racing Edition 1.2](ZZPicodriveVR/README.md)** | Mega Drive Virtua Racing | Dedicated edition for the original SVP cartridge, supporting European, US and Japanese versions |
| **[ZZPicoDriveMegaCD 1.2](ZZPicodriveMegaCD/README.md)** | Sega Mega-CD / Sega CD | CHD and CUE/BIN disc images, CD audio, multi-disc games and Sega CD 32X support |

The five editions are separate Amiga applications. Each directory provides a compiled Amiga distribution, corresponding source code, installation instructions and third-party licence notices.

## Compatibility

- MNT ZZ9000 with **XACP XX19c or a later compatible XACP firmware release**.
- **XX19d-RIX or a later compatible XACP release is recommended**, with the matching `zz9000.card` driver.
- Picasso96 and a suitable ZZ9000 screen mode.
- Paula audio or AHI, depending on the selected backend.
- PAL display requires a monitor supporting 50 Hz. Otherwise, select NTSC (60 Hz).

CPU requirements, supported game formats and controller options are documented separately for each edition.

The original MNT firmware and other non-XACP firmware branches are not compatible.

## Previous versions

The original MD and SMS 1.1 releases are retained in the **[archive/](archive/)** directory. The first Sega 32X release was version 1.2, followed by the 1.2.1 controller and launcher update.

## Credits and licensing

ZZ9000/AmigaOS ports and integration: **Xanxi**.

PicoDrive by **notaz, irixxxx and contributors**; Cyclone and other third-party components retain their original credits and licence terms.

The PicoDrive-based editions retain the project's historical **non-commercial licence**. Refer to each edition's `COPYING_PICODRIVE.txt` and `THIRD_PARTY_NOTICES.md` for details.

No commercial game ROMs, disc images or Mega-CD BIOS files are distributed.

[All XACP emulators](../README.md)