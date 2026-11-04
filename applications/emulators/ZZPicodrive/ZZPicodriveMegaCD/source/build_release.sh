#!/bin/bash
# build_release.sh - produces the ZZPicoDriveMegaCD program.
#
# Run from an MSYS2 shell (Windows) or Linux, in this folder:
#   ./build_release.sh            all three steps
#   ./build_release.sh blob       1. ARM Core1 blob      -> bin/zzmcd.bin
#   ./build_release.sh embed      2. blob -> C array      -> amiga/blob_data.c
#   ./build_release.sh launcher   3. 68k launcher         -> release/ZZPicoDriveMegaCD/ZZPicoDriveMegaCD
#
# Needs: step 1 the Arm GNU toolchain 13.2 (arm-none-eabi-gcc; on Windows
# "C:/ArmGNUToolchain/13.2 Rel1/bin" is picked up automatically, else set
# TOOLCHAIN_BIN); step 3 bebbo m68k-amigaos-gcc in PATH (or set M68K_GCC).
# BLOB=<file> embeds another blob in step 2 (e.g. the tested reference:
#   BLOB=reference/zzmcd_reference.bin ./build_release.sh embed ).
set -e
cd "$(dirname "$0")"
STEP=${1:-all}
BLOB=${BLOB:-bin/zzmcd.bin}
M68K_GCC=${M68K_GCC:-m68k-amigaos-gcc}
REF_MD5=2e509f1b0bf5b2d197f3bfa781d10007   # reference blob of this package (Arm GCC 13.2.1)

step_blob(){
  echo "=== 1/3 ARM Core1 blob (Mega-CD, 32X with SH-2 recompiler) ==="
  ZZVR_DRC=1 ./build_arm.sh
  m=$(md5sum bin/zzmcd.bin | cut -d' ' -f1)
  if [ "$m" = "$REF_MD5" ]; then echo "blob identical to the tested reference ($m)"
  else echo "NOTE: blob md5 $m differs from the tested reference $REF_MD5"
       echo "      (different toolchain/libc build): compare with the reference blob on the Amiga"; fi
}
step_embed(){
  echo "=== 2/3 embed $BLOB ==="
  [ -s "$BLOB" ] || { echo "ERROR: $BLOB missing - run step 1 or set BLOB=..."; exit 1; }
  bash tools/bin2c.sh "$BLOB" amiga/blob_data.c
}
step_launcher(){
  echo "=== 3/3 68k launcher ==="
  [ -s amiga/blob_data.c ] || { echo "ERROR: amiga/blob_data.c missing - run step 2"; exit 1; }
  command -v "$M68K_GCC" >/dev/null || { echo "ERROR: $M68K_GCC not found (set M68K_GCC)"; exit 1; }
  mkdir -p release/ZZPicoDriveMegaCD
  ( cd amiga && "$M68K_GCC" -O2 -noixemul -m68020 -Wno-pointer-sign \
        -o ../release/ZZPicoDriveMegaCD/ZZPicoDriveMegaCD ZZPicoDriveMegaCD.c blob_data.c -lamiga )
  cp README.md INSTALL.md RELEASE_NOTES_v1.2.md COPYING_PICODRIVE.txt THIRD_PARTY_NOTICES.md release/ZZPicoDriveMegaCD/
  mkdir -p release/ZZPicoDriveMegaCD/BIOS
  echo "Put the Mega-CD BIOS files here (see README.md). None is included." > release/ZZPicoDriveMegaCD/BIOS/README.txt
  ls -l release/ZZPicoDriveMegaCD/ZZPicoDriveMegaCD
  echo "done: release/ZZPicoDriveMegaCD/  (copy this folder to the Amiga)"
}
case "$STEP" in
  blob) step_blob ;;
  embed) step_embed ;;
  launcher) step_launcher ;;
  all) step_blob; step_embed; step_launcher ;;
  *) echo "usage: $0 [all|blob|embed|launcher]"; exit 2 ;;
esac
