# XACP-GPU hardware 3D demonstration — 11 October 2026

**ZZGear on a physical Amiga 4000/68060 + ZZ9000 using ToNi3141's RasterIX in the FPGA.**

- [Video: https://youtu.be/A24W72UVJ0A](https://youtu.be/A24W72UVJ0A)
- **Hardware GPU:** 59.5 FPS displayed (60 Hz limit); 163.5 FPS completed OFFSCREEN with RGB565 swap, 165.9 without swap.
- **Previous software renderer:** about 27–28 FPS on ZZ9000 Cortex-A9 Core1.
- **Driver:** librixgpu 1.0, providing FPGA commands, DDR command lists, GPU fences, PL330 endian conversion and framebuffer management.
- **Verified on hardware:** October 11, 2026. The linked public commit is dated by GitHub when published.

For architecture, reproducible statistics, raw measurements, source-snapshot scope and credits, see [the ZZGear RasterIX demonstration README](../demos/ZZGearRIX/README.md).

Credits: ToNi3141 (original RasterIX), Xanxi-Amiga (XACP-GPU integration), MNT Research (ZZ9000), Anthony Mo (firmware-build testing assistance).
