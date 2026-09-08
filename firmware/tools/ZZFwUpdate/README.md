# ZZFwUpdate 2.1

ZZFwUpdate is an AmigaOS command-line utility for transferring `BOOT.bin`
or another root-level file to the FAT32 microSD card of a ZZ9000 through
the Zorro bus.

This directory redistributes the historical **ZZFwUpdate 2.1**
implementation used with the XACP firmware branch as a firmware maintenance tool.

## Original author and license

ZZFwUpdate 2.1:

- Copyright (C) 2026 Dimitris Panokostas (MiDWaN)
- License: GNU General Public License v3.0 or later
  (`GPL-3.0-or-later`)

The original copyright and license notice is preserved in the source file.

ZZFwUpdate is not an XACP-authored application. It is a firmware maintenance
utility redistributed here for convenient use with XACP-compatible ZZ9000 firmware.

## Version included here

```text
ZZFwUpdate 2.1
17.05.2026
```

This is the historical pre-RESTORE version of ZZFwUpdate. Its FWUP command
set is limited to:

```text
OPEN
WRITE
CLOSE
ABORT
```

Later BlitterStudio versions added functions such as `RESTORE` and moved
the protocol implementation into shared FWUP client code. Those later
extensions are intentionally not included here.

## Executable

The `ZZFwUpdate` executable in this directory is the exact binary currently
used with the XACP setup for which this package was prepared.

```text
Size:    17556 bytes
MD5:     b513dbec52347e10a5946743e771c894
SHA-256: d94ae8498e391749a0c4e0bbeeb0576d5a29547243b4fb9d7a1df576aadc4eaf
```

The executable identifies itself as:

```text
$VER: ZZFwUpdate 2.1 (17.05.2026)
```

It contains the 2.1 functionality (including protocol probing, transfer
progress/timing and ABORT handling) and does not contain the later RESTORE
command.

The executable is preserved as-is. It was not rebuilt or modified for this
redistribution.

## Source

The corresponding upstream 2.1 source is provided under:

```text
source/ZZFwUpdate.c
```

It is taken from the BlitterStudio `zz9000-drivers` v2.1.0 source state.

Upstream source identity:

```text
Repository: BlitterStudio/zz9000-drivers
Tag:        v2.1.0
File:       ZZFwUpdate/ZZFwUpdate.c
Git blob:   5e84264f8dca32fda2c191c8354eac7c81e8e563
```

Build command documented by the original source:

```text
m68k-amigaos-gcc -O2 -noixemul -o ZZFwUpdate ZZFwUpdate.c -lamiga
```

The source and executable are version/function matched, but this package
does not claim a bit-perfect reproduction of the executable from a fresh
build, because compiler/newlib/binutils details can affect binary identity.

## Upstream

Original project:

```text
https://github.com/BlitterStudio/zz9000-drivers
```

Historical release containing ZZFwUpdate 2.1:

```text
https://github.com/BlitterStudio/zz9000-drivers/releases/tag/v2.1.0
```
