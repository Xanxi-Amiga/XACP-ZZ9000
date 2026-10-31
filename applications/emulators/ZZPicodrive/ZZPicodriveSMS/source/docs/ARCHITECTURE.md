# ZZPicoDriveSMS v1.2 - architecture and integration notes

This document describes the released source layout and key implementation
choices. The emulator is split between the AmigaOS 68k launcher (`amiga/`)
and a ZZ9000 Cortex-A9 Core1 payload (`Core1/`).

## Responsibilities

- **Amiga 68k:** Workbench/CLI launch, GUI, ROM and save files, controller
  input, Picasso96 display presentation, AHI/Paula PCM consumption and exit.
- **ARM Core1:** PicoDrive (Z80 and SMS VDP), framebuffer conversion, PSG,
  optional YM2413 FM synthesis, PCM mixing, shared ring production and
  emulator-state serialization.
- **XACP:** Core1 loading, shared-memory transport and ZZ9000 display access.
  This release does not require a change to the XACP firmware protocol.

## Sound and FM hardware

PicoDrive's `pico/sms.c` recognizes SMS FM ports `0xF0` (register select),
`0xF1` (data) and `0xF2` (mixer/presence). FM availability is controlled by
`POPT_EN_YM2413` before console reset. The program links PicoDrive's
`pico/sound/ym2413.c` against emu2413 1.6.3 and retains the upstream
save/load implementation for OPLL state.

The FM and PSG channels are mixed within PicoDrive into one 16-bit stereo
22050 Hz PCM stream. `ZZPICO_FM_SAT` prevents integer wrapping in the mixing
path. The audio transport between ARM and 68k does not depend on a fixed
number of PCM samples per emulated frame. AHI and Paula consume the same
shared-ring format by separate backends.

FM filtering uses PicoDrive's resampler. The OPLL and resampler allocate from
the Core1 private heap; they must be reinitialized and freed consistently
when the sound rate or emulated region changes. See `build_arm.sh` for the
compile-time feature flags.

## Japanese SMS behavior

`ZZPICO_SMS_JAP_IO` adds Japanese console TH/TR output read-back behavior.
`ZZPICO_SMS_FM_MIX` models the region-dependent mixer selection at `0xF2`.
The targeted code modifications are also documented by the explanatory
patches `docs/sms_jap_io.diff` and `docs/sms_fm_mix.diff`.

The FM toggle selects whether the emulated console exposes the FM unit.
The game still decides whether and how to use it. Test with region-specific
ROMs: some games enable FM only on Japanese hardware or Japanese releases.

## Display timing and cache attributes

The 68k side tracks the ZZ9000 vertical blank with a software PLL and PAN
acknowledgments. Core1 publishes frames when the previous output buffer is
available. Audio pacing is independent of AHI's fixed PCM block size.

`Core1/zzpico_mmu.c` maps section `0x05F` (the Core1 stack window) as normal
write-back/write-allocate memory. Previous uncached/strongly-ordered stack
accesses were a major performance bottleneck. The PCM ring and control
structures remain non-cacheable so ARM/68k ownership and ordering are clear.

## Save compatibility

Savestates preserve PicoDrive's state data and add a small application tag
for the console region and FM setting. Loading an incompatible tagged state
is refused to avoid mixing incompatible audio/video timing and chip state.

## Source, build and licensing

`build_all.sh` invokes the ARM build and embeds the resulting payload into the
Amiga 68k launcher. The supplied reference ARM blob is a build made in a
separate environment; its hash is not a requirement for toolchain-independent
binary reproducibility. Use `BUILD.md` for the actual toolchain configuration.

The upstream PicoDrive, Cyclone and emu2413 copyright and licence notices are
retained unchanged. See `THIRD_PARTY_NOTICES.md`.
