# librixgpu 1.0 - Core1 RasterIX G4B Driver (ZZ9000)

API version: `RIXGPU_VERSION = 0x00010000` (`rix_version()`).

This source snapshot provides four ARM Core1 driver modules. They do not call Core0, firmware services or the physical 68k CPU. Commands are sent through the AXI-Stream FIFO (ST0 to ST1) or through DDR command lists read by the FrameStreamingCore (MEM to ST1). The validated development lineage is ZZCube B1d and ZZGear C1c, without changing the G4B FPGA or FAST1 firmware for D1 testing.

## Modules

| Module | Purpose |
| --- | --- |
| `rixgpu.[ch]` | Global timer, WFI, FIFO, L0-L15 initialization, command submission, fences, PL330 byte swap, VDMA, capability and DDR-placement probes |
| `rixframe.[ch]` | Fenced DDR command-chunk ring, streamed GPU state, ordered texture binding, frame completion fence |
| `rixtex.[ch]` | 2 KiB page pool, RGB565 texture objects up to 256x256, CPU cache maintenance |
| `rixdesc.[ch]` | 39-word triangle descriptors, checked against the A07b reference in host tests |
| `rix_hw.h` | Hardware register, feature, texture and descriptor definitions |
| `rix_shared.h` | Demonstration 68k/Core1 mailbox definitions and diagnostic slots (`SD_*`, `RS_*`) |

## Validated hardware rules (G4B)

1. **Enable the correct write mask before MEMSET.** Color MEMSET with disabled color mask (REG 3, bits 15-18) can stall the command parser; depth MEMSET with disabled depth mask (bit 14) writes nothing. `rix_dl_clear()` and `rix_dl_clear_depth()` restore the respective masks.
2. **Texture page tables must be issued through the FIFO**, not inside DDR command lists. Otherwise the FSC/TMU AXI read path can stall. `rix_frame_texture()` closes the current chunk, submits an ordered texture configuration and page table, then opens a new chunk.
3. **Do not issue depth COMMIT or READ:** these operations are not acknowledged by the tested `RasterIXCoreEF.v` configuration. Depth MEMSET is supported.
4. **No G4B software GPU reset:** a permanently stalled command can require a power cycle. The initialization ladder and probes send individual operations and identify failures (E1A0, E1Bk, E1Cx).
5. **Fences:** scissored 64-pixel color MEMSET on hidden line 480, preceded by 64 DDR-mode NOPs. Command lists must never be overwritten before their corresponding fence.
6. **Y convention:** RasterIX stores a pixel at `((yres - 1) - y) * xres + x`; positive Y points up.
7. **Texture path:** contiguous little-endian RGB565 rows, ordered 2 KiB pages. An `OP_TEXTURE` upload copies the active texture into TMU BRAM. Measured 64x64 change: approximately 16-17 us; 256x256 change: approximately 233 us including FIFO-path overhead. FIFO MMIO cost was approximately 1.1 us/word; the transfer-only HP3 bandwidth was approximately 800 MB/s in the 256x256 probe.
8. **Memory placement:** the displayed color buffer stays in the 68k-visible window. DDR command lists, textures and Z-buffers may live outside that window, including the Core1 arena at 0x30000000, if the CPU cleans written cache lines (`rix_mem_clean`) or maps the region non-cacheable.

## Basic lifecycle

```c
rix_config cfg = { flags, idle_tick_us, mbox, probe_tri, probe_x, probe_y,
                   tmu_env, tmu_env_color, tmu_cfg, tex_pages, tex_base };
rc = rix_init(&cfg);  /* Runs initialization ladder and DMA/NEON/WFI checks. */
/* Submit required initial state with rix_dl_* and rix_submit_fifo(). */
rix_ring_cfg ring = { ring_base, slots, chunk_bytes, fence_x, color_bufs, n };
rix_ring_init(&ring); /* After the last initial command list is fenced. */
rix_texpool_init(base, pages, cacheable);
rix_tex_alloc(&t, 256, 256, 1);
rix_tex_write_rgb888(&t, rgb, 0);
/* Submit frames and wait for completion fences. */
rix_shutdown();
```

`mbox` has 544 words in the historical layout (`SH_END`). Core1 populates `SH_STATS+RS_*`, `SH_SELFTEST_DATA+SD_*`, `SH_CAPS`, and `SH_TIMER_HZ`; the host application owns the remaining slots. Another program such as ZZQuake2 can provide an equivalent layout at its own address.

## Per-frame submission

```c
rix_frame_desc d = {
    .color = FB, .zbuf = Z,
    .features = RIX_FEAT_TMU0 | RIX_FEAT_DEPTH_TEST,
    .fragment = rix_fragment(RIX_TF_LESS, 1, RIX_TF_ALWAYS, 0,
                             RIX_BF_ONE, RIX_BF_ZERO),
    .clear_rgba = 0, .clear_depth16 = 0xFFFF,
    .clear_color = 1, .clear_depth = 1,
    .rect_x0 = 0, .rect_y0 = 0, .rect_x1 = 0, .rect_y1 = 0
}; /* Empty rectangle means full screen. */
rix_frame_begin(&f, &d);
rix_frame_texture(&f, &tex); /* Ordered TMU0 binding. */
rix_frame_reg(&f, RIX_REG_TMU0_ENV, RIX_TMU_ENV_REPLACE);
for (...) {
    rix_tri_desc(&a, &b, &c, desc);
    rix_frame_tri(&f, desc);
}
rix_frame_fragment(&f, rix_fragment(RIX_TF_LESS, 0, RIX_TF_ALWAYS, 0,
                                    RIX_BF_ZERO, RIX_BF_SRC_COLOR));
/* Example second-pass lightmap state. */
rix_frame_features(&f, RIX_FEAT_TMU0 | RIX_FEAT_DEPTH_TEST | RIX_FEAT_BLENDING);
/* Further drawing operations. */
rix_frame_end(&f, fence16);
rix_frame_wait(FB, fence16, 1000000, &stop, &polls);
```

Commands are submitted as chunks fill, leaving a reserve of 200 words at the end of each chunk. GPU drawing overlaps Core1 list construction. `rix_frame_scissor()` configures clipping (x1 = 0 disables it). Register 0/3 changes must pass through `rix_frame_features()` / `rix_frame_fragment()` to restore state after chunk rotation. Frame statistics include `f.chunks`, `f.words`, `f.tris`, `f.ringwait_us`, `f.first_submit_tick`, and `f.tex_binds`.

The examples above illustrate the API; this snapshot is not a complete standalone build or runnable demonstration.

## Triangle descriptors

`rix_vertex` contains `{ sx, sy, z, invw, u, v, r, g, b, a }`. `rix_tri_desc()` builds 39 words and returns zero for degenerate or fully out-of-bounds geometry; callers must clip partially visible polygons. `z` is window-space depth 0..1 (0 = near), `invw` is 1/w. Host tests matched 3,600 A07b descriptors word-for-word.

## Textures

`rix_texpool_init(base, pages, cacheable)`; `rix_tex_alloc(&t, w, h, mag_linear)` for power-of-two sizes 8..256; `rix_tex_write_rgb565()` and `rix_tex_write_rgb888()` for uploads with selectable byte swapping. `t.cfg` is the REG 12 texture configuration; `t.base` and `t.pages` define storage. The version 1.0 texture API does not support mipmaps.

## Probes

`rix_run_probes(color, zbuf, tex)` checks depth ordering (including no-clear reuse), ONE/ONE blending, GREATER-0.5 alpha testing, and the 64x64 / 256x256 texture-switch costs. `rix_probe_placement(color, tex_src, tex_dst, zbuf)` confirms DDR-based texture and Z-buffer placement. Results are written to `SD_PROBE_*` slots.

## Error codes

- E110: GPU ID. E111-E114: FIFO (E114: packet not accepted within 100 ms).
- E120: fence timeout. E121: invalid value. E122: oversized command list. E123: invalid descriptor.
- E124: chunk ring timeout. E125: ring/API configuration. E126: texture pool. E127: texture dimensions.
- E130/E131: DMA. E140: no flip. E150: timer. E160: mode.
- E1A0: startup stall. E1Bk: initialization ladder step k. E1Cx: probe x.

## Version 1.0 limits

Fixed 640x481 allocation (`RIX_W`, `RIX_H_ALLOC`), one TMU, no mipmaps or stencil in the public texture API (low-level register access exists via `rix_frame_reg`), G4B-specific initialization layout in `rix_shared.h`, and demo-specific diagnostic mailbox. This is a driver source preview, not an independent ZIP-to-binary build.
