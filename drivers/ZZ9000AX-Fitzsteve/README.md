# ZZ9000AX Fitzsteve Edition FS1

Small community modification of the MNT Research ZZ9000AX AHI driver 4.19.

## Change

FS1 exposes **two AHI mixing channels** instead of one:

```c
AHIDB_MaxChannels: 1 -> 2
```

This is intended for XACP game ports that use **AHI sound effects together with MP3 or ZZMIDI music**. AHI performs the software mix and the original driver still sends a single stereo PCM stream to the ZZ9000AX.

The change was validated by Fitzsteve with ZZQuake on a 68030/25 + ZZ9000AX system.

The binary identifies itself as:

```text
ZZ9000AX 4.19-FS1 (19.09.2026)
```

## Scope

FS1 does not change the firmware-facing playback path, hardware registers, IRQ handling, buffers or MHI ownership logic. It does **not** add simultaneous AHI + MHI support; the original MNT 4.19 exclusion remains unchanged.

The source also includes a small `UtilityBase` type adjustment so the old 4.19 source builds with current amiga-gcc/NDK headers. This does not change audio behaviour.

## Build

From the repository root:

```sh
./build.sh
```

Equivalent command:

```sh
m68k-amigaos-gcc src/zz9000ax-ahi.c src/asmfuncs.s -O3 \
  -o src/zz9000ax.audio \
  -Wall -Wextra -Wno-unused-parameter \
  -nostartfiles -m68020 -ldebug
```

An AmigaOS NDK/AHI development environment is required.

## Install

1. Back up the existing `DEVS:AHI/zz9000ax.audio`.
2. Copy the FS1 binary to `DEVS:AHI/zz9000ax.audio`.
3. Reboot.
4. Open AHI preferences and select ZZ9000AX for the required unit.
5. Set **Channels = 2**.

To roll back, restore the previous `zz9000ax.audio` and reboot.

## Base

Based on MNT Research `zz9000ax.audio` 4.19 (03.01.2023), preserved in the historical ZZ9000 driver sources.

## License

GPL-3.0-or-later, following the original driver.
