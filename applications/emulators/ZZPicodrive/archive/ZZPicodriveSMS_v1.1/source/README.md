# ZZPicoDriveSMS 1.1 corresponding source

This directory contains the source corresponding to the public
**ZZPicoDriveSMS 1.1** release for the MNT ZZ9000.

## Release identity

Official public release archive:

```text
ZZPicodriveSMS_v1.1.lha
Size:    174795 bytes
SHA-256: 281d00e1fd91da9c0e47aa0dc1ea477a830fa59665c69ea3f362e3a1207500fc
```

The exact embedded ARM blob is:

```text
source/arm/zzpicodrive_sms_v1.1.bin
Size:    615640 bytes
SHA-256: 0ca4482bb0587719e32b06b7559e31d17b9f47cdb60ed3dc30880930d98f719e
```

The blob above was recovered directly from the final retained `blob_data.c` and
is present byte-for-byte inside the final retained 68k executable from the
release working tree.

## Release source mapping

Amiga 68k launcher:

```text
amiga/ZZPicoDriveSMS_v1.1.c
amiga/blob_data.c
amiga/bin2c.c
```

`ZZPicoDriveSMS_v1.1.c` is the final retained launcher source originally named
`ZZPicoDrive3e_INPUT.c`; it is renamed only for publication.

ARM Core1 side:

```text
arm/zzpico_core1.c
arm/zzpico_shared.h
arm/zzpico_font8x8.h
arm/zzpicodrive_sms_v1.1.bin
arm/build_sms.sh
arm/picodrive_patches.sh
arm/zzpico_*.c / .h / .S / .ld
arm/stubs/
```

The final Core1 source differs from an earlier retained source archive by the
removal of a temporary forced-PAL workaround; the final source published here
is the later version used for the public 1.1 build.

Historical technical comments and earlier internal XACP development-stage names are
retained where useful. Private development-only attribution comments were removed
from the publication copy; those comment-only edits do not change the compiled code.
The current public firmware baseline is **XX19a / XACP v1.6**.

## Upstream revisions

PicoDrive:

```text
notaz/picodrive
commit 26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee
```

Cyclone submodule at that PicoDrive commit:

```text
irixxxx/cyclone68000
commit 3ac7cf1bdeecb60e2414980e8dc72ff092f69769
```

emu2413 submodule referenced by PicoDrive:

```text
digital-sound-antiques/emu2413
commit a2dfc20ff507e4fd075cd325620bcea655e2c1f7
```

For version 1.1, YM2413/FM synthesis is disabled. Only the exact `emu2413.h`
header is needed to compile PicoDrive `sms.c`; the implementation is not linked.
The header and MIT license are retained under `third_party/emu2413/`.

## PicoDrive preparation

The historical SMS build applies two small compatibility edits to the exact
PicoDrive tree and installs the exact emu2413 header:

```text
bash arm/picodrive_patches.sh /tmp/zp/picodrive
```

The script is idempotent. It:

1. changes the `Pico32xStateLoaded()` no-op macro to a variadic no-op form for
   the `NO_32X` build;
2. guards the `p32x_event_times` reset in `state.c` when `NO_32X` is defined;
3. installs the exact emu2413 header required by `pico/sms.c`.

The retained standalone working-tree copies of `pico/sms.c`, `pico/mode4.c` and
`pico/sound/sn76496.*` were checked against commit `26ecb2b...` and are identical
to upstream; they therefore do not need separate patched copies in this source
publication.

## Historical ARM build layout

The retained build uses the original Unix-style `/tmp/zp` layout and the
`arm-none-eabi-` GNU toolchain.

1. Put the files from `arm/` directly in `/tmp/zp/`.
2. Extract the exact PicoDrive archive so its source root is `/tmp/zp/picodrive/`.
3. Extract the exact Cyclone archive into `/tmp/zp/picodrive/cpu/cyclone/`, so
   that directory contains the Cyclone Makefile and generator sources.
4. Run `bash picodrive_patches.sh /tmp/zp/picodrive`.
5. Run `bash build_sms.sh zzpicodrive_sms`.

The build script retains the final SMS compiler flags and object list. Unlike
an early retained script, the published script also generates `Cyclone.s`
automatically from the Cyclone generator sources when it is absent, using
PicoDrive's `cpu/cyclone_config.h` configuration.

The released reference blob is provided as
`arm/zzpicodrive_sms_v1.1.bin`. Reproducing the exact binary also depends on a
compatible compiler/newlib/binutils environment, so the reference blob SHA-256
is the release identity rather than a claim of toolchain-independent
reproducibility.

## 68k launcher build

The launcher source records the historical build command. The general form is:

```text
m68k-amigaos-gcc -O2 -noixemul -m68020 -Wno-pointer-sign \
    -o ZZPicoDriveSMS ZZPicoDriveSMS_v1.1.c blob_data.c -lamiga
```

The normal AmigaOS NDK, Picasso96 and AHI headers used by the source are also
required.

## Licensing

PicoDrive at the revision used here is not GPL-only. Its historical
non-commercial license requires complete source availability for modified
binary distributions.

See:

```text
../COPYING_PICODRIVE.txt
../THIRD_PARTY_NOTICES.md
third_party/
```

Original third-party copyright and license notices must be preserved.
