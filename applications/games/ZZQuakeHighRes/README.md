# ZZQuake HighRes 1.0

ZZQuake HighRes is the high-resolution companion pack for ZZQuake on classic
Amiga systems equipped with an MNT ZZ9000.

The Quake engine and software renderer run on the ZZ9000 Cortex-A9 Core1
through XACP, while AmigaOS handles Picasso96 presentation, input, file access
and audio integration.

The original **ZZQuake 1.0 / 320x240** release remains separate and unchanged.

## Builds

| Launcher | Resolution | Timedemo demo1 |
| --- | ---: | ---: |
| `ZZQuake640` | 640x480 | 41.9 fps |
| `ZZQuake800` | 800x600 | 29.6 fps |
| `ZZQuake1024` | 1024x768 | 20.0 fps |

Measurements were obtained on the validated Amiga 4000/68060 + ZZ9000 setup
used during development.

## Requirements

- Classic Amiga with MNT ZZ9000
- XACP v1.7-compatible firmware
- Picasso96 RTG
- Original Quake game data

Commercial Quake data is not included.

## Repository contents

Each resolution directory contains the compiled Amiga launcher, its Workbench
icon, the matching Core1 blob, and the resolution-specific public Core1 source:

```text
640/
    ZZQuake640
    ZZQuake640.info
    zzq640.bin
    zz9000/

800/
    ZZQuake800
    ZZQuake800.info
    zzq800.bin
    zz9000/

1024/
    ZZQuake1024
    ZZQuake1024.info
    zzq1024.bin
    zz9000/
```

`quakegeneric_source/` contains the shared upstream engine source used by the
three variants.

The Amiga 68k launchers are proprietary freeware binaries. Their source is not
included in the public repository.

## Installation

Keep each launcher with its matching Core1 blob. Place your own Quake data in
`id1/` exactly as with the original ZZQuake 1.0 release:

```text
id1/pak0.pak
id1/pak1.pak        optional, for registered Quake
```

Launch the desired build from Workbench or Shell.

## Mission packs and mods

The same game-selection options as ZZQuake 1.0 are retained:

```text
HIPNOTIC
ROGUE
GAME=name
```

Place the corresponding data directories next to `id1/`. Quake game data and
mission-pack data are not distributed with this project.

## Music

The three ZZQuake music backends are retained:

```text
MUSIC=MP3
MUSIC=MHI
MUSIC=CD
```

MP3 replacement tracks use the standard ZZQuake layout under `id1/music/`,
for example `track02.mp3`, `track03.mp3`, and so on.

For MP3 and MHI playback, 128 kbit/s CBR MP3 files are recommended for best
stability.

## Workbench ToolTypes

The supplied icons contain the same normal user options as ZZQuake 1.0,
disabled by default:

```text
(HIPNOTIC)
(ROGUE)
(GAME=copper)
(MUSIC=MP3)
(MUSIC=MHI)
(MHIDRIVER=mhiamiblaster.library)
(MUSIC=CD)
(CDDEVICE=scsi.device)
(CDUNIT=1)
```

No resolution ToolType is required because each launcher is fixed to its own
resolution and loads its matching Core1 blob.

## Building the Core1 blobs

See `BUILD.md`.

The public build tree covers the GPL-covered Core1 side only.

## Source and licensing

The `zzq640.bin`, `zzq800.bin` and `zzq1024.bin` Core1 programs are derived
from GPL-covered Quake / quakegeneric source. Complete corresponding source and
build material is included in this directory.

The source is based on `erysdren/quakegeneric` commit:

```text
13052102577c629650cf07a46151a4b6e1b19c3c
```

See `COPYING`, `LICENSE.quakegeneric`, `UPSTREAM.txt` and `BUILD.md`.

The Amiga 68k launchers are separate proprietary freeware components. Their
source is not included in the public repository.

No Quake PAK files, music or other commercial game assets are included.

## Credits

ZZQuake / XACP port and HighRes variants: **Xanxi, 2026**.

Based on Quake and quakegeneric.
