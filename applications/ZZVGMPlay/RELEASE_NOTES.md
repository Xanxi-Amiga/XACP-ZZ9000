# ZZVGMPlay v1.0 - Release notes

(c) Xanxi 2026

ZZVGMPlay brings VGM/VGZ game-music playback to AmigaOS 3.x,
with sound-chip emulation and stereo mixing performed on the
ZZ9000 / XACP Cortex-A9 Core1.

## Highlights

- Native AmigaOS 3.x GadTools player, AHI audio output and playback controls.
- VGM and compressed VGZ support, playlist management and Repeat.
- Remembered last directory and configurable default music drawer.
- Arcade systems including Sega System 18 and Neo Geo (YM2610).
- Mega Drive / Genesis, Master System FM, Atari ST, MSX SCC/SCC+,
  NES, Game Boy, PC Engine and Apple IIGS sound engines.
- Additional supported FM and PSG chip implementations.
- Optional Debug menu for WRITES / UND counters, hidden by default.
- Robust handling of player shutdown and resource cleanup.

## Installation

Keep ZZVGMPlay (68k executable) and zzvgm_core1.bin (Core1 payload)
from the same build in one drawer. Requires AmigaOS 3.x, ZZ9000,
a compatible XACP/Core1 environment and AHI.

The XACP firmware is not modified.

## Game music

Browse https://vgmrips.net/ for VGM/VGZ soundtrack packs.
Game music is not included. Compatibility varies with the chip
and command configuration used by individual files.

## Source

All required sources are bundled for offline Windows/MSYS building:

    sh build_all.sh

Third-party copyright and license notices are retained.
