# ZZPicoDriveSMS 1.1 - Master System Edition

## Summary

ZZPicoDriveSMS is a Sega Master System emulator for classic Amiga systems
equipped with an MNT ZZ9000.

The emulation core runs on the ZZ9000 ARM Core1. The Amiga 68k side handles the
launcher, file access, RTG display, input, audio output, SRAM and savestates.

## Highlights

- Sega Master System emulation using PicoDrive.
- ARM Core1 execution on the ZZ9000.
- PAL and NTSC support.
- AHI stereo audio backend.
- Direct Paula DMA audio backend.
- Keyboard, DB9 joystick and lowlevel.library controller support.
- Two-player support.
- SRAM support.
- Savestates.
- Embedded ARM blob in the executable.

## Audio note

YM2413 / FM audio is not enabled in version 1.1. Games with optional FM sound
use the standard PSG path in this release.

## Requirements

- MNT ZZ9000.
- **XACP v1.6 / firmware XX19a**, or a later firmware explicitly documented as
  compatible with ZZPicoDriveSMS.
- Matching `zz9000.card`.
- Picasso96.
- Valid 320x240x32 ZZ9000 RTG mode.
- AHI if using the AHI backend.

Firmware XX19a:

https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/XX19a

ZZPicoDriveSMS is not compatible with official MNT firmware 1.13 or other
firmware branches unless they explicitly provide the required XACP-compatible
interface.

## ROMs

No commercial ROMs are included.

Supported ROM formats:

- `.sms`
- `.bin`

Unsupported:

- `.zip`

## Aminet

Aminet package type:

```text
misc/emu
```

## Sources

The corresponding source for version 1.1 is published in `source/`.

The source publication records the exact released ARM blob and the final
retained 68k/Core1 source, together with the upstream revisions and build files
required for the SMS port.
