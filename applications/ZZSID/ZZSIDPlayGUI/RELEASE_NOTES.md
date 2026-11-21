# ZZSIDPlayGUI 1.0 — SID Music Player for AmigaOS 3.x / ZZ9000

**© 2026 Xanxi**

ZZSIDPlayGUI brings Commodore 64 SID music playback to classic Amiga computers equipped with the MNT ZZ9000 graphics card and XACP firmware.

The application combines a native AmigaOS 68k graphical interface with SID emulation running on the ZZ9000's ARM Cortex-A9 Core1.

The ARM processor handles C64 CPU emulation, SID synthesis and audio generation using **fake6502 and reSID**, while the Amiga manages the user interface, music files, playlists and stereo playback through AHI.

The result is a SID player that takes advantage of the ZZ9000's additional processing power while remaining fully integrated with AmigaOS.

## Features

- Native AmigaOS 3.x GadTools graphical interface.
- MOS **6581 and 8580** SID chip emulation using reSID.
- Selectable SID models and filter options.
- **1SID and 2SID playback**, including independent stereo and mirror modes.
- Automatic detection of the second SID address from supported PSID headers.
- Support for extended SID addresses, including `$DE00`.
- Playlist management and subtune selection.
- Autoplay and configurable Default Drawer with remembered directories.
- Stereo audio output through AHI.
- Low 68k processing overhead, with SID emulation performed on ARM Core1.
- A single AmigaOS executable with the ARM binary embedded. No separate Core1 file is required.

## 2SID Support

Version 1.0 introduces reliable independent 2SID stereo playback, including automatic routing of SID register accesses to the correct chip addresses.

The implementation has been tested on a real Amiga 4000 / ZZ9000 setup, using SID files from the High Voltage SID Collection (HVSC).

The tested configurations include:

`$D420`, `$D440`, `$D4A0`, `$D500`, `$D520`, `$D5A0` and `$DE00`.

Representative test music includes compositions by **A-Man, Cadaver, Proton, Richard Bayliss, LMan, Uctumi and Shogoon**.

Selected tunes were compared with playback on an **USBSID-Pico equipped with two real MOS 6581 SID chips**, with comparable musical results.

## Requirements

- AmigaOS 3.x on a compatible 68k Amiga.
- MNT ZZ9000 with compatible XACP firmware supporting ARM Core1 applications.
- AHI installed and configured for audio output.
- Your own Commodore 64 `.sid` music files.

## Installation

1. Download `ZZSIDPlayGUI_1.0_AmigaOS3.zip`.
2. Extract the archive to a drawer on your Amiga.
3. Launch `ZZSIDPlayGUI` from Workbench using the included `ZZSIDPlayGUI.info` icon.
4. Open a SID file or add music to the playlist.
5. Optionally configure your preferred music directory through the Default Drawer setting.

The ARM Core1 executable is already embedded in ZZSIDPlayGUI. **No additional ARM binary needs to be installed.**

SID music files are not included.

## Limitations

ZZSIDPlayGUI is a SID music player, not a complete cycle-accurate Commodore 64 emulator.

Some music requiring precise CIA timing or advanced RSID behaviour may not play correctly.

**Version 1.0 supports up to two SID chips of the same type.**

3SID playback, PSID v5 multi-SID configurations and independent SID model selection for each chip are **not supported in this release**.

## Future Developments

Further developments are planned, including:

- **`playsid.library` support**
- **SID MIDI support**

These features are not part of version 1.0.

## Source Code and Credits

ZZSIDPlayGUI is developed by **Xanxi** as part of the XACP project for the MNT ZZ9000.

The complete source package includes the AmigaOS 68k frontend, ARM Core1 engine, reSID, fake6502 and offline Windows/MSYS build scripts.

The software is distributed under the **GNU General Public License version 2**, with the original copyright and licensing notices of third-party components retained.

**Credits:**
- Xanxi — AmigaOS application, ARM integration and ZZ9000/XACP implementation.
- Dag Lem and contributors — reSID.
- Mike Chambers and contributors to the omarandlorraine fork — fake6502.

**Project:** https://github.com/Xanxi-Amiga/XACP-ZZ9000

**Release tag:** `ZZSIDPlayGUI-1.0`
