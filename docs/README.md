# XACP Documentation

This directory contains the architecture, protocol, developer and historical documentation for the **eXtended ARM Coprocessor Protocol (XACP)** used by Xanxi firmware and applications for the MNT ZZ9000.

The current public baseline is:

```text
Firmware: XX19d-RIX
Protocol: XACP v1.8
```

Firmware build numbers and XACP protocol versions are separate.

```text
XX19d-RIX  = public firmware build
XACP v1.8  = protocol / ABI / shared-memory baseline
```

---

## Project overview

For the origins, design philosophy and purpose of XACP, see:

**[Why XACP?](WHY_XACP.md)**

XACP started from experiments performed during March and April 2026 exploring direct use of the ZZ9000 ARM processors from Amiga software.

The first public demonstration of the concept was made on **7 May 2026**, when ARM-generated fractal demos were shown in an AmiBay development thread.

The project has since evolved into a general Amiga/ARM coprocessor environment supporting persistent firmware services and complete Core1 application engines.

Technical documentation in this directory describes how that environment is implemented.

---

# Current XACP baseline

## XACP v1.8 - XX19d-RIX

XX19d-RIX is the current public firmware for the MNT ZZ9000. It includes
RasterIX FPGA hardware 3D acceleration and the XACP v1.8 JPEG/PNG image
service. The corresponding ARM firmware, FSBL, FPGA RTL and Vivado project
are provided with the firmware release and in the repository source tree.

XACP v1.8 preserves the v1.7 high Core1 arena and established service
allocations. It adds a dedicated 1 MiB image-service transport window;
applications should use the v1.8 SDK and query runtime capabilities.

The AmigaOS JPEG/PNG datatypes remain separate development projects.
ZZQuake2 1.0 and ZZScreensavers 1.1 (Solar Winds) have been released as
separate application packages; neither is bundled in the firmware archive.

Current references:

- [XACP v1.8 developer notes](XACP_V1_8_DEVELOPER_NOTES.md)
- [RasterIX FPGA and Vivado sources](FPGA_RASTERIX_SOURCE.md)
- [XACP SDK](../sdk/README.md)
- [Firmware release XX19d-RIX](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/XX19d-RIX)
- [ZZQuake2 1.0 - Quake II](../applications/games/ZZQuake2/README.md)
- [ZZScreensavers 1.1 - Solar Winds](../applications/ZZScreensaver/README.md)


## XACP v1.7 (previous public ABI)

XACP v1.7 was the previous public ABI.

It preserves the coordinated low-DDR layout introduced with XACP v1.6 and adds a large ARM-only Core1 application arena.

The high DDR layout is:

```text
0x30000000 - 0x3F800000   248 MiB   Core1 application arena
0x3F800000 - 0x3FC00000     4 MiB   guard region
0x3FC00000 - 0x40000000     4 MiB   firmware / hardware reserved
```

The large Core1 arena allows increasingly substantial ARM-side applications to use predictable private memory without introducing independent application-specific DDR allocations.

The legacy SMUSH Codec1/37/47 implementation was removed from the current firmware because its fixed scratch buffers occupied this region.

The historical ABI values remain reserved.

---

## Historical v1.7 developer references

For development against the earlier XACP v1.7 baseline, use:

```text
XACP_V1_7_DEVELOPER_NOTES.md
../sdk/xacp_memory_map_v1_7.h
../sdk/README.md
../firmware/README.md
```

The authoritative reference for the current v1.8 memory allocations is:

```text
../sdk/xacp_memory_map_v1_8.h
```

New XACP software should use the common SDK definitions rather than introducing independent hard-coded DDR allocations.

---

# Relationship between XACP v1.6 and v1.7

XACP v1.7 is an evolution of the coordinated memory model introduced by XACP v1.6.

The v1.6 work established the common low-memory layout used by Core0 services and existing XACP applications.

It formalized allocations for areas including:

```text
command and streaming buffers
MP3 / PCM rings
ZZMIDI shared memory
Core1 application data
ARM-private application memory
SoundFont and MIDI data
save-state areas
firmware-private memory
```

XACP v1.7 keeps this established low-memory architecture and extends the platform with the new large Core1 high-memory arena.

For that reason, the following document remains useful architectural background:

```text
XACP_V1_6_DEVELOPER_DOCUMENTATION.md
```

However, it is **not** the normative memory-map reference for new software.

For current development, use:

```text
../sdk/xacp_memory_map_v1_8.h
```

---

# Architecture

XACP uses the ZZ9000 ARM processors in two complementary ways.

## Core0 services

Core0 runs the main firmware and can provide persistent ARM-assisted services to Amiga software.

Current examples include:

```text
MP3 / MP2 decoding
ZZMIDI SoundFont synthesis
shared multimedia operations
```

These services remain available while the Amiga continues to run normally.

---

## Core1 applications

Complete application-specific ARM engines can be loaded onto the second Cortex-A9.

Examples include:

```text
ZZDoom
ZZQuake
ZZDarkForcesNEXT
ZZPicoDrive
ZZRastan
ZZSpeech
ZZPPC
JuliaV2
```

The Amiga side remains responsible for normal AmigaOS integration such as files, input, GUI, RTG, AHI and CAMD.

The ARM side performs the computationally intensive part of the application.

A defining feature of XACP is therefore that an application is not limited to a fixed set of firmware operations:

**an XACP application can bring its own ARM-side engine.**

See:

**[Why XACP?](WHY_XACP.md)**

for the project philosophy behind this model.

---

# Memory model

The XACP memory map is part of the ABI.

Memory is divided according to ownership and purpose, including:

```text
Amiga-visible shared DDR
Core0 service memory
Core1 application memory
ARM-private memory
framebuffer-related regions
audio buffers
application staging areas
guard regions
firmware / hardware reserved space
```

The current exact layout is defined in:

```text
../sdk/xacp_memory_map_v1_8.h
```

Do not assume that an apparently unused DDR address is available.

An address may belong to:

```text
Amiga Zorro memory
a persistent Core0 service
an application shared-memory corridor
a Core1 private allocation
a framebuffer-related area
firmware-private data
hardware-reserved space
a compatibility guard region
```

All new allocations should be added to the common XACP map.

---

# Addressing conventions

XACP distinguishes between Amiga-side framebuffer-relative offsets and ARM physical DDR addresses.

The common framebuffer relationship is:

```text
Amiga board base
    +
0x00010000
    =
Amiga framebuffer-visible base
```

The corresponding ARM physical framebuffer base is:

```text
0x00200000
```

Shared XACP offsets defined relative to the framebuffer therefore need the documented address translation when accessed from the ARM side.

Do not independently reinterpret XACP offsets as ARM physical addresses.

Use the SDK definitions and the addressing rules documented in the current developer material.

---

# Cache coherency

The 68k and ARM processors do not automatically provide coherent cached views of shared DDR.

XACP software must therefore follow explicit ownership and cache-maintenance rules.

In general:

```text
68k writes data
      v
ARM invalidates the relevant cache range
      v
ARM reads the new data
```

and:

```text
ARM writes data
      v
ARM flushes the relevant cache range
      v
68k reads the result
```

Application-specific shared structures must follow the documented producer/consumer ownership rules.

Incorrect cache handling can produce failures that resemble memory corruption or protocol bugs.

---

# Endianness

The Amiga 68k is big-endian while the ARM Cortex-A9 is little-endian.

Shared structures must therefore have an explicitly defined byte order.

Historical and current XACP shared command structures generally use documented big-endian representations when exchanged with the Amiga.

ARM code must perform the required conversion rather than relying on native structure layout.

Never assume that a C structure can be shared directly between the 68k and ARM without an explicit ABI definition.

---

# SDK

The public XACP SDK is stored under:

```text
../sdk/
```

The current memory-map definition is:

```text
../sdk/xacp_memory_map_v1_8.h
```

The SDK should be treated as the common contract between:

```text
firmware
68k applications
Core0 services
Core1 binaries
shared-memory structures
```

Where practical, new software should include common XACP definitions directly rather than duplicating constants locally.

---

# Current platform components

XACP is used by several classes of application.

## Released / established projects

Examples include:

```text
ZZMIDI             persistent Core0 SoundFont / MIDI service
ZZDoom             Core1 Doom engine
ZZQuake            Core1 Quake engine
ZZQuake2           Core1 Quake II: software, TinyGL and RasterIX FPGA
ZZDarkForcesNEXT   Core1 The Force Engine / Dark Forces port
ZZPicoDrive        Core1 Mega Drive / Genesis emulator
ZZRastan           Core1 arcade game recreation
ZZSpeech           ARM-assisted speech synthesis
ZZVGMPlay          Core1 VGM/VGZ game-music player
ZZScreensavers     Core1 Earth/Julia and FPGA RasterIX Solar Winds
ZZPPC              experimental PPC32 / FPU execution
ZZ-MPEG            MPEG-1 / MP2 multimedia playback
ZZPlayGUI          MP3 / MP2 playback
ZZBench            68k / ARM / memory benchmarking
JuliaV2            Core1 validation and rendering
```

Additional integration includes ARM-assisted MP3/MP2 playback through:

```text
AmigaAMP external engine
mpega.library-compatible paths
```

Application-specific material belongs under:

```text
../applications/
```

---

## Currently in development

Current work includes:

```text
JPEG datatype       ARM-assisted JPEG decoding for Amiga datatype use

PNG datatype        ARM-assisted PNG decoding for Amiga datatype use

ZZMixer             shared audio mixing infrastructure

XACPNet.device      Amiga SANA-II network driver / ARM-assisted networking

Decompression       archive and data decompression services
```

Development projects are not automatically part of the current stable public ABI until they are explicitly documented and released.

---

# Firmware

Current public firmware:

```text
../firmware/BOOT_XX19d-RIX.bin
```

Corresponding public source:

```text
../firmware/source/XX19d-RIX/
```

See:

```text
../firmware/README.md
```

for firmware-specific information.

Historical firmware versions are retained for regression testing and compatibility work.

Firmware build numbers and XACP ABI versions should not be treated as interchangeable.

For example:

```text
XX19b      firmware build
XACP v1.7  protocol / ABI generation
```

---

# Compatibility

Do not assume that software is compatible merely because it runs on a ZZ9000.

An XACP application may depend on a particular combination of:

```text
firmware
zz9000.card
Amiga-side executable
Core0 service implementation
Core1 binary
XACP protocol version
shared-memory layout
```

The current public baseline for new software is:

```text
XX19d-RIX / XACP v1.8
```

Historical combinations should only be used when explicitly required by older software.

The XACP-compatible `zz9000.card` driver is required. When upgrading from a
non-XACP firmware/driver combination, install the driver distributed in the
firmware package to `LIBS:Picasso96/zz9000.card`. The driver is unchanged
from previous XACP firmware packages, including XX19c.

After replacing firmware or `zz9000.card`, a complete power-off before testing is recommended.

---

# Development rules

The XACP memory map and application-facing behaviour form part of the platform ABI.

Before assigning memory to a new firmware service or Core1 application:

1. Check the current XACP memory map.
2. Determine whether the allocation must be Amiga-visible or ARM-private.
3. Identify which processor or service owns the region.
4. Verify that it does not overlap existing allocations.
5. Add the allocation to the common XACP map.
6. Add compile-time overlap checks where practical.
7. Document any new application-visible behaviour.
8. Change the XACP ABI version if an incompatible change becomes necessary.

Do not return to independently selected application-specific DDR addresses.

The purpose of the common map is to allow multiple XACP services and applications to coexist predictably.

---

# Application development principle

XACP development is application-driven.

New platform functionality normally exists because a real application required it.

Examples include:

```text
fractal demos   -> early ARM execution and framebuffer experiments

multimedia      -> streaming and shared-buffer infrastructure

Doom            -> reliable Core1 execution and framebuffer handling

PicoDrive       -> emulator timing and audio requirements

Quake           -> larger memory and performance requirements

ZZMIDI          -> persistent realtime Core0 synthesis

later projects  -> improved Core0/Core1 coexistence and larger Core1 memory
```

The objective is not simply to increase the number of firmware operations.

The objective is to make the ARM processors of the ZZ9000 useful to Amiga software.

---

# Historical documentation

Older documentation is preserved under:

```text
archives/
```

Historical material describes real stages of XACP development, but its memory maps and compatibility statements may no longer apply to current software.

The broad progression is:

```text
early 2026
    ARM / 68k experiments

7 May 2026
    first public ARM fractal demonstrations on AmiBay

XACP V1
    early shared-command / MP3 / streaming architecture

XACP v1.5
    multimedia and ZZMIDI staging generation

XACP v1.6
    coordinated Core0 / Core1 DDR architecture

XACP v1.7
    large Core1 high-memory arena (previous public ABI)

XACP v1.8
    RasterIX FPGA integration and JPEG/PNG image-service transport
```

Historical documentation should be used for reference and regression work, not as the basis for new memory allocations.

---

# Documentation layout

The documentation tree is intended to separate current material from historical material.

```text
docs/
|-- README.md
|-- WHY_XACP.md
|-- XACP_V1_8_DEVELOPER_NOTES.md
|-- FPGA_RASTERIX_SOURCE.md
|-- XACP_V1_7_DEVELOPER_NOTES.md
|-- XACP_V1_6_DEVELOPER_DOCUMENTATION.md
`-- archives/
    |-- XACP_V1_NOTICE.md
    |-- XACP_V1_Developer_Documentation.pdf
    |-- XACP_V1_5_HISTORY_MEMORY_MAP_PART2.md
    `-- README_XACP_V1_LEGACY.md
```

The corresponding SDK definitions are under:

```text
../sdk/
```

and firmware-specific documentation under:

```text
../firmware/
```

---

# Which document should I read?

For users wanting to understand what XACP is and why it exists:

```text
WHY_XACP.md
```

For developers targeting the current platform:

```text
XACP_V1_8_DEVELOPER_NOTES.md
FPGA_RASTERIX_SOURCE.md
../sdk/xacp_memory_map_v1_8.h
../sdk/README.md
```

For deeper architectural background on the coordinated low-DDR model:

```text
XACP_V1_6_DEVELOPER_DOCUMENTATION.md
```

For historical investigation:

```text
archives/
```

For detailed firmware history:

```text
../CHANGELOG.md
../firmware/README.md
```

---

# Credits

XACP / Xanxi, 2026.

For use with the MNT ZZ9000 Amiga RTG / ARM platform.

XACP builds on the hardware, firmware and driver work of the MNT ZZ9000 project.

Thanks to the Amiga and ZZ9000 communities for testing, feedback and technical discussion.

Third-party software retains its respective authorship and licence.
