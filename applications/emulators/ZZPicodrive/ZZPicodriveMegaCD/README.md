# ZZPicoDriveMegaCD 1.2 — Sega Mega-CD / Sega CD Edition

**ZZPicoDriveMegaCD** runs Sega Mega-CD / Sega CD games from disc images stored on an Amiga hard disk, using the ZZ9000 ARM Core1 for PicoDrive emulation. AmigaOS handles disc-file access, input, video presentation and audio output. **Sega CD 32X** disc games are also supported; cartridge games are handled by ZZPicoDriveMD or ZZPicoDrive32X instead.

## Features

- **CHD (recommended)** and **CUE + BIN** disc images; `.m3u` playlists and multi-disc navigation.
- Mega-CD CD audio and PCM sound, mixed with Mega Drive FM/PSG; Paula or AHI output.
- Automatic BIOS choice by region; configurable BIOS and image drawers.
- Internal backup RAM (`.brm`) and five savestate slots.
- PAL 50 Hz / NTSC 59.98 Hz display output; Sega CD 32X support.

**Format notes:** CHD and CUE/BIN were validated on an A4000/060. ISO with WAV/BIN audio via CUE is supported by PicoDrive but was **not separately validated on the Amiga**.

## Requirements

- Amiga with **68030 at 25 MHz or higher**, MNT ZZ9000 and sufficient Fast RAM (tested on A4000/060).
- **XACP firmware XX19c or a later compatible XACP release**, with the matching `zz9000.card`. **XX19d-RIX or a later compatible XACP release is recommended**. Official MNT and BlitterStudio firmware are not compatible.
- Picasso96 **320×240, 32-bit** ZZ9000 screen mode. **PAL mode requires a monitor supporting 50 Hz; otherwise select NTSC (60 Hz) in the launcher.**
- CD images on the Amiga hard disk and the appropriate **Mega-CD BIOS**, supplied by the user. No BIOS, ROM or disc image is included.
- AHI is optional. **On slower supported CPUs (such as a 68030 at 25 MHz), prefer Paula audio over AHI.**

## BIOS setup

Place BIOS files in a `BIOS` drawer next to `ZZPicoDriveMegaCD`, or choose another folder using **Set BIOS drawer** in the launcher. One matching BIOS per region is enough. The launcher displays the BIOS detected for each region.

| Region | Recognised BIOS filenames (in search order) |
| --- | --- |
| USA | `us_scd2_9306.bin`, `SegaCDBIOS9303.bin`, `us_scd1_9210.bin`, `bios_CD_U.bin` |
| Europe | `eu_mcd2_9306.bin`, `eu_mcd2_9303.bin`, `eu_mcd1_9210.bin`, `bios_CD_E.bin` |
| Japan | `jp_mcd2_921222.bin`, `jp_mcd1_9112.bin`, `jp_mcd1_9111.bin`, `bios_CD_J.bin` |

The disc's region normally determines the BIOS. The launcher can force `usa`, `eur` or `jap`. Region-free BIOS versions are supported; extended-BRAM BIOS variants are not. A BIOS file alone may be selected to open the console's backup RAM manager.

## Getting started

Copy the application folder to the Amiga. Double-click `ZZPicoDriveMegaCD`, choose a disc image, region, audio backend and controller, then press **Start**. The ARM Core1 program is already embedded in the executable. The image and BIOS folders can be remembered in the launcher.

See [INSTALL.md](INSTALL.md) for initial setup.

## Controls and saves

| Keyboard | Action |
| --- | --- |
| Cursor keys | Direction |
| Keypad 1 / 2 / 3 | A / B / C |
| Keypad 4 / 5 / 6 | X / Y / Z |
| Keypad 0 | Mode |
| Return / Tab | Start player 1 / 2 |
| Shift+Esc | Quit |
| Del | Reset |
| F1–F5 | Load savestate slot 1–5 |
| Shift+F1–F5 | Save savestate slot 1–5 |
| F9 / Shift+F9 | Next / previous disc |

CD32 and Poseidon USB pads: blue/red/green = A/B/C, left/right shoulder = X/Y, yellow = Z, Play = Start. Mode remains on keypad 0. USB button mapping may need adjustment in Poseidon.

Backup RAM is saved beside the image as `.brm` on exit. Savestates (`.st1`–`.st5`) are kept beside the image and are reliable only with the **same region and audio backend**; PAL/NTSC-incompatible states are refused.

## Games with multiple discs

Use **F9** for the next disc and **Shift+F9** for the previous. Either keep the images in one folder with `(Disc 1)`, `(Disc 2)` (or `(Disk N)`) in their names, or select an `.m3u` playlist listing one image per line:

```text
Night Trap (USA) (Disc 1).chd
Night Trap (USA) (Disc 2).chd
```

Disc switching was tested during play with *Dracula Unleashed*. Switching at a game's actual **insert-next-disc prompt** has not yet been validated. A multi-disc game shares backup RAM across its discs; savestates belong to the disc currently inserted.

## Tested and known limitations

On A4000/060 with XX19d-RIX: *Sonic CD*, *Dune*, *Dracula Unleashed* (disc switching), and Sega CD 32X titles *Corpse Killer* and *Surgical Strike*. CHD and CUE/BIN formats were tested. Europe and US BIOS boot were tested on Amiga; Japanese BIOS boot was only tested on a PC test bench.

**MP3 audio tracks and direct playback from physical Sega CD discs are not supported in this release.** For multi-file disc images, keep the referenced track files in the expected locations. Use only game images and BIOS files you are legally entitled to use.

## Source and credits

See [source/BUILD.md](source/BUILD.md) for the complete offline build and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for licences. ZZ9000/AmigaOS port, Core1 integration, CD file server and hardware validation: **Xanxi**. PicoDrive: **notaz, irixxxx and contributors**; Cyclone: **Dave Haywood, notaz and irixxxx**; libchdr: **Romain Tisserand and contributors**; dr_flac: **David Reid**. PicoDrive source lineage: PokaMilkTea's **VirtuaRacingDeluxeEnhanced**. PicoDrive's historical non-commercial licence applies; see [COPYING_PICODRIVE.txt](COPYING_PICODRIVE.txt). Sega and related trademarks belong to SEGA; this project is not affiliated with SEGA.
