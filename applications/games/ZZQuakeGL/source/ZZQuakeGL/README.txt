ZZQuakeGL 1.0 - source code of the Core1 engine
===============================================

ZZQuakeGL runs Quake with id Software's GLQuake renderer on the ARM
Cortex-A9 Core1 of the MNT ZZ9000, drawn by the RasterIX GPU of the ZZ9000
3D bitstream (firmware XX19d-RIX, G4C). This archive holds the complete
corresponding source of the four Core1 engine files of the release
(zzqgl.bin, zzqgl_320.bin, zzqgl_800.bin, zzqgl_1024.bin), as required by
the GNU GPL v2. The 68k launcher ZZQuakeGL is freeware; its source is not
part of this archive.

Layout
------
  ZZQuakeGL/                         this port
    build_arm.sh                     builds the four engine files (offline)
    Core1/                           Quake 1 platform of the port:
      zzqgl_qgmain.c                 QG_Create / QG_Tick, memory map checks
      gl_vid_zz.c                    GLQuake video layer (VID_Init, GL_Init,
                                     GL_Begin/EndRendering, rix_* cvars)
      zzqgl_gl.c                     the gl* calls of GLQuake on ZZGL
      zzqgl_present.c                GPU frame (RGB565) -> Picasso96 buffer
      zzqgl.ld                       link script
      zzqgl_patch_engine.py          engine tree: patched copies
      zzqgl_patch_gl.py              adjustments of id's gl_*.c files
      zzqgl_footprint.py             memory map proof and footprint
      zzqgl_diag.h, zzqgl_version.h
      *.sha256                       checksums of the external inputs
      rix/                           GPU adapter (shared with ZZQuake2RIX),
                                     MMU attributes, GL header shims
    librixgpu/                       RasterIX driver 1.4.0 (0BSD), API.md
    zzgl/                            ZZGL 0.7.7, OpenGL 1.1 subset (0BSD)
    host/                            PC test bench with a model of the GPU
  ZZQuake640_SOURCES_launcher_and_Core1/Core1_GPL/
                                     ZZQuake640 1.0 Core1 runtime (GPL v2):
                                     640/zz9000/ and quakegeneric_source/,
                                     used unmodified
  Quake-master/WinQuake/             the id Software files used (GPL v2),
                                     from github.com/id-Software/Quake,
                                     commit bf4ac424ce754894ac8f1dae6a3981954bc9852d

Building
--------
Requirements: a POSIX shell with bash, sha256sum, python3, and the Arm GNU
toolchain 13.2 Rel1 (arm-none-eabi-gcc 13.2.1). On Windows, MSYS2 with the
toolchain installed in "C:/ArmGNUToolchain/13.2 Rel1/bin" (taken by
default; otherwise set ARMBIN to its bin folder).

From the ZZQuakeGL folder:

    bash build_arm.sh all          the four sizes
    bash build_arm.sh 640          one size (320, 640, 800 or 1024)

The script checks the SHA-256 of every external input and stops if one
differs, builds the engine tree from patched copies (nothing outside the
build folders is modified), compiles, links, checks the result (no NEON
code, entry point, memory map) and writes out/<name>.bin with its MD5 and
SHA-256. The following MD5 values identify the Windows/MSYS release binaries
compiled with Arm GNU 13.2 Rel1. A rebuild can be checked against them;
binary identity is not guaranteed when the build environment differs:

  zzqgl.bin       640x480   MD5 4afbed0d51764f79884453869677ca21
  zzqgl_320.bin   320x240   MD5 2da18d4d8235c6410509f46c6583a09a
  zzqgl_800.bin   800x600   MD5 b2ee6906a33ada8cf3fcba69f294fd3b
  zzqgl_1024.bin  1024x768  MD5 3a0710f2c03f777d15348001d72c50cb

Build options: ZZQGL_LM=id (id's inverted lightmaps and blend; default q2),
ZZQGL_NOMS2=1 (no .ms2 mesh cache).

PC test bench
-------------
host/build_host_q1.sh <out dir> builds the engine for 32-bit Linux (gcc
with multilib) against a software model of the RasterIX GPU and of the
launcher's buffers; the program runs the game on id1/pak0.pak and writes
frames as images. ZZQGL_RES selects the size.

Licences
--------
Quake, GLQuake: (c) id Software, GNU GPL v2 (Quake-master/gnu.txt).
quakegeneric: erysdren, GNU GPL v2.
ZZQuake640 Core1 runtime, ZZQuakeGL/Core1, build_arm.sh, host/host_q1.c:
GNU GPL v2 or later. librixgpu, ZZGL, host/sim_rix.c: 0BSD (SPDX headers in
every file).
RasterIX is the work of ToNi3141. MNT ZZ9000 by MNT Research.
