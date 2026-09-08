# XACP Firmware Builds

This directory contains Xanxi/XACP firmware builds for the MNT ZZ9000.

The current public firmware is:

```text
BOOT_XX19a.bin
```

Current protocol / ABI baseline:

```text
XACP v1.6
```

Firmware build numbers and XACP protocol versions are separate.

```text
XX19a     = firmware build
XACP v1.6 = protocol / ABI / shared-memory baseline
```

XX19a is therefore the firmware implementing XACP v1.6.

---

# Current firmware: XX19a

XX19a replaces XX19 as the current XACP firmware baseline.

XX19 introduced the XACP v1.5 multimedia map and the first formal large ZZMIDI staging model.

XX19a extends this work with the XACP v1.6 coordinated DDR architecture.

The main objective of v1.6 is to make DDR ownership explicit across the complete XACP environment rather than allowing individual applications and firmware services to reserve unrelated addresses independently.

---

## XACP v1.6 changes

As XACP expanded, the ZZ9000 DDR became shared by several different classes of software:

```text
Core0 firmware services
Core1 application blobs
Core1 private data
framebuffers
ROM and input data
audio rings
SoundFont data
MIDI data
save-state and persistent buffers
firmware-private heaps
```

Earlier layouts were sufficient while these components were developed independently, but they were no longer suitable for a general shared platform.

XACP v1.6 establishes a coordinated allocation model for these resources.

The XX19a baseline provides:

```text
explicit DDR ownership
separation of persistent Core0 services from Core1 applications
dedicated ZZMIDI shared communication corridor
ARM-private ZZMIDI SoundFont and MIDI storage
protected firmware/service memory
defined Core1 private application areas
separation of application blobs and runtime data
separation of framebuffers and ROM/data buffers
dedicated persistent/save-buffer allocations
guard regions
removal of known cross-project DDR overlaps
space reserved for future XACP expansion
```

New XACP software should use the current SDK definitions rather than introducing undocumented shared-memory or ARM-private addresses.

The authoritative memory-map header is:

```text
../sdk/xacp_memory_map_v1_6.h
```

---

## XACP addressing model

XACP v1.6 distinguishes shared framebuffer-relative addresses from ARM absolute physical addresses.

On the Amiga side:

```text
board = cd->cd_BoardAddr
fb    = board + 0x00010000
```

The ARM physical framebuffer base is:

```text
0x00200000
```

Therefore:

```text
ARM physical address =
    0x00200000 + framebuffer-relative offset
```

ARM-private allocations use absolute ARM physical addresses and must never be confused with framebuffer-relative offsets.

The SDK header defines the corresponding constants and conversion helpers.

---

## Established shared XACP regions

Earlier XACP generations established shared communication areas for services including MP3, MP2 and multimedia applications.

Current firmware retains these interfaces.

Examples include:

```text
XACP command block
stream-control block
MP3 input ring
generic PCM output ring
legacy Core1 shared area
ZZMPEG stream region
```

These areas remain part of the XACP environment and must not be reused by new applications.

Exact addresses are defined in the SDK.

---

## ZZMIDI service

XX19a contains the firmware-side SoundFont synthesis service used by ZZMIDI.

The synthesis engine uses:

```text
TinySoundFont
TinyMidiLoader
```

The Amiga-side ZZMIDI software handles application control, CAMD, files and AHI playback while the ZZ9000 ARM performs MIDI processing and SoundFont synthesis.

### XACP v1.6 ZZMIDI shared corridor

Unlike XACP v1.5, complete SoundFont and MIDI files no longer need large permanent Zorro-visible staging areas.

XACP v1.6 assigns ZZMIDI a dedicated 3 MiB shared communication corridor:

```text
fb+0x06000000
    ...
fb+0x06300000
```

Its current layout is:

```text
fb+0x06000000   control allocation
fb+0x06010000   realtime MIDI FIFO allocation
fb+0x06100000   PCM ring
fb+0x06200000   chunked upload window
fb+0x06300000   end of ZZMIDI shared corridor
```

The upload window is:

```text
1 MiB
```

SoundFont and MIDI data are transferred through this shared window in chunks and copied into ARM-private DDR.

The unused space reserved inside the ZZMIDI corridor is part of the ABI and must not be allocated independently by another application.

### ZZMIDI ARM-private storage

Complete SoundFont and MIDI data are stored in ARM-private memory after upload.

The XACP v1.6 reserved allocations are:

```text
ARM 0x23000000 - 0x25000000
    ZZMIDI SoundFont storage
    32 MiB reserved allocation

ARM 0x25000000 - 0x25600000
    ZZMIDI MIDI / parsed-data storage
    6 MiB reserved allocation

ARM 0x25600000 - 0x2F600000
    ZZMIDI / firmware service heap
    160 MiB
```

These are ABI allocation sizes.

They do not necessarily represent the user-visible maximum file size accepted by a particular ZZMIDI software release.

Application-level limits are documented by the corresponding ZZMIDI release.

### Guard region

The region:

```text
ARM 0x2F600000 - 0x30000000
```

is intentionally reserved as a 10 MiB guard.

It must not be used as generic scratch memory.

---

## ARM-private memory safety

ARM-private allocations must not be placed arbitrarily in high DDR.

Part of the ARM address space corresponds to live AmigaOS-visible ZZ9000 Zorro III Fast RAM.

The current safety model distinguishes:

```text
ARM 0x20000000 - 0x201F0000
    live Z3 collision region
    NEVER use for ARM-private storage

ARM 0x201F0000 - 0x22000000
    no-man's-land / guard region

ARM 0x22000000 - 0x30000000
    documented safe XACP private window
```

Current firmware maps only the documented safe private window for ARM-private XACP use.

Do not restore the older practice of treating `0x20000000 - 0x22000000` as generic private ARM memory.

---

## ZZPicoDrive / Core1 private allocation

The beginning of the safe private window is reserved for the established ZZPicoDrive / Core1 environment:

```text
ARM 0x22000000 - 0x23000000
```

Size:

```text
16 MiB
```

Persistent Core0 services such as ZZMIDI must not allocate inside this area.

ZZMIDI private storage begins at:

```text
0x23000000
```

This separation is part of the v1.6 allocation model.

---

## Additional reserved memory

XACP v1.6 also defines reserved regions outside the primary low private pool.

For example:

```text
ARM 0x30000000 - 0x33000000
```

is reserved for SMUSH codec use.

Reserved regions are not free memory and must not be repurposed by unrelated XACP applications.

---

## Core1 applications

XACP can dynamically launch complete ARM applications on the second Cortex-A9 core.

Current and historical examples include:

```text
ZZDoom
ZZPicoDrive
ZZRastan
JuliaV2
ZZBench ARM tests
video and graphics engines
experimental game engines
```

Core1 applications may require several distinct types of memory:

```text
executable blob
application-private memory
framebuffer
ROM or input data
communication mailbox
audio buffers
persistent data or save state
```

XACP v1.6 coordinates these allocations instead of allowing each application to reuse arbitrary DDR regions.

The allocation model is intended to remain reusable for future Core1 projects.

---

## Core0 / Core1 coexistence

A coordinated DDR map prevents memory collisions, but it does not automatically guarantee that every persistent Core0 service can run safely with every Core1 application.

Other shared resources may include:

```text
video state
interrupts
cache state
firmware state machines
audio resources
Core1 lifecycle state
```

Application combinations must therefore be validated explicitly.

ZZMIDIGate is an example of an Amiga-side mechanism used to coordinate ZZMIDI realtime operation with Core1 application startup.

---

## Cache coherency

Shared DDR is not automatically cache coherent between the Amiga and ARM.

The ARM Cortex-A9 uses a write-back data cache.

General XACP rule:

```text
68k writes -> ARM reads
    ARM invalidates the corresponding D-cache range before reading.

ARM writes -> 68k reads
    ARM flushes the corresponding D-cache range after writing.
```

Cache maintenance is part of the protocol.

Failure to follow these rules can cause stale commands, missing data, audio corruption or apparently random failures.

---

## Endianness

The two processors use different native byte order:

```text
Amiga 68k      big-endian
ARM Cortex-A9  little-endian
```

Shared structures must therefore define an explicit endian convention.

Established XACP command structures use big-endian shared fields where documented.

ARM code must perform the required conversions explicitly.

---

## Long-running services

Long-running ARM operations must not keep the 68k blocked inside an immediate Zorro command transaction.

Operations should ACK quickly and continue through mechanisms such as:

```text
deferred firmware state machines
Core1 execution
shared status structures
sequence counters
pollable completion state
```

This rule applies to workloads such as:

```text
audio decoding
SoundFont synthesis
video decoding
emulation
game engines
large data operations
```

---

## Firmware history

Older firmware builds are stored in:

```text
firmware/archive/
```

They are retained for historical reference, regression testing and applications tied to a particular release generation.

### XX19a — XACP v1.6

Current public firmware baseline.

Main changes:

```text
XACP v1.6 coordinated DDR architecture
explicit Core0 / Core1 memory ownership
dedicated ZZMIDI shared corridor
chunked ZZMIDI upload model
ARM-private SoundFont and MIDI storage
protected service heap
defined Core1 private application region
guard regions
removal of known cross-project DDR overlaps
current baseline for XACP development
```

Current binary:

```text
BOOT_XX19a.bin
```

---

### XX19 — XACP v1.5

XX19 introduced the public XACP v1.5 multimedia memory map.

Important changes included:

```text
ZZMIDI SoundFont service
OP_MIDI_SF2 = 0x0120
32 MiB shared SoundFont staging allocation
6 MiB shared MIDI staging allocation
large SoundFont playback stabilization
persistent SoundFont reuse
private ARM service-pool documentation
```

XX19 solved the previous SF2 / MIDI staging overlap and established the first formal large multimedia memory map.

Its large Zorro-visible ZZMIDI staging model has now been superseded by the XACP v1.6 shared upload corridor and ARM-private storage architecture.

---

### XX18m — first ZZMIDI firmware line

XX18m introduced the first ZZMIDI / SoundFont firmware work.

It also fixed the ZZ9000AX cold-boot initialization issue found in earlier firmware generations.

Validated work included:

```text
initial ZZMIDI service
OP_MIDI_SF2 groundwork
ZZMIDI player / daemon integration
ZZ9000AX cold-boot initialization fix
```

---

### XX18c — ZZDoom baseline

XX18c was the public ZZDoom-era firmware baseline.

Validated features included:

```text
dynamic Core1 launch
shared DDR command path
deferred PAN / fullscreen RTG synchronization
ZZDoom 320x200
ZZDoom 640x400
AHI sound effects
CAMD MIDI
AmigaOS file access
save / load support
clean Core1 STOP / return
```

---

### XX16c — MP3 / MP2 / ZZMPEG baseline

XX16c was the first major public multimedia XACP firmware line.

It established:

```text
ARM MP3 decoding
MP3 streaming
MP2 audio
ZZPlayGUI
AmigaAMP zz9000.engine
mpega.library integration
ZZ-MPEG
early Core1 applications
Julia / fractal validation
```

---

## Compatibility overview

| Firmware  | XACP generation | Main role                                         |
| --------- | --------------- | ------------------------------------------------- |
| **XX19a** | **XACP v1.6**   | Current coordinated Core0 / Core1 DDR baseline    |
| XX19      | XACP v1.5       | Large shared ZZMIDI staging / multimedia baseline |
| XX18m     | pre-v1.5        | Initial ZZMIDI and ZZ9000AX cold-boot fix         |
| XX18c     | earlier XACP    | ZZDoom public baseline                            |
| XX16c     | earlier XACP    | MP3 / MP2 / ZZMPEG multimedia baseline            |

A newer firmware may retain functionality introduced by older generations, but applications should only be considered compatible when that combination has been explicitly validated.

Do not assume compatibility merely because an application starts.

---

## Installation

The current firmware binary is:

```text
BOOT_XX19a.bin
```

Firmware may be installed using the normal ZZ9000 SD-card procedure or the historical ZZFwUpdate 2.1 utility supplied under:

```text
firmware/tools/ZZFwUpdate/
```

ZZFwUpdate and the firmware-side FWUP core originate from Dimitris Panokostas (MiDWaN) in the BlitterStudio ZZ9000 projects and are distributed under GPL-3.0-or-later.

The FWUP core files `fw_update.c` and `fw_update.h` included in the XX19a source tree are unmodified from the original BlitterStudio implementation introduced in May 2026. XACP integrates that original FWUP core into its own firmware main loop and reset/storage lifecycle.

XACP firmware integration: Xanxi.

The bundled ZZFwUpdate 2.1 is the historical pre-RESTORE version using the original FWUP command set:

```text
OPEN
WRITE
CLOSE
ABORT
```

Later BlitterStudio ZZFwUpdate / FWUP versions add protocol extensions such as `RESTORE` and should not be assumed to be compatible with this XACP firmware branch.

When updating:

```text
1. Close applications using the ZZ9000.
2. Install the new BOOT firmware.
3. Do not interrupt the flashing or file replacement process.
4. Completely power off the Amiga.
5. Power on again.
6. Test the required XACP applications.
```

A complete power cycle is recommended after replacing the firmware or `zz9000.card`.

Do not rely only on a warm reset when validating a new firmware installation.

---

## Firmware / application matching

Do not arbitrarily mix:

```text
firmware
zz9000.card
Amiga-side executable
Core1 blob
shared-memory definitions
```

from unrelated development generations.

A mismatch can cause:

```text
missing XACP operations
incorrect DDR addresses
black or white screen
RTG corruption
silent audio
incorrect save data
crash or lock-up
failure of Core1 to start
failure of Core1 to return cleanly
```

The fact that an application starts does not prove that its memory map or protocol is compatible with the installed firmware.

The current public baseline for new XACP software is:

```text
XX19a / XACP v1.6
```

---

## SDK

The current XACP SDK is stored under:

```text
../sdk/
```

The normative memory-map definition is:

```text
../sdk/xacp_memory_map_v1_6.h
```

New firmware services and applications should use these common definitions instead of independently assigning DDR addresses.

The SDK also contains compile-time overlap checks intended to detect invalid allocations during development.

---

## Documentation

Current architecture and developer documentation is stored under:

```text
../docs/
```

The current developer reference is:

```text
../docs/XACP_V1_6_DEVELOPER_DOCUMENTATION.md
```

Historical XACP V1 and v1.5 documentation is retained under:

```text
../docs/archives/
```

Historical memory maps should not be used as the basis for new software.

---

## Source code

XX19a is the current public source baseline for the Xanxi/XACP firmware changes.

The corresponding firmware source is available under:

```text
firmware/source/XX19a/
```

The firmware uses upstream and third-party components which retain their respective original licenses and copyright notices.

In particular:

```text
FWUP / firmware update core
    Dimitris Panokostas (MiDWaN)
    BlitterStudio ZZ9000 projects
    GPL-3.0-or-later

TinySoundFont
    Bernhard Schelling
    MIT License

TinyMidiLoader
    Bernhard Schelling
    zlib License
```

The original FWUP source files retain the copyright and SPDX notices of Dimitris Panokostas. Their XACP-specific integration into the XX19a firmware main loop and storage/reset lifecycle is part of the XACP firmware integration by Xanxi.

Detailed third-party notices are provided in:

```text
firmware/source/XX19a/THIRD_PARTY_LICENSES.txt
```

Publication of the XX19a firmware source does not imply that every Amiga-side XACP application is open source.

Application source availability and licensing are defined independently by each application package.

---

## Archived builds

Historical firmware binaries belong in:

```text
firmware/archive/
```

The root of `firmware/` should contain only the current firmware generation and its current documentation/source structure.

Archived firmware is retained for:

```text
development history
regression testing
historical application compatibility
```

Do not select a historical firmware merely because its build number appears close to the current release.

Some older applications were designed for a specific XACP generation.

---

## Development rule

The XACP v1.6 memory map is part of the platform ABI.

Future firmware services and Core1 applications must:

```text
use documented XACP memory regions
distinguish shared offsets from ARM absolute addresses
avoid undocumented private DDR allocations
preserve persistent Core0 service memory
preserve Core1 application allocations
preserve ZZMIDI shared and private regions
preserve guard and reserved regions
avoid live Amiga-visible Z3 memory
add new allocations to the common SDK map
update the XACP version if an incompatible ABI change becomes necessary
```

The purpose of this rule is to prevent the memory-map fragmentation that developed during the earlier experimental XACP generations.

---

## Credits

XACP / Xanxi, 2026.

Thanks to the MNT ZZ9000 project and to the Amiga / ZZ9000 community for the hardware platform, testing, feedback and technical discussion.

FWUP / ZZFwUpdate: Dimitris Panokostas (MiDWaN), BlitterStudio, GPL-3.0-or-later.

Third-party components retain their original authorship and licenses.
