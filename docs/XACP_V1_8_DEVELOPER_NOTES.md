# XACP v1.8 — Developer notes

Firmware baseline: **XX19d-RIX**. Protocol and shared-memory baseline: **XACP v1.8**.
These are independent version numbers. The first supported hardware combination is the MNT
ZZ9000 installed in an Amiga A4000 (Zorro III).

## Compatibility with v1.7

XACP v1.8 retains the v1.7 Core1 high-memory arena and existing service allocations:

| ARM physical range | Purpose |
| --- | --- |
| `0x30000000–0x3F800000` | 248 MiB Core1 application arena |
| `0x3F800000–0x3FC00000` | 4 MiB guard |
| `0x3FC00000–0x40000000` | 4 MiB reserved for firmware and hardware |

The former v1.6/v1.7 command buffers, audio services and MIDI allocations must not be moved.
Applications previously validated for XX19c need no new allocation merely to use their
existing features; compatibility of each program still depends on its own firmware checks.

## New v1.8 image-service window

| Address space | Range | Size |
| --- | --- | --- |
| Amiga, relative to the ZZ9000 framebuffer | `0x06300000–0x06400000` | 1 MiB |
| ARM physical | `0x06500000–0x06600000` | 1 MiB |

On the Amiga, `fb = board + 0x00010000`. For this shared range, the ARM physical address
is the framebuffer-relative offset plus `0x00200000`. These are **half-open** address
ranges. The image window is owned by the firmware service for compressed-input staging
and output tiles; applications must not allocate it for other purposes.

The image service uses a separate, bounded allocation within the existing Core0 heap.
It does **not** reserve another 128 MiB and does not use the Core1 high arena.

Canonical definitions: [`../sdk/xacp_memory_map_v1_8.h`](../sdk/xacp_memory_map_v1_8.h).
The headers for v1.6 and v1.7 remain available for older clients.

## JPEG and PNG image service

The firmware contains the decoder backends and **Image Service API v1.3**, opcode `0x0220`.
Use `QUERY` to check runtime capabilities before making other requests; do not assume that
all XACP firmware revisions provide this service.

Session order:

`QUERY → BEGIN → FEED (one or more chunks) → DECODE → GET_TILE or DIRECT_P96 → CLOSE`.

`CANCEL` allows abandoning an active session. `DECODE` can request a downscale-to-fit
output box without upscaling. `GET_TILE` on the JPEG row-streaming path is sequential.
The shared staging area is reused for input and output, so a client must not overwrite
pending data.

| Constraint | Current ABI limit |
| --- | --- |
| Input chunks | At most 1 MiB each |
| Total compressed data | At most 32 MiB |
| Image dimensions | At most 8192 × 8192 |
| General JPEG/source pixels | Up to 24 MiPixels, subject to decoder budget |
| PNG pixels | Up to 8 MiPixels |
| Decoder allocation budget | 72 MiB |
| Core0 input spool | Up to 32 MiB |

Progressive JPEG support exists, although certain layouts may be rejected by the memory
budget. JPEG uses a row-output path; PNG currently uses a more memory-intensive decode
path. RGBA8 tiles and a BGRA8 direct-to-Picasso96 path are exposed by the ABI.
The client must validate that a direct-P96 destination belongs to a suitable ZZ9000
bitmap; passing an arbitrary address is unsafe. The firmware performs additional bounds
checks, but those do not replace client ownership checks.

The public SDK provides:

- [`../sdk/xacp_image_service.h`](../sdk/xacp_image_service.h) — image opcode, requests, replies and capabilities;
- [`../sdk/amiga/xacp_image_client.h`](../sdk/amiga/xacp_image_client.h) and
  [`../sdk/amiga/xacp_image_client.c`](../sdk/amiga/xacp_image_client.c) — a small 68k client reference;
- [`../sdk/xacp_memory_map_v1_8.h`](../sdk/xacp_memory_map_v1_8.h) — shared DDR definitions.

These SDK headers are published for developers; the AmigaOS JPEG/PNG datatype integration
is **not included in XX19d-RIX** and will be released separately. Do not advertise the
presence of the firmware decoding service as an installed system datatype.

## RasterIX on the FPGA

RasterIX is a hardware 3D rasterizer incorporated into the ZZ9000 FPGA design. The
Cortex-A9 prepares graphics commands; RasterIX handles texturing, Z-buffer operations
and blending in hardware. The release uses the established one-TMU configuration.

The GPU and image service have **separate DDR windows**. The GPU command/submit ABI is
versioned independently from XACP 1.8; use
[`../sdk/zz_rasterix_submit.h`](../sdk/zz_rasterix_submit.h) and query the GPU capability
before submitting work. The GPU memory ranges are hardware-specific, not generic free
XACP allocations.

The first game using RasterIX is ZZQuake2, distributed separately. Its software and ARM
TinyGL renderers remain usable with the XX19c firmware; only its RasterIX renderer
requires this release. See [FPGA source notes](FPGA_RASTERIX_SOURCE.md).

## Other boards and independent integrations

The source and SDK are available for study and porting. In particular, developers of
other Zynq-based Amiga expansions can inspect the `arm/` firmware, the independent
`fpga/` RasterIX implementation and the host-side image-service client.

These are **ZZ9000-specific** addresses, boot images and Zorro/Picasso96 assumptions.
Do not install `BOOT_XX19d-RIX.bin` on a different board such as the Z3660.
Porting the components to other boards requires their own memory map, boot chain,
AXI/FPGA integration and host-driver verification. No out-of-the-box compatibility is
claimed.

## Sources and licenses

Complete ARM/FSBL and FPGA sources are available directly under
[`../firmware/source/XX19d-RIX/`](../firmware/source/XX19d-RIX/) in this
repository, and as the downloadable release attachment
`XACP.XX19d-RIX.sources.zip`. RasterIX is by ToNi3141, the ZZ9000 hardware and
firmware originate with MNT Research, and XACP integration is by Xanxi. Refer to the
original notices in the sources; the upstream license conditions remain in force.
