# XX19c / XACP v1.7 firmware source

This directory contains the publication source tree for firmware **XX19c**,
implementing the **XACP v1.7** baseline.

XX19c supersedes XX19b without changing the XACP v1.7 shared-memory ABI.

## Core0/Core1 stability changes

XX19c updates the Core1 execution path to prevent interference with persistent
Core0 firmware services.

The launch sequence now halts CPU1 before republishing launch state and uses
explicit memory-ordering barriers. Core1-side cache maintenance no longer
operates the shared PL310 L2 controller from the dynamic execution path.

These changes correct the conflict previously seen when a Core1 application
was launched while ZZMIDI realtime was active.

## Low firmware guard

The low firmware image must remain below the ARM framebuffer at:

```text
0x00200000
```

XX19c adds linker-time checks for both:

```text
hard limit:       0x00200000
64 KiB safe limit 0x001F0000
```

The validated build reports:

```text
__low_firmware_end = 0x001B9A2E
```

The guard verifies the existing layout; it does not change the public XACP
memory map.

## XACP v1.7 high Core1 arena

```text
0x30000000 - 0x3F800000   248 MiB usable
0x3F800000 - 0x3FC00000     4 MiB guard
0x3FC00000 - 0x40000000     4 MiB firmware/hardware reserved
```

The normative header remains:

```text
ZZ9000_proto.sdk/ZZ9000OS/src/xacp_memory_map_v1_7.h
```

## Source layout

```text
ZZ9000_proto.sdk/   firmware, FSBL, BSP and platform sources
util/               host-side build helper source
build_firmware.sh   firmware ELF build helper
COPYING             GNU GPL v3 license text
```

Generated object and ELF files from the development archive are not part of
the publication tree.

## License

The firmware source follows the licensing of the underlying ZZ9000/XACP
firmware tree and the notices contained in the source. The repository firmware
license file is GNU GPL v3.
