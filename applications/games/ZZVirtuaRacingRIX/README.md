# ZZVirtuaRacingRIX 1.0

Enhanced Virtua Racing Deluxe (Sega 32X) on classic Amigas with an MNT
ZZ9000: hardware-accelerated 3D graphics, improved image quality and smoother
presentation at up to 60 displayed images per second in 640 x 480.

The game logic and sound are emulated by PicoDrive on the ZZ9000 ARM Core1.
Race geometry is reconstructed from the game's polygon lists using the
VirtuaRacingDeluxeEnhanced HLE and rendered by the RasterIX FPGA GPU, instead
of displaying the original 32X's 20 FPS 3D picture. The 68k handles AmigaOS
input, audio output and screen presentation.

What you get over the plain 32X picture:

- **60 images a second** in the race (the 32X draws 20): the cars, the
  track and the camera are interpolated between the game's own frames;
- **640 x 480**, exact polygon edges;
- **half-transparent shadows and smoke** instead of the 32X checkerboard;
- **a longer view of the track** (6 track tiles by default, 7 or 8 as an
  option), faded into the distance;
- **a smooth sky** that follows the camera, without the dither;
- **wheels** that turn with the car's speed, blurred at high speed.

The menus, the HUD and the sound are the game's, emulated as they are.

## Requirements

- An Amiga with an MNT ZZ9000 and a 68020 or better.
- **XACP XX19d-RIX firmware** or a later compatible XACP firmware with
  RasterIX, and its matching `zz9000.card` driver:
  https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/XX19d-RIX
  Official MNT firmware 1.0-1.13 and BlitterStudio firmware are not compatible.
  Without RasterIX the emulated 32X renderer can still run at about 20 FPS.
- AmigaOS 3.x with Picasso96 (or a compatible RTG system) and the ZZ9000
  driver, with a 640 x 480 16-bit screen mode.
- About 6 MiB of free memory.
- A joypad: a CD32 pad (lowlevel.library) or a joystick in the joystick port.
- Optional: AHI (the default is Paula).
- About 4 MiB of free disk space for the game built with your ROM.
- **The Virtua Racing Deluxe (USA) ROM**, which you must provide (see below).

## Installation: your ROM goes inside the game

No ROM is included, and the game does not read a ROM file: it is built once,
on your Amiga, with your own dump of **Virtua Racing Deluxe (USA)** for the
32X inside it (a 3 MiB `.32x` file, CRC32 `7896B62E`). Other versions
(Europe, Japan) are refused.

1. Copy the `ZZVirtuaRacingRIX` drawer to your hard disk.
2. Double-click **ZZVirtuaRacingRIX_Setup** and choose your ROM.
3. The setup checks it and writes **ZZVirtuaRacingRIX** with its icon in the
   same drawer. The ROM file is not needed any more.

ZZVirtuaRacingRIX contains your ROM: keep it for yourself, do not share it.
Running the setup again replaces the game but keeps its icon (and the tool
types you set).

## Playing

Double-click the **ZZVirtuaRacingRIX** icon. Quit with **Shift + Esc**.

Options are tool types of the icon (select the icon, then Icons > Information
in Workbench):

| Tool type | Values | Default |
|---|---|---|
| `FARVIEW` | `6`, `7`, `8`: how many track tiles ahead are drawn | `6` |
| `AUDIO` | `PAULA` or `AHI` | `PAULA` |

The icon lists the other values in parentheses: `(FARVIEW=7)`,
`(FARVIEW=8)`, `(AUDIO=AHI)`. To choose one, remove its parentheses and put
the previous value in parentheses.

`FARVIEW=6` gives the smoothest picture. 7 and 8 show more of the track but
cost the ZZ9000's processor more: an occasional image may be held a little
longer.

## Controls

CD32 pad: blue / red / green = A / B / C, left / right shoulder = X / Y,
yellow = Z, play = Start. Return on the keyboard is Start as well.

In the race: **X**, **C**, **Y** and **Z** choose the four views (V.R.1
cockpit to V.R.4). The game's Options menu sets which buttons accelerate,
brake and shift.

Keys during the game:

| Key | Action |
|---|---|
| Shift + Esc | quit |
| F1 to F5 | load a saved state (slots 1-5) |
| Shift + F1 to F5 | save the state (slots 1-5) |
| Del | reset the console |

The game's records are saved in its drawer (`ZZVirtuaRacingRIX.srm`, saved
states `ZZVirtuaRacingRIX.st1` to `.st5`). A saved state is guaranteed only when
it is loaded with the same audio output (Paula / AHI) it was saved with.

## Known limits of 1.0

- **2 players VS is not available** (greyed out in the game's menu). Its
  split screen is drawn by the 32X itself, which the GPU path does not
  reproduce; it may come in a later version.
- Virtua Racing Deluxe (USA) only.
- If RasterIX stops during a game (a GPU error), the emulated 32X takes over
  the race drawing (20 images a second) until you quit, and a message says
  so at the end.

## Files

| File | |
|---|---|
| `ZZVirtuaRacingRIX_Setup` (+ `.info`) | builds the game with your ROM |
| `ZZVirtuaRacingRIX` (+ `.info`) | the game, once built (not in the archive) |
| `README.md` | this file |
| `RELEASE_NOTES_1.0.md` | what is new |
| `THIRD_PARTY_NOTICES.md`, `COPYING_PICODRIVE.txt`, `LICENSE_VRDE.txt`, `LICENSE_LIBRIXGPU_0BSD.txt` | licences |

Log of the last session started from the icon: `T:ZZVirtuaRacingRIX.log`.

## Credits and licences

- PicoDrive: notaz, irixxxx and contributors (non-commercial licence).
- Cyclone 68000: Dave Haywood, notaz, irixxxx.
- VirtuaRacingDeluxeEnhanced: PokaMilkTea (the high-level emulation of the
  game's 3D, the sky and the HUD handling).
- librixgpu, ZZ9000 Core1 port and Amiga launcher: Xanxi.

Non-commercial use only (PicoDrive terms). The complete source code is in the
source package (`BUILD.md`). See `THIRD_PARTY_NOTICES.md`.

Sega, 32X, Mega Drive, Genesis and Virtua Racing are trademarks of SEGA. This
project is not affiliated with SEGA.
