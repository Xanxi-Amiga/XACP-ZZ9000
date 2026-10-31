# Third-party source archives

ZZPicoDriveMD 1.1 uses PicoDrive by notaz and contributors plus the Cyclone
68000 core.

The exact upstream source archives included with this publication are:

- `archives/picodrive-26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee.zip`
- `archives/cyclone68000-3ac7cf1bdeecb60e2414980e8dc72ff092f69769.zip`

Upstream revisions used by the released MD 1.1 build:

PicoDrive:

`26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee`

Cyclone 68000:

`3ac7cf1bdeecb60e2414980e8dc72ff092f69769`

The `.url` files in this directory are optional links to the same upstream
revisions. The source archives themselves are already present locally.

## Cyclone.s

The Cyclone repository intentionally does not store a generated `Cyclone.s` at
the revision used here.

Its upstream Makefile builds the host-side `cyclone_gen` program from the
Cyclone C/C++ generator sources and runs it to create `Cyclone.s`.

The ZZPicoDrive MD ARM build script checks for `Cyclone.s` and generates it
through the upstream Cyclone Makefile with PicoDrive's
`cpu/cyclone_config.h` configuration when it is absent.

No Sega ROM images are part of this source package.
