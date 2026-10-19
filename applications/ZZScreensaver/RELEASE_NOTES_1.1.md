# ZZScreensavers 1.1 — Solar Winds with RasterIX

ZZScreensavers 1.1 adds **Solar Winds**, based on Terence M. Welsh's Really Slick Screensavers, to the existing Earth and Julia screensavers for classic AmigaOS 3.x equipped with the MNT ZZ9000.

The new Solar Winds renderer uses ZZ9000 Cortex-A9 Core1 and RasterIX hardware acceleration integrated into the FPGA by XACP firmware XX19d-RIX. Earth and Julia executables are carried forward unchanged from 1.0.

## What's new

- `ZZScreensaverS`: Solar Winds, 640x480 RGB565, FPGA RasterIX renderer.
- Six selectable effects: Regular, Cosmic Strings, Cold Pricklies, Space Fur, Jiggly and Undertow.
- `ZZScreensaverHotkeys` 1.2: Ctrl-Alt-S starts Solar Winds with a random preset from **1, 2, 3, 4 or 6** (never 5, no immediate repeat of the last accepted request).
- Solar Winds launches silently by default and implements an invisible Intuition pointer with cleanup/restoration. `DEBUG` enables diagnostics.
- Existing Ctrl-Alt-E and Ctrl-Alt-J remain supported.

The helper is a Commodities shortcut manager, **not an automatic inactivity-triggered screensaver**. No claim of 60 FPS for every preset is made.

## Requirements

- Classic AmigaOS 3.x, MNT ZZ9000, Picasso96 and XACP-compatible `zz9000.card`.
- **XACP firmware XX19d-RIX** for Solar Winds hardware acceleration. Earth and Julia retain support for XX19b and later.
- Only one dynamically loaded Core1 program may run at a time.

## Installation

Copy `ZZScreensaverE`, `ZZScreensaverJ`, `ZZScreensaverS`, and `ZZScreensaverHotkeys` from the LHA archive to `C:`. Keep the existing line in `S:User-Startup` or add:

```text
Run >NIL: C:ZZScreensaverHotkeys
```

Close the old commodity in Exchange before launching 1.2, or reboot. To choose a manual Solar Winds preset:

```text
ZZScreensaverS PRESET 6
```

Use `ZZScreensaverS DEBUG PRESET 3` for diagnostic output. Preset 5 is still available manually.

## Downloads

- `ZZScreensavers-1.1.lha` — Amiga executables and documentation.
- `ZZScreensavers-1.1-GPL-source.zip` — corresponding Solar Winds GPL source, including 68k launcher, ARM Core1 renderer and complete offline build scripts.
- `ZZScreensavers-1.1-SHA256SUMS.txt` — SHA-256 archive checksums.

## License and credits

Earth, Julia and the separate Hotkeys commodity are proprietary freeware by Xanxi. Solar Winds is a GPL-2.0-or-later derivative of the program by Terence M. Welsh, with an Amiga/Core1 adaptation by Xanxi. The original copyright notices and a copy of GPL v2 are included, together with source code. NASA Blue Marble imagery in Earth retains separate credit; MNT ZZ9000 is a product of MNT Research GmbH. RasterIX GPU by ToNi3141.

Technical source and instructions: https://github.com/Xanxi-Amiga/XACP-ZZ9000/tree/main/applications/ZZScreensaver
Firmware: https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/XX19d-RIX
