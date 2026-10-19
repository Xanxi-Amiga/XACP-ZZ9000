# XACP Games

Games running on the ZZ9000 ARM Core1 through XACP.

## Available games

### ZZDoom
Doom engine running on the ZZ9000 ARM Core1, with AmigaOS handling RTG display,
input, audio, MIDI and file access.

### ZZQuake
Quake software renderer running on the ZZ9000 ARM Core1, with AmigaOS handling
Picasso96 display, input, audio and file access. The public 1.0 release targets
320x240 in 32-bit RTG mode. Quake game data is not included.

### ZZQuake HighRes
Companion high-resolution pack for ZZQuake with separate 640x480, 800x600 and
1024x768 builds. Each variant uses its own Amiga launcher and matching Core1
blob. The original ZZQuake 1.0 320x240 release remains available separately.
Quake game data is not included.

### ZZDarkForcesNEXT
The Force Engine / STAR WARS: Dark Forces running natively on the ZZ9000 ARM
Core1, with AmigaOS handling Picasso96 display, input, AHI audio, filesystem
access and optional CAMD MIDI output. Supports resolutions from the original
320x200 up to 800x600, internal SoundFont music, external MIDI and selected
modern TFE features including Smooth VUE, autorun, crouch toggle and enhanced
secret/automap options. Original Dark Forces game data is not included.

### ZZQuake2 1.0

[ZZQuake2](ZZQuake2/README.md) brings Quake II to the ZZ9000 ARM Core1.
It provides three rendering backends:

- Core1 software: 320x240, 640x480 and 800x600.
- ARM TinyGL software OpenGL: 320x240 and 640x480.
- FPGA RasterIX hardware acceleration: 320x240, 640x480 and 800x600.

The RasterIX mode requires the [XX19d-RIX firmware](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/XX19d-RIX).
Software and TinyGL rendering remain compatible with XX19c.
The 68k handles AmigaOS integration, Picasso96 display, input, file access
and audio. User-supplied Quake II data is required.

[Download ZZQuake2 1.0](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZQuake2-1.0).

### ZZRastan
Standalone recreation of the Taito Rastan arcade hardware for ZZ9000 Core1.
The original arcade CPUs and hardware are emulated on the ARM side, while the
Amiga handles launch, RTG display, input and AHI audio.

See each game directory for requirements, installation and licensing details.
