# Third-party source used by ZZPicoDriveSMS 1.1

## PicoDrive

```text
Repository: notaz/picodrive
Commit:     26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee
Archive:    archives/picodrive-26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee.zip
```

## Cyclone 68000

PicoDrive points to this exact submodule revision:

```text
Repository: irixxxx/cyclone68000
Commit:     3ac7cf1bdeecb60e2414980e8dc72ff092f69769
Archive:    archives/cyclone68000-3ac7cf1bdeecb60e2414980e8dc72ff092f69769.zip
```

`Cyclone.s` is not stored in the Cyclone repository at this revision. It is
emitted by the host-side `cyclone_gen` program built by the upstream Cyclone
Makefile. `arm/build_sms.sh` performs this generation automatically when needed.

## emu2413

PicoDrive references:

```text
Repository: digital-sound-antiques/emu2413
Commit:     a2dfc20ff507e4fd075cd325620bcea655e2c1f7
```

ZZPicoDriveSMS 1.1 uses only `emu2413.h` as a compile-time interface dependency;
YM2413 synthesis itself is disabled and not linked. The exact header and MIT
license are included in `emu2413/`.

The `.url` files are optional upstream references only. No network checkout is
required when the exact PicoDrive and Cyclone archives are present in
`archives/`.
