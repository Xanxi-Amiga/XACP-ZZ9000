# ZZPicoDrive32X 1.2 — first public Sega 32X edition

**1 November 2026**

ZZPicoDrive32X 1.2 is the **first public 32X release** in the XACP
ZZPicoDrive series. It builds on **ZZPicoDriveMD 1.2**, reusing its smooth
PAL/NTSC presentation, Amiga launcher and audio synchronisation, while
adding PicoDrive's Sega 32X emulation and dual-SH-2 recompiler.

## Main features

- Smooth 32X gameplay with excellent PAL and NTSC performance.
- Display output synchronised to **50 Hz PAL** and **59.98 Hz NTSC**.
- ARM Core1 execution of the PicoDrive engine, with 32X SH-2 recompilation.
- Recompiler stability adjustment for **Virtua Racing Deluxe**.
- AHI and direct Paula DMA audio, including 32X PWM, YM2612 FM and PSG.
- Workbench launcher, configurable ROM drawer, keyboard and gamepad input.
- Savestates, SRAM saving and clean return to AmigaOS.
- Complete corresponding source code and third-party licence notices.

## Compatibility

- Amiga with MNT ZZ9000 and Picasso96 (tested on A4000/68060).
- **XACP XX19c or later** and its matching `zz9000.card`.
  **XX19d-RIX or a later compatible update is recommended.**
- Sega 32X cartridge images in **`.32x` and `.bin`** format.
- AHI optional; direct Paula audio is available without AHI.

Savestates should be loaded with the **same region and audio backend** used
when saving; states from the other PAL/NTSC family are rejected.

## Distribution and credits

The Amiga ZIP contains the executable with embedded ARM Core1 program,
Workbench icon and installation documentation. The matching source ZIP
contains the Amiga 68k launcher, ARM Core1, PicoDrive, Cyclone and build
materials.

Port, launcher, Core1 integration and real-hardware validation: **Xanxi**.
PicoDrive: **notaz, irixxxx and contributors**; Cyclone 68000:
**Dave Haywood, notaz and irixxxx**. PicoDrive base derived from
**PokaMilkTea's VirtuaRacingDeluxeEnhanced**.

The historical **PicoDrive non-commercial licence** applies. No ROMs or
game data are included. This project is not affiliated with SEGA.
