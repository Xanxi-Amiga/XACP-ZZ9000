# XACP Documentation

This directory contains the protocol, architecture and historical documentation for the **eXtended ARM Coprocessor Protocol (XACP)** used by the Xanxi firmware and applications for the MNT ZZ9000.

The current public baseline is:

```text
Firmware:  XX19a
Protocol:  XACP v1.6
```

Firmware build numbers and XACP protocol versions are separate.

```text
XX19a     = firmware build
XACP v1.6 = protocol / ABI / shared-memory baseline
```

---

## Project overview

For the origins, design philosophy and purpose of XACP, see:

**[Why XACP?](WHY_XACP.md)**

Technical documentation below describes the protocol and implementation.

---

## Current documentation

The current architecture and developer reference is:

```text
XACP_V1_6_DEVELOPER_DOCUMENTATION.md
```

It describes:

* XACP architecture
* Core0 firmware services
* Core1 applications
* Amiga / ARM addressing conventions
* shared DDR ownership
* ARM-private memory
* cache-coherency and endian rules
* application / firmware compatibility
* rules for developing new XACP software

For exact memory addresses and allocation constants, the authoritative public reference is:

```text
../sdk/xacp_memory_map_v1_6.h
```

New software should use this header instead of introducing independent hard-coded DDR addresses.

---

## XACP v1.6

XACP v1.6 formalizes DDR ownership across the complete XACP platform.

Earlier generations were developed incrementally as new applications and services were added. This eventually created the possibility of collisions between:

```text
Core0 firmware services
Core1 applications
framebuffers
ROM and input data
audio buffers
SoundFont and MIDI data
application-private memory
save-state buffers
firmware-private heaps
```

XACP v1.6 makes these allocations part of the protocol ABI.

The current firmware implementing this baseline is:

```text
XX19a
```

See:

```text
../firmware/README.md
../sdk/README.md
```

---

## SDK

The public XACP SDK is stored in:

```text
../sdk/
```

The current memory-map header is:

```text
../sdk/xacp_memory_map_v1_6.h
```

The header is the normative reference for exact XACP v1.6 memory allocations.

Documentation in this directory explains the architecture and allocation model but should not be used as a substitute for the SDK constants when writing software.

---

## Historical documentation

Older XACP documentation is preserved under:

```text
archives/
```

This includes documentation for the original XACP V1 and XACP v1.5 generations.

Historical documents describe real stages of XACP development, but their memory maps and compatibility statements may no longer apply to current software.

In particular:

```text
XACP V1     early MP3 / streaming / shared-command architecture
XACP v1.5   XX19 multimedia and ZZMIDI staging baseline
XACP v1.6   XX19a coordinated Core0 / Core1 DDR baseline
```

Do not use V1 or v1.5 memory-map definitions as the basis for new XACP applications.

---

## Suggested archive layout

```text
docs/
├── README.md
├── XACP_V1_6_DEVELOPER_DOCUMENTATION.md
└── archives/
    ├── XACP_V1_NOTICE.md
    ├── XACP_V1_Developer_Documentation.pdf
    ├── XACP_V1_5_HISTORY_MEMORY_MAP_PART2.md
    └── README_XACP_V1_LEGACY.md
```

---

## Current platform components

XACP is used by several different classes of application.

Examples include:

```text
ZZMIDI       persistent Core0 SoundFont / MIDI service
ZZDoom       Core1 Doom engine
ZZPicoDrive  Core1 Mega Drive / Genesis emulator
ZZRastan     Core1 arcade game engine
ZZSpeech     ARM-assisted speech synthesis
ZZ-MPEG      MPEG-1 / MP2 multimedia playback
ZZPlayGUI    MP3 / MP2 playback
ZZBench      68k / ARM / memory benchmarking
JuliaV2      Core1 validation and rendering
```

Application-specific documentation belongs under:

```text
../applications/
```

---

## Compatibility

Do not assume that software is compatible merely because it runs on a ZZ9000.

An XACP application may depend on a particular combination of:

```text
firmware
zz9000.card
Amiga-side executable
Core1 binary
protocol version
shared-memory layout
```

The current baseline for new software is:

```text
XX19a / XACP v1.6
```

Historical combinations should only be used when required by software explicitly tied to an older generation.

---

## Development rule

The XACP memory map is part of the ABI.

Do not allocate apparently unused DDR addresses independently.

Before assigning memory to a new firmware service or Core1 application:

1. Check the current SDK memory map.
2. Identify whether the allocation is shared or ARM-private.
3. Verify that it does not overlap an existing service or application.
4. Add the allocation to the common XACP map.
5. Add compile-time overlap checks where practical.
6. Change the XACP ABI version if an incompatible memory-map change becomes necessary.

The goal is to keep XACP usable as a shared acceleration platform rather than returning to application-specific memory maps.

---

## Credits

XACP / Xanxi, 2026.

For use with the MNT ZZ9000 Amiga RTG / ARM platform.

Thanks to the MNT ZZ9000 project and to the Amiga and ZZ9000 communities for testing, feedback and technical discussion.

Third-party software retains its respective authorship and license.
