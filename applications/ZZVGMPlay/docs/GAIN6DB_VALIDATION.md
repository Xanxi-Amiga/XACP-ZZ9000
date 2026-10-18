# System 18 +6.02 dB output-gain validation

ZZVGMPlay v1.0 - (c) Xanxi 2026.

## Minimal change

In `core1/zzvgm_core1.c`, when `g_system18` is active, the complete signed
64-bit left/right mix is multiplied by 2 (6.0206 dB), **after the existing
individual FM/RF gains and the other source additions, and before `sat16()`**.
This preserves relative mix levels. No engine, firmware, ABI, playlist, Repeat,
chip-volume table, or audio backend was otherwise adjusted. Files that do not
select the RF5C68/System 18 backend receive no additional gain.

The playback build contains no metering or per-sample diagnostic overhead:
clip counting and the pre-gain FNV-1a reference hash are guarded by
`#ifdef ZZVGM_S18_HOST`, used only by the native PC verification harness.

## Verified on the original private 22-track Moonwalker VGM/VGZ pack

- Sample rate: 44,100 Hz.
- Tracks processed end-to-end: 22/22.
- Total stereo frames: 42,248,054.
- The pre-gain stereo PCM stream matched the checkpoint production mixer for
  **all 22 tracks**, proving no unintended change to chip mixing.
- Final-output FNV-1a fingerprints match the post-gain references for 22/22.
- Output clipping detected **before** the final sat16: 0 clipped channels.
- Highest measured output **sample** peak: 25,496 out of +32,767, about
  -2.18 dBFS (11,271 counts of positive peak headroom).
- Every track's measured post-gain sample peak is precisely twice the original.

The original VGM music files are **not** redistributed in the candidate.
The test can be repeated on a machine with native GCC and the private pack:

```sh
python3 tests/host/check_production.py --music-zip /path/to/Moonwalker.zip
```

Native validation does **not** establish intersample true-peak, external DAC
level, hardware ARM CPU load, absence of underruns (UND), or distortion on
unseen System 18 material. Other System 18 files with higher peaks might
still trigger the existing `sat16()` protection, which would sound distorted;
there is intentionally no dynamic compression/limiting in this change.

## MSYS / Amiga hardware verification

From the ZIP root in the configured offline Windows MSYS environment:

```sh
sh build_all.sh
```

The build uses explicitly configured toolchain executables, includes all
third-party source dependencies, uses `-mcrt=nix20` for 68k, and performs no
network operations. Verify both freshly produced outputs and deploy together:
`amiga/ZZVGMPlay` and `amiga/zzvgm_core1.bin`.

1. Verify `core1/zzvgm_core1.map`, including ARM linker memory limits.
2. Listen to Title, Bad, Smooth Criminal, Voice Wohoou and Dance Magic.
3. Compare perceived levels to Atari ST playback at the same external gain.
4. Inspect CPU usage, UND, stop/restart, playlist transitions and Repeat.
5. Recheck A07g non-System-18 tracks (AY/EMU2149, SCC/SCC+, SMS FM, etc.).
6. Only after hardware checks and listening evaluation consider public release.

No cross-compiled or hardware-tested ARM/68k binary is included in this ZIP.
