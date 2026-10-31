# Third-party notices

ZZPicoDriveMD is a modified binary distribution of PicoDrive. The complete
source of every component used by the program is included in this package.
No ROM and no ROM-derived data are included.

## PicoDrive (`third_party/picodrive/`)

Copyright (c) notaz, 2006-2013, and contributors; see `AUTHORS` and `COPYING`
in the source tree and `COPYING_PICODRIVE.txt`.

Revision: notaz/picodrive `26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee`.

Licence: PicoDrive's historical non-commercial licence. Redistributions may
not be sold or used in a commercial product, and modified binary
redistributions must include the complete source code.

Modifications in this package: `pico/pico_int.h` (one macro) and
`pico/state.c` (one `#ifndef NO_32X`) so that the tree builds without the 32X
code. Components with their own terms, unchanged: CZ80 (Stephane
Dallongeville); FAME/C, Musashi and emu2413 are present but not compiled.

## Cyclone 68000 (`third_party/picodrive/cpu/cyclone/`)

Copyright (c) 2004, 2011 Dave "FinalDave" Haywood, notaz, irixxxx; MAME
licence / GPLv2 (`License.txt`, `License-GPLv2.txt`).

Revision: irixxxx/cyclone68000 `3ac7cf1bdeecb60e2414980e8dc72ff092f69769`,
unmodified. `Cyclone.s` is the generator output for `cpu/cyclone_config.h`,
shipped pre-generated (MD5 `ff097962906336cf7879c22ff42bcc18`).

## ZZPicoDriveMD (`amiga/`, `Core1/`, `tools/`, build scripts)

Copyright (c) 2026 Xanxi (Xanxi-Amiga / XACP-ZZ9000).

Sega, Mega Drive and Genesis are trademarks of SEGA. Not affiliated.
