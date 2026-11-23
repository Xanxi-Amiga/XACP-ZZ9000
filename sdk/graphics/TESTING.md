# Migration and test checklist (not performed by this commit)

Before replacing any application-local ZZGL/librixgpu files:

1. Diff the target application's fixed source version against this snapshot.
2. Check for missing APIs, especially QuakeGL LUMINANCE texture handling and
   OpenLara clear-split / preclear hooks.
3. Verify Core1 MMU, graphics aperture, frame buffers, fence addresses,
   texture residency, DMA and cache maintenance on the target firmware.
4. Compile fully offline with the established Amiga 68k and ARM toolchains;
   do not change the running firmware or overclock the card.
5. Compare image output and known performance baselines on the physical
   A4000/68060 at nominal ZZ9000 clocks; inspect stalls and crashes, not
   just FPS.
6. Only publish an updated game binary if a specific, measurable improvement
   or fix justifies a new release.

Do not replace the original vendored copies in historical game releases.
