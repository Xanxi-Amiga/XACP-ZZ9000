# ZZMIDI

**ZZMIDI 1.0** is a General MIDI SoundFont synthesizer for Amiga systems equipped with an **MNT ZZ9000** running **XACP-compatible firmware**.

The Amiga sends MIDI data to the ZZ9000, where the ARM Cortex-A9 runs the SoundFont synthesis engine. Audio is returned to the Amiga and played through **AHI**, leaving the 68k CPU almost completely free for the application or game.

ZZMIDI supports standard **CAMD** applications, standalone MIDI file playback, and ZZDoom integration.

---

## Features

* General MIDI SoundFont synthesis on the ZZ9000 ARM
* SoundFont 2 (`.sf2`) banks up to **10 MiB**
* 64 voices
* 32 kHz, 16-bit stereo output through AHI
* realtime CAMD service
* GadTools preferences/control panel
* Shell control tools
* standalone MIDI player: **ZZMIDIPlay**
* SoundFont analyser: **ZZSF2Info**
* Core0/Core1 coexistence with current **XX19c** firmware
* **ZZMIDIGate** compatibility helper for older XX19a/XX19b firmware
* very low 68k CPU usage

Applications using standard CAMD clusters can use ZZMIDI without being specifically written for it.

Examples include:

* OpenDune
* DarkForces
* DoomAttack (with CAMDDoomSound patch)
* MIDIKeys virtual keyboard
* CAMD MIDI players and sequencers
* ZZDoom

---

## Firmware requirement

ZZMIDI 1.0 requires:

* **XACP v1.6 / firmware XX19a or later**, or
* another firmware explicitly documented as compatible with the ZZMIDI/XACP interface.

The current recommended firmware is:

**Firmware XX19c / XACP v1.7**

https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/XX19c

XX19c keeps the XACP v1.7 ABI and fixes the Core0/Core1 startup conflict that could occur when a Core1 application was launched while ZZMIDI realtime was active.

### Important

ZZMIDI 1.0 is **not compatible with the official MNT ZZ9000 firmware 1.13**.

ZZMIDI 1.0 is **not compatible with BlitterStudio firmware releases**, unless a future firmware is explicitly documented as XACP-compatible.

Do not assume that a firmware is compatible merely because it runs on a ZZ9000.

---

## Requirements

* AmigaOS 3.1 or later
* 68020 or better
* MNT ZZ9000
* XACP-compatible firmware, with **XX19c / XACP v1.7 recommended**
* AHI
* `camd.library`

---

## CAMD setup

ZZMIDI realtime MIDI operation requires `camd.library`.

The standard CAMD package is available from Aminet:

https://aminet.net/mus/midi/camd.lha

Installing `camd.library` alone is **not enough**.

Run the **MidiPorts** preferences program included with the CAMD package and associate the standard CAMD clusters:

```text
out.0
in.0
```

with the **Amiga serial port**, then save the configuration.

ZZMIDI listens to `out.0` by default.

---

## AHI configuration for games

If a game uses AHI for sound effects or speech **in addition to ZZMIDI music**, configure:

```text
AHI Unit 0: 2 channels
```

Do **not** use a single channel in this situation.

With only one channel, the game audio and ZZMIDI can compete for the same AHI output and cause audio failures or other problems.

---

## ZZDoom, Core1 applications and ZZMIDIGate

### XX19c and later compatible firmware

Firmware **XX19c** fixes the Core0/Core1 startup/cache-coherency conflict that could occur when a Core1 application was launched while the ZZMIDI realtime engine was active.

With XX19c, **ZZMIDIGate is no longer required for the historical ZZDoom/ZZMIDI Core0/Core1 startup conflict**.

ZZDoom and other compatible Core1 applications can therefore be launched normally while ZZMIDI is active.

### Older XX19a / XX19b firmware

On older **XX19a** and **XX19b** firmware, starting ZZDoom or another Core1 application while the ZZMIDI realtime engine is active can cause a white screen or system lock-up.

For those older firmware versions, use **ZZMIDIGate** before launching the Core1 application.

Procedure:

1. Start ZZMIDI normally.

2. Run:

```text
ZZMIDIGate
```

3. Wait for:

```text
*** realtime paused -- LAUNCH CORE1 APP NOW ***
```

4. Only then launch ZZDoom or the other Core1 application.

ZZMIDIGate pauses ZZMIDI realtime before the Core1 startup, waits until the Core1 application is running, then restores and verifies ZZMIDI realtime.

When ZZDoom exits, ZZMIDIGate detects the exit, performs a ZZMIDI `RT_OFF` / `RT_ON` re-prime, verifies that the service has been restored, and then exits.

For Core1 applications that are not automatically detected, leave ZZMIDIGate running until the application has finished, then return to its Shell and press **Ctrl-C** so it can restore ZZMIDI realtime and exit cleanly.

ZZMIDIGate is retained for compatibility with older XACP firmware installations; it is not required for this historical startup issue on XX19c.

---

## Included applications

The ZZMIDI 1.0 release includes:

### ZZMIDIDaemon

Resident realtime SoundFont synthesis service.

### ZZMIDICAMDIn

CAMD receiver feeding realtime MIDI events to ZZMIDI.

### ZZMIDIPrefs

Workbench/GadTools control panel for selecting a SoundFont and starting, stopping or restarting the service.

### ZZMIDIctl

Shell control utility.

### ZZMIDIGate

Compatibility helper for safely using ZZMIDI with Core1 applications on older XX19a/XX19b firmware.

### ZZMIDIPlay

Standalone GUI MIDI file player using the ZZ9000 SoundFont engine.

ZZMIDIPlay lets you listen to MIDI files **without starting the realtime CAMD synthesizer service**.

It loads and uses a SoundFont directly, independently of ZZMIDIDaemon / ZZMIDICAMDIn.

Typical use:

1. Launch ZZMIDIPlay.
2. Use the **rightmost button** to select and load a SoundFont (`.sf2`).
3. Add one or more MIDI files to the playlist.
4. Play them directly through the ZZ9000 synthesis engine and AHI.

This makes ZZMIDIPlay useful both as a simple standalone MIDI player and as a convenient way to audition a SoundFont before using it with the realtime CAMD service.

### ZZSF2Info

SoundFont analysis utility reporting size and synthesis complexity.

---

## SoundFonts

The ZZMIDI realtime service accepts SoundFont 2 banks up to:

```text
10 MiB
10485760 bytes
```

For ZZMIDI 1.0:

* avoid spaces in SoundFont paths
* keep paths below approximately 250 characters
* plain ASCII names are recommended
* on classic FFS partitions, keep individual file names within the filesystem limits

`ZZSF2Info` can be used to inspect a SoundFont before loading it.

---

## Historical versions

The original public **ZZMIDIPlay v0.5** release targeted:

```text
XACP v1.5
Firmware XX19
```

Its source code and documentation are retained for historical reference under:

```text
archive/ZZMIDIPlay-v0.5/
```

**ZZMIDIPlay v0.5 is not compatible with the current XX19c / XACP v1.7 baseline.**

It uses the older XACP v1.5 shared-memory layout and must not be used with ZZMIDI 1.0 or current XACP v1.7 firmware.

Use the current ZZMIDI 1.0 `ZZMIDIPlay` instead.

---

## Download

Binary releases are provided through the GitHub **Releases** section.

The ZZMIDI 1.0 distribution is supplied as an Amiga `.lha` archive containing the programs, documentation and applicable third-party license notices.

---

## License

### ZZMIDI 1.0

ZZMIDI 1.0 is **proprietary freeware**.

Copyright © 2026 Xanxi.
All rights reserved.

The current Amiga-side ZZMIDI application source code is **not distributed**.

Third-party applications may freely use ZZMIDI through its documented CAMD and command-line interfaces.

Developers wishing to embed, bundle, integrate or redistribute ZZMIDI itself as part of another software package or distribution are welcome to contact the author for permission.

The historical ZZMIDIPlay v0.5 source remains available in the archive because it was previously published. Its presence does not make ZZMIDI 1.0 open source.

### Firmware

The XACP / XX19c firmware is a separate project and is distributed under its own open-source licensing terms.

### Third-party components

ZZMIDI uses:

* **TinySoundFont** by Bernhard Schelling — MIT License
* **TinyMidiLoader** by Bernhard Schelling — zlib License

Additional data files included in binary distributions retain their respective original licenses.

---

## Author

**Xanxi**

2026

ZZ9000 is a product of MNT Research GmbH.

ZZMIDI is an independent software project and is not an official MNT Research product.
