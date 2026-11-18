# ZZHalfLife 1.0 - Half-Life for Amiga and MNT ZZ9000

ZZHalfLife brings the original **Half-Life** to AmigaOS with the MNT ZZ9000.
The **Xash3D FWGS** engine and Half-Life game code run on the ZZ9000's ARM
Cortex-A9 Core1. The FPGA's **RasterIX** GPU accelerates 3D rendering, while
the Amiga handles the Workbench launcher, display, input, disk access and audio.

This is a free, unofficial port. **The original game data are not included.**
A legitimate copy of Half-Life (Steam) is required.

## Hardware and display

- AmigaOS 3.x with Picasso96 and an MNT ZZ9000.
- RasterIX-enabled ZZ9000 firmware supporting XACP 1.8 (XX19d-RIX or a compatible later version).
- Developed and optimized on an Amiga 4000/68060; also tested on a 68030/25 MHz system.

| Resolution | Use |
| --- | --- |
| 320x240 | Optional low-resolution mode |
| **640x480** | **Default and reference resolution, used for development and optimization** |
| 800x600 | Optional high-resolution mode |

On the A4000/060, a 19-minute gameplay session at 640x480 averaged **47 FPS**.
Performance varies by scene and system.

## Installation

1. Install the original Half-Life on your PC through Steam.
2. Run the supplied `ZZHalfLife-Data.exe` on Windows to prepare your own game
   files, including the required `liblist.gam`. The resulting game data must not
   be redistributed.
3. Extract `ZZHalfLife.lha` to a drawer on your Amiga, then place the prepared
   game files in its `data/` directory.
4. Double-click the **ZZHalfLife Workbench icon** to start the game.

See `README` inside the Amiga archive for the file layout, controls and
Workbench tooltypes. For a slower 68k system, the optional tooltypes
`FPSMAX=30` and `POLL=4000` can be added to the icon's Information window.

## Game support

- Half-Life single-player campaign.
- **Multiplayer is not supported.**
- Opposing Force and Blue Shift are not included in this release; support may
  be considered in the future.

## Gameplay video

[ZZHalfLife - 9-minute gameplay video](https://youtu.be/EkbUq131PZ8)

## Source code and licensing

The `source/ZZHalfLife_1.0_src.zip` archive contains the ARM engine, game
module, ZZGL/librixgpu, Core1 integration, data preparation tools, build
scripts and their applicable license notices. The engine and HLSDK game code
are loaded as separate ARM modules. The AmigaOS 68k launcher is distributed
as a separate freeware executable; its source is not included.

The engine uses Xash3D FWGS (GPLv3-or-later) and the Half-Life game code is
based on FWGS hlsdk-portable (Valve Half-Life 1 SDK license). FreeVGUI and
other components retain their own licenses; see the source archive and
`LICENSES.txt` in the Amiga package.

Half-Life is a trademark of Valve Corporation. This independent port is not
affiliated with or endorsed by Valve.

**Port:** Xanxi  |  **Engine:** Xash3D FWGS contributors  |  **RasterIX:** ToNi3141
