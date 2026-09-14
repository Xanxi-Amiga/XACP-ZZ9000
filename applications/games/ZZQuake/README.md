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

## Mission packs / expansions

ZZQuake supports the two official Quake mission packs.

### Quake Mission Pack No. 1: Scourge of Armagon

Developed by Hipnotic Interactive.

Place the expansion data in:

```text
hipnotic/
    pak0.pak
```

A registered installation therefore normally contains:

```text
id1/pak0.pak
id1/pak1.pak
hipnotic/pak0.pak
```

Launch from Shell with:

```text
ZZQuake HIPNOTIC
```

or enable the `HIPNOTIC` Workbench ToolType.

### Quake Mission Pack No. 2: Dissolution of Eternity

Developed by Rogue Entertainment.

Place the expansion data in:

```text
rogue/
    pak0.pak
```

A registered installation therefore normally contains:

```text
id1/pak0.pak
id1/pak1.pak
rogue/pak0.pak
```

Launch from Shell with:

```text
ZZQuake ROGUE
```

or enable the `ROGUE` Workbench ToolType.

`HIPNOTIC` and `ROGUE` are mutually exclusive.

Other Quake mods can be selected with `GAME=name`, with the corresponding
game directory placed next to `id1/`.

## Music

ZZQuake provides three mutually exclusive music backends:

```text
MUSIC=MP3
MUSIC=MHI
MUSIC=CD
```

### MP3 file layout

The MP3 and MHI backends use replacement CD tracks stored under:

```text
id1/music/
```

with names such as:

```text
track02.mp3
track03.mp3
track04.mp3
...
```

Quake CD audio starts at track 02.

For both `MUSIC=MP3` and `MUSIC=MHI`, **128 kbit/s CBR MP3** files are
recommended for best playback stability.

### MUSIC=MP3

`MUSIC=MP3` uses the native XACP MP3 decoding pipeline of the ZZ9000.

MP3 decoding is offloaded to the ZZ9000 ARM through the XACP multimedia
pipeline rather than being performed by the Amiga 68k CPU. The Quake engine
continues to run on ARM Core1 while the XACP MP3 service handles music
decoding.

Recommended format:

```text
MP3 CBR
128 kbit/s
```

### MUSIC=MHI

`MUSIC=MHI` uses an external MHI-compatible decoder through the selected MHI
driver.

Example:

```text
MUSIC=MHI
MHIDRIVER=mhiamiblaster.library
```

The same files under `id1/music/` are used.

Recommended format:

```text
MP3 CBR
128 kbit/s
```

Higher bitrates may work depending on the MHI driver and hardware.

### MUSIC=CD

`MUSIC=CD` plays audio tracks from a physical Quake CD.

The device and unit can be selected with:

```text
CDDEVICE=scsi.device
CDUNIT=1
```

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
