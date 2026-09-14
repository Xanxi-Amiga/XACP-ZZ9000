# ZZQuake 1.0

ZZQuake runs the Quake software renderer on the second ARM Cortex-A9 core of
the MNT ZZ9000 while AmigaOS remains responsible for host integration such as
Picasso96 display, input, file access and audio output.

The public 1.0 release is the **320x240 32-bit** edition.

## Requirements

- Amiga with MNT ZZ9000
- current XACP-compatible ZZ9000 firmware; the release was validated with
  firmware XX19b / XACP v1.7
- Picasso96 RTG
- original Quake game data

No Quake game data is included.

## Installation

Copy the ZZQuake files to a directory on the Amiga and place your own Quake
data in `id1/`:

```text
Shareware:
    id1/pak0.pak

Registered Quake:
    id1/pak0.pak
    id1/pak1.pak
```

Launch `ZZQuake` from Workbench or Shell.

## Workbench ToolTypes

The supplied icon contains these disabled options:

```text
(HIPNOTIC)
(ROGUE)
(GAME=copper)
(MUSIC=MP3)
(MUSIC=MHI)
(MHIDRIVER=mhiamiblaster.library)
(MUSIC=CD)
(CDDEVICE=scsi.device)
(CDUNIT=1)
```

Remove the surrounding parentheses to enable an option.

`MUSIC=MHI` requires a compatible MHI driver. The default example is
`mhiamiblaster.library`.

`MUSIC=CD` uses the configured Amiga device/unit for CD audio.

## Command line

Normal use does not require passing the PAK files explicitly. With the Quake
data installed under `PROGDIR:id1/`, simply run:

```text
ZZQuake
```

Mission packs and music backends can also be selected from the command line
using the same keywords as the Workbench ToolTypes.

A `DEBUG` command-line option enables verbose launcher diagnostics. It is not
part of normal Workbench operation.

## Release scope

ZZQuake 1.0 is released at 320x240 only. Higher-resolution builds are not part
of this release.

## Source and licensing

`zzquake.bin` contains the ARM Core1 Quake engine and is derived from the GPL
Quake / quakegeneric source. Complete corresponding source is included under:

```text
source/arm/
```

The source is based on `erysdren/quakegeneric` commit:

```text
13052102577c629650cf07a46151a4b6e1b19c3c
```

The release ARM source rebuilds a blob bit-identical to the validated binary:

```text
zzquake.bin
MD5    26519fdf7a3836ae68afd291b21825f6
SHA256 9d3789f340296d355a8d6fcf3e083eb1232b7344cc2effd100a6ffc0bcd9cb89
```

The Amiga 68k launcher is a separate proprietary freeware component. Its source
is not included in the public repository.

See `COPYING` and `source/arm/README.md` for the GPL-covered ARM component and
its build instructions.

## Credits

ZZQuake / XACP port: Xanxi, 2026.

Quake is copyright its respective rights holders. ZZQuake does not distribute
Quake game data, PAK files or other commercial game assets.
