# XACP Applications

This directory contains the applications, games, emulators, multimedia tools and validation software developed for the Xanxi XACP platform on the MNT ZZ9000.

XACP applications use the ARM Cortex-A9 processors and DDR memory of the ZZ9000 to offload workloads from the Amiga 68k while retaining AmigaOS integration for display, input, files, audio and user interfaces.

The current platform baseline is:

```text
Firmware:  XX19b
Protocol:  XACP v1.7
```

## Current compatibility

**All current public XACP applications in this repository are compatible with the latest firmware published in the repository.**

Historical firmware builds are retained for reference, regression testing and documentation of the platform's evolution, but they are not required for normal use of current applications.

The only exception is the set of **very early fractal / Core1 demonstration programs from early May 2026**. These predate the current XACP architecture and are preserved for historical purposes only. They should not be considered part of the current compatibility baseline.

---

## Application overview

| Directory                               | Application               | Role                                           | Status               |
| --------------------------------------- | ------------------------- | ---------------------------------------------- | -------------------- |
| `ZZMIDI/`                               | **ZZMIDI**                | SoundFont MIDI synthesis using the ZZ9000 ARM  | Current              |
| `ZZSpeech/`                             | **ZZSpeech**              | CMU Flite speech synthesis on ARM Core1        | Current              |
| `emulators/ZZPicodrive/ZZPicodriveMD/`  | **ZZPicoDriveMD 1.1**     | Sega Mega Drive / Genesis emulator             | Current              |
| `emulators/ZZPicodrive/ZZPicodriveSMS/` | **ZZPicoDriveSMS 1.1**    | Sega Master System emulator                    | Current              |
| `games/ZZDoom/`                         | **ZZDoom**                | Doom engine running on ARM Core1               | Current              |
| `games/ZZQuake/`                        | **ZZQuake 1.0**           | Quake software renderer running on ARM Core1   | Current              |
| `games/ZZDarkForcesNEXT/`               | **ZZDarkForcesNEXT**      | The Force Engine / Dark Forces on ARM Core1    | Current              |
| `games/ZZRastan/`                       | **ZZRastan 1.0**          | Rastan arcade hardware recreation on ARM Core1 | Current              |
| `mpegplayer/`                           | **ZZ-MPEG**               | MPEG-1 video / MP2 audio playback              | Advanced Beta        |
| `mp3/`                                  | **ZZMP3Play / ZZPlayGUI** | ARM-accelerated MP3 / MP2 playback             | Current / historical |
| `mp3/`                                  | **zz9000.engine**          | AmigaAMP external ARM decoding engine          | Current / historical |
| `mp3/`                                  | **mpega.library**          | XACP-backed MPEGA-compatible decoding          | Current / historical |
| `benchmarks/`                           | **ZZBench GUI**           | 68k / ARM / memory bandwidth benchmark         | Current              |
| `fractals/core1-julia-v2/`              | **JuliaV2**               | Core1 execution and clean-return validation    | Current validation   |
| `fractals/`                             | **early ZZFractal demos** | Early XACP graphical experiments               | Historical only      |

Detailed installation, usage and licensing information belongs in each application's own directory.

---

# ZZMIDI

Directory:

```text
ZZMIDI/
```

ZZMIDI provides General MIDI / SoundFont synthesis using the ZZ9000 ARM.

The Amiga side handles the user interface, files and AHI playback while the firmware-side ARM service performs MIDI parsing and SoundFont synthesis using TinySoundFont.

Main capabilities include:

```text
SoundFont MIDI synthesis
TinySoundFont ARM rendering
TinyMidiLoader MIDI parsing
SoundFont banks up to the supported XACP staging limit
AHI audio playback
GUI MIDI file playback
persistent SoundFont reuse
XACP shared MIDI service
```

ZZMIDI is an example of a persistent **Core0 firmware service**, unlike applications such as ZZPicoDrive or ZZDoom which execute complete engines on Core1.

The ZZMIDI firmware memory regions are protected by the XACP shared DDR allocation model.

See the ZZMIDI directory for detailed release documentation and SoundFont recommendations.

---

# ZZSpeech

Directory:

```text
ZZSpeech/
```

ZZSpeech provides speech synthesis for AmigaOS using CMU Flite running on ZZ9000 ARM Core1.

The Amiga side provides:

```text
resident daemon
Intuition / GadTools GUI
ZZSay command-line client
ZZSPEECH: DOS handler
AHI playback
WAV export
external .flitevox voice support
```

Unlike ZZMIDI, ZZSpeech does not require the speech engine to be permanently built into the main firmware.

The Flite Core1 program is loaded dynamically when ZZSpeech starts.

Only one dynamically loaded Core1 application can execute at a time, so ZZSpeech should be stopped cleanly before launching another Core1 application such as ZZPicoDrive, ZZDoom, ZZQuake, ZZDarkForcesNEXT or ZZRastan.

ZZSpeech 1.0 is distributed as closed-source freeware. See its own documentation and license files for redistribution and third-party licensing terms.

---

# ZZPicoDrive

Directory:

```text
emulators/ZZPicodrive/
```

ZZPicoDrive runs the PicoDrive emulation engine on ZZ9000 ARM Core1 while the Amiga side provides file access, RTG display, input, audio output, SRAM and savestate handling.

Two public editions are currently included.

## ZZPicoDriveMD 1.1

Directory:

```text
emulators/ZZPicodrive/ZZPicodriveMD/
```

Sega Mega Drive / Genesis emulator.

Main features include:

```text
Mega Drive / Genesis emulation
ARM Core1 execution
PAL and NTSC operation
Picasso96 RTG output
AHI stereo audio
direct Paula DMA audio
keyboard input
DB9 joystick input
lowlevel.library / CD32-style controllers
two-player support
SRAM
savestates
embedded ARM Core1 blob
```

No commercial ROMs are included.

---

## ZZPicoDriveSMS 1.1

Directory:

```text
emulators/ZZPicodrive/ZZPicodriveSMS/
```

Sega Master System edition of ZZPicoDrive.

It uses the same general XACP / Core1 architecture as the Mega Drive edition.

Features include:

```text
Master System emulation
ARM Core1 execution
PAL and NTSC operation
Picasso96 RTG output
AHI or Paula audio
keyboard / joystick / controller input
two-player support
SRAM
savestates
```

YM2413 / FM audio is not enabled in the 1.1 release.

No commercial ROMs are included.

---

# ZZDoom

Directory:

```text
games/ZZDoom/
```

ZZDoom is a Doom port based on `doomgeneric`.

The Doom engine runs on the ZZ9000 ARM Core1 while AmigaOS provides the host environment.

Main features include:

```text
ARM Core1 Doom engine
320x200 output
640x400 output
Picasso96 RTG display
keyboard input
AHI sound effects
MIDI integration
AmigaOS file access
save / load support
clean Core1 stop and return
```

ZZDoom was one of the projects that established the practical dynamic Core1 application model later reused by more complex XACP software.

Historical ZZDoom binaries are retained in the application's own archive directory.

---

# ZZQuake

Directory:

```text
games/ZZQuake/
```

ZZQuake 1.0 runs the Quake software renderer on the ZZ9000 ARM Cortex-A9 Core1 while AmigaOS provides the launcher, Picasso96 display, input, file access and audio integration.

The current public release targets 320x240 in 32-bit RTG mode.

Main features include:

```text
ARM Core1 Quake engine
320x240 software rendering
Picasso96 RTG display
keyboard and mouse input
AHI sound effects
CD audio support
MP3 music backend
MHI music backend
registered Quake support
Scourge of Armagon support
Dissolution of Eternity support
AmigaOS file access
save / load support
clean Core1 stop and return
```

Quake game data is not included. Users must provide their own `pak0.pak` and, for registered Quake, `pak1.pak`.

The ARM Core1 component is derived from GPL-licensed Quake / quakegeneric code. Corresponding source is included in the ZZQuake directory. The Amiga-side launcher is distributed separately as proprietary freeware.

---

# ZZDarkForcesNEXT

Directory:

```text
games/ZZDarkForcesNEXT/
```

ZZDarkForcesNEXT runs The Force Engine and STAR WARS: Dark Forces natively on the ZZ9000 ARM Cortex-A9 Core1 while AmigaOS provides Workbench integration, Picasso96 display, input, AHI audio, filesystem access and optional CAMD MIDI.

Unlike a minimal reproduction of the original DOS version, ZZDarkForcesNEXT also exposes selected modern The Force Engine features while retaining the classic software-rendered game.

Main features include:

```text
native TFE execution on ARM Core1
320x200 to 800x600 software rendering
Picasso96 asynchronous triple buffering
Workbench launch and ToolTypes
AHI sound effects
internal SoundFont music
external CAMD MIDI
save/load and quicksave/quickload
pilot progression
persistent in-game configuration
Smooth VUE
Autorun
Crouch Toggle
secret-found messages
automap key colors
automap secret display
clean Core1 stop and return
```

Original Dark Forces commercial data is not included.

The TFE-derived Core1 component and corresponding source are distributed under GPL-2.0. The Amiga-side launcher is distributed separately as a closed-source binary.

---

# ZZRastan

Directory:

```text
games/ZZRastan/
```

ZZRastan 1.0 is a standalone recreation of the original Rastan arcade hardware for Amiga systems equipped with the ZZ9000.

It is not a normal 68k game port.

The arcade CPUs and hardware are emulated on the ZZ9000 ARM Cortex-A9 Core1 while AmigaOS provides:

```text
program launch
Picasso96 / RTG display
keyboard and joystick input
AHI audio output
ROM loading
```

The ARM side implements:

```text
Cyclone 68000 execution
CZ80 Z80 execution
Taito video hardware
YM2151 FM audio
MSM5205 ADPCM audio
game scheduling
framebuffer rendering
```

Rastan World and Rastan Saga Japan ROM sets are supported.

No commercial game ROMs or game assets are distributed.

The source corresponding to the ARM Core1 blob is included where required by the applicable third-party licenses. The Amiga-side launcher and GUI remain proprietary.

---
