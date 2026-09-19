# FS1 — 19/09/2026

- Exposes two AHI mixing channels instead of one.
- Intended for XACP game ports using AHI SFX with MP3 or ZZMIDI music.
- Keeps the original MNT 4.19 playback, register, IRQ and MHI behaviour unchanged.
- Includes a source-only `UtilityBase` compatibility adjustment for current amiga-gcc/NDK headers.
- Validated by Fitzsteve with ZZQuake on 68030/25 + ZZ9000AX.
