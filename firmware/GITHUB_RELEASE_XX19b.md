# Firmware XX19b / XACP v1.7

XX19b is the XACP v1.7 firmware baseline for the MNT ZZ9000.

It keeps the established XX19a / XACP v1.6 layout below `0x30000000` and adds a high ARM-only Core1 arena.

## Changes

- 248 MiB ARM-only Core1 arena at `0x30000000-0x3F800000`
- 4 MiB guard at `0x3F800000-0x3FC00000`
- top 4 MiB kept for firmware/hardware allocations
- legacy SMUSH Codec1/37/47 implementation removed from the build
- IMA ADPCM and the established v1.6 low-memory map retained
- corresponding firmware source published under `firmware/source/XX19b/`

## Installation

Install `BOOT_XX19b.bin` using the normal ZZ9000 firmware procedure, then perform a complete power cycle.

## Source

```text
firmware/source/XX19b/
sdk/xacp_memory_map_v1_7.h
```

## License

Firmware source retains the repository firmware licensing and third-party notices. See `firmware/COPYING` and the source tree.
