# XACP v1.7 developer notes

Firmware baseline: **XX19b**

XACP v1.7 keeps the XX19a / v1.6 layout below `0x30000000` unchanged and adds a high ARM-only Core1 arena.

## ARM-only Core1 arena

```text
0x30000000 - 0x3F800000   248 MiB usable
0x3F800000 - 0x3FC00000     4 MiB guard
0x3FC00000 - 0x40000000     4 MiB firmware/hardware reserved
```

The Amiga Zorro window cannot directly access this arena. Host data must be staged in visible memory and copied by ARM code. Core1 applications using the arena must map it in their own MMU tables.

## Legacy SMUSH codecs

The Codec1/37/47 implementation is removed from the XX19b build because its fixed scratch buffers occupied the new high arena. Existing `ACC_CMPTYPE` values are retained for ABI compatibility; those requests are unsupported and ignored. IMA ADPCM is unchanged.
