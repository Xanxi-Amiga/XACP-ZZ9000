# ZZDarkForcesNEXT

**ZZDarkForcesNEXT** is a ZZ9000/XACP port of **The Force Engine** for *STAR WARS: Dark Forces* on classic Amiga systems.

The TFE game engine and classic software renderer run natively on the ZZ9000's **Zynq-7020 Cortex-A9 Core1**. The Amiga side provides Workbench launch, Picasso96 display presentation, input, AHI sound, filesystem staging/writeback and optional CAMD MIDI output.

Port by **Xanxi**, 2026.

> A legally obtained copy of *STAR WARS: Dark Forces* is required. Original commercial game data is not included.

## Features

- Native TFE execution on ZZ9000 Core1.
- Launch directly from the Workbench icon.
- ToolTypes for video and music selection.
- Five public display modes, validated on real hardware:
  - `320x200`
  - `320x240`
  - `640x400`
  - `640x480`
  - `800x600`
- Picasso96 asynchronous triple-buffered presentation.
- Sound effects through AHI at 11025 Hz.
- Three music modes:
  - `SC55` - Roland SC-55 SoundFont synthesized on the ZZ9000.
  - `TIMGM6MB` - TimGM6mb SoundFont synthesized on the ZZ9000.
  - `CAMD` - MIDI output through `camd.library` to an external synthesizer.
- TFE save/load support, quicksaves and pilot progress written back to the Amiga filesystem.
- Persistent in-game Configuration screen.
- Optional TFE gameplay improvements:
  - Autorun
  - Crouch Toggle
  - Secret Message
  - Map Key Colors
  - Map Secrets
  - Smooth VUE
- Cooperative Core1 return on exit.

## Requirements

- Classic Amiga with ZZ9000.
- Picasso96.
- AHI.
- A compatible XACP/ZZ9000 firmware able to launch Core1 applications.
- A legally owned copy of *STAR WARS: Dark Forces*.
- `camd.library` and a MIDI interface/synthesizer only if using `CAMD`.

Development and validation were performed on a real **Amiga 4000 / 68060 + ZZ9000** system.

## Installation

Create a drawer for the game and place the release files there.

A typical installation is:

```text
ZZDarkForces/
    ZZDarkForces
    ZZDarkForces.info
    zzdf.bin

    Data/
        DARK.GOB
        SOUNDS.GOB
        SPRITES.GOB
        TEXTURES.GOB
        ...original Dark Forces LFD files...
        weapons.json
        projectiles.json
        effects.json
        pickups.json
        TfeMessages.txt

    SoundFonts/
        <SC-55 SoundFont>.sf2    optional, for SC55
        <TimGM6mb SoundFont>.sf2 optional, for TIMGM6MB
```

Copy the original Dark Forces data from your legally owned installation into `Data/`.

The supplied TFE external-data files must also be copied into `Data/`:

```text
weapons.json
projectiles.json
effects.json
pickups.json
TfeMessages.txt
```

The launcher accepts common case variants of the `Data` and `SoundFonts` drawer names.

For SoundFonts, the **file name itself does not have to match one exact spelling**. The final launcher scans the SoundFonts drawer for `.sf2` files and matches them case-insensitively:

- `SC55` selects an `.sf2` file whose name contains `55`.
- `TIMGM6MB` selects an `.sf2` file whose name contains `TIM` or `GM6`.

This allows names such as `Roland_SC_55.sf2`, `Roland SC-55.sf2`, or other punctuation/case variants. If no matching SoundFont is found, the launcher reports the `.sf2` files it found and falls back to CAMD.

## Workbench ToolTypes

The public configuration is intentionally simple. Open the icon's **Information** window and select one video mode and one music mode.

### Video - choose one

| ToolType | Description |
|---|---|
| `320x200` | Original DOS geometry; fixed-point software renderer. |
| `320x240` | 4:3 square-pixel mode, well suited to modern flat-panel displays. |
| `640x400` | Higher-detail classic 320x200 geometry. |
| `640x480` | Higher-detail 4:3 square-pixel mode. |
| `800x600` | Highest public resolution. |

If no video ToolType is present, the default is `320x200`.

### Music - choose one

| ToolType | Description |
|---|---|
| `SC55` | Uses a Roland SC-55 SoundFont found in the SoundFonts drawer; the launcher matches `.sf2` filenames containing `55` (case-insensitive). |
| `TIMGM6MB` | Uses a TimGM6mb SoundFont found in the SoundFonts drawer; the launcher matches `.sf2` filenames containing `TIM` or `GM6` (case-insensitive). |
| `CAMD` | Sends MIDI through `camd.library` to an external synthesizer. |

If no music ToolType is present, the default is `CAMD`.

**If you own a real Roland SC-55 connected to the Amiga MIDI interface, use `CAMD`.**  
`SC55` means the SoundFont, not the physical Roland module.

A ToolType can be disabled without deleting it by putting it in brackets, for example:

```text
(SC55)
```

## In-game Configuration

Open the normal Dark Forces menu with `ESC`, then select **CONFIG**.

The ZZDarkForcesNEXT configuration screen includes:

- Master volume
- Sound FX volume
- Music volume
- Mouse sensitivity
- Autorun
- Crouch Toggle
- Secret Message
- Map Key Colors
- Map Secrets
- Smooth VUE

Settings are stored in:

```text
Data/ZZDFOPT.CFG
```

## Saves

TFE save files, quicksaves and `DARKPILO.CFG` are staged from `Data/` and written back there when changed.

Quicksave / quickload use the TFE mappings:

```text
Alt+F5    quicksave
Alt+F9    quickload
```

## Rendering

`320x200` uses TFE's classic fixed-point software renderer.

The higher public resolutions use TFE's classic floating-point software renderer. The GPU renderer is not used by this port.

Higher resolutions require more rendering work, so frame rate decreases as resolution increases.

## Source

The GPL-covered Core1 blob is built from a pinned The Force Engine revision:

```text
ed9e51c315078d6460551593e48fd294e131fc83
```

The source repository contains the corresponding TFE-derived/Core1 source and the build system for `zzdf.bin`.

The **68k launcher `ZZDarkForces` is closed source** and is distributed separately as a binary. Its source is not part of the public GPL source repository.

See [BUILDING_BLOB.md](BUILDING_BLOB.md) and [LICENSING.md](LICENSING.md).

## License

The Force Engine is licensed under **GNU GPL-2.0**. The TFE-derived ZZDarkForcesNEXT Core1 blob and its corresponding source are distributed under GPL-2.0.

The 68k launcher is distributed separately as a closed-source binary.

SoundFonts are separate data files with their own licenses. See [SOUNDFONT_LICENSES.md](SOUNDFONT_LICENSES.md).

*STAR WARS: Dark Forces* and related names, trademarks and commercial game assets belong to their respective rights holders. No original commercial game data is included.

## Credits

- **The Force Engine / TFE** - upstream engine and Dark Forces reverse engineering.
- **Xanxi** - ZZ9000/XACP port, Core1 platform layer, Amiga integration and hardware validation.
- ZZ9000 / XACP contributors and the classic Amiga community.
