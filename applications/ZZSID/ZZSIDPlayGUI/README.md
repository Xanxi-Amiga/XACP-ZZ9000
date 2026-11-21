# ZZSIDPlayGUI 1.0 — SID player for AmigaOS 3.x and ZZ9000 / XACP

**(c) Xanxi, 2026.**

ZZSIDPlayGUI is a native AmigaOS 68k **GadTools** SID music player using the
ZZ9000's ARM Cortex-A9 **Core1** for Commodore 64 CPU/SID emulation. The ARM
payload runs fake6502 and reSID; AmigaOS handles the interface, SID file
loading, playlist, and stereo output through **AHI**. The ARM blob is embedded
in the 68k executable; **no separate `.bin` is needed on the Amiga**.

## Features

- SID music files (PSID, with limited RSID compatibility; not a full C64 machine).
- Emulation of MOS 6581 / 8580 via reSID, including selectable filter options.
- **1SID** and **2SID independent stereo**; optional 2SID mirror for 1SID tunes.
- Automatic SID2 address selection from PSID v3/v4 headers. Valid ranges:
  `$D420`–`$D7E0` and `$DE00`–`$DFE0` (32-byte alignment).
- Native GadTools GUI, playlist, subtunes, autoplay, and a configurable
  **Default Drawer** with remembered directories.
- AHI stereo output; audio processing and SID emulation on ARM Core1.
- One AmigaOS executable with its matching ARM payload built in.

## Requirements

- AmigaOS 3.x on a supported 68k Amiga (cross-compiled with `-m68020`).
- MNT ZZ9000 running compatible **XACP firmware with Core1 support** (project
  developed against XACP v1.8; no RasterIX GPU is required).
- AHI installed and configured with a working output device.
- `.sid` music files supplied by the user. No copyrighted music is distributed.

## Installation

Extract the GitHub release's **AmigaOS3** archive, copy `ZZSIDPlayGUI` and `ZZSIDPlayGUI.info` into the same drawer
on the Amiga, and run the program from its Workbench icon.
Choose your SID files or add them to the playlist. Configure your default
music drawer through the GUI. Do **not** copy a standalone `zzresid_core1.bin`:
it is already embedded in `ZZSIDPlayGUI`.

## Build from complete sources — offline Windows/MSYS

The entire local ARM and 68k source tree is included, with reSID, fake6502 and
the compatible ABI 5 header. No internet is used by the build scripts.

Run from the project folder:

```sh
sh build_all.sh
```

This builds **A2**, the source revision that routes SID2 writes and reads in
both `$D400`–`$D7FF` and `$DE00`–`$DFFF`, then packages the Amiga release.
Outputs (after a successful build):

```text
output/ZZSIDPlayGUI
output/ZZSIDPlayGUI_1.0_AmigaOS3.zip
```

The toolchains must exist locally at:

```text
C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-g++
C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-gcc
C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-objcopy
C:/amiga-gcc/bin/m68k-amigaos-gcc
```

The Amiga linker uses `-mcrt=nix20`; the ARM build uses VFPv3-D16. No `PATH`
changes, automatic downloads, or MSYS `/c/...` compiler paths are required.
The build checks source and ABI consistency and verifies that the rebuilt ARM
blob appears **exactly once** inside the Amiga executable.

For an optional historical regression binary (A1, **not for release**):

```sh
sh build_A1_reference.sh
```

A1 uses the preserved original ARM blob and **does not** support SID2 at
`$DE00`. Do not confuse its output with the 1.0 release executable.

## Coverage and limitations

SID2 configurations tested on the developer's Amiga (A1/A2 progression):
`$D420`, `$D440`, `$D4A0`, `$D500`, `$D520`, `$D5A0`, `$DE00`, plus 1SID,
2SID mirror, autoplay and Default Drawer. Selected 2SID files were compared
by ear to an **USBSID-Pico with two real MOS 6581 SID chips**. Tests of individual tunes do not establish
universal file compatibility or cycle-accurate emulation.

- PSID v5 multi-SID configuration is not implemented.
- 3SID playback is not part of this 1.0 release.
- RSID/CIA timing is **not cycle-accurate**; some tunes may play incorrectly.
- Different SID models for SID1 and SID2 are not independently configurable.
- This is not a `playsid.library` replacement.

Version 1.0 supports up to **two SID chips of the same type**.
The 3SID, PSID v5 multi-SID and independent per-chip SID model options are
**not supported in this release**.

Planned future developments include `playsid.library` support and SID MIDI support.

The Workbench icon (`assets/ZZSIDPlayGUI.info`) provides both normal and
selected NewIcons images.

## Development and credits

- Original 68k and ARM integration: **Xanxi**.
- reSID SID emulation: **Dag Lem and reSID contributors**, GPL-2.0-or-later.
- fake6502: **Mike Chambers**, with subsequent modifications from the
  `omarandlorraine/fake6502` fork; source states GNU GPL v2.0.
- See `THIRD_PARTY.md` and `COPYING.GPL-2.0.txt` for third-party license notices.

**Publication checklist:** confirm the licensing of original Xanxi-authored
files before uploading a public binary. The bundled third-party components
are GPL; see `PUBLICATION_CHECKLIST.md`.

See [RELEASE_NOTES.md](RELEASE_NOTES.md) and [BUILD_DETAILS.md](BUILD_DETAILS.md).
