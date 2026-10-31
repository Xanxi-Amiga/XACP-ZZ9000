# ZZPicoDriveSMS

**ZZPicoDriveSMS** is a Sega Master System emulator for classic Amiga systems
equipped with an **MNT ZZ9000**.

The Amiga 68k side handles the launcher, file access, RTG display, input,
audio output, SRAM and savestates. The emulation core runs on the ZZ9000
ARM Core1 and renders directly into ZZ9000 RTG memory.

This is the **Master System Edition** of ZZPicoDrive.

## Features

- Sega Master System emulation using PicoDrive.
- ARM Core1 execution on the ZZ9000.
- Native AmigaOS launcher with ROM selection and options.
- Direct RTG rendering through Picasso96.
- Low 68k CPU usage.
- PAL and NTSC support.
- AHI stereo audio backend.
- Direct Paula DMA audio backend.
- Keyboard controls.
- lowlevel.library / CD32-style controller support.
- DB9 joystick support.
- Two-player support.
- SRAM support for games that use battery backup.
- Savestates.
- Embedded ARM blob in the executable.

## Requirements

- Amiga with MNT ZZ9000.
- **XACP v1.6 / firmware XX19a**, or a later firmware explicitly documented
  as providing the required ZZPicoDrive/XACP interface.
- Matching `zz9000.card` file.
- Picasso96 RTG setup.
- Picasso96 mode supporting 320x240 in 32-bit colour.
- AHI installed if using the AHI audio backend.

Current XACP firmware release:

https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/XX19a

ZZPicoDriveSMS is not compatible with official MNT firmware 1.13 or with
MiDWaN / BlitterStudio firmware unless a future version explicitly provides
compatible XACP interfaces.

Do not mix `BOOT.bin`, `zz9000.card` and ZZPicoDrive executables from unrelated
firmware generations.

## ROM format

Supported:

- `.sms`
- `.bin`

Not supported:

- `.zip`

No Sega Master System ROM images are included.

Only use ROM files you are legally entitled to use.

## Audio

ZZPicoDriveSMS produces 16-bit stereo PCM audio at 22050 Hz.

The Master System PSG audio emulation runs on the ZZ9000 ARM side. The ARM
writes the stereo PCM stream into shared memory and the 68k side plays it back.

**YM2413 / FM audio is not enabled in version 1.1.** Games with optional FM
sound use the standard PSG path in this release.

Audio modes:

### AHI

16-bit stereo output through AHI.

### Paula DMA

Direct playback through Paula channels AUD0/AUD1, bypassing AHI.

### None

Audio disabled.

## Controls

ZZPicoDriveSMS supports keyboard, DB9 joystick and CD32/USB controllers through
`lowlevel.library`, including two-player operation when suitable input sources
are selected.

### Keyboard

| Key | Master System input |
| --- | --- |
| Cursor keys | Direction pad |
| Numeric keypad 1 | Button 1 |
| Numeric keypad 2 | Button 2 |
| Return | Start / Pause when supported |

## Hotkeys

| Key | Action |
| --- | --- |
| Shift + Esc | Quit emulator |
| F1-F5 | Load savestate slot 1-5 |
| Shift + F1-F5 | Save savestate slot 1-5 |

## Installation

See [INSTALL.md](INSTALL.md).

## Source code

The corresponding source for **ZZPicoDriveSMS 1.1** is published in
[`source/`](source/).

It contains the final Amiga 68k launcher source, the exact embedded ARM blob,
the final ARM Core1 source and the retained SMS build files.

The exact upstream PicoDrive and Cyclone revisions used by the build are:

```text
PicoDrive  26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee
Cyclone    3ac7cf1bdeecb60e2414980e8dc72ff092f69769
```

PicoDrive references emu2413 commit
`a2dfc20ff507e4fd075cd325620bcea655e2c1f7`; version 1.1 uses only its exact
header for the SMS build because YM2413 synthesis itself is disabled.

See `source/README.md` for the source mapping, historical build layout and
rebuild notes.

Historical technical comments referring to earlier internal XACP development stages
are retained where useful; private development-only attribution comments are omitted.

## Credits

ZZ9000 / AmigaOS port, launcher, ARM integration, audio/video/input/save
handling and real-hardware validation:

**Xanxi**

Emulation core:

**PicoDrive by notaz and contributors**

See the included license and third-party notice files.
