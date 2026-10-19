# ZZQuake HighRes 1.0

High-resolution ZZQuake builds for classic Amiga systems equipped with an MNT ZZ9000.

The Quake software renderer runs on ZZ9000 Cortex-A9 Core1 through XACP. AmigaOS handles Picasso96 presentation, input, file access and audio integration.

This pack is separate from the original ZZQuake 1.0 320x240 release.

| Build | Resolution | Timedemo demo1 |
| --- | ---: | ---: |
| ZZQuake640 | 640x480 | 40.9 fps |
| ZZQuake800 | 800x600 | 29.6 fps |
| ZZQuake1024 | 1024x768 | 20.0 fps |

The measurements above are from the validated Amiga 4000/68060 + ZZ9000 setup used for this package.

## Contents

- `quakegeneric_source/` - shared GPL engine source.
- `640/zz9000/`, `800/zz9000/`, `1024/zz9000/` - resolution-specific Core1 source and build files.
- `640/zzq640.bin`, `800/zzq800.bin`, `1024/zzq1024.bin` - validated Core1 blobs.

The proprietary Amiga 68k launcher source is not included in the public repository or source archive.

## Requirements

- Classic Amiga with MNT ZZ9000.
- XACP v1.7-compatible firmware.
- Picasso96.
- Original Quake game data.

Commercial Quake data is not included.

## Build

See `BUILD.md`.

## Licensing

Quake and the included quakegeneric-derived engine source are distributed under GPL-2.0-or-later. See `COPYING`, `LICENSE.quakegeneric` and `UPSTREAM.txt`.
