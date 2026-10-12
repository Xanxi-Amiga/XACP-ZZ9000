# ZZGear RasterIX D1 - English integration status

**11 October 2026, 23:00:** D1 ran on the physical Amiga (hardware runs 7-8). The rotating gear displayed at 59.53 FPS, with 5.97 ms measured GPU busy time. Hardware depth/blending/alpha and DDR placement probes reported `WORKS`.

**D1a correction:** a diagnostic slot collision caused a misleading `L15 FAIL` when the placement probe overwrote the L15 record. D1a relocates those probe values to slots 128-131 (`SD_COUNT=144`, `SH_END=560`). Rendering was not changed. No separate D1a hardware run is attached.

## Driver extraction

- `rixgpu`: validated GPU command/submit path, `rix_mem_clean`, no-wait FIFO submission and placement probe.
- `rixframe`: fenced DDR list-chunk ring, ordered texture binding and per-frame management.
- `rixtex`: texture page pool, object allocation and uploads.
- `rixdesc`: triangle descriptors; original host tests matched 3,600 A07b triangles bit-for-bit.
- In the full D1 project, the `zzgear_app.c` application uses `rix_frame_begin/tri/end`, `rix_tex_*` and `rix_tri_desc` instead of its own command-ring management. `gear_xform` provides 1..2000-frustum window-space depth.

This GitHub milestone contains **only the driver-source preview**, not the original runnable launcher, gear geometry and complete build assets.

## Original developer build and checks

The developer project was built under MSYS using `sh build_all.sh`, ARM GNU Toolchain 13.2 Rel1 and amiga-gcc. The omitted launcher identifies itself as `ZZGearRIX 2.0 D1 librixgpu 1.0`; the omitted Core1 blob was 255,120 bytes.

Original host validations: `test_rix` 3,600 descriptor comparisons (zero differences), `gear_painter` about 0.018% disagreement, 27 `sim_rix` cases including placement, and `dirtycheck` zero changed pixels. These checks are not runnable from this source-only publication.

## Physical hardware test procedures (for the complete developer build)

1. `ZZGearRIX SELFTEST PROBE`: expect initialization 15/15, depth/blend/alpha `WORKS`, approximately 16-17 us and 233 us texture switches, and `placement (arena) WORKS` with texture at 0x30000000 and Z-buffer at 0x30020000.
2. `ZZGearRIX 300`: check normal 60 Hz gear presentation.
3. `ZZGearRIX 1000 OFFSCREEN`: compare with the earlier C1b/C1c reference of approximately 163 completed frames/s. This is a test target, not a separate D1a measurement.

For DDR placement, the original `zzgear_mmu.c` maps 0x30000000..0x30FFFFFF as write-back. Core1 copies the texture to that arena and cleans the cache (`rix_mem_clean`) before GPU access. The GPU owns the depth buffer. The scanned-out color buffer remains in the legacy video window.

**Limits:** fixed 640x481 allocation, one TMU, no mipmaps in version 1.0, demo-specific initialization and mailbox. See `librixgpu/API.md`.

Raw hardware output: `RESULTS_D1_run7_monitor600.txt`.
