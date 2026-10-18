# XX19d-RIX corresponding sources

This repository directory contains the ARM firmware, FSBL and the RasterIX
FPGA sources for XACP XX19d-RIX on the MNT ZZ9000, alongside the Vivado project
needed to regenerate the FPGA bitstream. The same tree is also distributed as
`XACP.XX19d-RIX.sources.zip` on the GitHub release page for convenient download. Public protocol version: **XACP v1.8**.

The SHA-256 of the reference firmware image is:

`901393712ca3056ffe8e8895afc08b553d3c50590648d3182a56b1ab2e0ef23b`

## Contents

- `arm/ZZ9000_proto.sdk/` — ARM firmware and FSBL sources;
- `arm/sdk/` — image-service and memory-map headers, plus a 68k client;
- `fpga/RasterIX_G1/RasterIX/rtl/RasterIX/` — RasterIX rendering and DDR command RTL;
- `fpga/XACPGPU_G4C/ZZ9000_proto/ZZ9000_proto.xpr` — complete custom Vivado project;
- `build/BOOT_XX19d-RIX.bif` — partition layout;
- `PROVENANCE.json` — verification hashes for the distributed image and its parts.

The identifiers in the Vivado directory names are historical build paths and
have been retained to keep project references intact; they are not a public
firmware version identifier.

## Rebuild status

The archived build manifest matches the exact BOOT image distributed in this
release. This source archive is a clean distribution snapshot; the FPGA was successfully opened, its IP outputs regenerated, and its RTL
synthesized with Vivado 2025.2 from a clean extracted archive on 18 October
2026. This is **not** a verified full implementation or a bit-for-bit rebuild
of the production BOOT, and no new bitstream was flashed.
Vivado 2025.2, suitable AMD/Xilinx IP tooling, the ARM cross-toolchain and
board-specific validation are needed to reproduce the design.

This distribution excludes generated vendor IP implementation HDL, compiled
BSP archives and temporary synthesis checkpoints. Regenerate those using the
licensed toolchain as needed.

## Licensing

RasterIX is by ToNi3141 (GPL-3.0-or-later), XACP integration is by Xanxi,
and the ZZ9000 firmware and hardware originate with MNT Research. The
firmware and SDK components retain their original copyright and license
notices. See `LICENSES.md` and in-tree licensing information.
