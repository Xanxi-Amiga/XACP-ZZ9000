#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# build_host_q1.sh - the ZZQuakeGL GL path on the PC (Linux, 32-bit): the engine tree of the
# blob, the GL binding, the video layer, the fork point and ZZGL/librixgpu against the GPU model
# of ZZGear (sim_rix.c).  Not part of the blob.
#   ZZQUAKE_RELEASE=... IDQ=... sh host/build_host_q1.sh <out dir>
# Needs gcc with 32-bit support (gcc-multilib), python3.  ASCII only.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
TG="$(cd "$HERE/.." && pwd)"
OUTD="$1"
[ -n "$OUTD" ] || { echo "usage: build_host_q1.sh <out dir>"; exit 1; }
[ -n "$ZZQUAKE_RELEASE" ] || ZZQUAKE_RELEASE="$TG/../ZZQuake640_SOURCES_launcher_and_Core1"
[ -n "$IDQ" ] || IDQ="$TG/../Quake-master/WinQuake"
Z="$ZZQUAKE_RELEASE/Core1_GPL/640/zz9000"
QG="$ZZQUAKE_RELEASE/Core1_GPL/quakegeneric_source"
mkdir -p "$OUTD/o" "$OUTD/gen"
cp "$QG/quakekeys.h" "$OUTD/gen/"
sed 's/#define QUAKEGENERIC_RES_X 320/#ifndef QUAKEGENERIC_RES_X\n#define QUAKEGENERIC_RES_X 320\n#endif/; s/#define QUAKEGENERIC_RES_Y 240/#ifndef QUAKEGENERIC_RES_Y\n#define QUAKEGENERIC_RES_Y 240\n#endif/' "$QG/quakegeneric.h" > "$OUTD/gen/quakegeneric.h"
python3 -I "$TG/Core1/zzqgl_patch_engine.py" "$QG" "$IDQ" "$OUTD/engine" > /dev/null
E="$OUTD/engine"
# the ARM barriers of the release patches and of the adapter (__asm__ volatile("dsb")) become a
# no-op macro call on the PC (copies, the sources are not touched)
mkdir -p "$OUTD/hsrc"
for f in "$E"/*.c "$TG/Core1/zzqgl_qgmain.c" "$TG/Core1/gl_vid_zz.c" "$Z/snd_zz9000.c"; do
    sed 's/__asm__ volatile(/zzq_host_asm(/g' "$f" > "$OUTD/hsrc/$(basename "$f")"
done
E="$OUTD/hsrc"
case "${ZZQGL_RES:-640}" in 640) RX=640; RY=480 ;; 320) RX=320; RY=240 ;; 800) RX=800; RY=600 ;; 1024) RX=1024; RY=768 ;; *) echo "ZZQGL_RES: 640, 320, 800 or 1024"; exit 1 ;; esac
RESDEF="-DZZQ2_VID_W=$RX -DZZQ2_VID_H=$RY -DZZQGL_VID_W=$RX -DZZQGL_VID_H=$RY"
CF="-m32 -O1 -w -fno-strict-aliasing -fcommon -DGLQUAKE -DZZQGL_HOST -Dzzq_host_asm(...)=(void)0 -DZZQ_PRODUCTION -DQUAKEGENERIC_RES_X=$RX -DQUAKEGENERIC_RES_Y=$RY $RESDEF -I$OUTD/engine -I$OUTD/gen -I$Z -I$TG/Core1 -I$TG/Core1/rix -I$TG/Core1/rix/shim -I$TG/librixgpu -I$TG/zzgl"
for f in "$OUTD"/engine/*.c; do
    b=$(basename "$f" .c)
    gcc $CF -c "$E/$b.c" -o "$OUTD/o/eng_$b.o"
done
gcc $CF -c "$E/snd_zz9000.c" -o "$OUTD/o/snd_zz9000.o"
gcc $CF -Wall -Wno-unknown-pragmas -c "$E/gl_vid_zz.c" -o "$OUTD/o/gl_vid_zz.o"
gcc $CF -Wall -Wno-unknown-pragmas -c "$E/zzqgl_qgmain.c" -o "$OUTD/o/zzqgl_qgmain.o"
gcc -m32 -O1 -Wall -I"$TG/Core1/rix/shim" -I"$TG/librixgpu" -I"$TG/zzgl" -I"$TG/Core1" -DRIX_HOST_SIM -DZZQ2RIX_HOST -c "$TG/Core1/zzqgl_gl.c" -o "$OUTD/o/zzqgl_gl.o"
RF="-m32 -O1 -Wall -Wno-misleading-indentation -Wno-unused-function -DRIX_HOST_SIM -DZZQ2RIX_HOST $RESDEF -I$TG/librixgpu -I$TG/zzgl -I$TG/Core1/rix -I$TG/Core1 -I$HERE"
for s in zzq2rix_gpu zzq2rix_mmu; do
    gcc $RF -c "$TG/Core1/rix/$s.c" -o "$OUTD/o/$s.o"
done
for s in rixgpu rixframe rixtex rixdesc rixclip; do
    gcc $RF -c "$TG/librixgpu/$s.c" -o "$OUTD/o/rix_$s.o"
done
gcc $RF -c "$TG/zzgl/zzgl.c" -o "$OUTD/o/zzgl.o"
gcc $RF -DSIM_RIX_NO_APP -c "$HERE/sim_rix.c" -o "$OUTD/o/sim_rix.o"
gcc $CF -Wall -Wno-unknown-pragmas -c "$HERE/host_q1.c" -o "$OUTD/o/host_q1.o"
gcc -m32 -o "$OUTD/host_q1" "$OUTD"/o/*.o -lm \
    -Wl,--wrap=R_DrawWorld -Wl,--wrap=R_DrawBrushModel -Wl,--wrap=R_DrawWaterSurfaces -Wl,--wrap=R_NewMap -Wl,--wrap=Host_Shutdown -Wl,--wrap=Sys_SendKeyEvents -Wl,--wrap=GL_MakeAliasModelDisplayLists
echo "built $OUTD/host_q1"
echo "run:  $OUTD/host_q1 <basedir> <shots dir> \"60:shot console;400:shot demo;800:quit\""
