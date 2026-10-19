#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# build_all.sh - ZZQuake2TGL: the three Core1 blobs (one per resolution).
#
#   sh build_all.sh
#
# Offline. The MSYS utilities are put first in the PATH (see below). The ARM compiler is taken from
# ARMBIN, or "C:/ArmGNUToolchain/13.2 Rel1/bin" when it exists, or the
# PATH (Linux). No 68k build: the launcher is ZZQuake2_A0, unchanged.
#
# Source trees (not modified), as environment variables or defaults:
#   ZZQUAKE_RELEASE  ZZQuake640 release bundle
#                    default: ../ZZQuake640_SOURCES_launcher_and_Core1
#   Q2               quake2generic, commit 84f34ed5
#                    default: ../quake2generic
#
# Output: out/zzq2tgl.bin (640x480), out/zzq2tgl_400.bin (400x300),
#         out/zzq2tgl_320.bin (320x240), each with its footprint.
# ASCII only.

set -e

# MSYS: a cross-toolchain folder placed first in the PATH (seen:
# C:\amiga-gcc\bin) ships its own "sh", whose root is C:\ and not the
# MSYS root: it gives paths such as "/Softwares/..." that the MSYS
# tools (sha256sum, cygpath...) then look for under C:\msys64. The
# script therefore runs itself again, once, under the bash of MSYS2,
# looked for by every name it can have from either shell, then the
# ordinary bash (Linux). ZZQ_BASH=<path> forces another one.
if [ -z "$ZZQ_REEXEC" ]; then
    for zzq_b in "$ZZQ_BASH" /c/msys64/usr/bin/bash.exe /msys64/usr/bin/bash.exe \
                 C:/msys64/usr/bin/bash.exe /usr/bin/bash /bin/bash; do
        if [ -n "$zzq_b" ] && [ -x "$zzq_b" ]; then
            ZZQ_REEXEC=1; export ZZQ_REEXEC
            exec "$zzq_b" "$0" "$@"
        fi
    done
fi
PATH="/usr/bin:/bin:$PATH"; export PATH
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

[ -n "$ZZQUAKE_RELEASE" ] || ZZQUAKE_RELEASE="$HERE/../ZZQuake640_SOURCES_launcher_and_Core1"
[ -n "$Q2" ] || Q2="$HERE/../quake2generic"
[ -d "$ZZQUAKE_RELEASE" ] || { echo "ERROR: ZZQUAKE_RELEASE not found: $ZZQUAKE_RELEASE"; exit 1; }
[ -d "$Q2" ] || { echo "ERROR: Q2 not found: $Q2"; exit 1; }
export ZZQUAKE_RELEASE Q2

for m in 3 1 0; do
    echo "################ ZZQ2TGL_MODE=$m ################"
    ZZQ2TGL_MODE=$m "${BASH:-sh}" "$HERE/build_arm.sh"
done
echo
echo "== blobs =="
ls -l out/*.bin
md5sum out/*.bin
