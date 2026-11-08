#!/bin/bash
# build_release.sh - produces ZZVirtuaRacingRIX 1.0.
#
# Run from an MSYS2 shell (Windows) or Linux, in this folder:
#   ./build_release.sh            all three steps
#   ./build_release.sh blob       1. ARM Core1 blob      -> bin/zzvr_a3a.bin
#   ./build_release.sh embed      2. blob, game icon -> C -> amiga/blob_data.c, amiga/icon_data.c
#   ./build_release.sh launcher   3. 68k program          -> release/ZZVirtuaRacingRIX/ZZVirtuaRacingRIX_Setup
#
# The game icon amiga/icons/ZZVirtuaRacingRIX.info (tool types FARVIEW, AUDIO)
# is embedded in the program: the setup writes it next to the game it builds.
#
# Needs: step 1 the Arm GNU toolchain 13.2 (arm-none-eabi-gcc; on Windows
# "C:/ArmGNUToolchain/13.2 Rel1/bin" is picked up automatically, else set
# TOOLCHAIN_BIN); step 3 bebbo m68k-amigaos-gcc in PATH (or set M68K_GCC).
# BLOB=<file> embeds another blob in step 2 (e.g. the reference of the package:
#   BLOB=reference/zzvr_a3a_reference.bin ./build_release.sh embed ).
set -e
cd "$(dirname "$0")"
STEP=${1:-all}
BLOB=${BLOB:-bin/zzvr_a3a.bin}
M68K_GCC=${M68K_GCC:-m68k-amigaos-gcc}
REF_MD5=30e6449767edfb1c09fadb18c12b9a46   # reference blob of this package (Arm GCC 13.2.1)

step_blob(){
  echo "=== 1/3 ARM Core1 blob (SH-2 recompiler) ==="
  ZZVR_DRC=1 ./build_arm.sh
  m=$(md5sum bin/zzvr_a3a.bin | cut -d' ' -f1)
  if [ "$m" = "$REF_MD5" ]; then echo "blob identical to the reference of the package ($m)"
  else echo "NOTE: blob md5 $m differs from the reference $REF_MD5"
       echo "      (different toolchain/libc build); BLOB=reference/zzvr_a3a_reference.bin embeds the reference"; fi
}
step_embed(){
  echo "=== 2/3 embed $BLOB ==="
  [ -s "$BLOB" ] || { echo "ERROR: $BLOB missing - run step 1 or set BLOB=..."; exit 1; }
  bash tools/bin2c.sh "$BLOB" amiga/blob_data.c
  bash tools/bin2c.sh amiga/icons/ZZVirtuaRacingRIX.info amiga/icon_data.c g_icon
}
step_launcher(){
  echo "=== 3/3 68k launcher ==="
  [ -s amiga/blob_data.c ] && [ -s amiga/icon_data.c ] || { echo "ERROR: amiga/blob_data.c or amiga/icon_data.c missing - run step 2"; exit 1; }
  command -v "$M68K_GCC" >/dev/null || { echo "ERROR: $M68K_GCC not found (set M68K_GCC)"; exit 1; }
  mkdir -p release/ZZVirtuaRacingRIX
  ( cd amiga && "$M68K_GCC" -O2 -noixemul -m68020 -Wno-pointer-sign \
        -o ../release/ZZVirtuaRacingRIX/ZZVirtuaRacingRIX_Setup ZZVirtuaRacingRIX.c blob_data.c icon_data.c -lamiga )
  cp README.md RELEASE_NOTES_1.0.md COPYING_PICODRIVE.txt THIRD_PARTY_NOTICES.md Core1/vrde/LICENSE_VRDE.txt release/ZZVirtuaRacingRIX/
  cp amiga/icons/ZZVirtuaRacingRIX_Setup.info release/ZZVirtuaRacingRIX/
  cp Core1/librixgpu/LICENSE_0BSD.txt release/ZZVirtuaRacingRIX/LICENSE_LIBRIXGPU_0BSD.txt
  ls -l release/ZZVirtuaRacingRIX/ZZVirtuaRacingRIX_Setup
  echo "done: release/ZZVirtuaRacingRIX/  (copy this folder to the Amiga)"
}
case "$STEP" in
  blob) step_blob ;;
  embed) step_embed ;;
  launcher) step_launcher ;;
  all) step_blob; step_embed; step_launcher ;;
  *) echo "usage: $0 [all|blob|embed|launcher]"; exit 2 ;;
esac
