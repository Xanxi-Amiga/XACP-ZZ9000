# ZZPicoDrive32X v1.2.1

**ZZPicoDrive32X** is a Sega 32X emulator for classic Amiga systems equipped
with an **MNT ZZ9000**. It also runs Mega Drive / Genesis games.

The emulation core (PicoDrive, with its SH-2 recompiler for the two 32X
processors) runs on the ZZ9000 ARM Core1 and renders directly into ZZ9000 RTG
memory. The Amiga 68k side handles the launcher, file access, display flips,
input, audio output, SRAM and savestates, and stays almost idle.

This is the **Sega 32X Edition** of ZZPicoDrive.

## What is new in v1.2.1

- **X / Y / Z on the CD32 pad and USB pads** (Poseidon): left / right
  shoulder = X / Y, yellow = Z. Virtua Racing Deluxe changes the view with
  them (v1.2 had them on the keyboard only).
- **Safer start**: if the ARM program does not answer, the launcher reports it,
  parks the ARM core cleanly and loads it once more; started from the
  Workbench, the log goes to `T:ZZPicoDrive32X.log` and a failed start opens a
  requester with its reason.
- Same emulation as v1.2 (the ARM program is unchanged).

See [RELEASE_NOTES_v1.2.1.md](RELEASE_NOTES_v1.2.1.md).

## What is new in v1.2

- **Every frame is displayed.** A frame that finds no free screen buffer when
  it is finished is shown as soon as one is free, instead of being skipped.
- **Real 50 Hz output for PAL games** and **59.98 Hz output for NTSC games**,
  with the 320x240 screen, its x2 scaler and the scanlines kept.
- **Paula and AHI at the same level**: the display stays synchronised with
  either backend; with AHI a frame is no longer overwritten by the next one.
- **Savestates work with Paula and with AHI**, and a savestate of the other
  region (PAL / NTSC) is refused instead of breaking the game.
- **Quick start, clean exit and immediate relaunch** without resetting the
  ZZ9000.
- **The mouse never quits the game** (Shift+Esc quits).
- **Default ROM drawer** remembered in the configuration.

See [RELEASE_NOTES_v1.2.md](RELEASE_NOTES_v1.2.md) for the details.

## Requirements

- Amiga with MNT ZZ9000 and a 68020 or better (tested on A4000/060).
- XACP firmware **XX19c or later compatible XACP release**, with its matching
  `zz9000.card` Picasso96 driver. **XX19d-RIX or later compatible XACP firmware
  is recommended.** The 32X Core1 program uses XACP Core1 memory for the
  SH-2 recompiler code cache and 32X memory.
  Firmware XX19d-RIX package (including the matching driver):
  https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/XX19d-RIX
- Picasso96 with a ZZ9000 **320x240, 32-bit** screen mode.
- AHI, only for the AHI audio backend. The Paula backend needs nothing.
- A monitor or TV that accepts 640x480 at 50 Hz and at 59.98 Hz (most do;
  see `no50` / `no60` below if yours does not).

ZZPicoDrive32X is not compatible with the official MNT firmware or with other
ZZ9000 firmware branches: it needs specific XACP Core1, memory and display
interfaces.

## ROM formats

Supported: `.32x`, `.bin` (32X), and the Mega Drive formats `.bin`, `.gen`,
`.md`. Not supported: `.zip`, `.smd` (decompress or convert them first). ROMs
up to 4 MB.

No ROM is included. Only use ROM files you are legally entitled to use.

## Starting

From Workbench: double-click **ZZPicoDrive32X**, choose the ROM, region, audio
backend and controller, then **Start**.

**ROM drawer**: *Set ROM drawer...* chooses the drawer the ROM requester always
opens in (shown on the *ROM dir* line); *Clear* goes back to the last drawer
used. Both are saved at once in `ZZPicoDrive32X.cfg` (`romdir=`, `lastdir=`).
If the drawer's volume is not mounted, the requester opens normally and the
setting is kept. From the Shell, a ROM name alone that is not in the current
directory is looked for in the ROM drawer.

From the Shell:

```text
ZZPicoDrive32X <rom> [region] [flags]

  ZZPicoDrive32X Doom.32x
  ZZPicoDrive32X Doom.32x usa ahi
  ZZPicoDrive32X "Virtua Racing Deluxe (USA).32x" usa paula kbd
```

- Region and flags are keywords and can be given in any order (upper or
  lower case). The Core1 program embedded in ZZPicoDrive32X is used; the form
  with `-` in second position (`Doom.32x - usa ahi`) also works. Any other
  word is taken as an external Core1 blob file (developers only).
- `region`: `auto` (default), `usa`, `jap`, `eur`, `jappal`.
- `flags`:
  - audio: `ahi`, `paula`, `silence`
  - input: `kbd`, `cd32`, `db9`, `usb`, `port=N`, `2p`
  - display: `no50` (PAL games stay on the 60 Hz screen mode),
    `no60` (NTSC games use the plain 59.5 Hz screen mode), `osd`
  - diagnostics only: `pri0`, `norecheck`, `nosettle`, `tof`, `gate`, `inputlog`

`osd` shows one line of Core1 figures in the top border (`32XD`, frame time
last / average / maximum in ms, 32X enabled, SH-2 program counters).

Started from the Shell, ZZPicoDrive32X prints timing and audio statistics when
it exits (frames emulated and displayed, audio underruns, exit reason).
Started from the Workbench, the same log is written to `T:ZZPicoDrive32X.log`.

## Audio

ZZPicoDrive32X produces 16-bit stereo PCM at 22050 Hz: YM2612 FM, PSG and the
32X PWM channels are emulated and mixed on the ARM side.

- **Paula**: direct DMA playback on AUD0/AUD1, no AHI needed.
- **AHI**: 16-bit stereo through AHI (sound cards, mixing with other programs).
- **None**: no sound.

## Controls

Player 1 uses a 6-button pad (X / Y / Z / Mode): Virtua Racing Deluxe changes
the view with X / Y / Z.

| Key | 32X / Mega Drive |
| --- | --- |
| Cursor keys | D-pad |
| Keypad 1 / 2 / 3 | A / B / C |
| Keypad 4 / 5 / 6 | X / Y / Z |
| Keypad 0 | Mode |
| Return | Start (player 1) |
| Tab | Start (player 2) |

| CD32 pad / USB pad (Poseidon) | 32X / Mega Drive |
| --- | --- |
| Blue / Red / Green | A / B / C |
| Left / right shoulder | X / Y |
| Yellow | Z |
| Play | Start |

Mode is on keypad 0 only. With a USB pad, choose in Poseidon's HID settings
which physical button produces each CD32 button (for an 8BitDo M30: A B C on
blue, red, green and X Y Z on left shoulder, right shoulder, yellow).

Controllers: DB9 joystick or pad, CD32 pad, USB pad through Poseidon
(lowlevel.library). Two players with two controllers on different ports.

## Hotkeys

| Key | Action |
| --- | --- |
| Shift + Esc | Quit |
| Del | Console reset |
| F1 - F5 | Load savestate slot 1 - 5 |
| Shift + F1 - F5 | Save savestate slot 1 - 5 |

## Savestates

Savestates are stored next to the ROM as `ROM.st1` ... `ROM.st5` (about
700 KB each for a 32X game); battery saves as `ROM.srm`.

**A savestate is guaranteed only when it is loaded with the same region and
the same audio output (AHI / Paula) it was saved with. In any other case it is
not guaranteed.** A savestate saved in the other region family (PAL / NTSC) is
refused by the launcher with a message.

## Installation

See [INSTALL.md](INSTALL.md).

## Source code and building

The complete source is included: 68k launcher (`amiga/`), ARM Core1 program
(`Core1/`), PicoDrive and Cyclone (`third_party/`), and the build scripts.
See [BUILD.md](BUILD.md).

## Credits

ZZ9000 / AmigaOS port, launcher, ARM Core1 integration, audio, video, input,
save handling and real-hardware validation: **Xanxi**.

Emulation core: **PicoDrive by notaz, irixxxx and contributors** (32X and SH-2
recompiler); Cyclone 68000 by Dave Haywood, notaz and irixxxx. The PicoDrive
tree comes from VirtuaRacingDeluxeEnhanced by PokaMilkTea.

See [COPYING_PICODRIVE.txt](COPYING_PICODRIVE.txt) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Sega, 32X, Mega Drive and Genesis are trademarks of SEGA. This project is not
affiliated with SEGA.
