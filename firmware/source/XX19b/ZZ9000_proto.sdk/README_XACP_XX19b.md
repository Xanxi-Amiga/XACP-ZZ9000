# XACP v1.7 / firmware XX19b

XX19b keeps the XX19a / XACP v1.6 layout below `0x30000000` unchanged.

XACP v1.7 adds:

- 248 MiB ARM-only Core1 arena at `0x30000000-0x3F800000`
- 4 MiB guard at `0x3F800000-0x3FC00000`
- protected top 4 MiB at `0x3FC00000-0x40000000`
- removal of the legacy SMUSH Codec1/37/47 implementation that occupied the new Core1 arena

Core1 applications using the high arena must map it in their own MMU translation tables and must not allocate in the guard or firmware/hardware-reserved range.
