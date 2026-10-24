# ZZOpenLara 1.0 - Tomb Raider I on Amiga / ZZ9000

**Xanxi - XACP for MNT ZZ9000**  
OpenLara original engine: Timur "XProger" Gagiev.

ZZOpenLara ports the original 1996 **Tomb Raider I** to classic Amigas equipped
with the **MNT ZZ9000**. The engine runs on the card's ARM Cortex-A9 Core1;
the Amiga 68k launcher handles Workbench integration, files, input and audio.

## Renderers and requirements

| Renderer | Resolutions | Minimum validated firmware family |
| --- | --- | --- |
| RasterIX FPGA acceleration | 320x240, 640x480, 800x600 | **XX19d-RIX** |
| ARM software rendering | 320x240, 640x480 | **XX19c** |

Requires AmigaOS 3.x, ZZ9000, Picasso96 and AHI. Developed and tested on
an A4000/68060 with XACP firmware. For the RasterIX renderer, install the
XACP-compatible Picasso96 `zz9000.card` supplied with the XACP firmware.

Software rendering (640x480) includes the final status/menu-bar drawing correction.

## Download and installation

The **Amiga binary package** is attached to the corresponding
[GitHub release](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases).
It includes the 68k GUI/launcher, the four matching ARM binaries, a Workbench
icon, standalone Windows conversion tools, and installation guides in English
and French. The **ARM engine source** is available in the [`source/`](source/)
subdirectory and as a separate release archive.

**You must provide your own original Tomb Raider I PC game data.** For the
complete experience, including the full soundtrack and PlayStation
loading screens, you will also need the original Tomb Raider I PlayStation
CD (or a disc image made from your own copy).

No copyrighted game data is included in this release.

1. Extract the entire `ZZOpenLara/` directory onto your Amiga hard drive.
   Keep `ZZOpenLara`, `ZZOpenLaraGUI`, `ZZOpenLaraGUI.info` and all four
   `zzopenlara*.bin` files together in the same directory.
2. From your own **Tomb Raider I PC CD**, copy the **contents** of `DATA/`
   (including `TITLE.PHD` and the level files) directly to `ZZOpenLara/data/`.
   Do not create an extra `DATA/` subdirectory.
3. For cinematics, copy the `.RPL` files from the PC CD's `FMV/` directory
   to the **same** `ZZOpenLara/data/` directory.
4. **Optional full soundtrack (58 tracks):** on Windows, double-click
   `tools/ZZOpenLara-Music-Complete.bat`. Select the PlayStation BIN/CUE's
   `.cue` in the first dialog, then the PC CD audio `.cue` (or the PC WAV
   tracks folder) in the second. The tool produces
   `tools/complete_music/ol_music.pak`. Copy it to `ZZOpenLara/data/`.
   This single two-source run is necessary: dragging the PSX and PC images
   separately onto the music EXE would overwrite the first result.
5. **Optional PSX loading screens:** drag the PlayStation `.cue` onto
   `tools/ZZOpenLara-PSX-Screens.exe`. Copy the five generated `.RAW` files
   from `psx_screens/` beside the PlayStation disc image into
   `ZZOpenLara/data/`.
6. Start `ZZOpenLaraGUI` from Workbench and choose the renderer, resolution
   and audio settings.

The two Windows `.exe` tools are standalone: **end users do not need Python**.
The Python source tools are also included for experienced users on other
platforms, but macOS/Linux workflows have not been tested for this release.

The five extracted PSX loading-screen files are `GYMLOAD.RAW`,
`AZTECLOA.RAW`, `GREEKLOA.RAW`, `EGYPTLOA.RAW` and `ATLANLOA.RAW`.

For a detailed description of controls, troubleshooting and data layout,
see `README_EN.txt` or `README_FR.txt` **inside the Amiga binary archive**.

## Source and license

- Original OpenLara by Timur "XProger" Gagiev: **BSD-2-Clause**.
- Original ZZOpenLara/XACP ARM adaptations explicitly so marked: **GPLv3**
  with a limited attribution term under section 7(b), where permitted.
- Historical runtime components already carrying `GPL-2.0-or-later` notices
  retain those notices. Other components retain their own licenses.
- `librixgpu`/ZZGL portions identified as such: **0BSD**.
- Amiga 68k `ZZOpenLara` and `ZZOpenLaraGUI`: Xanxi **freeware executables**;
  their sources are not part of this ARM source publication.

See [`source/LICENSES.txt`](source/LICENSES.txt) and
[`source/ATTRIBUTION.txt`](source/ATTRIBUTION.txt) for the full details.

RasterIX FPGA core: ToNi3141. ZZ9000 hardware: MNT Research.
The port is an independent project, not an official Tomb Raider or MNT release.
All Tomb Raider game assets remain copyrighted by their respective owners.

Project: https://github.com/Xanxi-Amiga/XACP-ZZ9000
