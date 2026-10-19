#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# build_all.sh - ZZQuake2: the nine Core1 blobs, from this folder only.
#
#   sh build_all.sh
#
# Needs: arm-none-eabi-gcc 13.2.1 (Arm GNU Toolchain 13.2.rel1), python3,
# sha256sum, md5sum, a POSIX shell. Linux is the tested host.
#
# Output:
#   soft/out/zzq2a0.bin  zzq2_320.bin     zzq2_800.bin      (software)
#   tgl/out/zzq2tgl.bin  zzq2tgl_320.bin  zzq2tgl_400.bin   (TinyGL)
#   rix/out/zzq2rix.bin  zzq2rix_320.bin  zzq2rix_800.bin   (RasterIX)
#
# Each build first checks the SHA-256 of the files it takes from
# ZZQuake640_Core1/ and quake2generic/; neither tree is modified.
# ASCII only.

set -e
cd "$(dirname "$0")"
ZZQUAKE_RELEASE=../ZZQuake640_Core1
Q2=../quake2generic
export ZZQUAKE_RELEASE Q2

echo "######## software renderer ########"
( cd soft && ZZQ2_RES=640 sh build_arm.sh \
          && ZZQ2_RES=320 sh build_arm.sh \
          && ZZQ2_RES=800 sh build_arm.sh )

echo "######## TinyGL renderer ########"
( cd tgl && sh build_all.sh )

echo "######## RasterIX renderer ########"
( cd rix && sh build_all.sh )

echo "######## the nine blobs ########"
md5sum soft/out/zzq2a0.bin soft/out/zzq2_320.bin soft/out/zzq2_800.bin \
       tgl/out/zzq2tgl.bin tgl/out/zzq2tgl_320.bin tgl/out/zzq2tgl_400.bin \
       rix/out/zzq2rix.bin rix/out/zzq2rix_320.bin rix/out/zzq2rix_800.bin
