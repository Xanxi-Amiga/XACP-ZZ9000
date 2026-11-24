# ZZScreensavers 1.2 - Digital Rain / RasterIX

ZZScreensavers 1.2 adds MatrixView digital rain to the existing Earth, Julia and Solar Winds screensavers for classic AmigaOS and the ZZ9000/XACP. ZZScreensaverR is the seventh released XACP application to use RasterIX FPGA acceleration.

## What's new

- ZZScreensaverR 1.0: 640x480 RGB565 digital rain, rendered by Cortex-A9 Core1 with RasterIX blending the flares, at approximately 60 FPS across all three presets on the tested A4000/68060.
- Three selectable grids: 80x60, 64x48 (default) and 40x30.
- ZZScreensaverHotkeys 1.3: Ctrl-Alt-R selects a random grid, avoiding immediate repeats. Earth, Julia and Solar Winds shortcuts are unchanged.
- Digital Rain exits on keyboard/mouse activity or when its screen is no longer in front.

## Video demonstration

[![ZZScreensaverR - Digital Rain at 60 FPS](https://img.youtube.com/vi/gXnYjHUTfBw/hqdefault.jpg)](https://youtu.be/gXnYjHUTfBw)

## Installation

This is a complete package; no previous installation is required. Quit the old Hotkeys commodity in Exchange if running, copy the five executables to `C:`, then restart the commodity or reboot. The existing `S:User-Startup` line is unchanged:

```text
Run >NIL: C:ZZScreensaverHotkeys
```

Classic AmigaOS 3.x, ZZ9000, Picasso96 and a matching XACP driver are required. Digital Rain requires XX19d-RIX or later; reported hardware measurements were made on XX19e-RIX. Only one Core1 program may run at a time. Disable competing screen blankers while Digital Rain is running.

## Downloads and licensing

- `ZZScreensavers-1.2.lha`: complete Amiga package with all four savers, Hotkeys 1.3, original Earth/Julia icons, documentation and notices.
- `ZZScreensaverR-1.0-GPL-source.zip`: corresponding Digital Rain source and offline build scripts.
- `ZZScreensavers-1.2-SHA256SUMS.txt`: release checksums.

Digital Rain is based on MatrixView by Alex Zolotov, with changes by Tugrul Galatali and the MPlayer port by Pigeon. It is distributed under GPL-2.0-or-later. Solar Winds remains GPL-2.0-or-later and its unchanged corresponding source remains available in the 1.1 release. Earth, Julia and Hotkeys remain proprietary freeware by Xanxi. Original notices are retained.
