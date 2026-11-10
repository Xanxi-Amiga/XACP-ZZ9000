# ZZQuakeGL 1.0

ZZQuakeGL runs **Quake 1 with id Software's original GLQuake renderer**, hardware-accelerated by **RasterIX** in the MNT ZZ9000 FPGA. The Quake engine and renderer run on the ZZ9000's ARM Cortex-A9 Core1; a separate Amiga 68k launcher handles Picasso96 display, input, files and audio.

Four resolutions are supported without changing the firmware or FPGA bitstream.

## Performance

`timedemo demo1`, Amiga 4000/68060 with ZZ9000, 16-bit Picasso96 screen:

| Resolution | FPS |
| --- | ---: |
| 320 x 240 | 106.5 |
| 640 x 480 | 48.0 |
| 800 x 600 | 35.2 |
| 1024 x 768 | 24.1 |

Results are measurements on the development machine, not guarantees for other systems. The 1024 x 768 build uses a dedicated DDR framebuffer layout.

## Gameplay video

[![ZZQuakeGL 1.0 - RasterIX hardware-accelerated GLQuake gameplay](https://img.youtube.com/vi/cV312wjwoPc/hqdefault.jpg)](https://youtu.be/cV312wjwoPc)

[Watch ZZQuakeGL 1.0 on YouTube](https://youtu.be/cV312wjwoPc)

## Requirements

- AmigaOS 3.0 or later, Picasso96, Amiga with MNT ZZ9000
- XACP-compatible `zz9000.card` and **XX19d-RIX** firmware (or another firmware explicitly compatible with its RasterIX G4C interface)
- Original Quake data, shareware or registered; game PAK files are not included

## Installation

Place the following files in one Amiga drawer:

```text
ZZQuakeGL
ZZQuakeGL.info
zzqgl.bin         640 x 480
zzqgl_320.bin     320 x 240
zzqgl_800.bin     800 x 600
zzqgl_1024.bin    1024 x 768
```

Copy your own `id1/pak0.pak` into `id1/`; add `id1/pak1.pak` for registered Quake. The same game-data drawer used by ZZQuake640 can be used, including saves and configuration.

The Workbench icon defaults to **640 x 480**, **MUSIC=MP3** and
**MAXFPS=50**. Shell launch defaults to 640 x 480 as well:

```text
ZZQuakeGL          640 x 480
ZZQuakeGL 320      320 x 240
ZZQuakeGL 800      800 x 600
ZZQuakeGL 1024     1024 x 768
```

The default screen depth is 16-bit; `DEPTH=32` requests a 32-bit screen. Music options include `MUSIC=MP3`, `MUSIC=MHI` and `MUSIC=CD`.

## Source and licensing

The **Core1 engine blobs are open source**. Complete corresponding Core1 sources, original GLQuake files, the ZZQuake640 Core1 runtime, the ZZGL/librixgpu adapter and the offline build script are under `source/ZZQuakeGL/` alongside `source/Quake-master/` and `source/ZZQuake640_SOURCES_launcher_and_Core1/`. The same source tree is also available as `source/ZZQuakeGL_1.0_src.zip`.

The **68k launcher is separately distributed as a proprietary freeware executable**. Its private source files are not included in this repository. Quake data and commercial assets are never redistributed.

Quake and GLQuake: id Software (GNU GPL v2). quakegeneric: erysdren (GNU GPL v2). RasterIX: ToNi3141. ZZQuakeGL/XACP port, ZZGL and librixgpu: Xanxi (port sources GPL; ZZGL/librixgpu 0BSD). ZZ9000 hardware: MNT Research. Individual source files retain their licence identifiers.

## Known limitations

**ZZQuakeGL 1.0 supports the original Quake game (`id1`) only.** The *Scourge of Armagon* (`HIPNOTIC`) and *Dissolution of Eternity* (`ROGUE`) mission packs, and custom mods selected with `GAME=name` (including Copper), are **not supported**. Use the original software-rendered ZZQuake for these games. Support may be added in a future update. The updated 68k launcher rejects these options explicitly.

## Release files

The five executables in this directory are the validated Windows/MSYS builds
for version 1.0. `ZZQuakeGL.info` defaults to 640 x 480, `MUSIC=MP3` and
`MAXFPS=50`. Other settings can be selected through Workbench Icon Information.
`SHA256SUMS` records the exact files in this repository.

[The GitHub release](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZQuakeGL-1.0) offers an Amiga LHA with the binaries and documentation,
plus the complete Core1 GPL source ZIP.
