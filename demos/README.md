# XACP Demonstrations

A gallery of technical demonstrations running on **real Amiga hardware with an MNT ZZ9000**. These experiments explore the ZZ9000's Cortex-A9 processors, DDR memory, RTG display path and, more recently, its FPGA fabric.

This directory is an **index of demonstrations, not a separate application distribution**. A demonstration can be accompanied by source code, measurements, a downloadable program, or only a video. The availability of each item is stated explicitly below.

## FPGA graphics acceleration

### XACP-GPU / ZZGearRIX — RasterIX hardware 3D

**11 October 2026 — [Video](https://youtu.be/A24W72UVJ0A) · [Technical report and driver source snapshot](ZZGearRIX/README.md)**

Hardware-accelerated 3D rendering on a physical Amiga 4000/68060 with the ZZ9000 FPGA running ToNi3141's RasterIX. The ZZ9000's ARM Core1 prepares commands for the FPGA renderer.

The rotating ZZGear scene achieved **59.53 FPS on a 60 Hz display** and **163.5–165.9 completed FPS offscreen**, compared with approximately **27–28 FPS** using the earlier ARM TinyGL software renderer. These results describe the particular ZZGear scene, not expected performance in games.

**Availability:** `librixgpu 1.0` ARM driver source snapshot, API documentation and hardware test evidence are included under [`ZZGearRIX/`](ZZGearRIX/). The complete ZZGear application, its binaries and the FPGA Vivado project are **not** included in this milestone.

The RasterIX FPGA technology demonstrated by ZZGearRIX is now included in the
[XX19d-RIX firmware release](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/XX19d-RIX).
Separately released applications using the FPGA renderer include
[ZZQuake2 1.0](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZQuake2-1.0)
and [ZZScreensavers 1.1 / Solar Winds](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZScreensavers-1.1).
These are application releases, not binaries bundled with the demo index.

## ARM software 3D demonstrations

### ZZTeapot — ARM software-rendered 3D

**[Video](https://youtu.be/GSLhxQDLhEI) · [Project notes](../applications/ZZTeapot/README.md)**

Classic Utah teapot rendered in software on the ZZ9000's Cortex-A9 processor. This predates the FPGA RasterIX demonstration: **ZZTeapot is an ARM software-rendering experiment, not an FPGA-accelerated demo**.

**Availability:** Video and project notes only. No public source or executable is included.

## Coprocessor and CPU-architecture experiments

### ZZPPC — PowerPC/FPU execution on Cortex-A9

**6 September 2026 — [Video](https://youtu.be/lP0kV6TtkcY) · [Project notes](../applications/emulators/ZZPPC/README.md)**

Proof of concept running the **original, unmodified FlashMandelWOS** PowerPC/FPU workload on the ZZ9000's ARM Core1, without a physical PowerPC accelerator. An experimental `powerpc.library` redirects the workload to a custom PPC32 interpreter; the Amiga displays the resulting fractal.

**Availability:** Video and architecture documentation only. This is not a general WarpOS or PowerPC compatibility release; no public ZZPPC binaries or source are included.

## Early XACP ARM and framebuffer experiments

### Mandelbrot rendering and ARM compute offloading

**7 May 2026 — [Video 1](https://youtu.be/PaW-dg9df0M) · [Video 2](https://youtu.be/C3OIMTwJfA8) · [Historical project notes](../applications/fractals/README.md)**

Early demonstrations of ARM-side Mandelbrot computation, shared DDR, RTG framebuffer output and experimental rendering pipelines. Some historical clients predate the current XACP memory map and may require adaptation to work with modern firmware.

### Core1 Julia fractal

**May 2026 — [Video 1](https://youtu.be/SRgtJtQZTxo) · [Video 2](https://youtu.be/Uj4IW-JH0ro) · [Project notes](../applications/fractals/core1-julia-v2/README.md)**

Continuous Julia fractal rendering on ARM Core1 with direct RTG output. Historic Core1 Julia program files are maintained under [`applications/fractals/`](../applications/fractals/); check their own README for compatibility and licensing restrictions.

## How this directory is organized

- **`demos/`** is the public gallery: demonstration videos, engineering notes, measurements and small technical proof-of-concept packages.
- **`applications/`** remains the canonical location for programs, ports, interpreters, drivers and experimental engines, even when a video highlights one of them.
- **`firmware/`**, **`sdk/`** and the project documentation remain in their existing locations. The RasterIX source and FPGA build are not implicitly part of the `librixgpu` snapshot.

**Scope:** Results are reported for the particular hardware configurations and workloads described in each project's documentation. Video-only entries are not presented as source or binary releases.

XACP integration: Xanxi-Amiga. MNT ZZ9000: MNT Research and contributors. RasterIX: [ToNi3141](https://github.com/ToNi3141/RasterIX). See the individual project pages for additional credits and applicable licences.
