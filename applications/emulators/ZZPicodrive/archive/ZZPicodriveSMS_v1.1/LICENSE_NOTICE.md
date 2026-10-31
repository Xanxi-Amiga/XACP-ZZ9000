# License / source notice

ZZPicoDriveSMS is distributed without any commercial ROM image.

The emulation core is **PicoDrive by notaz and contributors**.

ZZ9000 / AmigaOS port, launcher, ARM Core1 integration,
audio/video/input/save handling and XACP integration are by **Xanxi**.

The corresponding source for ZZPicoDriveSMS 1.1 is published under:

```text
source/
```

The release is based on PicoDrive commit:

```text
26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee
```

PicoDrive at this revision uses its historical non-commercial license. Modified
redistributions must include the complete source code for the components used by
the binary.

The corresponding Cyclone revision is:

```text
3ac7cf1bdeecb60e2414980e8dc72ff092f69769
```

The SMS build also uses the `emu2413.h` header from emu2413 commit
`a2dfc20ff507e4fd075cd325620bcea655e2c1f7`. The header is reproduced in the
source package with its MIT license. YM2413 synthesis is not linked into this
release.

The PicoDrive license text is reproduced at `COPYING_PICODRIVE.txt`; original
PicoDrive and Cyclone notices are preserved in their exact upstream source
archives.

See `THIRD_PARTY_NOTICES.md` and `source/third_party/README.md`.
