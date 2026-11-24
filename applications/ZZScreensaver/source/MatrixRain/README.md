# ZZScreensaverR 1.0 - MatrixView for ZZ9000/XACP

A "digital rain" screensaver for classic AmigaOS 3.x on a ZZ9000.
XACP XX19d-RIX or later is required; the reported hardware measurements
were made with XX19e-RIX. The scene is MatrixView by Alex Zolotov (2003),
as adapted by Tugrul Galatali and carried in MPlayer. It runs on the
Cortex-A9 Core1 of the ZZ9000 at 640x480 RGB565: Core1 writes the characters
into the frame buffer and the FPGA RasterIX blends the flares over them. The
68k side only opens the screen, watches keyboard and mouse and presents the
frames.

Everything around the scene is the ZZScreensaverS (Solar Winds) 1.1 source
tree: launcher, Core1 presenter, ZZGL, librixgpu.

**Disable other screen blankers (MCP, Workbench Blanker, etc.) before running
ZZScreensaverR.** The launcher drives the ZZ9000 video registers directly.
It stops when its screen is no longer in front and avoids further screen PAN
operations in that case. Blanking without a screen-order change cannot be
detected; concurrent blankers are unsupported.

Unofficial fan-made program. Not affiliated with, or endorsed by, the owners
of any film or trademark.

## Local offline build

Under Windows/MSYS with the local ARM Cortex-A9 and Amiga 68k cross-compilers
(same as ZZScreensaverS):

```sh
sh build_all.sh
```

Toolchains are referenced by explicit `C:/...` paths (ARM GNU Toolchain
13.2 Rel1, amiga-gcc); nothing is downloaded. The build writes the Amiga
executable `ZZScreensaverR` in this directory, with the Core1 program
embedded. No data file is needed beside it.

## Launch

```text
ZZScreensaverR
ZZScreensaverR PRESET 1
ZZScreensaverR 600 PRESET 2 DEBUG >RAM:zzsr.txt
```

Any key, mouse button or mouse movement ends it, and so does another screen
coming to the front. It is silent unless `DEBUG` is given. A number is a frame limit: the program ends by itself after that
many frames and, with `DEBUG`, prints its counters.

`PRESET` selects the character grid:

| Preset | Grid | Cell on a 640x480 screen |
|---|---|---|
| 1 | 80 x 60 | 8 x 8 pixels |
| 2 (default) | 64 x 48 | 10 x 10 pixels |
| 3 | 40 x 30 | 16 x 16 pixels |

`TOBACK n` is a test option: after n frames the program sends its own screen
to the back, which must end it (see below).

With ZZScreensaverHotkeys 1.3, Ctrl-Alt-R starts it with one of the three
grids, drawn at random, never the same twice in a row.

## How the characters are drawn

Two textured, blended triangles per character cost more than the pixels they
produce: with the 64 x 48 grid drawn that way, RasterIX alone needed 18.5 ms
a frame, more than one refresh of the screen. The cells are therefore not
triangles: Core1 writes them into the frame buffer, the way the presenter of
ZZScreensaverS writes its previous frame there. One small bitmap per glyph is
sampled once from the font, as the bilinear texture would be at this cell
size; a cell is that bitmap times its brightness. Every cell is written,
black ones too, so a frame needs no clear. The flares, and anything that is
not exactly a grid cell, are kept and drawn by RasterIX afterwards, on top,
blended.

This path is used only where its assumptions hold, checked at start against
the projection RasterIX gets: cells of a whole number of pixels, in the
expected places, the whole screen covered. Otherwise the adapter falls back
to RasterIX for every cell, through tables that reproduce word for word the
descriptors ZZGL would build (the "direct path"); the first frame after the
start is drawn that way too. `matrix/matrixview.c` retains the original drawing logic apart from the
marked no-picture brightness fix:
`matrix/mv_gl.c` recognises the cells among the scene's quads.

Reported hardware measurements on an A4000/060 with XX19e-RIX for version 1.0: 59.2 frames/s
(80 x 60), 59.4 (64 x 48) and 59.5 (40 x 30) over 600 frames each, one frame
per refresh of that screen, no dropped frame. With a development build that
had the same drawing code: 72000 frames in one run of the 64 x 48 grid
without a dropped frame. No frame rate is claimed for other machines.

## Run on the Amiga

Copy `ZZScreensaverR` and the scripts `ZZScreensaverR_Test` and
`ZZScreensaverR_GuardTest` into one drawer and, from a Shell in that drawer:

```text
Execute ZZScreensaverR_Test
```

Three runs of 600 frames follow each other, lightest grid first, nothing to
type. Do not touch the mouse or the keyboard: that ends the current run
early. Everything is appended to `zzsr_10.txt` in the same drawer.

A second script checks the stop on a change of front screen:

```text
Execute ZZScreensaverR_GuardTest
```

One run of the 64 x 48 grid: after 300 frames (about five seconds) the
program sends its own screen to the back, the Workbench shows, and the
program must end at once. `zzsr_guard.txt` then holds the line
`Screen no longer in front after 300 frames`. Should the stop not work, the
run ends by itself 60 frames later.

## What the PC bench checks, and what it does not

```sh
python3 tools/host_build.py --sanitize
python3 tools/host_validate.py
python3 tools/host_test.py --sanitize
python3 tools/host_stream.py --sanitize
```

- `host_validate.py`: the Core1 presenter, ZZGL and librixgpu run the
  scene against the transport simulator (`tests/sim_rix.c`), as shipped and
  on the fallback path: three presets, an early stop and a 6000-frame run
  each, with no fence timeout, no buffer lifetime violation, and Core1
  writing a frame buffer only while no list, swap or scan-out uses it. Logs
  in `tests/validation/`.
- `host_test.py`: the scene is drawn by one software rasteriser directly,
  through the adapter with a stand-in that refuses what ZZGL refuses
  (identical frames), and through the adapter with its pixel path, into a
  buffer that starts each frame with leftover pixels (frames within the
  rounding of RGB565 of the reference). It also writes the pictures of
  `previews/`.
- `host_stream.py`: on the fallback path the simulator receives the same
  lists, word for word, as when every quad goes through ZZGL; as shipped the
  pixel path is in use, drops nothing, and RasterIX receives a small
  fraction of the triangles.
- Not checked on a PC: the pixels RasterIX really produces, how long Core1
  takes to write a screen, and the frame rate. The simulator is not a
  performance predictor. The pictures of `previews/` are PC renderings, not
  captures.

## Source tree

| Directory | Content | Licence |
|---|---|---|
| `matrix/matrixview.c`, `.h`, `matrixview_font.h` | the scene and its font, from MPlayer; one marked change | GPL-2.0-or-later |
| `matrix/mv_gl.c`, `gl_common.h` | the scene's GL calls on ZZGL, the pixel path and the direct path | GPL-2.0-or-later |
| `matrix/mv_sys.c`, `manager.c`, `matrix_scene.h` | memory, random numbers, presenter interface | GPL-2.0-or-later |
| `matrix/history.c`, `.h` | previous-frame copy of the presenter (not used by this scene) | 0BSD |
| `ARM_RasterIX/` | 68k launcher, Core1 presenter, start-up, MMU | 0BSD |
| `zzgl/`, `librixgpu/` | fixed-function GL (three entry points added at the end of `zzgl.c`) and RasterIX driver (unchanged) | 0BSD |
| `tests/`, `tools/` | PC bench and build scripts | as marked in each file |
| `upstream/matrixview/` | the three MPlayer files, untouched | GPL-2.0-or-later |

## Licensing

MatrixView: Copyright (C) 2003 Alex Zolotov, with changes by Tugrul Galatali;
MPlayer video-out port by Pigeon (2006). ZZ9000 adaptation: Copyright (C)
2026 Xanxi. The scene, its adaptation and the resulting `ZZScreensaverR`
executable are distributed under the **GNU GPL version 2 or later**; see
`COPYING`. Other files carry 0BSD notices and stay usable under those terms
on their own.

Source provenance: see `PROVENANCE.md`.
