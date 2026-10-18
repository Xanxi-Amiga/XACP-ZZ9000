# ZZVGMPlay v1.0

**(c) Xanxi 2026**

**VGM/VGZ music player for AmigaOS 3.x, accelerated by the ZZ9000 and XACP Cortex-A9 Core1.**

Enjoy original arcade, console and computer game music on your classic Amiga. The 68k application provides a native GadTools interface, file handling, playlists and AHI output, while sound-chip emulation and PCM mixing run on the ZZ9000 Cortex-A9 Core1.

**Looking for music? Visit [VGMRips.net](https://vgmrips.net/) for downloadable VGM/VGZ game soundtrack packs.** Extract the packs and add the `.vgm` or `.vgz` files to the ZZVGMPlay playlist. Playback depends on the sound chips used by each pack.

## Features

- Native AmigaOS 3.x GadTools interface with playback controls and progress display.
- Support for `.vgm` and gzip-compressed `.vgz` music files.
- Playlist: Add, Delete, Move, Clear, Previous/Next and Repeat.
- Remembered last directory and configurable default music drawer from the **Settings** menu.
- Optional live diagnostic counters from **Debug > Show WRITES / UND**, hidden by default.

## Supported systems and sound chips

ZZVGMPlay supports a range of VGM sound hardware used in arcade machines, consoles and computers, including:

- **Arcade:** Sega System 18, Neo Geo (YM2610), and selected YM2151, OPL and other arcade sound systems.
- **Sega consoles:** Mega Drive / Genesis (YM2612 and SN76489) and Master System FM (YM2413).
- **Other consoles:** NES APU, Game Boy APU and PC Engine PSG.
- **Computers:** Atari ST (AY-3-8910/YM2149), MSX (SCC/SCC+) and Apple IIGS (ES5503).
- **Other sound chips:** OPL2/OPL3, YM2203 and additional supported VGM chip implementations.

Support is determined by the VGM commands and sound-chip configuration used by each file; not every VGM pack is necessarily compatible.

## Requirements

- AmigaOS 3.x, a supported 68k Amiga and a ZZ9000 with **XACP firmware XX19c or later**.
- AHI audio support.
- The matching pair `ZZVGMPlay` (68k executable) and `zzvgm_core1.bin` (ARM payload) from the **same build**.

The firmware XACP and its shared-memory ABI are not modified by ZZVGMPlay. No game music is included in the source distribution.

## Build from source - offline MSYS

All required third-party source files are included locally. From the project directory:

```sh
sh build_all.sh
```

Expected outputs:

```text
amiga/ZZVGMPlay
amiga/zzvgm_core1.bin
```

Copy both files to the same directory on your Amiga. No network downloads, `git clone`, `curl`, `wget` or PATH overrides are required by the build scripts.

Reference Windows/MSYS toolchains:

```text
C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-gcc
C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-g++
C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-objcopy
C:/amiga-gcc/bin/m68k-amigaos-gcc
```

The 68k link uses `-mcrt=nix20`. Other installations may need to adapt these paths.

## Tests and development

Host-side regression tests and supporting files are available under `tests/`. The application and Core1 payload should always be deployed from the same build.

## Source, credits and notices

ZZVGMPlay (c) Xanxi 2026. Third-party sound-emulation engines retain their original copyright, licensing notices and comments, including comments in their original languages. See the respective source headers, `third_party/` and `licenses/`.

See [RELEASE_NOTES.md](RELEASE_NOTES.md) for the v1.0 release summary.
