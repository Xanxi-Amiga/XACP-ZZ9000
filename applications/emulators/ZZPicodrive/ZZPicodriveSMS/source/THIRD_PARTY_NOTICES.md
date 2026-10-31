# Third-party notices

ZZPicoDriveSMS is a modified binary distribution of PicoDrive. The complete
source of every component used by the program is included in this package.
No ROM and no ROM-derived data are included.

## PicoDrive (`third_party/picodrive/`)

Copyright (c) notaz, 2006-2013, and contributors; see `AUTHORS` and `COPYING`
in the source tree and `COPYING_PICODRIVE.txt`.

Revision: notaz/picodrive `26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee`.

Licence: PicoDrive's historical non-commercial licence. Redistributions may
not be sold or used in a commercial product, and modified binary
redistributions must include the complete source code.

Modifications in this package:
- `pico/pico_int.h` (one macro) and `pico/state.c` (one `#ifndef NO_32X`) so
  that the tree builds without the 32X code;
- `pico/sound/sound.c`: saturating PSG + FM mix and clamped FM sample, under
  `ZZPICO_FM_SAT` (diff in `docs/`);
- `pico/sound/sound.c`: with `ZZPICO_FM_NATIVE`, the OPLL outputs at its
  native rate in PAL too (no emu2413 rate converter before the FIR);
- `pico/sms.c` + `pico/sound/sound.c`: with `ZZPICO_SMS_FM_MIX`, port $F2
  bits D1 D0 drive the audio mixer of the Japanese console in the Japan
  region (00 PSG, 01 FM, 10 none, 11 both), as in Genesis Plus GX;
- `pico/sms.c`: with `ZZPICO_SMS_JAP_IO`, a Japanese console reads back 0 on
  the TH/TR pins set as outputs (ports $DC/$DD), as the 315-5297 I/O chip
  and Genesis Plus GX do (diff in `docs/`);
- `pico/sound/emu2413/`: emu2413 1.6.3 (see below), used by
  `pico/sound/ym2413.c`.

Components with their own terms, unchanged: CZ80 (Stephane Dallongeville);
FAME/C and Musashi are present but not compiled.

## emu2413 (`third_party/emu2413/`, `pico/sound/emu2413/`)

Copyright (c) 2001-2019 Mitsutaka Okazaki, MIT License
(`third_party/emu2413/LICENSE`). Version 1.6.3, upstream
digital-sound-antiques/emu2413 commit
`83254be0de24b3506438c45b2870017885cb778c`, the copy used and validated in
ZZVGMPlay 1.0 (`third_party/emu2413/README_ZZVGM.txt`). Compiled (YM2413 FM).

## Cyclone 68000 (`third_party/picodrive/cpu/cyclone/`)

Copyright (c) 2004, 2011 Dave "FinalDave" Haywood, notaz, irixxxx; MAME
licence / GPLv2 (`License.txt`, `License-GPLv2.txt`).

Revision: irixxxx/cyclone68000 `3ac7cf1bdeecb60e2414980e8dc72ff092f69769`,
unmodified. `Cyclone.s` is the generator output for `cpu/cyclone_config.h`,
shipped pre-generated (MD5 `ff097962906336cf7879c22ff42bcc18`).

## ZZPicoDriveSMS (`amiga/`, `Core1/`, `tools/`, build scripts)

Copyright (c) 2026 Xanxi (Xanxi-Amiga / XACP-ZZ9000).

Sega and Master System are trademarks of SEGA. Not affiliated.
