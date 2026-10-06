# XACP Firmware Builds

Current public baseline:

```text
Firmware: XX19c
Protocol: XACP v1.7
```

Firmware build numbers and XACP protocol versions are separate.

XX19c supersedes XX19b as the public firmware baseline. It does **not** change
the XACP v1.7 ABI or shared-memory map.

## What XX19c fixes

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

Normative header:

```text
../sdk/xacp_memory_map_v1_7.h
```

The high arena is ARM-only. Host-side data must be staged through visible
memory and copied by ARM code.

## Compatibility change inherited from XX19b: SMUSH Codec1/37/47

The legacy SMUSH Codec1/37/47 implementation remains removed because its
fixed scratch buffers occupied the XACP v1.7 Core1 arena. Existing
`ACC_CMPTYPE` values remain reserved and are not renumbered; those legacy
requests are unsupported. IMA ADPCM and unrelated paths are unchanged.

## Source

Corresponding source:

```text
source/XX19c/
```

The firmware source is distributed under the repository firmware licensing;
see `COPYING` and the notices in the source tree.

## Installation

Install `BOOT_XX19c.bin` using the normal ZZ9000 firmware procedure, then
perform a complete power-off before validation.

Historical firmware remains under `archive/`.
