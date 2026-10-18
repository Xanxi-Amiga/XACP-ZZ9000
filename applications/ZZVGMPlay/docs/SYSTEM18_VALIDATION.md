# System 18: integration and hardware validation record

## Integration implemented

- RF5C68 header clock at `0x40` is checked against the data boundary.
- RF5C68 mask uses bit 18; SCC retains bit 17. Display-only dual-FM uses bit 19.
- `52/53` FM chip 0; `A2/A3` FM chip 1, isolated from the GENS path.
- `B0` RF register writes, `C1` banked RF byte writes, `67/C0` RAM writes, and
  `68/01` transfers from the existing fragmented PCM bank reader (256-byte chunks).
- Upper chip-selector bits for unsupported second RF5C68 devices yield error 23.
- RF source range errors yield error 24. Backend pool initialization errors yield 22.
- FM volumes `0x80` each and RF volume `0xB0` (Q8 volumes; Q8 resampler outputs).
- Signed 64-bit accumulation and final 16-bit saturation. Existing engine gains and
  normalizer are unchanged outside the RF5C68 profile.
- The private 256 KiB pool is allocated *after* `zz_arcade_init`; the incoming VGM
  data and Core1 upload/messaging memory remain in their original regions.
- Track changes call `vgm_init` and `zz_s18_init` afresh, resetting the pool and cores.
  Native VGM internal loop uses continuous chip state, as in the existing player.
- Native test of actual parse/mix path matched Step 1 hashes **before gain** for all 22 Moonwalker tracks.
- Final System 18-only x2 output gain is applied after summing chips and before sat16.
- All 22 amplified host renders matched new expected PCM hashes, with no sample clipping.
- Max measured output sample peak: 25,496 (about -2.18 dBFS); intersample peaks not tested.

## Still to be checked on A4000 + ZZ9000

1. Run `sh build_all.sh` in your configured offline Windows MSYS shell.
2. Confirm the ARM link `ASSERT(_bss_end < 0x043C0000)` succeeds. Inspect the final
   `core1/zzvgm_core1.map` for `_bss_end` and the extra FM static tables.
3. Deploy **both newly produced** `amiga/ZZVGMPlay` and `amiga/zzvgm_core1.bin`.
4. Verify Title, Bad, Smooth Criminal, Voice Wohoou and Dance Magic at 44.1 kHz.
   Check register integration, music fidelity, audio underruns and 68060 CPU usage.
5. Run all 22 tracks, playlist transitions, global Repeat and stop/restart.
6. Regression checks: Genesis FM+PSG; SMS FM; AY EMU2149+SCC/SCC+; arcade PCM;
   other established chip combinations; default drawer and settings persistence.
7. Compare an audible output against an independent known-good VGMPlay renderer.

The baseline ARM BSS ended at `0x04374600` in the provided A07g map. The memory
headroom to `0x043C0000` was ~310 KiB before adding System 18. The imported FM
core contributes roughly 162 KiB static BSS on this host plus compiled code;
actual ARM section sizes and available linker headroom must be checked.

## Source provenance

System 18 upstream engine files: ValleyBell/libvgm commit
`a6eed945ecf4e5a25cf82004779c6f7e4fd5c319`.
The upstream `fmopn.c`, `rf5c68.c` and `Resampler.c` are retained with their
original notices. `core1/prepare_system18.py` deterministically generates
`Resampler_baremetal.c`, changing only the initial buffer allocation to 1024
frames. The RF and FM compilation units redirect allocation to the private pool.
Third-party licensing notices are preserved. No private music assets are included.
