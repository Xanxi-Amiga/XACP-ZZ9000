# ZZStrider 1.0

**ZZStrider** is a standalone emulator of the Capcom CPS-1 arcade hardware for Strider (1989), for classic Amiga systems equipped with an MNT ZZ9000. It combines the ZZRastan/XACP architecture with an adapted FinalBurn Alpha CPS-1 emulation engine running natively on ARM Core1. It is not a port of MAME.

**ZZStrider is freeware for non-commercial use. It is not open-source software as a whole.**

ZZStrider builds on the ZZRastan infrastructure. The arcade machine runs on the second Cortex-A9 core (Core1) of the ZZ9000: 68000, Z80, CPS-1 video, YM2151 and MSM6295. The Amiga handles display presentation through Picasso96, input and AHI audio. Rendering is performed in software on Core1.

## Performance

Full arcade speed (approximately 59.64 FPS) was reported during development on an A4000 with a 68060 at 50 MHz (approximately 20% 68k CPU usage), and on a 68030 system tested by Fitzsteve (approximately 70% 68k CPU usage). These figures are hardware-dependent.

## Requirements

- AmigaOS 3.x, 68030 or better.
- MNT ZZ9000. Requires firmware XX19d-RIX or later XACP-compatible firmware.
- Picasso96 with a 640x480 32-bit screen mode.
- AHI unit 0 for audio.
- The 16 ROM files of the MAME `strider` parent set. No ROM images are distributed.

## Installation and use

Extract the binary archive to an Amiga drawer. Place the unzipped ROM files in its `roms` drawer:

```text
30.11f   35.11h   31.12f   36.12h   st-14.8h
st-2.8a  st-11.10a st-5.4a st-9.6a st-1.7a st-10.9a st-4.3a st-8.5a
09.12b   18.11c   19.12c
```

Every ROM is checked against its expected CRC32. Launch `ZZStriderGUI`, select the ROM drawer, sound and difficulty, then choose Play. Alternatively, from a Shell:

```text
ZZStrider roms zzstrider.bin
```

For a timed 60-second run with a report:

```text
ZZStrider roms zzstrider.bin 60 >RAM:strider.log
```

Optional arguments: `db9`, `p0` / `p1` (default `p1`), `nosound`, `poll=N` (1000-8000 microseconds, default 2000), `dipa=XX`, `dipb=XX`, `dipc=XX`.

Keyboard: 5 = coin, 1 = start, arrows = move, A = attack, S = jump, P = pause, Esc or mouse click = quit. DB9 joysticks and compatible lowlevel.library controllers are supported.

## Licensing and distribution

ZZStrider 1.0 is distributed as freeware for non-commercial use.

### Amiga launcher and GUI

The Amiga 68k launcher and GUI are separate proprietary closed-source programs.
Copyright (c) 2026 Xanxi. All rights reserved. They may be redistributed unmodified as part of the complete non-commercial ZZStrider package. No permission is granted to modify or relicense these proprietary components.

### ARM Core1 blob

The ARM blob contains ZZStrider-specific code and third-party emulation components under different licence terms; it does not have a single blanket licence. The source distribution is provided in `ZZStrider_1.0_src.zip`, including the adapted FinalBurn Alpha 2012 CPS-1 engine, FBNeo and MAME-derived adaptations, Cyclone 68000 under its historical MAME licence option (not GPLv2), CZ80, PicoDrive headers and Jarek Burczynski's YM2151 implementation. The MSM6295 implementation follows the BSD-licensed MAME reference.

The applicable third-party terms, including publication of modified source, non-commercial distribution and restrictions on soliciting donations, must be respected. See `licenses/README.md` and the full licence texts included in both archives. No repository-wide licence overrides these terms. No ROMs or other commercial game data are included. Strider remains the property of its respective rights holders.

## Downloads

Both the binary package and ARM source archive are available from the [ZZStrider 1.0 release](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZStrider_V1.0).

## Credits

Xanxi (ZZ9000/XACP integration); FB Alpha, FBNeo and MAME contributors; FinalDave and notaz (Cyclone 68000); notaz and PicoDrive contributors; Stephane Dallongeville and NJ (CZ80); Jarek Burczynski (YM2151); Mirko Buffoni, Aaron Giles and Andrew Gardner (MAME OKI sound reference); MNT Research (ZZ9000); Fitzsteve (68030 testing).

