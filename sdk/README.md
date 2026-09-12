# XACP SDK

This directory contains the public headers and helper definitions for the
**eXtended ARM Coprocessor Protocol (XACP)** used by the Xanxi ZZ9000 firmware
branch.

Current public baseline:

```text
Firmware: XX19b
Protocol: XACP v1.7
```

Firmware build numbers and XACP protocol versions are separate.

## Current memory-map headers

XACP v1.7 is an additive extension of the XACP v1.6 memory map.

The established low-memory layout remains defined by:

```text
xacp_memory_map_v1_6.h
```

The XX19b high Core1 arena is defined by:

```text
xacp_memory_map_v1_7.h
```

New software using the v1.7 high arena should use the v1.7 definitions and
must preserve all allocations inherited from v1.6.

## XACP v1.7 high Core1 arena

XX19b adds the following ARM-only allocation:

```text
0x30000000 - 0x3F800000   248 MiB  Core1 application arena
0x3F800000 - 0x3FC00000     4 MiB  guard
0x3FC00000 - 0x40000000     4 MiB  firmware/hardware reserved
```

The arena is not directly accessible through the Amiga Zorro window.
Host-side data must be staged in visible memory and copied by ARM code.
Core1 applications using this area must map it in their own MMU tables.

The guard and reserved regions must not be used by applications.

## Inherited XACP v1.6 layout

The shared and ARM-private allocations below `0x30000000` remain unchanged
from XX19a / XACP v1.6. This includes the established XACP command and stream
regions, MP3 buffers, ZZMPEG region, ZZMIDI shared buffers, ZZMIDI private
storage, service heap and the existing guard region.

For those definitions, use:

```text
xacp_memory_map_v1_6.h
```

Do not reassign an address merely because it appears unused by one application.
All persistent Core0 services and Core1 applications share the same physical DDR.

## Legacy SMUSH codecs

XX19b removes the legacy SMUSH Codec1/37/47 implementation because its fixed
scratch buffers occupied addresses now assigned to the v1.7 Core1 arena.

Existing ABI values are retained and are not renumbered, but those legacy codec
requests are unsupported in XX19b. IMA ADPCM and unrelated XACP services are
unchanged.

## Addressing

Shared XACP offsets are framebuffer-relative on the Amiga side:

```text
fb = board + 0x00010000
```

The ARM physical framebuffer base is:

```text
0x00200000
```

Therefore, for established shared regions:

```text
ARM physical address = 0x00200000 + framebuffer-relative offset
```

ARM-private allocations, including the v1.7 high arena, use absolute ARM
physical addresses and must not be treated as framebuffer-relative offsets.

## Compatibility

For current development, use the explicitly validated combination:

```text
XX19b
XACP v1.7
matching application binaries and shared-memory definitions
```

After changing firmware or `zz9000.card`, perform a complete power-off before
validation.

Historical SDK definitions are retained under `archive/` where applicable.
The v1.6 header remains at the SDK root because v1.7 preserves and builds on
that layout rather than replacing it.

## Developer notes

Additional v1.7 information is available in:

```text
../docs/XACP_V1_7_DEVELOPER_NOTES.md
../firmware/README.md
```

New allocations should be added to the versioned XACP memory map instead of
being introduced as undocumented hard-coded DDR addresses.

## License

The public XACP SDK headers and memory-map definitions in this directory are
licensed under the Zero-Clause BSD license (0BSD).

See `LICENSE` for the full license text.

SPDX-License-Identifier: 0BSD

## Credits

XACP / Xanxi, 2026.

For use with the MNT ZZ9000 Amiga RTG / ARM platform.
