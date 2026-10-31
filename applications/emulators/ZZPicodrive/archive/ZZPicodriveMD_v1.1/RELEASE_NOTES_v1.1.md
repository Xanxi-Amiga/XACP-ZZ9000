# ZZPicoDriveMD 1.1 - Genesis / Mega Drive Edition

## Summary

ZZPicoDriveMD is a Sega Mega Drive / Genesis emulator for classic Amiga systems equipped with an MNT ZZ9000.

The emulation core runs on the ZZ9000 ARM Core1. The Amiga 68k side handles the launcher, file access, RTG display, input, audio output, SRAM and savestates.

## Highlights

* Sega Mega Drive / Genesis emulation using PicoDrive
* ARM Core1 execution on the ZZ9000
* PAL and NTSC support
* AHI stereo audio backend
* Direct Paula DMA audio backend
* Keyboard, DB9 joystick and lowlevel.library controller support
* Two-player support
* SRAM support
* Savestates
* Embedded ARM blob in the executable

## Requirements

* MNT ZZ9000
* XACP v1.6 / firmware XX19a or later, or explicitly compatible firmware
* Matching `zz9000.card`
* Picasso96
* Valid 320x240x32 ZZ9000 RTG mode
* AHI if using the AHI backend

Firmware XX19a:

https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/XX19a

ZZPicoDriveMD is not compatible with the official MNT firmware 1.13.

It is also not compatible with other firmware branches unless they explicitly provide the XACP interfaces required by ZZPicoDriveMD.

## ROMs

No commercial ROMs are included.

Supported ROM formats:

* `.bin`
* `.gen`
* `.md`

Unsupported:

* `.zip`
* `.smd`

## Sources

The corresponding source code for **ZZPicoDriveMD 1.1** is now published in:

```text
source/
```

The source distribution includes the Amiga-side launcher and integration code, ZZ9000 ARM/Core1 code, build files and the PicoDrive components used by this release.

PicoDrive base:

```text
26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee
```

Cyclone 68000:

```text
3ac7cf1bdeecb60e2414980e8dc72ff092f69769
```

See the included license and third-party notices for details.

## Aminet

Aminet package type:

```text
misc/emu
```