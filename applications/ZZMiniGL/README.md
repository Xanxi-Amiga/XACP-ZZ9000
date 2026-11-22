# ZZMiniGL for ZZ9000 - Beta 1

**MiniGL 29.1 compatibility library for AmigaOS 3.x, a physical MNT ZZ9000, XACP v1.8, public firmware XX19d-RIX and FPGA RasterIX.**

ZZMiniGL provides a functional but incomplete subset of the `minigl.library` 29.1 interface (dispatch ABI 5). Unmodified compatible AmigaOS 68k applications run on the 68060; their MiniGL commands are passed through the ZZ9000 ARM Cortex-A9 Core1 to the RasterIX FPGA renderer. The completed 16-bit image is presented through Picasso96.

This is **Beta 1**, not a complete MiniGL or OpenGL implementation.

## Requirements

- AmigaOS 3.x with a **68060 and FPU**, a physical ZZ9000 and Picasso96.
- Public ZZ9000 firmware **XX19d-RIX** with XACP v1.8 and RasterIX.
- A ZZ9000 Picasso96 16-bit mode of **320x240**, **640x480** or **800x600**.
- AHI sound for the tested GLQuake configurations.

## Installation - private drawer only

**Do not copy `minigl.library` into `LIBS:` or replace another MiniGL installation.**

**`minigl.library` and `ZZMiniGLLoad` MUST come from the same Beta 1 archive.** An older loader paired with a newer library will refuse to start. Keep both files together in the target program's drawer, along with a suitable launch script.

For GLQuakeR200 1.5.1, get the unmodified 68060 executable separately from Aminet (`game/shoot/GLQuakeR200`), name it `GLQuakeR200` where the script expects that name, and supply your own legally obtained Quake `id1/` data. From the drawer containing the game, run:

```
Execute ZZGLQuakeR200Video640
```

The `ZZGLQuakeR200` script launches the 320x240 configuration. The `ZZGLQuake640` script is for the separate classic MiniGL GLQuake distribution. Read the header of each AmigaDOS script before running it.

`ZZDemos` is a batch launcher for several third-party demonstrations. **PiStorm3D `gears` and its `data/chrome.ppm` texture are included**. Other binaries are **not included**: RadeonGL demos GLSmoke, Spinner, Glow, Landscape and Showcase, and PiStorm3D demos library_test and warp. Download those programs from their original authors and place them in the same drawer as described by the script.

The beta allows **one MiniGL context / Core1 client at a time**. Close other Core1 applications before launching another MiniGL program. The loader's `RECORD` option enables diagnostic recording; `ZZMiniGLLoad BLACKBOX` can retrieve saved diagnostics after a crash, before running another ZZ9000 program. Logging can reduce speed and should be disabled for performance comparisons.

## Included third-party MiniGL Gears demonstration

The original PiStorm3D `gears` executable and its required `data/chrome.ppm`
texture are included unchanged from the PiStorm3D distribution on Aminet.
Their original credits and license notices are included under `LICENSES/`.

`gears` is launched by the existing `ZZDemos` AmigaDOS script, together with
other supported third-party demonstrations. To run the complete batch,
first obtain and copy the other demonstration executables listed in the
`ZZDemos` script into the same drawer. Then, from that drawer, type:

```
Execute ZZDemos
```

No extra Gears launcher is provided. The bundled `gears` and `data/chrome.ppm`
are PiStorm3D demo files, not sources or binaries from any private ZZGear test.
Do not replace this beta's `minigl.library` with PiStorm3D's version.

Upstream: Steffen Haeuser, PiStorm3D, Aminet `util/libs/PiStorm3D`.
Source repository: https://github.com/SteffenHaeuser/PiStorm3D .

## Validated software and performance

- **GLQuakeR200 1.5.1**: unmodified 68060 binary, tested on two real A4000/ZZ9000 configurations.
- **Classic MiniGL GLQuake** (`MiniGL_Library_Quake1`): unmodified binary tested with AHI, on earlier development builds.
- **RadeonGL 0.15 demonstration executables**: GLSmoke, Spinner, Glow, Landscape, Showcase.
- **PiStorm3D 30.0 demonstration executables**: library_test, gears, warp.

The following `timedemo demo1` results were measured with **development build A8.26**, on an A4000 / CyberStorm MK II 68060 50 MHz / ZZ9000, 16-bit screen, VSync disabled:

| Resolution | No sound | AHI sound |
|---|---:|---:|
| 320x240 | 28.7 FPS | 25.0 FPS |
| 640x480 | 24.2 FPS | 22.3 FPS |
| 800x600 | 19.4 FPS | 18.6 FPS |

A separate **A8.29** hardware run confirmed **22.2 FPS at 640x480 with AHI**. The other resolutions were not remeasured on A8.29. These results are observations on the stated hardware, not performance guarantees.

## Known limitations

- ABI 5: **205 dispatch entries**, **115 with implemented handlers** and **90 unimplemented**. Some implemented handlers support only a subset of parameters and operations.
- **One context only**; simultaneous multi-window contexts are not supported.
- 16-bit screens limited to 320x240, 640x480 and 800x600.
- Single physical texture unit. Second-texture operations require an additional rendering pass.
- Maximum physical texture size 256x256; larger source textures can be reduced. **No mipmaps**.
- 68060/FPU required. No compatibility claim for other 68k CPUs, PiStorm, Emu68 or UAE.
- The classic GLQuake Paula sound path has caused resets; AHI was used successfully.
- Heavy geometry workloads can be much slower than equivalent demonstrations on Radeon hardware.
- Not every MiniGL demo, game or rendering mode has been tested.

## Source code and licenses

The matching **ZZMiniGL Beta 1 Source** archive contains the functional sources for **`minigl.library` and `ZZMiniGLLoad`**, including the embedded ARM Core1 payload, all necessary code-generation tools and an offline Windows/MSYS build. From its top-level directory, run:

```
sh build_all.sh
```

Use the locally installed ARM and Amiga 68k toolchains described in `tools/windows_build.py`. The provided release executables were taken from the hardware-validated A8.29 build; the repackaged source was not fully recompiled in this environment. `minigl.library` and `ZZMiniGLLoad` must remain a matching pair.

The MiniGL 29.1 interface is subject to the **Hyperion MiniGL Open Source License**. The Core1 start-up/MMU components are adapted from GPL-2.0-or-later XACP source. ZZGL and librixgpu carry 0BSD notices, with additional conditions for any incorporated third-party code. See `THIRD_PARTY_LICENSES.md` and the original license texts in the source archive.

**Credits:** MiniGL shared-library interface - Steffen Haeuser / Hyperion; RadeonGL reference implementation - Marcus Gerards; RasterIX - ToNi3141; ZZ9000 - MNT Research; ZZMiniGL and XACP integration - Xanxi.

No commercial Quake data or RadeonGL binaries are redistributed. The only bundled external demo is the unmodified PiStorm3D `gears` executable and its required chrome texture, with original licence notices.
