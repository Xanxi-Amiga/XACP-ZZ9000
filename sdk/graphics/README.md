# ZZGL + librixgpu - Core1 RasterIX graphics sources

**Status: public development source snapshot, not a stable common SDK release.**

This directory preserves the source snapshot used by ZZMiniGL Beta 1:

- ZZGL: `0.8.10-unified-dev1`, including ZZMiniGL A6 extensions (normals,
  texture coordinate generation, indexed/client-array drawing and uploads).
- librixgpu: the matching 14-file low-level RasterIX Core1 driver snapshot.
- Firmware target: **XX19d-RIX**, **XACP v1.8**, **one TMU**, Cortex-A9 Core1.
- Source lineage: `applications/ZZMiniGL/vendor/{zzgl,librixgpu}` at
  repository commit `158fe613a4ee8a5b17c73ffea341180991d33ab4`.

The files are deliberately **byte-identical** to their original source files.
See `SOURCE_MANIFEST.txt` for the original Git blob SHA-1 values.

## Contents

- `zzgl/`: OpenGL subset / rendering front-end used on Core1.
- `librixgpu/`: RasterIX GPU driver, geometry, frame and texture helpers.
- `LICENSES/`: license texts applicable to individual components.
- `PROVENANCE.md`: origin and licensing notes.
- `TESTING.md`: integration gates for users wishing to adopt this snapshot.

## Scope and limitations

These are reusable **sources**, not an independent, drop-in AmigaOS library.
A consumer still needs its own Core1 startup, MMU mapping, memory allocation,
XACP transport, application-facing glue, presentation and platform hooks.
There is no standalone binary and no universal build script here.

Do **not** assume source-level or binary-level drop-in compatibility with the
versions bundled in ZZQuake2, ZZQuakeGL, ZZOpenLara, ZZHalfLife or other ports.
Those applications have their own fixed, tested integration and memory maps.

The underlying RasterIX FPGA in XX19d-RIX has **one TMU** and texture/format
limitations. There is no implied support for a two-TMU bitstream or an OpenGL
implementation beyond the subset actually present in these sources.

No game or application is rebuilt, modified or re-released by publishing this
reference directory. Performance benefits require per-application profiling,
porting and hardware verification.

## Build and integration

Builds must be offline: no network downloads or source fetches in build scripts.
Do not add blanket changes to toolchain flags; the caller's Core1 runtime and
MMU configuration must agree with the library's allocated memory regions.
For ARM applications using bare-metal libgcc/libm, retain `.ARM.extab`,
`.ARM.exidx`, `__exidx_start` and `__exidx_end` in the linker script.

The optional GPL-derived raster-walker reference model is
`librixgpu/rix_walk_model.h`. It is excluded from normal builds when
`ZZGL_ENABLE_WALK_MODEL=0`; examine its separate GPL-3.0-or-later notice.

## Validation status

The **original ZZMiniGL Beta 1 application** has been released and tested on
hardware. That does **not** validate this snapshot as a generic replacement
for every application-specific ZZGL/librixgpu version. No new game build or
performance regression test is claimed for this directory.

The next step is to reconcile the old branches, integrate missing per-game
features where necessary, and test against known working builds before
promoting this directory to a stable shared-library release.

## Upstream and credits

- RasterIX: ToNi3141 - https://github.com/ToNi3141/RasterIX
- ZZ9000: MNT Research
- ZZGL/librixgpu XACP integration: Xanxi (2026)
- Repository: https://github.com/Xanxi-Amiga/XACP-ZZ9000

Different files have different licenses. See `PROVENANCE.md` and license notices
in the source files. **Do not describe the entire tree as unconditionally
0BSD.**
