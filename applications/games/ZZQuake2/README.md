# ZZQuake2 - Quake II for AmigaOS 3.x and ZZ9000

ZZQuake2 is a Quake II port for classic 68k Amigas equipped with the
MNT ZZ9000 running XACP firmware. The AmigaOS launcher manages host
integration; the game engine and renderers run on the ZZ9000 ARM
Cortex-A9 Core1.

The Amiga 68k launcher 0.8.6, GUI 0.1.2 and all eight distributed
Core1 binaries were verified against the final source MD5s from the
author's A4000 on 19 October 2026. These are the eight renderer builds
supported by the public GUI.

## Rendering modes

| Renderer | Core1 version | Available output modes |
| --- | --- | --- |
| Software | 0.8.5 | 320x240, 640x480, 800x600 |
| ARM TinyGL | 0.6.1 | 320x240, 640x480 |
| RasterIX FPGA | 0.10.3 | 320x240, 640x480, 800x600 |

The software and ARM TinyGL renderers are compatible with XX19c firmware.
**Upgrading to XX19d-RIX (XACP v1.8) is recommended** to benefit from the
RasterIX hardware-accelerated OpenGL renderer integrated into the ZZ9000
FPGA. RasterIX requires XX19d-RIX or a later explicitly compatible XACP
firmware; TinyGL itself runs in software on the ARM Core1.

## Requirements

- AmigaOS 3.x, supported 68k Amiga, MNT ZZ9000 and Picasso96.
- The XACP-compatible `zz9000.card` is REQUIRED when using XACP.
  Users moving from non-XACP firmware must install the driver supplied
  with the XACP firmware; it is unchanged from earlier XACP releases.
- An XACP-compatible firmware. Software and TinyGL are compatible with
  XX19c; XX19d-RIX is recommended for its hardware RasterIX renderer.
- Quake II demo or full-game data from a lawful source. No PAK files are
  included with ZZQuake2.
- Optional MP3 soundtrack files encoded at **128 kbps maximum**, using a
  supported music backend. Audio can also use the supported CD/MHI paths.

## Installation

Extract `ZZQuake2-1.0.lha` and keep the Amiga launcher
`ZZQuake2_A0`, `ZZQuake2GUI` and **all eight `.bin` files in the same
`ZZQuake2/` directory**. Do not move the Core1 `.bin` files into
`baseq2/` or separate renderer directories.

`ZZQuake2_A0` is the historical filename of the 68k launcher. The `A0`
suffix remains from development; the executable supplied with version 1.0
is **launcher v0.8.6**, not an early test build. Keep its filename unchanged.

Add your own game data and, optionally, music as follows:

```text
ZZQuake2/
  ZZQuake2_A0                 (Amiga 68k launcher)
  ZZQuake2GUI                 (Amiga GUI)
  zzq2a0.bin                  (software 640x480)
  zzq2_320.bin                (software 320x240)
  zzq2_800.bin                (software 800x600)
  zzq2tgl.bin                 (ARM TinyGL 640x480)
  zzq2tgl_320.bin             (ARM TinyGL 320x240)
  zzq2rix.bin                 (RasterIX 640x480)
  zzq2rix_320.bin             (RasterIX 320x240)
  zzq2rix_800.bin             (RasterIX 800x600)
  baseq2/
    pak0.pak                   (Quake II demo data)
    full/
      pak0.pak                 (Quake II full-game data; recommended location)
    music/
      track02.mp3              (optional music)
      track03.mp3
      ...
```

- **Demo:** put the demo `pak0.pak` in `ZZQuake2/baseq2/`.
- **Full game:** we recommend putting the full-game `pak0.pak` in
  `ZZQuake2/baseq2/full/` so that the demo and registered data can
  coexist. Select **Full pak** in `ZZQuake2GUI` to use the full game.
  Both demo and full-game data are supported by launcher 0.8.6.
- **MP3 music:** put your own MP3 tracks in `ZZQuake2/baseq2/music/`,
  named `track02.mp3`, `track03.mp3`, etc. Encode them at **no more
  than 128 kbps**. Select the MP3 music backend in the launcher/GUI.
  Commercial soundtrack files are not distributed with the port.

Start `ZZQuake2GUI` to select the renderer, resolution and game data,
or run the supplied `ZZQuake2_A0` launcher directly. The complete
release LHA includes the matching eight Core1 executables; no separate
Core1 download or manual blob pairing is required.

## Current release-component versions

- ZZQuake2_A0 launcher: 0.8.6 (Amiga 68k freeware executable)
- ZZQuake2GUI: 0.1.2 (Amiga 68k freeware executable)
- Software Core1 blobs: 0.8.5
- ARM TinyGL Core1 blobs: 0.6.1
- RasterIX Core1 blobs: 0.10.3

The versions and all eight Core1 blobs included in the Amiga LHA
were verified by hash from the author's A4000 on 19 October 2026.
An earlier tester LHA had older Core1 binaries and is not a suitable
public release artifact.

Save-game and audio functions are included in the final candidate.
Quake II cinematic playback is NOT included in this release.

## Source and license

**The source code and build scripts for the ARM Core1 engine blobs
are provided** under [`source/`](source/) in this repository, and in
the separate `ZZQuake2-1.0-GPL-source.zip` release archive. The eight
released binaries are built from that public source tree.

Quake II engine: id Software (GPL v2), via ozkl/quake2generic.
ZZQuake640 Core1 foundation: id Software, erysdren and contributors
(GPL v2). TinyGL: Fabrice Bellard (MIT-style license).
ZZQuake2 adapters: Xanxi (GPL-2.0-or-later).
librixgpu and ZZGL: Xanxi (0BSD).
RasterIX FPGA: ToNi3141; XACP integration: Xanxi.

The 68k launcher and GUI are provided as **freeware executables**.
Their source code is not part of the public distribution; the GPL
source release covers the ARM Core1 game engines. Third-party copyrights
and license notices remain in their respective source trees.

This independent project is not an official MNT Research or id Software
product. It contains no Quake II commercial game data or CD music.

