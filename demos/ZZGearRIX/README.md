# XACP-GPU — RasterIX FPGA 3D acceleration on a physical ZZ9000

**Public ZZGear demonstration recorded: 11 October 2026**  
**GitHub milestone prepared: 12 October 2026**  
**Hardware:** Amiga 4000 / Motorola 68060 + MNT ZZ9000 (Xilinx Zynq-7020)  
**Status:** functional experimental GPU demonstrator and Core1 driver snapshot, **not** a finished Warp3D implementation.

[![ZZGear on XACP-GPU (YouTube)](https://img.youtube.com/vi/A24W72UVJ0A/hqdefault.jpg)](https://youtu.be/A24W72UVJ0A)

**[Watch the demonstration on YouTube](https://youtu.be/A24W72UVJ0A)**

The video compares the same rotating chrome-textured 40-tooth gear, using the previous ARM Core1 TinyGL software renderer (~27–28 FPS) and the new **RasterIX FPGA hardware renderer** (59.5 FPS displayed, capped by the screen's 60 Hz refresh).

## Measured results

| Renderer / mode | Measurement | What it means |
|:--|--:|:--|
| Core1 ARM TinyGL | ~27–28 FPS | Previous software-rendered implementation |
| XACP-GPU D1, MONITOR 600 | **59.53 fenced FPS; 59.43 screen flips/s** | Real 60 Hz display; 600 completed frames; 0 dropped, 0 fence timeouts, 0 DMA faults |
| XACP-GPU C1b, OFFSCREEN, 1,000 frames | **163.5 completed frames/s** | Real GPU-completed framebuffer frames without the 60 Hz display cap |
| XACP-GPU C1b, OFFSCREEN NOSWAP | **165.9 completed frames/s** | Same scene without the RGB565 swap step |
| XACP-GPU D1 GPU busy | **5.97 ms/frame** | Approximately 167.5 frame/s instantaneous GPU capacity on ZZGear |

These figures concern **the ZZGear scene only**; they are not predicted frame rates for Quake II or other games.

All measurements are from **11 October 2026 hardware runs** on the real Amiga. D1a is a follow-up diagnostic-slot fix over D1 without changing the renderer; the D1a variant does **not** have a distinct attached hardware run. The C1b OFFSCREEN results and the D1 MONITOR log are separate tests.

## How it works

- **RasterIX** by ToNi3141 is implemented in the ZZ9000 FPGA (G4B bitstream).
- **XACP Core1 Cortex-A9** builds hardware triangle descriptions and command lists; the physical 68060 does not rasterize the triangles.
- Lists are streamed from DDR; hardware GPU completion fences and VDMA address checks make buffering safe.
- The Zynq **PL330 DMA** handles 16-bit RGB565 endian conversion (about 0.49 ms per 640×480 frame).
- The shared **librixgpu 1.0** modules provide registers/command submission (`rixgpu`), frame management (`rixframe`), texture/page allocation (`rixtex`) and vertex-to-triangle descriptors (`rixdesc`).
- Separate hardware probes established depth test, blending, alpha test and GPU access to textures and depth data in the Core1 DDR arena outside the legacy 2 MiB video window.

## Developer snapshot included

The `demos/ZZGearRIX/librixgpu/` directory contains the **Core1 driver sources and API document** extracted from the D1a developer archive:

- `rixgpu.[ch]`, `rixframe.[ch]`, `rixtex.[ch]`, `rixdesc.[ch]`
- `rix_hw.h`, `rix_shared.h`, `API.md`

**Important:** This is a **driver source snapshot**, not a complete ready-to-run ZZGear distribution. The original D1a developer archive also includes launcher, runtime, gear geometry, compiled/embedded blob and third-party demonstration resources. Those files are intentionally excluded from this initial public milestone pending a third-party rights/build audit. No fully runnable Amiga binary is attached here.

The D1a developer archive from which the snapshot was made has SHA-256:

`73d5f2d51de9e2a0c9b2f2cf6b94ace608d43aa6902e6e03072afc82045939a2`

The `evidence/` directory contains original D1 monitor output and technical status documents. Some archived diagnostic notes include resolved issues or historical tests; consult the D1a status notes instead of treating every earlier test line as a current failure.

## Next step: Quake II

The near-term goal is **ZZQuake2 with an XACP-GPU RasterIX renderer**, using the same `librixgpu` layer. First finish the existing software Quake II port, then test matching scenes and timedemos in software, ARM TinyGL and FPGA hardware modes.

Potential optimization targets are DDR texture handling, texture-state changes, lightmaps, clipping, and CPU/GPU scheduling. No Quake II hardware FPS are claimed here.

## Credits and scope

- **RasterIX engine:** [ToNi3141](https://github.com/ToNi3141/RasterIX).
- **XACP-GPU integration on the real ZZ9000:** [Xanxi-Amiga](https://github.com/Xanxi-Amiga).
- **MNT ZZ9000 original hardware and firmware:** MNT Research and contributors.
- **Firmware-build testing assistance:** Anthony Mo.
- The historical rotating-gear demo is derived from a MiniGL gears example; its third-party code/resources are **not** contained in this initial driver-only snapshot.

This is an independent FPGA RasterIX integration, not a claim to have invented RasterIX or to be the first RasterIX Amiga implementation. The purpose of this dated record is to document a working XACP-GPU implementation and reproducible test figures.

