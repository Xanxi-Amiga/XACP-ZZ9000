# ZZScreensavers 1.0

**ZZScreensaverE** and **ZZScreensaverJ** are XACP screensavers for classic
Amiga systems equipped with an MNT ZZ9000.

The Amiga side handles launch, Picasso96 display presentation and input while
the rendering workload runs on ZZ9000 ARM Cortex-A9 Core1.

Current release:

```text
ZZScreensavers 1.0
Firmware: XACP v1.7 / XX19b or later
Recommended firmware: XX19c
```

## Included programs

| Program | Description |
|---|---|
| **ZZScreensaverE** | Rotating textured Earth on Core1, 640x480 RTG output |
| **ZZScreensaverJ** | Animated Julia fractal on Core1, 320x240 16-bit output |
| **ZZScreensaverHotkeys** | Optional Commodities helper: Ctrl-Alt-E / Ctrl-Alt-J |

Both screensavers are silent standalone executables with their Core1 program
embedded in the Amiga executable.

They exit on keyboard input, mouse click, mouse movement, or Ctrl-C when
applicable from Shell.

## ZZScreensaverE

ZZScreensaverE renders a rotating Earth using a dedicated Core1 renderer.
The final display path is 640x480 32-bit RTG. The Earth texture is an embedded
1024x512 RGB565 equirectangular map with bilinear sampling.

The texture is derived from NASA Visible Earth / Blue Marble imagery. Full
attribution is provided in `NASA_CREDITS.txt` and in the binary release.

## ZZScreensaverJ

ZZScreensaverJ renders an animated Julia fractal on Core1 using a 320x240
16-bit display path. The 68k remains primarily responsible for AmigaOS and
input integration.

## Hotkeys

`ZZScreensaverHotkeys` uses `commodities.library` and provides:

```text
Ctrl-Alt-E    ZZScreensaverE
Ctrl-Alt-J    ZZScreensaverJ
```

The helper launches `C:ZZScreensaverE` and `C:ZZScreensaverJ`.

## Requirements

- Classic Amiga with MNT ZZ9000
- Picasso96
- XACP v1.7 compatible firmware
- XX19b or later; XX19c is recommended

Only one dynamically loaded Core1 application can execute at a time.

## Download

The complete Amiga package is distributed through the GitHub Releases page as:

```text
ZZScreensavers-1.0.lha
```

## Source and license

ZZScreensaverE, ZZScreensaverJ and ZZScreensaverHotkeys are proprietary
closed-source freeware.

No source code is distributed.

The embedded NASA imagery is separate material and is not claimed as
Xanxi-owned content. See `NASA_CREDITS.txt`.

## Author

Xanxi, 2026.

ZZ9000 is a product of MNT Research GmbH.
ZZScreensavers is an independent software project and is not an official MNT
Research or NASA product.
