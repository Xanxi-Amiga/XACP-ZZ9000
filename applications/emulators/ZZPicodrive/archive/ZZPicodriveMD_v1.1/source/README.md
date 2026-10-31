# ZZPicoDriveMD 1.1 corresponding source

This directory contains the source corresponding to the public
**ZZPicoDriveMD 1.1** release for the MNT ZZ9000.

## Release source mapping

Amiga 68k launcher:

```text
amiga/ZZPicoDriveMD_v1.1.c
amiga/blob_data.c
```

ARM Core1 side:

```text
arm/zzpico_core1.c
arm/zzpicodrive_md_v1.1.bin
arm/build_cyc_audioreal.sh
arm/zzpico_*.c / .h / .S / .ld
arm/stubs/
```

The exact embedded ARM blob is:

```text
Size:    598968 bytes
SHA-256: 07484259b5a62949322198aced99369cb3f2ae0627f32a7cc6a97946b98c671b
```

`arm/zzpico_core1.c` is byte-for-byte identical to the final MD v1.1 Core1
source retained in the original build archive.

The historical source files are intentionally preserved rather than rewritten.
Some comments therefore mention earlier internal development-stage names even
though the current public firmware baseline is **XX19a / XACP v1.6**.

## Upstream PicoDrive revision

ZZPicoDriveMD 1.1 is based on:

```text
notaz/picodrive
commit 26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee
```

The Cyclone source used by this PicoDrive revision is:

```text
irixxxx/cyclone68000
commit 3ac7cf1bdeecb60e2414980e8dc72ff092f69769
```

The exact upstream source archives are already included in:

```text
third_party/archives/
```

No repository checkout or network fetch is required to obtain the corresponding
third-party source.

The `.url` files in `third_party/` are optional upstream references only.

## Historical ARM build layout

The retained build script uses the original Unix-style `/tmp/zp` layout and the
`arm-none-eabi-` GNU toolchain.

For that layout:

1. Put the files from `arm/` directly in `/tmp/zp/`.
2. Extract the included PicoDrive archive so its source root is
   `/tmp/zp/picodrive/`.
3. Extract the included Cyclone archive into
   `/tmp/zp/picodrive/cpu/cyclone/`, so that directory contains the Cyclone
   `Makefile`, `Main.cpp`, `Ea.cpp`, `Cyclone.h` and the other generator
   sources.
4. Run `arm/build_cyc_audioreal.sh` in a shell environment with the ARM GNU
   toolchain, `make` and a native C++ compiler available.

Normal archive extraction tools are sufficient for steps 1-3.

## Cyclone.s

At Cyclone commit `3ac7cf1bdeecb60e2414980e8dc72ff092f69769`,
`Cyclone.s` is intentionally **not stored in the repository**.

The upstream Cyclone Makefile defines:

```text
all: Cyclone.s
Cyclone.s: cyclone_gen
    ./cyclone_gen
```

`cyclone_gen` is built from the Cyclone C/C++ generator sources and then run on
the build host to emit `Cyclone.s`.

The retained ZZPicoDrive ARM build script checks for `Cyclone.s` and invokes the
upstream Cyclone Makefile with PicoDrive's `cpu/cyclone_config.h` configuration
to generate it when necessary before assembling it with `arm-none-eabi-gcc`.

## ARM build

The historical build script retains the original compiler flags and exact object
list used for the MD build.

The released reference blob is provided in:

```text
arm/zzpicodrive_md_v1.1.bin
```

Its SHA-256 is the release identity shown above. Reproducing the exact binary
also depends on using a compatible toolchain/build environment.

## 68k launcher build

The launcher source records the original build command. The general form is:

```text
m68k-amigaos-gcc -O2 -noixemul -m68020 -Wno-pointer-sign \
    -o ZZPicoDriveMD ZZPicoDriveMD_v1.1.c blob_data.c -lamiga
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
third_party/archives/
```

Original third-party copyright and license notices must be preserved.
