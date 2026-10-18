# ZZVGMPlay v1.0 - Release notes

**(c) Xanxi 2026**

The first public version of ZZVGMPlay brings VGM/VGZ playback to AmigaOS 3.x using the ZZ9000's Cortex-A9 Core1 for emulation and audio mixing.

### Highlights

- Native 68k GadTools player with playlist, Repeat, AHI output, selectable default drawer and time/progress display.
- Broad FM, PSG, arcade and console chip support, including OPL2/OPL3, YM2612, YM2151, AY/YM2149, SMS FM/YM2413, SCC/SCC+ and YM2610.
- Additional System 18 backend for dual YM2612 plus RF5C68 PCM, bringing Sega's **Michael Jackson's Moonwalker** arcade soundtrack into the supported test set.
- System 18 final mix gain adjusted by +6.02 dB; the 22-track reference pack passes the production-parser host test with zero clipped output samples.
- Cleaner default playback status; **Debug > Show WRITES / UND** makes technical counters available on demand.
- Defensive shutdown cleanup for asynchronous AHI requests, GUI resources and Core1 STOP handoff.

### Installation

Place `ZZVGMPlay` and the matching `zzvgm_core1.bin` in the same Amiga directory. Run the 68k executable on an AmigaOS 3.x system equipped with ZZ9000, the compatible XACP/Core1 environment and AHI.

The XACP firmware is unchanged. The source distribution builds fully offline via `sh build_all.sh` under the configured Windows/MSYS toolchains.

### Notes

Game-music files are not included. Some chips and command combinations remain dependent on the original game's VGM data and are not guaranteed by the Moonwalker regression test. The GUI and shutdown changes require final real-Amiga acceptance testing before tagging the public release.
