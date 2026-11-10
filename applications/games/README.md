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

### ZZQuakeGL 1.0

[ZZQuakeGL](ZZQuakeGL/README.md) runs Quake 1 with id Software's original
GLQuake renderer, hardware-accelerated by RasterIX in the ZZ9000 FPGA. The
engine and GL renderer execute on ARM Cortex-A9 Core1, while the Amiga 68k
handles Picasso96 display, input, audio and file access.

Four rendering resolutions are supported: 320x240 (106.5 FPS), 640x480
(48.0 FPS), 800x600 (35.2 FPS) and 1024x768 (24.1 FPS), measured using
`timedemo demo1` on an Amiga 4000/68060 with ZZ9000 at 16-bit colour.

Requires XX19d-RIX / XACP v1.8 and an XACP-compatible `zz9000.card`.
The Workbench icon defaults to 640x480, MUSIC=MP3 and MAXFPS=50.
Original Quake `id1` data is required and is not distributed. Mission packs
and `GAME=` mods are not supported in v1.0. The Core1 sources are GPL-covered;
the Amiga launcher is binary-only freeware.

[Download ZZQuakeGL 1.0](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZQuakeGL-1.0) |
[Watch gameplay video](https://youtu.be/cV312wjwoPc).

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

### ZZVirtuaRacingRIX 1.0

[ZZVirtuaRacingRIX](ZZVirtuaRacingRIX/README.md) enhances Sega's
Virtua Racing Deluxe (32X) with hardware-accelerated 3D graphics rendered
by the RasterIX GPU in the ZZ9000 FPGA at 640x480. Interpolation provides
up to 60 displayed images per second; original game logic and sound
remain emulated by PicoDrive on ARM Core1.

Improved shadows, smoke, sky, track draw distance and wheel effects.
Requires XX19d-RIX or later compatible XACP firmware for RasterIX.
The Workbench setup builds the executable from a user-supplied USA ROM;
the game ROM is not included.

[Download ZZVirtuaRacingRIX 1.0](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZVirtuaRacingRIX-1.0) |
[Watch gameplay video](https://youtu.be/NvPGxhvwGZo).

### ZZRastan
Standalone recreation of the Taito Rastan arcade hardware for ZZ9000 Core1.
The original arcade CPUs and hardware are emulated on the ARM side, while the
Amiga handles launch, RTG display, input and AHI audio.

See each game directory for requirements, installation and licensing details.
