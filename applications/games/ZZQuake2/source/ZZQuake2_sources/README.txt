ZZQuake2 - Quake II for the Amiga 4000 + MNT ZZ9000
Core1 source code (GPL)
====================================================

This package holds the complete source of the nine Core1 blobs of
ZZQuake2 (the ARM code that runs Quake II on the second core of the
ZZ9000), and everything needed to rebuild them byte for byte.

  Renderer        Version  Blobs
  --------------  -------  ----------------------------------------------
  Software        0.8.5    zzq2a0.bin (640x480), zzq2_320.bin, zzq2_800.bin
  TinyGL          0.6.1    zzq2tgl.bin (640x480), zzq2tgl_320.bin
  RasterIX (GPU)  0.10.3   zzq2rix.bin (640x480), zzq2rix_320.bin,
                           zzq2rix_800.bin

The 68k programs ZZQuake2_A0 (launcher, 0.8.6) and ZZQuake2GUI (0.1.2)
are freeware executables but not part of this package. No game data is included: Quake II (demo or full game) is
needed to play.


LAYOUT
------
  build_all.sh          builds the nine blobs (see BUILDING)
  COPYING               GNU General Public License, version 2
  soft/                 software renderer (ref_soft)
    build_arm.sh          one blob per run (ZZQ2_RES=640|320|800)
    Core1/                adapter: video, sound, music, save games,
                          memory, patch scripts applied to COPIES of
                          Quake II files, linker script
  tgl/                  TinyGL renderer (ref_gl on TinyGL, in software)
    build_all.sh, build_arm.sh, Core1/ (+ Core1/tgl/), tinygl/
  rix/                  RasterIX renderer (ref_gl on the RasterIX GPU of
                        the ZZ9000 FPGA)
    build_all.sh, build_arm.sh, Core1/ (+ Core1/rix/),
    librixgpu/ (RasterIX driver), zzgl/ (GL layer)
  ZZQuake640_Core1/     Core1 side of ZZQuake640 (ZZQuake 1.0 HighRes,
                        GPL): boot, MMU, shared memory protocol with the
                        68k launcher, file and video services. Used as
                        it is; Quake II takes over at QG_Create().
  quake2generic/        ozkl/quake2generic, commit
                        84f34ed5382323d96b81a44a44f73496791084a3 (id
                        Software's Quake II GPL source, portable).

Neither ZZQuake640_Core1/ nor quake2generic/ is modified: each build
checks their files against the SHA-256 lists in */Core1/*.sha256 and
stops if one differs. The few engine changes are made by the Python
scripts in */Core1/zzq2_patch_*.py on copies, in the build folder.


BUILDING
--------
  Host: Linux (tested). Tools: arm-none-eabi-gcc 13.2.1 (Arm GNU
  Toolchain 13.2.rel1, the compiler of the ZZQuake640 release),
  python3, sha256sum, md5sum, sh.

      sh build_all.sh

  The blobs land in soft/out/, tgl/out/ and rix/out/. One blob at a
  time, for example:

      cd soft
      ZZQUAKE_RELEASE=../ZZQuake640_Core1 Q2=../quake2generic \
      ZZQ2_RES=320 sh build_arm.sh

  The builds are reproducible: with the compiler above, the result is
  the released files, byte for byte (checked when this package was
  made, in a different folder):

    cfbf2e12838a4b6c451828e8df48fc4f  soft/out/zzq2a0.bin
    936e67143eea5c4fbb686397ef267c0a  soft/out/zzq2_320.bin
    f9f68137a8f760280ad1b095b2c47192  soft/out/zzq2_800.bin
    1e412f81365e2f791b140e1e8d26a42a  tgl/out/zzq2tgl.bin
    cee0c2dae68d0837ee5b0667224f3fe7  tgl/out/zzq2tgl_320.bin
    5f297a032e0ad9c756bde5a384251cbd  tgl/out/zzq2tgl_400.bin
    d5de6b035aca666c7c6081b3662d0a71  rix/out/zzq2rix.bin
    bc3f46ebb18ac718e2a9678c5885591d  rix/out/zzq2rix_320.bin
    192d6ec24919a006db7a1b3832a18237  rix/out/zzq2rix_800.bin


LICENSES AND CREDITS
--------------------
  - Quake II engine and game code: id Software, GPL version 2
    (quake2generic/LICENSE, gnu.txt). quake2generic: ozkl.
  - ZZQuake640 Core1 side and the Quake engine base it comes from
    (quakegeneric, erysdren; Quake, id Software): GPL version 2
    (ZZQuake640_Core1/Core1_GPL/COPYING, LICENSE.quakegeneric,
    UPSTREAM.txt).
  - ZZQuake2 Core1 adapters (soft/, tgl/Core1, rix/Core1): Xanxi, GPL
    version 2 or later (SPDX headers).
  - TinyGL: Fabrice Bellard, "TinyGL Evolved" version; MIT-style
    license (tgl/tinygl/LICENSE).
  - rix/zzgl: Xanxi, 0BSD (SPDX headers). rix/librixgpu: Xanxi.
  - RasterIX, the FPGA rasterizer the RIX blobs drive, is part of the
    ZZ9000 bitstream and is not in this package.
  The blobs, as a whole, are distributed under the GPL version 2
  (COPYING).
