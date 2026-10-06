# Changelog

## ZZMIDI 1.01 - 7 October 2026

Maintenance update for the XX19c / XACP v1.7 firmware baseline.

### Changed

- XX19c / XACP v1.7 is now the ZZMIDI firmware baseline.
- Removed `ZZMIDIGate` from the distribution.
- ZZDoom and other compatible Core1 applications can now be launched normally while ZZMIDI realtime is active.
- The historical Core0/Core1 startup/cache-coherency conflict previously requiring `ZZMIDIGate` is fixed directly in XX19c firmware.
- Updated ZZDoom/Core1 usage and firmware compatibility documentation.

### Compatibility

- The remaining ZZMIDI application binaries and the synthesis/audio path are unchanged from ZZMIDI 1.0.
- Users intentionally remaining on XX19a or XX19b should continue using ZZMIDI 1.0 together with `ZZMIDIGate`.
- ZZMIDI 1.01 requires XX19c / XACP v1.7 or a later firmware explicitly documented as compatible.

---

## ZZScreensavers 1.0 - 6 October 2026

Initial public release of the XACP screensaver package.

### Added

- `ZZScreensaverE`: rotating textured Earth rendered on ZZ9000 Core1.
- 640x480 32-bit RTG Earth display path.
- Embedded 1024x512 RGB565 NASA Visible Earth / Blue Marble texture with bilinear sampling.
- `ZZScreensaverJ`: animated Julia fractal rendered on Core1 at 320x240 16-bit.
- `ZZScreensaverHotkeys`: optional Commodities helper using Ctrl-Alt-E and Ctrl-Alt-J.
- Silent launch and clean exit on keyboard or mouse activity.
- Closed-source freeware binary release.

### Requirements

- XACP v1.7 / XX19b or later.
- XX19c recommended.

---


## XX19c - XACP v1.7 stability baseline

XX19c supersedes XX19b as the current firmware baseline while retaining the
same XACP v1.7 protocol and shared-memory map.

### Fixed

- Core0/Core1 coexistence during dynamic Core1 startup.
- A launch/cache-coherency race that could produce a white screen or system
  lock-up when a Core1 application was started while a persistent Core0
  service such as ZZMIDI was active.
- Core1 cache maintenance no longer operates the shared PL310 L2 controller
  from the Core1 execution path; launch-side maintenance is restricted to L1
  where appropriate.
- Core1 is halted before its vector, target and GO state are republished,
  with explicit memory-ordering barriers around launch metadata.
- The ZZMIDIGate workaround is no longer required on XX19c for the historical
  ZZDoom/ZZMIDI Core0/Core1 startup conflict.

### Added

- Linker-time hard guard preventing the low firmware image from reaching the
  framebuffer at `0x00200000`.
- Additional 64 KiB safety limit at `0x001F0000`.
- `__low_firmware_end` verification at link time.

### Compatibility

- XACP remains **v1.7**.
- The XACP v1.7 high Core1 arena is unchanged.
- The shared DDR ABI is unchanged.
- Applications validated for XX19b remain compatible unless they document a
  stricter firmware dependency.

Validated XX19c low-firmware end:

```text
0x001B9A2E
```

---


## v1.7.0 / XX19b - XACP v1.7 public baseline

XX19b supersedes XX19a as the current firmware baseline.

### Added

- 248 MiB ARM-only Core1 arena at `0x30000000-0x3F800000`.
- 4 MiB guard at `0x3F800000-0x3FC00000`.
- protected firmware/hardware region at `0x3FC00000-0x40000000`.
- versioned firmware source under `firmware/source/XX19b/`.

### Changed

- XACP protocol/ABI baseline advances from v1.6 to v1.7.
- Legacy SMUSH Codec1/37/47 implementation is removed from the firmware build because its fixed scratch buffers occupied the new high Core1 arena.
- Existing `ACC_CMPTYPE` values remain reserved; legacy requests are unsupported.
- XACP v1.6 allocations below `0x30000000` remain unchanged.


## v1.6.0 / XX19a - XACP v1.6 public baseline

XX19a is the current Xanxi/XACP firmware baseline for the MNT ZZ9000.

It implements **XACP v1.6**, which formalizes the shared DDR allocation model used by persistent Core0 firmware services and dynamically loaded Core1 applications.

XACP protocol versions and firmware build numbers remain separate:

```text
XX19a     = firmware build
XACP v1.6 = protocol / ABI / shared-memory baseline
```

### Added

* XACP v1.6 shared DDR architecture.
* Explicit ownership of shared DDR regions.
* Defined separation between persistent Core0 services and Core1 applications.
* Protected memory regions for ZZMIDI SoundFont and MIDI data.
* Defined Core1 executable and runtime areas.
* Defined framebuffer, ROM/data and persistent-buffer allocations.
* Protected firmware-private memory.
* Allocation model intended for reuse by future XACP applications.
* Versioned firmware source structure:

  ```text
  firmware/source/XX19a/
  ```

### Changed

* Shared DDR allocation is now treated as part of the XACP ABI rather than as a collection of application-specific addresses.
* Core1 projects are expected to use documented XACP allocations instead of independently selecting unused-looking DDR regions.
* The memory layout has been reorganized to allow persistent firmware services and Core1 applications to coexist without undocumented memory reuse.
* XX19a supersedes XX19 as the current firmware baseline.
* XACP v1.6 supersedes XACP v1.5 as the current memory-map baseline.

### Fixed

* Known cross-project DDR allocation conflicts found as the number of XACP services and Core1 applications increased.
* Potential overlap between application work areas, persistent buffers and firmware-service memory.
* Memory-map fragmentation inherited from earlier independently developed XACP projects.

### Repository organization

The firmware directory has been reorganized around a current + archive model.

Current firmware:

```text
firmware/BOOT_XX19a.bin
```

Historical firmware:

```text
firmware/archive/
```

The root of `firmware/` is reserved for the current firmware generation, documentation and versioned source structure.

### Source publication

XX19a is the firmware generation selected as the current source-publication baseline.

Firmware source is published together with the applicable third-party source and license notices.

Publication of firmware source does not imply that every Amiga-side XACP application is open source. Application licensing and source availability remain specific to each project.

---

## Application ecosystem - 2026 expansion

During the development cycle leading to XACP v1.6, the XACP platform expanded significantly beyond its original audio and demonstration applications.

### ZZMIDI 1.0

ZZMIDI evolved into the main persistent Core0 multimedia service built into the current firmware line.

Main capabilities include:

```text
SoundFont MIDI synthesis on the ZZ9000 ARM
TinySoundFont synthesis engine
TinyMidiLoader MIDI parsing
large SoundFont support
Amiga-side GUI MIDI player
AHI audio playback
persistent SoundFont reuse
shared XACP MIDI service
```

The ZZMIDI service is one of the principal reasons persistent Core0 memory is now explicitly protected by the XACP shared-memory ABI.

---

### ZZPicoDriveMD 1.1

Public Sega Mega Drive / Genesis emulator using ZZ9000 ARM Core1.

Validated features include:

```text
PicoDrive emulation on ARM Core1
Picasso96 RTG display
PAL and NTSC operation
AHI stereo audio
direct Paula DMA audio
keyboard input
DB9 joystick support
lowlevel.library controller support
two-player support
SRAM
savestates
embedded Core1 blob
```

The Amiga 68k remains responsible for system integration while the emulation workload runs on Core1.

---

### ZZPicoDriveSMS 1.1

Public Sega Master System edition of ZZPicoDrive.

It shares the Core1 / Amiga host architecture of ZZPicoDriveMD and provides:

```text
Master System emulation
PAL / NTSC operation
RTG display
AHI or Paula audio
keyboard / DB9 / controller input
two-player support
SRAM
savestates
```

YM2413 FM audio is not enabled in the 1.1 release.

---

### ZZRastan 1.0

First standalone arcade-machine recreation in the XACP project.

The original Rastan arcade hardware is emulated on ZZ9000 ARM Core1 while AmigaOS handles launching, RTG display, input, audio output and ROM loading.

Implemented hardware includes:

```text
Cyclone 68000 CPU emulation
CZ80 sound CPU emulation
Taito video hardware model
YM2151 FM audio
MSM5205 ADPCM audio
keyboard and joystick input
clean Core1 stop and relaunch
```

No commercial game ROMs are distributed.

The ARM Core1 source required by the applicable third-party licenses is published with the project. The Amiga launcher and GUI remain separate proprietary components.

---

### ZZSpeech 1.0

Initial public speech-synthesis release.

ZZSpeech uses a dynamically loaded CMU Flite engine on ZZ9000 ARM Core1.

The Amiga side provides:

```text
resident daemon
Intuition / GadTools GUI
ZZSay CLI client
ZZSPEECH: DOS handler
AHI playback
WAV export
external .flitevox voice support
```

ZZSpeech demonstrates a resident service architecture implemented through a dynamically loaded Core1 application rather than by modifying the main firmware.

---

### ZZDarkForcesNEXT 1.0

Public release of The Force Engine / STAR WARS: Dark Forces for Amiga
systems equipped with a ZZ9000.

The TFE engine and software renderer run natively on ZZ9000 ARM Core1
through XACP, while AmigaOS handles Workbench integration, Picasso96
display, input, AHI audio, filesystem access and optional CAMD MIDI.

Main features include:

```text
native The Force Engine execution on ARM Core1
320x200, 320x240, 640x400, 640x480 and 800x600
Picasso96 asynchronous triple buffering
AHI sound effects
internal Roland SC-55 SoundFont music
internal TimGM6mb SoundFont music
external MIDI through CAMD
save/load and quicksave/quickload
pilot progression
persistent in-game configuration
Smooth VUE
Autorun
Crouch Toggle
secret-found messages
automap key colors
automap secret display
clean cooperative Core1 stop and return
```

Dark Forces game data is preloaded into the XACP v1.7 248 MiB ARM-only
Core1 arena before gameplay, allowing the engine to operate without
repeated Amiga-side disk access once started.

The release includes the closed-source Amiga 68k launcher and the
GPL-2.0 Core1 blob. Complete corresponding source/build material for
the TFE-derived Core1 component is published in the repository.

Original STAR WARS: Dark Forces commercial game data is not included.

Public release:

```text
ZZDarkForcesNEXT-1.0
25 September 2026
```

Release page:

https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZDarkForcesNEXT-1.0

---

## v1.5.0 / XX19 - XACP v1.5 public baseline

XX19 established the first formal large multimedia shared-memory baseline.

### Added

* XACP v1.5 shared multimedia memory map.

* ZZMIDI SoundFont playback service.

* `OP_MIDI_SF2 = 0x0120`.

* 32 MB SoundFont staging area:

  ```text
  fb+0x05800000 - fb+0x07800000
  ```

* 6 MB MIDI staging area:

  ```text
  fb+0x07800000 - fb+0x07E00000
  ```

* Private ARM service-pool documentation.

* SDK memory-map definitions.

### Fixed

* Previous SoundFont / MIDI staging overlap.
* MIDI staging moved away from the old overlapping location.
* Large SoundFont playback path stabilized.
* Persistent SoundFont reuse across consecutive MIDI files.
* MIDI / TML reload without unnecessary reparsing of an unchanged SoundFont.

### Notes

XX19 replaced the internal XX18t development naming for the public XACP v1.5 release.

XACP v1.5 was the protocol / ABI / memory-map baseline.

XX19 was the firmware build implementing it.

XX19 has now been superseded by XX19a / XACP v1.6.

---

## XX18m - first ZZMIDI firmware line

Firmware generation introducing ZZMIDI into the XACP firmware tree.

### Added

* Initial ZZMIDI / SoundFont firmware path.
* `OP_MIDI_SF2 = 0x0120` groundwork.
* Initial ZZMIDI player / daemon integration.

### Fixed

* ZZ9000AX cold-boot initialization issue.

Earlier builds could require a reset after cold power-on before the ZZ9000AX initialized correctly.

XX18m fixed that initialization problem.

---

## XX18c - ZZDoom public baseline

Firmware generation associated with the first public ZZDoom baseline.

### Added

* Dynamic Core1 application launch.
* Shared DDR command path.
* Deferred PAN / fullscreen RTG synchronization.
* ZZDoom 320x200 support.
* ZZDoom 640x400 support.
* AHI sound-effects path.
* CAMD MIDI path.
* AmigaOS file access.
* Save / load support.
* Clean Core1 STOP / return path.

### ZZDoom

ZZDoom is based on `doomgeneric`.

The Doom engine runs on ZZ9000 ARM Core1 while AmigaOS handles:

```text
RTG display
keyboard input
AHI sound effects
MIDI
file access
savegames
```

ZZDoom established much of the practical Core1 execution model later reused by other XACP applications.

---

## XX16c - MP3 / MP2 / ZZMPEG baseline

First major public XACP multimedia firmware generation.

### Added

* ARM MP3 decoding.

* MP3 streaming pipeline.

* MP2 audio support.

* ZZMP3Play / ZZPlayGUI.

* AmigaAMP external engine:

  ```text
  zz9000.engine
  ```

* `mpega.library` XACP integration.

* ZZ-MPEG MPEG-1 / MP2 playback.

* Early Core1 application infrastructure.

* Julia / fractal validation applications.

XX16c is the firmware generation where XACP became a practical multimedia offload platform rather than only an experimental coprocessor interface.

---

## ZZ-MPEG - MPEG-1 / MP2 multimedia path

ZZ-MPEG demonstrated the Core1 multimedia model for video playback.

Validated functionality includes:

```text
MPEG-1 Program Stream input
ARM/Core1 video decoding
Workbench window output
Picasso96 fullscreen output
MP2 audio through XACP
AHI playback
clean STOP / return path
```

---

## ZZBench GUI - platform validation

ZZBench GUI provides CPU and memory-bandwidth measurements for the XACP hardware/software architecture.

Tests include:

```text
68k Dhrystone / Whetstone
ARM Core1 Dhrystone / Whetstone
Amiga Chip RAM bandwidth
Amiga Fast RAM bandwidth
ZZ9000 DDR bandwidth through ARM
ZZ9000 DDR bandwidth through Zorro III
```

Later releases expanded the benchmark while retaining its role as an XACP validation tool.

---

## JuliaV2 - Core1 validation

JuliaV2 established the clean Core1 application lifecycle later reused by larger projects.

Validated model:

```text
load ARM blob into DDR
launch Core1
communicate through shared memory
render / compute on ARM
STOP cleanly
return control to AmigaOS
launch another Core1 application afterwards
```

Earlier ZZFractal and Mandelbrot applications are retained as historical examples of XACP graphical offload.

---

## Legacy XACP V1

The original XACP V1 documentation is retained for historical reference under:

```text
docs/
docs/archives/
```

XACP evolved through several generations:

```text
early XACP       MP3 / graphics / basic ARM offload
XX16c            multimedia and early Core1 baseline
XX18c            mature dynamic Core1 execution / ZZDoom
XX18m            first persistent ZZMIDI service
XX19 / v1.5      formal multimedia memory map
XX19a / v1.6     coordinated Core0 / Core1 shared DDR ABI
```

The current platform baseline is:

```text
XX19c
XACP v1.7
```
