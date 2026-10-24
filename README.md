# XACP v1.8

**eXtended ARM Coprocessor Protocol for the MNT ZZ9000 Amiga RTG board.**

XACP uses the ZZ9000 ARM Cortex-A9 cores and DDR memory as a general
coprocessor platform while AmigaOS handles host integration such as RTG,
input, files, GUI, AHI audio and CAMD MIDI.

Current public baseline:

```text
Firmware: XX19d-RIX
Protocol: XACP v1.8
```

Firmware build numbers and XACP protocol versions are separate.

## Why XACP?

The ZZ9000 is already an excellent RTG graphics card. XACP explores another
part of the hardware: its dual-core ARM processor and DDR memory.

The central idea is to make that processing power available to Amiga software,
allowing the ARM to act as an additional processor for demanding workloads
while the 68k and AmigaOS remain in control of the machine.

XACP has grown from early ARM/68k experiments in March and April 2026 and
public ARM fractal demonstrations on 7 May 2026 into a platform supporting
complete ARM-side engines, emulation, multimedia and persistent services
such as SoundFont MIDI synthesis.

**[Why XACP?  -  Origins, philosophy and goals](docs/WHY_XACP.md)**

## Architecture

### Core0 services

Persistent firmware-side services include MP3/MP2 decoding, ZZMIDI/SoundFont
synthesis and shared multimedia operations.

### Core1 applications

Complete engines can be dynamically loaded onto the second Cortex-A9. Current
projects include ZZDoom, ZZQuake, ZZQuake HighRes, ZZQuake2,
ZZDarkForcesNEXT, ZZPicoDrive, ZZRastan, ZZPPC, ZZSpeech,
ZZScreensavers (Earth, Julia and Solar Winds), ZZVGMPlay and
other experimental engines.

## XACP v1.8  -  RasterIX and image decoding

XX19d-RIX adds the RasterIX hardware 3D renderer to the ZZ9000 FPGA and
introduces XACP v1.8 with a dedicated JPEG/PNG image-service window. The
AmigaOS datatype clients are in development and will be released later.

RasterIX is now used by three separately released applications:

- [ZZQuake2 1.0](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZQuake2-1.0) - Quake II with software, TinyGL and FPGA RasterIX renderers.
- [ZZScreensavers 1.1](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZScreensavers-1.1) - FPGA-accelerated Solar Winds.
- [ZZOpenLara 1.0](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZOpenLara-1.0) - Tomb Raider I with software and FPGA RasterIX rendering.

The ZZQuake2 software and ARM TinyGL renderers, as well as the ZZOpenLara software renderer, remain compatible with XX19c. RasterIX acceleration requires XX19d-RIX.

- [Firmware XX19d-RIX release](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/XX19d-RIX)
- [Firmware and corresponding sources](firmware/README.md)
- [XACP v1.8 developer notes](docs/XACP_V1_8_DEVELOPER_NOTES.md)
- [Image service and GPU SDK](sdk/README.md)
- [RasterIX FPGA source guide](docs/FPGA_RASTERIX_SOURCE.md)

## XACP v1.7 (retained memory layout)

v1.7 preserves the XX19a / XACP v1.6 low DDR map and adds a large ARM-only
Core1 allocation:

```text
0x30000000 - 0x3F800000   248 MiB Core1 arena
0x3F800000 - 0x3FC00000     4 MiB guard
0x3FC00000 - 0x40000000     4 MiB firmware/hardware reserved
```

The legacy SMUSH Codec1/37/47 implementation is removed because its fixed
scratch buffers occupied the new arena; its ABI values remain reserved.

See:

```text
firmware/README.md
docs/XACP_V1_7_DEVELOPER_NOTES.md
sdk/xacp_memory_map_v1_7.h
```

## XX19c stability update (previous release)

XX19c supersedes XX19b while keeping the XACP v1.7 ABI and shared-memory map
unchanged.

It fixes a Core0/Core1 coexistence problem that could occur when a Core1
application was launched while a persistent Core0 service such as ZZMIDI was
active. On XX19c, the ZZMIDIGate workaround previously required for this
ZZDoom/Core1 startup conflict is no longer necessary.

XX19c also adds linker-time protection for the low firmware image so future
firmware growth cannot silently reach the framebuffer at `0x00200000`.

## Current firmware

```text
firmware/BOOT_XX19d-RIX.bin
```

Corresponding source:

```text
firmware/source/XX19d-RIX/
```

Historical builds and source remain archived for regression testing.

## Applications

| Project | Role |
|---|---|
| **ZZMIDI** | SoundFont MIDI synthesis on Core0 with AHI playback |
| **[ZZVGMPlay](applications/ZZVGMPlay/README.md)** | VGM/VGZ game music; arcade, console and computer sound-chip emulation on Core1 |
| **ZZPicoDrive** | Sega emulation on Core1 |
| **ZZPPC** | Experimental PPC32/FPU execution on Core1 |
| **ZZDoom** | Doom engine on Core1 |
| **ZZQuake** | WinQuake/quakegeneric software renderer on Core1 |
| **[ZZQuake2 1.0](applications/games/ZZQuake2/README.md)** | Quake II on Core1 with software, ARM TinyGL and FPGA RasterIX rendering |
| **[ZZOpenLara 1.0](applications/games/ZZOpenLara/README.md)** | Tomb Raider I on Core1 with ARM software and FPGA RasterIX rendering |
| **ZZQuake HighRes** | 640x480, 800x600 and 1024x768 Quake builds on Core1 |
| **ZZDarkForcesNEXT** | The Force Engine / Dark Forces on Core1 with enhanced TFE features |
| **ZZRastan** | Rastan arcade hardware recreation on Core1 |
| **ZZSpeech** | Speech synthesis acceleration |
| **[ZZScreensavers 1.1](applications/ZZScreensaver/README.md)** | Core1 Earth and Julia plus FPGA RasterIX-accelerated Solar Winds |
| **ZZ-MPEG** | MPEG-1 / MP2 playback |
| **ZZPlayGUI / MP3 tools** | ARM-accelerated MP3/MP2 decoding |
| **zz9000.engine** | AmigaAMP external XACP engine |
| **mpega.library integration** | MPEGA-compatible ARM decode path |
| **ZZBench GUI** | 68k/ARM/memory benchmarking |

Application material lives under `applications/`.

## FPGA Acceleration Demos

| Project | Description |
|---|---|
| **[XACP-GPU / ZZGearRIX](demos/ZZGearRIX/README.md)** | Experimental RasterIX FPGA 3D acceleration on a real Amiga 4000/68060 with ZZ9000. Demonstrated on 11 October 2026: 59.53 FPS displayed and 163-166 FPS offscreen in ZZGear. |

XACP-GPU uses the ZZ9000's Cortex-A9 Core1 and the FPGA
RasterIX pipeline to accelerate 3D rendering.

[Video, technical details and benchmarks](demos/ZZGearRIX/README.md)

Experimental GPU demos and associated documentation are stored
under `demos/`.

## Host drivers

| Driver | Role |
|---|---|
| **ZZ9000AX Fitzsteve Edition** | Two-channel variant of the MNT ZZ9000AX AHI 4.19 driver for XACP game ports using AHI SFX together with MP3 or ZZMIDI music |

Driver material lives under `drivers/`.

## Compatibility

XX19d-RIX / XACP v1.8 is the current public baseline. It retains the
v1.7 high Core1 arena. Applications targeting v1.7 must still check
runtime capabilities for new GPU and image-service functions.


Use application, firmware, shared-memory definitions and `zz9000.card`
combinations that have been explicitly validated together. After replacing
firmware or the card driver, perform a complete power-off before testing.

XX19c remains XACP v1.7-compatible with applications validated for XX19b,
unless an individual application documents a stricter requirement.

## Repository structure

```text
applications/   Amiga applications and Core1 programs
drivers/        AmigaOS host drivers and XACP compatibility variants
docs/           protocol/developer documentation
firmware/       current firmware, source and historical archive
sdk/            shared XACP headers
demos/          FPGA acceleration and technical demonstrations
CHANGELOG.md    project history
```

## Source and licensing

Firmware and individual applications have separate licensing requirements.
Some Amiga-side Xanxi applications are proprietary freeware, while GPL and
other third-party components retain their original licenses. Each application
directory documents its own source/licensing split.

## Credits

XACP / Xanxi, 2026.

Thanks to MNT Research and the ZZ9000/Amiga community for the hardware
platform, testing and technical discussion.
