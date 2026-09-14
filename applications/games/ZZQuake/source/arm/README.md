# ZZQuake 1.0 - ARM Core1 source (320x240 release)

This archive contains the complete source required to rebuild the ZZQuake
ARM Core1 blob used by the public 320x240 release. The Amiga 68k launcher
is a separate component and is not included here.

Reference blob:

    zzquake.bin
    MD5    26519fdf7a3836ae68afd291b21825f6
    SHA256 9d3789f340296d355a8d6fcf3e083eb1232b7344cc2effd100a6ffc0bcd9cb89

## Source layout

- `quakegeneric_source/` - exact upstream source tree used for the build,
  plus `snd_dma.c`, `snd_mem.c`, and `snd_mix.c` from the GPL Quake sources.
- `zz9000/` - ZZ9000/XACP platform code, memory/filesystem glue, audio
  backend, linker script, engine patcher, and build script.
- `patched_tree/` - the final engine source after applying the included patcher,
  provided for direct inspection; it is reproducible from the two directories above.
- `COPYING` - GNU GPL v2 text from the Quake source distribution.
- `LICENSE.quakegeneric` - license shipped by quakegeneric.

## Upstream provenance

The quakegeneric files in this archive were verified against:

    repository: https://github.com/erysdren/quakegeneric
    commit:     13052102577c629650cf07a46151a4b6e1b19c3c
    date:       2025-01-28

The local upstream tree, excluding the three added id Software sound files,
has Git tree SHA-1:

    aad02dc8f4e9efc1f5dcb1fa67aceb430299bf99

which exactly matches the `source/` tree of that commit.

## Build

Copy the source to a temporary build directory and run the validated build
script in production mode:

    mkdir -p /tmp/zzquake-build
    cp quakegeneric_source/* /tmp/zzquake-build/
    cd zz9000
    ZZQ_PROD=1 bash build_zzquake.sh /tmp/zzquake-build

Required tools:

    arm-none-eabi-gcc
    arm-none-eabi-binutils
    python3

The output blob is `zz9000/build/zzquake.blob`.

`ZZQ_PROD=1` is part of the release build contract.

## Notes

The source comments have been cleaned for publication. The cleanup changes
comments and build diagnostics only; the C/H/assembly token stream outside
comments is unchanged from the validated source package.

Quake game data is not included.
