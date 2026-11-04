# ZZPicoDriveMD Virtua Racing Edition 1.2

**ZZPicoDriveVR** runs the original Mega Drive / Genesis cartridge version of **Virtua Racing** on an Amiga equipped with an MNT ZZ9000. It emulates the cartridge's SVP (SSP1601) processor through PicoDrive on the ZZ9000 ARM Core1. The Amiga handles input, files, display and audio output.

**Virtua Racing only:** European, US and Japanese cartridge ROMs are supported. Other Mega Drive games belong in [ZZPicoDriveMD](../ZZPicodriveMD/README.md); 32X games belong in [ZZPicoDrive32X](../ZZPicodrive32X/README.md).

## Features

- Full-speed PAL and NTSC gameplay on the tested A4000/060; 50 Hz PAL and 59.98 Hz NTSC display output.
- Full-width display of Virtua Racing's 256-pixel video mode. Optional scanlines can be enabled with `ZZScanlines` or `ZZScanlinesGUI`, supplied with XACP firmware.
- Paula or AHI sound; DB9, CD32 and Poseidon USB controllers.
- Five savestate slots; remembers the ROM drawer; clean exit and relaunch.

## Requirements

- Amiga with a **68030 at 25 MHz or higher**, and MNT ZZ9000 (tested on A4000/060).
- **XACP firmware XX19c or a later compatible XACP release**, with its matching `zz9000.card` driver. **XX19d-RIX or a later compatible XACP release is recommended**. Official MNT and BlitterStudio firmware are not compatible.
- Picasso96 with a **320×240, 32-bit** ZZ9000 screen mode. **PAL mode requires a monitor supporting 50 Hz**. Otherwise, select **NTSC (60 Hz)** in the launcher.
- AHI only if you choose AHI audio; Paula works without it.

## Playing

Double-click `ZZPicoDriveVR`, choose your Virtua Racing ROM, region, audio output and controller, then select **Start**. Accepted cartridge formats: `.bin`, `.gen`, `.md` (not ZIP or SMD). The edition recognises Virtua Racing from the ROM header and rejects other games.

The ROM drawer can be saved in the Workbench launcher.

## Controls

| Keyboard | Action |
| --- | --- |
| Cursor keys | Direction |
| Keypad 1 / 2 / 3 | A / B / C |
| Keypad 4 / 5 / 6 | X / Y / Z |
| Keypad 0 | Mode |
| Return / Tab | Start player 1 / 2 |
| Shift+Esc | Quit |
| Del | Reset |
| F1–F5 | Load savestate slot 1–5 |
| Shift+F1–F5 | Save savestate slot 1–5 |

**Virtua Racing camera views:** C, X, Y and Z. On CD32 and Poseidon USB pads, blue/red/green = A/B/C, left/right shoulder = X/Y, yellow = Z, Play = Start. Mode remains on keypad 0. USB physical button mapping may need configuration in Poseidon.

Savestates are stored beside the ROM (`.st1` to `.st5`) and include the SVP state. For reliable loading, keep the **same region and audio backend** as when the state was saved. PAL/NTSC-incompatible states are refused.

## Installation, source and licensing

See [INSTALL.md](INSTALL.md) for setup and [BUILD.md](BUILD.md) for the complete offline source build. No ROM is supplied.

ZZ9000/Amiga port and hardware validation: **Xanxi**. PicoDrive and SVP emulation: **notaz, irixxxx and contributors**; Cyclone: **Dave Haywood, notaz and irixxxx**. PicoDrive source lineage: PokaMilkTea's **VirtuaRacingDeluxeEnhanced**. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [COPYING_PICODRIVE.txt](COPYING_PICODRIVE.txt) for the non-commercial licence and full credits. Sega and Virtua Racing trademarks belong to SEGA; this project is not affiliated with SEGA.
