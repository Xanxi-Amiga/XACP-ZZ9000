# ZZVGMPlay v1.0

**(c) Xanxi 2026**  
VGM/VGZ music player for **AmigaOS 3.x** with audio emulation accelerated by the **ZZ9000 / XACP Cortex-A9 Core1**.

ZZVGMPlay lets a classic Amiga play music from arcade games, consoles and other sound-chip systems. The 68k application handles the native Amiga interface, file access, playlists and AHI output; chip emulation and PCM mixing run on Core1.

## Features

- Native AmigaOS 3.x player with a compact GadTools interface, transport controls and progress display.
- VGM and compressed VGZ support.
- Playlist with Add, Delete, Move, Clear, Previous/Next and Repeat.
- Remembered last directory and configurable default music drawer (**Settings** menu).
- FM and PSG playback including OPL2/OPL3, YM2612, YM2151, AY-3-8910/YM2149, YM2413, SN76489, and additional arcade/console sound engines.
- Support for Konami SCC/SCC+, YM2610, RF5C68, ES5503 and selected other chips (subject to format and chip-specific limitations).
- **Sega System 18 / Moonwalker:** dual YM2612 + RF5C68, using an isolated libvgm-based backend. The dedicated System 18 output path has a +6.02 dB final gain, without changing the FM/PCM balance.
- **Debug > Show WRITES / UND** toggles the live counters. The normal display is clean, and the debug view is off by default.

The firmware XACP and its shared-memory ABI are **not modified** by this application.

## Requirements

- AmigaOS 3.x, a supported 68k Amiga and a ZZ9000 with the compatible XACP/Core1 environment.
- AHI audio support.
- The matching pair `ZZVGMPlay` (68k executable) and `zzvgm_core1.bin` (ARM payload) from the **same build**.

This source package does not ship prerecorded game music or a prebuilt ARM/68k executable.

## Build from source - offline MSYS

All required third-party source files are bundled locally. The supplied scripts are configured for the project's known Windows/MSYS cross-compilers. From the repository root:

```sh
sh build_all.sh
```

Expected outputs:

```text
amiga/ZZVGMPlay
amiga/zzvgm_core1.bin
```

Copy both files to the same directory on your Amiga. No source downloads, `git clone`, `curl`, `wget`, PATH overrides or additional build preparation are performed by the scripts.

The reference toolchains are:

```text
C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-gcc
C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-g++
C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-objcopy
C:/amiga-gcc/bin/m68k-amigaos-gcc
```

The 68k link uses `-mcrt=nix20`. Builders using a different directory layout must adjust these tool paths locally. The Cortex-A9 linker script provides ARM exception-index sections.

## Debug menu

**Debug > Show WRITES / UND** is a checkmarked, non-modal display option. Enabling it shows the current write count and audio underruns in the third row while music is playing. When paused, the loop count is also available. The normal interface does not display these diagnostic counters.

## System 18 testing

All **22 tracks** of the privately held Sega System 18 Moonwalker test pack passed a PC-side test of the actual Core1 parser and stereo mixer at 44.1 kHz. The production mixer reproduced the recorded pre-gain references and the post-gain output had **zero clipped sample channels** across the test set. The largest sample peak was 25,496 out of 32,767. This is not a measurement of intersample peaks or a substitute for testing on the ZZ9000.

The test references and reproducibility script are under `tests/host/`. Music files are intentionally excluded. If you already have the original private test pack, the host test can be rerun with:

```sh
python3 tests/host/check_production.py --music-zip /path/to/Moonwalker.zip
```

## Source, credits and notices

ZZVGMPlay (c) Xanxi 2026. The project contains third-party sound-emulation engines with their own original copyright and licensing notices. See the respective source headers, `third_party/` and `licenses/` for retained notices. The included references remain attributed to their original authors.

See [RELEASE_NOTES.md](RELEASE_NOTES.md) for the v1.0 feature summary.
