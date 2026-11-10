#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# build_arm.sh - ZZQuakeGL, Core1 blob (Quake 1, GLQuake renderer of id
# Software on ZZGL 0.7.7 / librixgpu 1.4.0: the RasterIX GPU of the ZZ9000
# G4C bitstream), on the UNCHANGED ZZQuake640 release runtime.
# Derived from build_arm.sh of ZZQuake2RIX 0.10.3.
#
#   ZZQUAKE_RELEASE=/path/to/ZZQuake640_SOURCES_launcher_and_Core1 \
#   IDQ=/path/to/Quake-master/WinQuake \
#   sh build_arm.sh
#
# ZZQUAKE_RELEASE : the ZZQuake640 release source bundle (the folder that
#                   holds Core1_GPL/ and launcher_68k_PRIVATE/).
#                   Default: ../ZZQuake640_SOURCES_launcher_and_Core1
# IDQ             : id-Software/Quake, commit bf4ac424, folder WinQuake.
#                   Default: ../Quake-master/WinQuake
# ARMBIN          : folder of arm-none-eabi-gcc. Default: the MSYS install
#                   "C:/ArmGNUToolchain/13.2 Rel1/bin" when it exists, else
#                   the PATH. PATH is never changed.
# ZZQGL_LM        : q2 (default) or id, the lightmap blend (zzqgl_patch_gl.py)
# ZZQGL_NOMS2     : 1 = no glquake/*.ms2 mesh cache (zzqgl_patch_gl.py)
# ZZQGL_RES       : size of the screen the blob draws for (as ZZQ2_RES of
#                   ZZQuake2RIX 0.10, whose GPU adapter handles all three):
#                     640 (default)  640 x 480  out/zzqgl.bin
#                     320            320 x 240  out/zzqgl_320.bin  (drawn inside
#                                    the 640 x 481 GPU buffers, as ZZQuake2RIX 320)
#                     800            800 x 600  out/zzqgl_800.bin  (rix_set_geometry,
#                                    native, as ZZQuake2RIX 800)
#                     1024           1024 x 768 out/zzqgl_1024.bin (buffers in the
#                                    GPU reservation of the arena, pool 12 MiB,
#                                    layout probe before the game; arbitration
#                                    of 10/11/2026)
#                   The launcher ZZQuakeGL opens the matching screen (options
#                   320 / 800) and loads the matching blob by default.
#
# Nothing outside this folder is modified: the release and id files are
# compiled from copies made after their SHA-256 has been checked.
# Offline: no download of any kind.  ASCII only.

set -e
export SOURCE_DATE_EPOCH=1
export LC_ALL=C

# Everything is relative to this folder, as in the ZZGear/ZZQuake2 builds:
# no path conversion of any kind.
cd "$(dirname "$0")"

# Arguments: the size(s) to build, instead of ZZQGL_RES:
#   bash build_arm.sh            640 only (or ZZQGL_RES)
#   bash build_arm.sh 320        one size
#   bash build_arm.sh all        640, 320 and 800, one after the other
if [ "$1" = "all" ]; then
    for r in 640 320 800 1024; do
        bash "$0" "$r" || exit 1
    done
    echo "== all blobs =="
    for n in zzqgl zzqgl_320 zzqgl_800 zzqgl_1024; do
        ls -l "out/$n.bin"
        md5sum "out/$n.bin"
        sha256sum "out/$n.bin"
    done
    exit 0
fi
case "$1" in
    320|640|800|1024) ZZQGL_RES="$1" ;;
    "") ;;
    *) echo "ERROR: argument: '$1' (320, 640, 800, 1024 or all)"; exit 1 ;;
esac
A="Core1"
T="Core1/rix"
LR="librixgpu"
ZG="zzgl"
OUT="out"
case "${ZZQGL_RES:-640}" in
    640) RESX=640; RESY=480; NAME="zzqgl";     B="build" ;;
    320) RESX=320; RESY=240; NAME="zzqgl_320"; B="build_320" ;;
    800) RESX=800; RESY=600; NAME="zzqgl_800"; B="build_800" ;;
    1024) RESX=1024; RESY=768; NAME="zzqgl_1024"; B="build_1024" ;;
    *)   echo "ERROR: ZZQGL_RES: '$ZZQGL_RES' (640, 320, 800 or 1024)"; exit 1 ;;
esac

fail() { echo "ERROR: $*"; exit 1; }

# ---- tools (the ZZGear convention: fixed MSYS install, else the PATH) ----
if [ -z "$ARMBIN" ] && [ -x "C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-gcc.exe" ]; then
    ARMBIN="C:/ArmGNUToolchain/13.2 Rel1/bin"
fi
if [ -n "$ARMBIN" ]; then
    X=""; [ -x "$ARMBIN/arm-none-eabi-gcc.exe" ] && X=".exe"
    AGCC="$ARMBIN/arm-none-eabi-gcc$X"; ANM="$ARMBIN/arm-none-eabi-nm$X"
    AOBJDUMP="$ARMBIN/arm-none-eabi-objdump$X"; AOBJCOPY="$ARMBIN/arm-none-eabi-objcopy$X"
    AREADELF="$ARMBIN/arm-none-eabi-readelf$X"; ASIZE="$ARMBIN/arm-none-eabi-size$X"
else
    AGCC=arm-none-eabi-gcc; ANM=arm-none-eabi-nm; AOBJDUMP=arm-none-eabi-objdump
    AOBJCOPY=arm-none-eabi-objcopy; AREADELF=arm-none-eabi-readelf; ASIZE=arm-none-eabi-size
fi
"$AGCC" --version >/dev/null 2>&1 || fail "arm-none-eabi-gcc not found (ARMBIN='$ARMBIN')"
if command -v python3 >/dev/null 2>&1 && python3 -c "import sys" >/dev/null 2>&1; then PY=python3
elif command -v python >/dev/null 2>&1; then PY=python
else fail "Python 3 is required"; fi
export ZZQGL_NM="$ANM"

echo "== blob: $NAME (${RESX}x${RESY}, GLQuake on RasterIX) =="

# ---- the two source trees, RELATIVE to this folder ---------------------
# The two external source folders: ZZQUAKE_RELEASE / IDQ if given, else
# looked for next to this folder, then inside it.
if [ -z "$ZZQUAKE_RELEASE" ]; then
    for c in "../ZZQuake640_SOURCES_launcher_and_Core1" "ZZQuake640_SOURCES_launcher_and_Core1"; do
        if [ -d "$c" ]; then ZZQUAKE_RELEASE="$c"; break; fi
    done
fi
if [ -z "$IDQ" ]; then
    for c in "../Quake-master/WinQuake" "Quake-master/WinQuake"; do
        if [ -d "$c" ]; then IDQ="$c"; break; fi
    done
fi
[ -n "$ZZQUAKE_RELEASE" ] || fail "ZZQuake640_SOURCES_launcher_and_Core1 not found next to or inside this folder"
[ -n "$IDQ" ] || fail "Quake-master/WinQuake (id-Software/Quake bf4ac424) not found next to or inside this folder"
[ -d "$ZZQUAKE_RELEASE" ] || fail "ZZQUAKE_RELEASE: no such directory: $ZZQUAKE_RELEASE"
[ -d "$IDQ" ] || fail "IDQ: no such directory: $IDQ"

# ---- locate the release Core1 tree (several bundle layouts) -----------
Z=""
for c in "$ZZQUAKE_RELEASE/Core1_GPL/640/zz9000" \
         "$ZZQUAKE_RELEASE/640/zz9000" \
         "$ZZQUAKE_RELEASE"; do
    if [ -f "$c/zzquake_main.c" ]; then Z="$c"; break; fi
done
[ -n "$Z" ] || fail "zzquake_main.c not found under $ZZQUAKE_RELEASE"
QG=""
for c in "$Z/../../quakegeneric_source" "$Z/../quakegeneric_source" \
         "$ZZQUAKE_RELEASE/Core1_GPL/quakegeneric_source"; do
    if [ -f "$c/quakegeneric.h" ]; then QG="$c"; break; fi
done
[ -n "$QG" ] || fail "quakegeneric.h (Quake tree of the release) not found"
[ -f "$IDQ/gl_rsurf.c" ] || fail "IDQ: gl_rsurf.c not found in $IDQ"
echo "  release Core1 : $Z"
echo "  release QG    : $QG"
echo "  id WinQuake   : $IDQ"

# SHA-256 lists are relative to their tree: prefix them, check from here
shacheck() {   # shacheck <list> <tree> <what>
    sed "s#^\([0-9a-f]*\)  #\1  $2/#" "$1" > "$B/check.sha"
    sha256sum -c --quiet "$B/check.sha" || fail "$3 differs from the expected sources - nothing built"
}
rm -rf "$B"
mkdir -p "$B/gen" "$B/q1obj" "$OUT"
echo "== release files: SHA-256 check =="
shacheck "$A/release_core1.sha256" "$Z" "a release Core1 file"
shacheck "$A/release_qg.sha256" "$QG" "a release quakegeneric file"
echo "PASS release ($(wc -l < "$A/release_core1.sha256" | tr -d ' ') + $(wc -l < "$A/release_qg.sha256" | tr -d ' ') files)"
echo "== id WinQuake: SHA-256 check (commit bf4ac424, GL files) =="
shacheck "$A/idq_gl.sha256" "$IDQ" "an id WinQuake file (commit bf4ac424? CRLF checkout?)"
echo "PASS id ($(wc -l < "$A/idq_gl.sha256" | tr -d ' ') files)"
echo "== librixgpu / zzgl / GPU adapter: SHA-256 check =="
sha256sum -c --quiet "$T/rix_vendored.sha256" || fail "librixgpu/, zzgl/ or Core1/rix/ differs from the vendored tree - nothing built"
echo "PASS vendored ($(wc -l < "$T/rix_vendored.sha256" | tr -d ' ') files)"

# ---- headers of the release (same as ZZQuake2 A0 / RIX) ----------------
cp "$QG/quakekeys.h" "$B/gen/quakekeys.h"
$PY - "$QG/quakegeneric.h" "$B/gen/quakegeneric.h" <<'PYEOF'
import sys
g = open(sys.argv[1], encoding='latin-1').read()
for old, new in (
    ('#define QUAKEGENERIC_RES_X 320',
     '#ifndef QUAKEGENERIC_RES_X\n#define QUAKEGENERIC_RES_X 320\n#endif'),
    ('#define QUAKEGENERIC_RES_Y 240',
     '#ifndef QUAKEGENERIC_RES_Y\n#define QUAKEGENERIC_RES_Y 240\n#endif')):
    if g.count(old) != 1:
        raise SystemExit('quakegeneric.h: unexpected content')
    g = g.replace(old, new)
open(sys.argv[2], 'w', encoding='latin-1', newline='').write(g)
PYEOF
echo "2912eeb9e08a7e3abad1569b4290924e4b49f8c4ad219f5c08532b658eb0f583  $B/gen/quakegeneric.h" \
    | sha256sum -c --quiet - || fail "generated quakegeneric.h is not the release-patched header"

# ---- the engine tree: copies, patched (zzqgl_patch_engine.py) -----------
echo "== engine tree: quakegeneric (release) + id GL files, patched copies =="
$PY -I "$A/zzqgl_patch_engine.py" "$QG" "$IDQ" "$B/engine"
E="$B/engine"

# ---- flags --------------------------------------------------------------
ARCH="-mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 -mfloat-abi=hard"
RESDEF="-DZZQ2_VID_W=$RESX -DZZQ2_VID_H=$RESY -DZZQGL_VID_W=$RESX -DZZQGL_VID_H=$RESY"
RELFLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O2 -ffreestanding \
-ffunction-sections -fdata-sections -w -I$B/gen -I$Z -DZZQ_PRODUCTION \
-DQUAKEGENERIC_RES_X=$RESX -DQUAKEGENERIC_RES_Y=$RESY"
PMAP="-ffile-prefix-map=$E=engine -ffile-prefix-map=$B=build \
-ffile-prefix-map=$A=Core1 -ffile-prefix-map=$Z=zzquake640"
# the engine (quakegeneric + id GL): the release flags, GLQUAKE defined, the GL shim and ZZGL
# headers reachable, newlib stdio redirected to the release file system (zzquake_stdio.h)
# -fcommon: id's GL files and quakegeneric's share tentative definitions (sb_lines, skytexturenum,
# the SGIS pointers), as every 1997 compiler merged them
ENGFLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O2 -ffreestanding -fcommon \
-ffunction-sections -fdata-sections -w -DGLQUAKE -DZZQ_PRODUCTION $PMAP \
-DQUAKEGENERIC_RES_X=$RESX -DQUAKEGENERIC_RES_Y=$RESY $RESDEF \
-I$E -I$B/gen -I$Z -I$A -I$T -I$T/shim"
# librixgpu and ZZGL: EXACTLY the engine's FPU and ABI (VFPv3-d16, hard float), never NEON:
# the release's Core1 stack is 8-byte misaligned at entry (0x05ffff4c) and GCC's NEON code
# faults on it (ZZQuake2RIX, 14/10).  -fno-tree-vectorize as a second guard.
RIXFLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O2 -ffreestanding -fno-builtin \
-fno-tree-vectorize -fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables \
-ffunction-sections -fdata-sections -Wall -Wno-misleading-indentation -Wno-unused-function \
-Wno-missing-braces $PMAP -I$LR -I$ZG"
# the adapter files of this port, -Wall clean
ADFLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O2 -ffreestanding -Wno-unknown-pragmas \
-ffunction-sections -fdata-sections -Wall -Wno-unused-function $PMAP $RESDEF -I$A -I$Z -I$T -I$T/shim -I$LR -I$ZG -I$B/gen"

echo "== safe_mem.c: -O0, no builtins, linked FIRST (release rule) =="
"$AGCC" $ARCH -mno-unaligned-access -fno-short-enums -O0 -ffreestanding \
    -fno-builtin-memcpy -fno-builtin-memmove -fno-builtin-memset \
    -c "$Z/safe_mem.c" -o "$B/safe_mem.o"

echo "== release objects (compiled from ZZQUAKE_RELEASE, unmodified) =="
"$AGCC" $RELFLAGS -c "$Z/zzquake_entry.S" -o "$B/zzquake_entry.o"
"$AGCC" $RELFLAGS -c "$Z/zzquake_abort.S" -o "$B/zzquake_abort.o"
for f in zzquake_main zzquake_fs_mem zzquake_mmu zzquake_syscalls zzquake_a1_fstest zzquake_platform; do
    "$AGCC" $RELFLAGS -c "$Z/$f.c" -o "$B/$f.o"
done
# the release sound driver (SNDDMA_* on the PCM ring): an engine object (quakedef.h), as the
# 640 release build compiled it
"$AGCC" $ENGFLAGS -include "$Z/zzquake_stdio.h" -c "$Z/snd_zz9000.c" -o "$B/q1obj/snd_zz9000.o"

echo "== fork point and presentation (blob .bss: used before .glbss is cleared) =="
"$AGCC" $ADFLAGS -I"$E" -include "$Z/zzquake_stdio.h" -DGLQUAKE -c "$A/zzqgl_qgmain.c" -o "$B/zzqgl_qgmain.o"
"$AGCC" $ADFLAGS -c "$A/zzqgl_present.c" -o "$B/zzqgl_present.o"

echo "== GL binding, video layer, GPU adapter (q1obj: BSS in .glbss) =="
"$AGCC" $ADFLAGS -I"$E" -include "$Z/zzquake_stdio.h" -DGLQUAKE -c "$A/gl_vid_zz.c" -o "$B/q1obj/gl_vid_zz.o"
"$AGCC" $ADFLAGS -c "$A/zzqgl_gl.c" -o "$B/q1obj/zzqgl_gl.o"
"$AGCC" $RIXFLAGS $RESDEF -I"$T" -I"$A" -c "$T/zzq2rix_gpu.c" -o "$B/q1obj/zzq2rix_gpu.o"
"$AGCC" $RIXFLAGS $RESDEF -I"$T" -c "$T/zzq2rix_mmu.c" -o "$B/q1obj/zzq2rix_mmu.o"

echo "== librixgpu 1.4.0 and ZZGL 0.7.7 objects (VFP hard float, no NEON) =="
n=0
for f in rixgpu rixframe rixtex rixdesc rixclip; do
    "$AGCC" $RIXFLAGS -c "$LR/$f.c" -o "$B/q1obj/rix_$f.o"
    n=$((n + 1))
done
"$AGCC" $RIXFLAGS -c "$ZG/zzgl.c" -o "$B/q1obj/zzgl.o"
n=$((n + 1))
echo "  $n objects"

echo "== engine objects (forced include zzquake_stdio.h, -DGLQUAKE) =="
n=0
for f in "$E"/*.c; do
    b=$(basename "$f" .c)
    "$AGCC" $ENGFLAGS -include "$Z/zzquake_stdio.h" -c "$f" -o "$B/q1obj/$b.o"
    n=$((n + 1))
done
echo "  $n objects"

# ---- object checks -------------------------------------------------------
echo "== unaligned-access and short-enums checks =="
FAIL=0
for o in "$B"/*.o "$B"/q1obj/*.o; do
    if "$AREADELF" -A "$o" 2>/dev/null | grep -q "Tag_CPU_unaligned_access: v6"; then
        echo "ERROR: $o was built WITHOUT -mno-unaligned-access"; FAIL=1; fi
    if "$AREADELF" -A "$o" 2>/dev/null | grep -q "Tag_ABI_enum_size: small"; then
        echo "ERROR: $o was built with short enums"; FAIL=1; fi
done
[ $FAIL = 0 ] && echo "PASS unaligned, enums" || exit 1

echo "== stdio / exit redirection check (engine, GL, GPU objects) =="
FAIL=0
for o in "$B"/q1obj/*.o "$B/zzqgl_qgmain.o" "$B/zzqgl_present.o"; do
    if "$ANM" "$o" | grep -wE \
"U (fopen|fclose|fread|fwrite|fseek|ftell|fgetc|getc|feof|fprintf|fscanf|fflush|printf|remove|rename|fputs|fputc|putc|puts|putchar|fgets|vfprintf|vprintf|perror)"
    then echo "ERROR: $o calls newlib stdio"; FAIL=1; fi
    if "$ANM" "$o" | grep -wE "U (exit|_exit|_Exit|abort)"
    then echo "ERROR: $o calls exit/abort"; FAIL=1; fi
done
[ $FAIL = 0 ] && echo "PASS redirection" || exit 1

# ---- link ------------------------------------------------------------------
WRAPPED="zzq_publish_stats R_DrawWorld R_DrawBrushModel R_DrawWaterSurfaces R_NewMap Host_Shutdown Sys_SendKeyEvents"
WRAPOPT=""
for s in $WRAPPED; do WRAPOPT="$WRAPOPT -Wl,--wrap=$s"; done
echo "== link (safe_mem.o FIRST, ASSERT 2MB blob, ASSERT .glbss) =="
"$AGCC" $ARCH -nostartfiles -T "$A/zzqgl.ld" \
    -Wl,--gc-sections -Wl,-Map="$B/$NAME.map" \
    $WRAPOPT \
    "$B/safe_mem.o" $(ls "$B"/*.o | grep -v safe_mem) \
    $(ls "$B"/q1obj/*.o) \
    -lgcc -lc -lm -o "$B/$NAME.elf" 2> "$B/link.log" || {
        grep -v "variable-size enums" "$B/link.log"; fail "link failed"; }
grep -v "variable-size enums" "$B/link.log" || true

echo "== interposers: the engine calls go through the adapter =="
"$AOBJDUMP" -d "$B/$NAME.elf" > "$B/dis.txt"
for s in $WRAPPED; do
    grep -Eq "[[:space:]]bl?[[:space:]].*<__wrap_$s>" "$B/dis.txt" || \
        fail "calls to $s are not interposed"
    grep -Eq "[[:space:]]bl?[[:space:]].*<$s>" "$B/dis.txt" || \
        fail "__wrap_$s does not reach the real $s"
done
echo "PASS interposers ($(echo $WRAPPED | wc -w) functions)"

echo "== NEON check: no vld1/vst1/q-register instruction in the blob =="
if grep -E "^\s*[0-9a-f]+:\s+[0-9a-f]+\s+v(ld[1-4]|st[1-4])\b" "$B/dis.txt" | head -3 | grep -q .; then
    grep -E "^\s*[0-9a-f]+:\s+[0-9a-f]+\s+v(ld[1-4]|st[1-4])\b" "$B/dis.txt" | head -5
    fail "NEON load/store found (the Core1 stack is not 8-byte aligned)"
fi
if grep -E "\bq([0-9]|1[0-5])\b" "$B/dis.txt" | grep -vE "<[^>]*>:" | head -1 | grep -q .; then
    grep -E "\bq([0-9]|1[0-5])\b" "$B/dis.txt" | grep -vE "<[^>]*>:" | head -5
    fail "NEON q-register instruction found"
fi
echo "PASS no NEON"

"$AOBJCOPY" -O binary "$B/$NAME.elf" "$B/$NAME.bin"

echo "== mem* override check (no newlib mem* in the link) =="
if grep -qE "libc_a-(memcpy|memset|memmove)" "$B/$NAME.map"; then
    fail "newlib mem* linked - is safe_mem.o first?"
fi
echo "PASS memstubs"

echo "== entry check: zzq_entry at 0x04900000, VFP enabled in asm, no stack =="
grep -q "^04900000 <zzq_entry>:" "$B/dis.txt" || fail "zzq_entry is not at 0x04900000"
sed -n '/<zzq_entry>:/,/vmsr/p' "$B/dis.txt" > "$B/entry_head.txt"
if grep -E "\\bv(push|pop|ldr|str|add|sub|mul|div|cvt|cmp|mov)" "$B/entry_head.txt"; then
    fail "VFP instruction before vmsr fpexc"
fi
if grep -E "\\b(push|pop)\\b|, *sp\\b.*;|mov[[:space:]]+sp," "$B/entry_head.txt"; then
    fail "zzq_entry uses the stack"
fi
echo "PASS entry"

echo "== layout and footprint =="
$PY -I "$A/zzqgl_footprint.py" "$B/$NAME.elf" "$B/$NAME.map" "$B/$NAME.bin" "$ANM" \
    > "$B/footprint.txt" || { cat "$B/footprint.txt"; fail "layout check failed"; }
cat "$B/footprint.txt"

echo "== ASCII check of the sources of this port =="
if LC_ALL=C grep -l -P '[^\x00-\x7F]' "$A"/*.c "$A"/*.h "$A"/*.py "$A"/*.ld "$T"/*.c "$T"/*.h "$T"/shim/GL/*.h "$ZG"/*.c "$ZG"/*.h "$LR"/*.c "$LR"/*.h build_arm.sh 2>/dev/null | grep -q .; then
    LC_ALL=C grep -l -P '[^\x00-\x7F]' "$A"/*.c "$A"/*.h "$A"/*.py "$A"/*.ld "$T"/*.c "$T"/*.h "$T"/shim/GL/*.h "$ZG"/*.c "$ZG"/*.h "$LR"/*.c "$LR"/*.h build_arm.sh
    fail "non-ASCII byte in a source file"
fi
echo "PASS ascii"

cp "$B/$NAME.bin" "$OUT/$NAME.bin"
cp "$B/footprint.txt" "$OUT/${NAME}_footprint.txt"
echo "== output =="
ls -l "$OUT/$NAME.bin"
md5sum "$OUT/$NAME.bin"
sha256sum "$OUT/$NAME.bin"
case "$RESX" in 320) OPT=" 320" ;; 800) OPT=" 800" ;; 1024) OPT=" 1024" ;; *) OPT="" ;; esac
echo "DONE: copy out/$NAME.bin next to the ZZQuakeGL launcher on the Amiga,"
echo "      then: ZZQuakeGL$OPT  (G4C bitstream, GPU idle)"
