#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# build_all.sh - ZZQuake2RIX: the three Core1 blobs (0.10): out/zzq2rix.bin
# (640x480), out/zzq2rix_320.bin (320x240), out/zzq2rix_800.bin (800x600).
# Run from anywhere: everything is relative to this folder, the two
# source trees are expected next to it (../ZZQuake640_SOURCES_launcher_and_Core1,
# ../quake2generic) unless ZZQUAKE_RELEASE / Q2 give other RELATIVE paths.
# ASCII only.
set -e
cd "$(dirname "$0")"
ZZQ2_RES=640 sh build_arm.sh
ZZQ2_RES=320 sh build_arm.sh
ZZQ2_RES=800 sh build_arm.sh
echo "== MD5 =="
md5sum out/zzq2rix.bin out/zzq2rix_320.bin out/zzq2rix_800.bin Core1/rix/*.c Core1/rix/*.h Core1/*.c Core1/*.h librixgpu/*.c librixgpu/*.h zzgl/*.c zzgl/*.h | tee MD5SUMS.txt
