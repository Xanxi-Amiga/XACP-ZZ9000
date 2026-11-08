# ZZVirtuaRacingRIX 1.0 - release notes

First release of an enhanced Virtua Racing Deluxe (32X, USA) on the MNT
ZZ9000. PicoDrive emulates game logic and sound on ARM Core1, while the race
3D is reconstructed by VirtuaRacingDeluxeEnhanced and hardware-rendered by
RasterIX at up to 60 displayed images per second in 640 x 480.

## In 1.0

- Installed by **ZZVirtuaRacingRIX_Setup**: it asks for your Virtua Racing
  Deluxe (USA) ROM (CRC32 `7896B62E`, other versions refused) and writes the
  game **ZZVirtuaRacingRIX** with the user's ROM embedded and its icon.
  The game never reads an external ROM file.
- No configuration window: double-click and play. Two tool types:
  `FARVIEW=6|7|8` (track tiles drawn ahead, 6 by default) and
  `AUDIO=PAULA|AHI` (Paula by default).
- Without a RasterIX firmware the game runs drawn by the emulated 32X
  (about 20 images a second) and says at the end that a firmware with
  RasterIX (XACP XX19d-RIX or later compatible XACP firmware) is needed
  for hardware-accelerated 3D rendering.
- 2 players VS is not offered (greyed out in the game's menu): the
  split-screen GPU rendering path is not supported in version 1.0.

## What the GPU path does

- 60 images a second in the race: every image interpolates the cars, the
  track and the camera between two game frames (the 32X itself draws 20).
- The 32X's SH-2 processors no longer draw the race (their drawing commands
  are skipped); they are given back the drawing if RasterIX fails.
- Half-transparent shadows and smoke, exact polygon edges, 640 x 480.
- Longer view of the track, faded into the distance (6 tiles; 7 / 8 with
  `FARVIEW`).
- The sky drawn from the console's video memory, following the
  interpolated camera, without the dither.
- Wheels turned by the car's speed, with a blurred copy at high speed.
- The background is written once per console line by the ARM and its second
  row copied by the PL330 DMA engine.

## Measured on an A4000/060 with ZZ9000 and XACP XX19d-RIX

- 10-minute races: 1.1 % of the race frames kept their previous image
  (6 tiles), 0 audio underruns, 0 dropped frames.
- Every course, car, view, Time Attack, replays, saved states and a long
  session tested.

## Source

Complete source in the source package; see `BUILD.md` and
`THIRD_PARTY_NOTICES.md`.
