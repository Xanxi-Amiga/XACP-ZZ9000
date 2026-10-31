# ZZPicoDriveMD

**ZZPicoDriveMD** is a Sega Mega Drive / Genesis emulator for classic Amiga
systems equipped with an **MNT ZZ9000**.

The Amiga 68k side handles the launcher, file access, RTG display, input,
audio output, SRAM and savestates. The emulation core runs on the ZZ9000
ARM Core1 and renders directly into ZZ9000 RTG memory.

This is the **Genesis / Mega Drive Edition** of ZZPicoDrive.

## Features

- Sega Mega Drive / Genesis emulation using PicoDrive.
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

ZZPicoDriveMD is not compatible with official MNT firmware 1.13 or with
MiDWaN / BlitterStudio firmware unless a future version explicitly provides
compatible XACP interfaces.

Do not mix `BOOT.bin`, `zz9000.card` and ZZPicoDrive executables from unrelated
firmware generations.

## ROM format

Supported:

- `.bin`
- `.gen`
- `.md`

Not supported:

- `.zip`
- `.smd`

No Sega / Mega Drive / Genesis ROM images are included.

Only use ROM files you are legally entitled to use.

## Audio

ZZPicoDriveMD produces 16-bit stereo PCM audio at 22050 Hz.

The Mega Drive audio emulation, including YM2612 FM and PSG, runs on the
ZZ9000 ARM side. The ARM writes the stereo PCM stream into shared memory and
the 68k side plays it back.

Audio modes:

### AHI

16-bit stereo output through AHI.

### Paula DMA

Direct playback through Paula channels AUD0/AUD1, bypassing AHI.

### None

Audio disabled.

## Controls

ZZPicoDriveMD supports keyboard, DB9 joystick and CD32/USB controllers through
`lowlevel.library`, including two-player operation when suitable input sources
are selected.

### Keyboard

| Key | Mega Drive input |
| --- | --- |
| Cursor keys | Direction pad |
| Numeric keypad 1 | Button A |
| Numeric keypad 2 | Button B |
| Numeric keypad 3 | Button C |
| Return | Start |

## Hotkeys

| Key | Action |
| --- | --- |
| Shift + Esc | Quit emulator |
| F1-F5 | Load savestate slot 1-5 |
| Shift + F1-F5 | Save savestate slot 1-5 |

## Installation

See [INSTALL.md](INSTALL.md).

## Source code

The corresponding source for **ZZPicoDriveMD 1.1** is published in
[`source/`](source/).

It contains the Amiga 68k launcher source, the exact embedded ARM blob, the
final ARM Core1 source and the retained build files.

The exact upstream source archives used for this publication are included in:

```text
source/third_party/archives/
```

They contain PicoDrive commit
`26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee` and Cyclone 68000 commit
`3ac7cf1bdeecb60e2414980e8dc72ff092f69769`.

No network download or repository checkout is required to obtain the
corresponding third-party source. See `source/README.md` for the historical
build layout and Cyclone generation details.

Historical comments inside the corresponding source files are retained as they
appeared in the development/build tree, even where they mention earlier
internal XACP stage names.

## Credits

ZZ9000 / AmigaOS port, launcher, ARM integration, audio/video/input/save
handling and real-hardware validation:

**Xanxi**

Emulation core:

**PicoDrive by notaz and contributors**

See the included license and third-party notice files.
