# Installing ZZPicoDrive32X 1.2

## Firmware and Picasso96

ZZPicoDrive32X supports **XACP XX19c** or a later compatible firmware,
with its matching `zz9000.card`. **XX19d-RIX or a later compatible update is
recommended.** The firmware and driver are not bundled.

The ZZ9000 must provide a **320x240, 32-bit Picasso96 screen mode**.
The display should support 640x480 at 50 Hz for PAL games and 59.98 Hz
for NTSC games.

Do not mix firmware and card-driver versions. After changing firmware or
the card driver, power off the Amiga completely before testing.

## Audio

Use **Paula** for direct hardware audio, or install AHI to select the
**AHI** backend.

## Install and start

1. Extract `ZZPicoDrive32X_v1.2_Amiga_PUBLIC.zip` and copy the `ZZPicoDrive32X`
   drawer to a location on your Amiga.
2. Double-click the **ZZPicoDrive32X** icon in Workbench.
3. Select a 32X ROM (`.32x` or `.bin`), region, audio backend and controller.
4. Click **Start**.

The ARM Core1 program is already embedded in the Amiga executable.
No additional ARM binary is needed to run the bundled executable.

No copyrighted game ROMs are included.
