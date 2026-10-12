# ZZGear RasterIX C1-C1c / D1 - English measurements summary

This is a **condensed English summary** of the original, longer internal French test notebook, **not** a complete translation. It preserves the principal measurements and distinguishes hardware measurements from host tests. Full internal records are not included in the public driver-source preview.

**Hardware:** Amiga 4000 / 68060, ZZ9000 FPGA G4B and firmware FAST1, Picasso96 640x480x16, all recorded 11 October 2026.

## C1 - displayed benchmark

- 1,639 displayed images in 27.531 s = **59.53 FPS** (60 Hz limit), 1,639 completed/fenced frames, 1,638 VDMA flips, zero lost frames or timeouts.
- Approximately 978 triangles/frame; 10.5 DDR chunks and 40,181 command words/frame.
- Core1 build/submit approximately 4.80 ms, including 2.29 ms transformation/culling/sorting and 1.02 ms ring wait.
- Build-start-to-fence mean 8.50 ms included initial Core1 preparation; estimated GPU time approximately 6.0 ms.
- PL330 RGB565 swap approximately 0.48 ms/frame.
- A C1 timer-wrap bug produced incorrect displayed interval rates after a 32-bit 333 MHz timer wrap. C1a corrected the accounting; the main 1,639 frame count and 68k-clock FPS remain valid.

## C1b - 1,000 fenced OFFSCREEN frames

| Mode | Completed FPS | Elapsed time |
| --- | ---: | ---: |
| OFFSCREEN | **163.5** | 6.116 s |
| OFFSCREEN NOSWAP | **165.9** | 6.028 s |

GPU throughput for this **specific ZZGear scene** was around 6.0-6.1 ms/frame. A displayed C1b `GPU capacity 107/s` counter was misleading in pipelined OFFSCREEN mode (included preceding-frame wait); it should not be used as the effective completed FPS.

## C1c - capability probes and texture performance

- Z-buffer LESS with both triangle orderings and retained depth: **WORKS**. C1b's earlier depth-clear error was caused by a disabled depth write mask; fixed in C1c.
- ONE/ONE blending: **WORKS**. Alpha GREATER 0.5: **WORKS**.
- 64x64 texture switch: approximately **16 us** GPU path per switch.
- 256x256 texture switch: approximately **233 us** GPU path per switch.
- FIFO MMIO cost: approximately **1.1 us/word**, supporting DDR lists rather than per-word MMIO scene submission.
- 256x256 path: approximately 70 us FIFO/page-table overhead plus approximately 160 us for 128 KiB of transfer (roughly 800 MB/s for the transfer phase).
- Full-clear OFFSCREEN: **121.9 FPS** (1,000 completed frames in 8.20 s), versus 163.5 FPS for the dirty-clear benchmark.

Texture switch costs alone do **not** establish Quake II performance; real scene texture ordering and lightmaps must be measured.

## D1 - librixgpu 1.0 on physical hardware

Raw output is included as `RESULTS_D1_run7_monitor600.txt`.

- 600 completed/fenced frames, **59.53 FPS**, zero dropped frames, zero fence timeouts.
- **5.97 ms/frame GPU busy** for the measured ZZGear scene.
- Core1 transformations approximately 2.38 ms/frame, ring wait approximately 1.00 ms/frame.
- Approximately 10.6 DDR chunks and 40,191 command words/frame.
- Separate D1 depth, blending, alpha and DDR placement probes: **WORKS**. Placement verified a texture at **0x30000000** and Z-buffer at **0x30020000**, beyond the legacy 2 MiB framebuffer window.
- A D1 diagnostic-slot overlap falsely marked L15 as failed. D1a moves the placement results and does not change rendering; no separate D1a hardware run is attached.

## Interpretation and limits

Verified on this physical G4B platform: GPU command submission and fences, display presentation, PL330 swap, depth/blending/alpha operations, texture uploads and GPU access to the Core1 arena. No Quake II FPS, Warp3D compatibility or standalone reproducible build is claimed. Full D1a build dependencies and the launcher are outside this developer snapshot.
