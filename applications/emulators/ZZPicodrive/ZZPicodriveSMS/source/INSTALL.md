# Installation

## 1. Firmware

ZZPicoDriveSMS v1.2 needs the XACP firmware **XX19c or later** with its
matching `zz9000.card`. The current firmware is recommended: XX19d-RIX at the
time of this release, or any later update. The firmware is not included.

Do not mix `BOOT.bin`, `zz9000.card` and ZZPicoDrive executables from
different firmware generations. Power the Amiga off completely after changing
the firmware or the card file.

## 2. Picasso96

Make sure the ZZ9000 offers a **320x240, 32-bit (True Colour)** screen mode.
A High Colour Workbench is not enough: ZZPicoDriveSMS opens its own 320x240x32
screen.

During the game ZZPicoDriveSMS reprograms the ZZ9000 output to 640x480 at
50 Hz (PAL games) or 59.98 Hz (NTSC games) and restores it when it exits. If
your monitor refuses one of these, start from the Shell with `no50` or `no60`.

## 3. Audio

Install AHI only if you want the AHI backend. The Paula backend plays directly
through the Amiga audio hardware.

## 4. Copy ZZPicoDriveSMS

Copy the `ZZPicoDriveSMS` folder anywhere on the Amiga. The program is a single
executable: the ARM Core1 part is embedded in it.

## 5. ROMs

Copy your own Master System ROMs (`.sms`, `.bin`) to a folder of
your choice. No ROM is included.

## 6. Run

Double-click **ZZPicoDriveSMS**, or start it from the Shell (see README.md).
Choose ROM, region, audio backend, FM sound and controller, then **Start**.
Return is the Pause button; Shift+Esc quits.
