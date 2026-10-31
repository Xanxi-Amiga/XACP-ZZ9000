# ZZPicoDriveSMS third-party notices

ZZPicoDriveSMS is a ZZ9000 / AmigaOS port built around **PicoDrive**.

## PicoDrive

Upstream project: **notaz/picodrive**

Corresponding release base:

```text
PicoDrive commit: 26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee
Date:             2025-04-03
```

PicoDrive at this revision is distributed under its historical non-commercial
license, reproduced in `COPYING_PICODRIVE.txt`.

The exact PicoDrive source archive for the revision above is retained under:

```text
source/third_party/archives/
```

PicoDrive authorship, contributor information and the original `COPYING` file
are preserved inside that upstream archive.

## Cyclone 68000

The retained build links the Cyclone 68000 core referenced by the PicoDrive
revision above:

```text
Repository: irixxxx/cyclone68000
Commit:     3ac7cf1bdeecb60e2414980e8dc72ff092f69769
```

The exact Cyclone source archive for that commit is retained in
`source/third_party/archives/`.

Cyclone is dual-licensed under GNU GPL v2 and its historical MAME-style
non-commercial license. The original Cyclone license files are preserved
inside the upstream archive.

`Cyclone.s` is generated source and is not stored in the Cyclone repository at
this revision. The retained build script generates it from the upstream
Cyclone generator sources before assembling it.

## emu2413 header

PicoDrive references:

```text
Repository: digital-sound-antiques/emu2413
Commit:     a2dfc20ff507e4fd075cd325620bcea655e2c1f7
```

ZZPicoDriveSMS 1.1 does **not** link the YM2413 synthesis implementation. Its
SMS build nevertheless needs the exact `emu2413.h` interface header included by
PicoDrive `sms.c`. That exact header is preserved under `source/third_party/emu2413/`
and is licensed under the MIT License by Mitsutaka Okazaki.

## ZZ9000 / XACP integration

ZZ9000 / AmigaOS launcher, Core1 integration, shared-memory protocol,
video/audio/input/save handling and real-hardware validation:

**Xanxi, 2026**

No Sega ROM images or proprietary game data are included.

ZZ9000 is a product of MNT Research GmbH. ZZPicoDriveSMS and XACP are independent
software projects and are not official MNT Research products.
