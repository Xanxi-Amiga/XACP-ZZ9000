# ZZScreensaverS — Solar Winds for ZZ9000/XACP

Solar Winds is a hardware-accelerated screensaver for classic AmigaOS 3.x on a ZZ9000 with XACP XX19d-RIX firmware. It renders at 640x480 RGB565 using the Cortex-A9 Core1 and FPGA RasterIX.

This directory contains the corresponding source tree for the `ZZScreensaverS` Amiga executable distributed with ZZScreensavers 1.1: the Amiga 68k launcher, ARM Core1 application, Solar Winds simulation, librixgpu/ZZGL interfaces, and offline build scripts. The exact release executables are published separately. A bit-for-bit reproducible build has not been independently demonstrated.

## Local offline build

Under Windows/MSYS with the preinstalled local ARM Cortex-A9 and Amiga 68k cross-compilers:

```sh
sh build_all.sh
```

Toolchains are referenced via explicit `C:/...` paths; no PATH override and no download are required. Linker command for Amiga 68k uses `-mcrt=nix20`. The build generates `ZZScreensaverS` (the ARM payload is embedded in the 68k executable) and `ZZSolarWindsRIX` (an identical compatibility copy). Neither is a replacement for the separately supplied, hardware-tested release binary without verification.

## Launch

```text
ZZScreensaverS
ZZScreensaverS PRESET 2
ZZScreensaverS PRESET 6
ZZScreensaverS DEBUG PRESET 3 > RAM:ZZSW_A1_3.txt
```

There are six individual presets: 1 Regular, 2 Cosmic Strings, 3 Cold Pricklies, 4 Space Fur, 5 Jiggly, 6 Undertow. The separate Hotkeys 1.2 commodity randomly selects only 1, 2, 3, 4 or 6 on Ctrl-Alt-S; it is not part of this GPL source directory. Preset 5 remains manually selectable. Solar Winds is silent by default and implements Intuition pointer hiding/restoration. Hardware validation of the specific 1.1 binaries is distinct from code inspection and host simulation.

## Licensing

Original Solar Winds by Terence M. Welsh, Copyright (C) 1999–2010.
Amiga/Core1 adaptation by Xanxi, Copyright (C) 2026.
Solar Winds derivative sources and the resulting Solar Winds executable are distributed under **GNU GPL version 2 or later**; see `COPYING`. Other internally developed files contain 0BSD notices and are GPL-compatible. Upstream reference sources under `upstream/solarwinds/` preserve their original credits and notices. `LICENSES/0BSD.txt` contains the additional applicable permissive license. These terms do not change the proprietary freeware license of the separate Earth, Julia, or Hotkeys executables.

Source provenance: see `PROVENANCE.md`.
