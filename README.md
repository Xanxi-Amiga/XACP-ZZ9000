# XACP v1.7

**eXtended ARM Coprocessor Protocol for the MNT ZZ9000 Amiga RTG board.**

XACP uses the ZZ9000 ARM Cortex-A9 cores and DDR memory as a general
coprocessor platform while AmigaOS handles host integration such as RTG,
input, files, GUI, AHI audio and CAMD MIDI.

Current public baseline:

```text
Firmware: XX19b
Protocol: XACP v1.7
```

Firmware build numbers and XACP protocol versions are separate.


## Why XACP?

The ZZ9000 is already an excellent RTG graphics card. XACP explores another
part of the hardware: its dual-core ARM processor and DDR memory.

The central idea is to make that processing power available to Amiga software,
allowing the ARM to act as an additional processor for demanding workloads
while the 68k and AmigaOS remain in control of the machine.

XACP has grown from early ARM/68k experiments and public fractal demonstrations
in May 2026 into a platform supporting complete ARM-side engines, emulation,
multimedia and persistent services such as SoundFont MIDI synthesis.

**[Why XACP? — Origins, philosophy and goals](docs/WHY_XACP.md)**


## Architecture

### Core0 services

Persistent firmware-side services include MP3/MP2 decoding, ZZMIDI/SoundFont
synthesis and shared multimedia operations.

### Core1 applications

Complete engines can be dynamically loaded onto the second Cortex-A9.  Current
projects include ZZDoom, ZZQuake, ZZPicoDrive, ZZRastan, ZZPPC, ZZSpeech and
other experimental engines.

## XACP v1.7

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

## Current firmware

```text
firmware/BOOT_XX19b.bin
```

Corresponding source:

```text
firmware/source/XX19b/
```

Historical builds and source remain archived for regression testing.

## Applications

| Project | Role |
|---|---|
| **ZZMIDI** | SoundFont MIDI synthesis on Core0 with AHI playback |
| **ZZPicoDrive** | Sega emulation on Core1 |
| **ZZPPC** | Experimental PPC32/FPU execution on Core1 |
| **ZZDoom** | Doom engine on Core1 |
| **ZZQuake** | WinQuake/quakegeneric software renderer on Core1 |
| **ZZRastan** | Rastan arcade hardware recreation on Core1 |
| **ZZSpeech** | Speech synthesis acceleration |
| **ZZ-MPEG** | MPEG-1 / MP2 playback |
| **ZZPlayGUI / MP3 tools** | ARM-accelerated MP3/MP2 decoding |
| **zz9000.engine** | AmigaAMP external XACP engine |
| **mpega.library integration** | MPEGA-compatible ARM decode path |
| **ZZBench GUI** | 68k/ARM/memory benchmarking |

Application material lives under `applications/`.

## Host drivers

| Driver | Role |
|---|---|
| **ZZ9000AX Fitzsteve Edition** | Two-channel variant of the MNT ZZ9000AX AHI 4.19 driver for XACP game ports using AHI SFX together with MP3 or ZZMIDI music |

Driver material lives under `drivers/`.

## Compatibility

Use application, firmware, shared-memory definitions and `zz9000.card`
combinations that have been explicitly validated together.  After replacing
firmware or the card driver, perform a complete power-off before testing.

## Repository structure

```text
applications/   Amiga applications and Core1 programs
drivers/        AmigaOS host drivers and XACP compatibility variants
docs/           protocol/developer documentation
firmware/       current firmware, source and historical archive
sdk/            shared XACP headers
CHANGELOG.md    project history
```

## Source and licensing

Firmware and individual applications have separate licensing requirements.
Some Amiga-side Xanxi applications are proprietary freeware, while GPL and
other third-party components retain their original licenses.  Each application
directory documents its own source/licensing split.

## Credits

XACP / Xanxi, 2026.

Thanks to MNT Research and the ZZ9000/Amiga community for the hardware
platform, testing and technical discussion.
