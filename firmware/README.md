# XACP Firmware Builds

Current public baseline:

```text
Firmware: XX19d-RIX
Protocol: XACP v1.8
```

Firmware build numbers and XACP protocol versions are separate.

XX19d-RIX supersedes XX19c as the public firmware baseline. XACP v1.8
retains existing v1.7 allocations and introduces the image-service window.

## New in XX19d-RIX

- RasterIX hardware texturing, Z testing and blending inside the ZZ9000 FPGA.
  The separately released [ZZQuake2 1.0](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZQuake2-1.0)
  uses the FPGA renderer alongside two software rendering modes.
  [ZZScreensavers 1.1](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZScreensavers-1.1)
  adds the FPGA-accelerated Solar Winds screensaver.
- JPEG and PNG decoding backends and XACP Image Service API v1.3.
- XACP v1.8: dedicated 1 MiB shared image window. AmigaOS datatypes are
  under development and are not shipped with the firmware.
- Public headers and example client in the repository's `sdk/` directory.

Firmware release: [XX19d-RIX](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/XX19d-RIX).
Firmware: `BOOT_XX19d-RIX.bin`. Corresponding source and FPGA Vivado project
are supplied as `XACP.XX19d-RIX.sources.zip` on the release page.

The reference hardware is an Amiga A4000/Zorro III with MNT ZZ9000. Install
from a backed-up FAT32 microSD card and cold-start the machine. Do not assume
compatibility with different expansion boards or Zorro II installations.

See `../docs/XACP_V1_8_DEVELOPER_NOTES.md` and the previous XX19c release
for the history of Core0/Core1 corrections.

## XX19c fixes (historical)

### Core0 / Core1 coexistence

XX19c fixes a Core1 launch/cache-coherency problem that could interfere with
persistent firmware services running on Core0.

The Core1 startup path now:

- halts CPU1 before republishing its launch vector and target;
- publishes launch metadata with explicit memory barriers;
- avoids Core1-side operations on the shared PL310 L2 controller;
- performs the relevant Core1 cache maintenance at L1 instead.

This removes the firmware-side conflict that could cause a white screen or
system lock-up when launching ZZDoom or another Core1 application while
ZZMIDI realtime was active.

With XX19c, **ZZMIDIGate is no longer required as a workaround for this
Core0/Core1 startup conflict**. It remains useful only for older XX19a/XX19b
installations that retain the previous launch behaviour.

### Low firmware / framebuffer guard

The low firmware image lives below the ARM framebuffer, which starts at:

```text
0x00200000
```

XX19c adds linker-time checks so future firmware growth cannot silently cross
that boundary.

The linker now enforces:

```text
hard framebuffer limit:     0x00200000
64 KiB safety limit:        0x001F0000
```

The validated XX19c build ends at:

```text
__low_firmware_end = 0x001B9A2E
```

leaving 288,210 bytes before the framebuffer.

This is a build-time safety mechanism and does not change the public XACP
memory map.

## XACP v1.7 high arena

```text
0x30000000 - 0x3F800000   248 MiB Core1 application arena
0x3F800000 - 0x3FC00000     4 MiB guard
0x3FC00000 - 0x40000000     4 MiB firmware/hardware reserved
```

Historical v1.7 header: `../sdk/xacp_memory_map_v1_7.h`.
Current XACP v1.8 header: `../sdk/xacp_memory_map_v1_8.h`.


The high arena is ARM-only. Host-side data must be staged through visible
memory and copied by ARM code.

## Compatibility change inherited from XX19b: SMUSH Codec1/37/47

The legacy SMUSH Codec1/37/47 implementation remains removed because its
fixed scratch buffers occupied the XACP v1.7 Core1 arena. Existing
`ACC_CMPTYPE` values remain reserved and are not renumbered; those legacy
requests are unsupported. IMA ADPCM and unrelated paths are unchanged.

## Source

XX19d-RIX corresponding sources are available directly in the repository:
`source/XX19d-RIX/`, including the ARM firmware, FSBL, FPGA RTL and
Vivado project. The XX19c sources remain under `source/XX19c/`.


The firmware source is distributed under the repository firmware licensing;
see `COPYING` and the notices in the source tree.

## Installation

Install `BOOT_XX19d-RIX.bin` from the release package using the documented
microSD method, then perform a complete power-off before validation.
Keep the prior `BOOT_XX19c.bin` for rollback.

Historical firmware remains under `archive/`.
