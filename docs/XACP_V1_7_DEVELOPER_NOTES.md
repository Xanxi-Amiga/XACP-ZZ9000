# XACP v1.7 developer notes

Firmware baseline: **XX19c**

XACP v1.7 keeps the XX19a / v1.6 layout below `0x30000000` unchanged and adds
a high ARM-only Core1 arena.

XX19c is a stability update to the same XACP v1.7 ABI. It does not introduce
a new protocol version or a new shared-memory layout.

## ARM-only Core1 arena

```text
0x30000000 - 0x3F800000   248 MiB usable
0x3F800000 - 0x3FC00000     4 MiB guard
0x3FC00000 - 0x40000000     4 MiB firmware/hardware reserved
```

The Amiga Zorro window cannot directly access this arena. Host data must be
staged in visible memory and copied by ARM code. Core1 applications using the
arena must map it in their own MMU tables.

## XX19c Core0/Core1 launch fix

XX19c changes the dynamic Core1 launch sequence without changing the public
Core1 application ABI.

The important implementation rules are:

- CPU1 is held before launch vectors, trampoline target and GO state are
  republished.
- publication is bracketed with explicit DMB/DSB/ISB ordering.
- Core1 must not perform generic cache operations that reach the shared PL310
  controller while Core0 firmware services are active.
- Core1 launch-side cache maintenance is therefore restricted to L1 where
  appropriate.

This corrects the Core0/Core1 coexistence failure previously seen when a
Core1 application was started while the ZZMIDI Core0 service was active.

Applications do not need a new XACP ABI to benefit from this fix.

## XX19c low-firmware linker guard

The ARM framebuffer starts at:

```text
0x00200000
```

The low firmware linker region physically extends beyond that address, so an
uncontrolled increase in `.text`, `.rodata`, `.data` or `.bss` could otherwise
cross into framebuffer memory.

XX19c defines:

```text
_XACP_LOW_FB_BASE       = 0x00200000
_XACP_LOW_FW_SAFE_LIMIT = 0x001F0000
```

and asserts at link time that `__low_firmware_end` remains below both values.

Validated XX19c build:

```text
__low_firmware_end = 0x001B9A2E
```

This guard is deliberately a verification mechanism only. It does not move
sections and does not alter the XACP memory map.

## Legacy SMUSH codecs

The Codec1/37/47 implementation remains removed from the XX19c build because
its fixed scratch buffers occupied the high Core1 arena. Existing
`ACC_CMPTYPE` values are retained for ABI compatibility; those requests are
unsupported and ignored. IMA ADPCM is unchanged.
