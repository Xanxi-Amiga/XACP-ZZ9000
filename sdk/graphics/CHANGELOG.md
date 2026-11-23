# Changelog - ZZGL / librixgpu shared source snapshot

## Development snapshot - 2026-11-23

- Published the **unchanged** ZZGL `0.8.10-unified-dev1` and matching
  `librixgpu` sources from the released ZZMiniGL Beta 1 vendor tree.
- Added an explicit immutable source manifest and per-file license notices.
- No new GPU code, ABI change, firmware change or application rebuild.
- This is NOT a claim of cross-game compatibility or new performance gains.

## Future work (not yet part of this snapshot)

- Compare with final ZZHalfLife sources and their corresponding GL interfaces.
- Audit QuakeGL `GL_LUMINANCE` support, texture residency and uploads.
- Audit OpenLara clear-split / preclear hooks and frame timing.
- Compile and test consumers against a genuinely shared API and platform glue.
- Record repeatable benchmarks on a physical ZZ9000 at nominal clock rates.
