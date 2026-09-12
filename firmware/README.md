# XACP Firmware Builds

Current public baseline:

```text
Firmware: XX19b
Protocol: XACP v1.7
```

Firmware build numbers and XACP protocol versions are separate.

XX19b supersedes XX19a as the firmware baseline for new XACP applications. It retains the XACP v1.6 layout below `0x30000000` and adds the v1.7 high Core1 arena.

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

The high arena is ARM-only. Host-side data must be staged through visible memory and copied by ARM code.

## Compatibility change: SMUSH Codec1/37/47

The legacy SMUSH Codec1/37/47 implementation is removed from the XX19b build because its fixed scratch buffers occupied the new Core1 arena. Existing `ACC_CMPTYPE` values remain reserved and are not renumbered; those legacy requests are unsupported. IMA ADPCM and unrelated paths are unchanged.

## Source

Corresponding source:

```text
source/XX19b/
```

The firmware source is distributed under the repository firmware licensing; see `COPYING` and the notices in the source tree.

## Installation

Install `BOOT_XX19b.bin` using the normal ZZ9000 firmware procedure, then perform a complete power-off before validation.

Historical firmware remains under `archive/` and `source/XX19a/` for reference.
