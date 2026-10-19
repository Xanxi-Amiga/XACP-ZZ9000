#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# build_arm.sh - ZZQuake2TGL, Core1 blob (Quake II, ref_gl on TinyGL).
#
#   ZZQUAKE_RELEASE=/path/to/ZZQuake640_SOURCES_launcher_and_Core1 \
#   Q2=/path/to/quake2generic \
#   ZZQ2TGL_MODE=3 \
#   sh build_arm.sh
#
# ZZQUAKE_RELEASE : the ZZQuake640 release source bundle (the folder
#                   that holds Core1_GPL/ and launcher_68k_PRIVATE/).
# Q2              : ozkl/quake2generic at commit
#                   84f34ed5382323d96b81a44a44f73496791084a3
# ZZQ2TGL_MODE    : gl_mode rendered by TinyGL, one blob per mode
#                   (shown at 640x480 in every case):
#                     3  640x480   out/zzq2tgl.bin      (default)
#                     1  400x300   out/zzq2tgl_400.bin
#                     0  320x240   out/zzq2tgl_320.bin
# ARMBIN          : folder of arm-none-eabi-gcc. Default: the MSYS
#                   install "C:/ArmGNUToolchain/13.2 Rel1/bin" when it
#                   exists, else the PATH. /usr/bin and /bin are put
#                   first in the PATH for the file utilities.
#
# Nothing outside this folder is modified: the release and Quake II
# files are compiled from where they are after their SHA-256 has been
# checked; TinyGL is patched on a copy (build_tgl*/tinygl).
# Offline: no download of any kind.
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
export SOURCE_DATE_EPOCH=1
export LC_ALL=C

HERE="$(cd "$(dirname "$0")" && pwd)"
A="$HERE/Core1"
T="$A/tgl"
OUT="$HERE/out"

fail() { echo "ERROR: $*"; exit 1; }

# ---- tools -------------------------------------------------------------
if [ -z "$ARMBIN" ] && [ -x "C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-gcc.exe" ]; then
    ARMBIN="C:/ArmGNUToolchain/13.2 Rel1/bin"
fi
if [ -n "$ARMBIN" ]; then
    AGCC="$ARMBIN/arm-none-eabi-gcc"; ANM="$ARMBIN/arm-none-eabi-nm"
    AOBJDUMP="$ARMBIN/arm-none-eabi-objdump"; AOBJCOPY="$ARMBIN/arm-none-eabi-objcopy"
    AREADELF="$ARMBIN/arm-none-eabi-readelf"
else
    AGCC=arm-none-eabi-gcc; ANM=arm-none-eabi-nm; AOBJDUMP=arm-none-eabi-objdump
    AOBJCOPY=arm-none-eabi-objcopy; AREADELF=arm-none-eabi-readelf
fi
"$AGCC" --version >/dev/null 2>&1 || fail "arm-none-eabi-gcc not found (ARMBIN='$ARMBIN')"
if command -v python3 >/dev/null 2>&1 && python3 -c "import sys" >/dev/null 2>&1; then PY=python3
elif command -v python >/dev/null 2>&1; then PY=python
else fail "Python 3 is required"; fi
# The footprint script calls the nm of the toolchain
export ZZQ2_NM="$ANM"

MODE="${ZZQ2TGL_MODE:-3}"
case "$MODE" in
    3) NAME="zzq2tgl";     B="$HERE/build_tgl" ;;
    1) NAME="zzq2tgl_400"; B="$HERE/build_tgl_400" ;;
    0) NAME="zzq2tgl_320"; B="$HERE/build_tgl_320" ;;
    *) fail "ZZQ2TGL_MODE: 3, 1 or 0" ;;
esac
# Rasterization time in the release profiler (step TEX): on by default
# (two Global Timer reads per triangle). ZZQ2TGL_PROF=0 removes it.
RPROF="-DZZQ2TGL_RASTER_PROFILE"
[ "${ZZQ2TGL_PROF:-1}" = 0 ] && RPROF=""
echo "== blob: $NAME (gl_mode $MODE, raster profile $([ -n "$RPROF" ] && echo ON || echo OFF)) =="

[ -n "$ZZQUAKE_RELEASE" ] || fail "ZZQUAKE_RELEASE is not set"
[ -n "$Q2" ] || fail "Q2 is not set"
[ -d "$ZZQUAKE_RELEASE" ] || fail "ZZQUAKE_RELEASE: no such directory: $ZZQUAKE_RELEASE"
[ -d "$Q2" ] || fail "Q2: no such directory: $Q2"
ZZQUAKE_RELEASE="$(cd "$ZZQUAKE_RELEASE" && pwd)"
Q2="$(cd "$Q2" && pwd)"

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
    if [ -f "$c/quakegeneric.h" ]; then QG="$(cd "$c" && pwd)"; break; fi
done
[ -n "$QG" ] || fail "quakegeneric.h (Quake I tree of the release) not found"
# MSYS2 on Windows: the compiler is a native Windows program; hand it
# C:/... paths rather than /c/... ones (no effect on Linux).
if command -v cygpath >/dev/null 2>&1; then
    HERE="$(cygpath -m "$HERE")"; A="$HERE/Core1"; T="$A/tgl"; OUT="$HERE/out"
    Z="$(cygpath -m "$Z")"; QG="$(cygpath -m "$QG")"; Q2="$(cygpath -m "$Q2")"
fi
echo "  release Core1 : $Z"
echo "  release QG    : $QG"
echo "  quake2generic : $Q2"

echo "== release files: SHA-256 check =="
( cd "$Z"  && sha256sum -c --quiet "$A/release_core1.sha256" ) || \
    fail "a release Core1 file differs from the ZZQuake640 release - nothing built"
( cd "$QG" && sha256sum -c --quiet "$A/release_qg.sha256" ) || \
    fail "a release quakegeneric header differs from the ZZQuake640 release - nothing built"
echo "PASS release"

echo "== quake2generic: SHA-256 check (commit 84f34ed5, ref_gl included) =="
( cd "$Q2" && sha256sum -c --quiet "$A/q2_sources_tgl.sha256" ) || \
    fail "quake2generic differs from commit 84f34ed5 (CRLF checkout? other commit?) - nothing built"
echo "PASS quake2generic ($(wc -l < "$A/q2_sources_tgl.sha256" | tr -d ' ') files)"

echo "== TinyGL (vendored, tinygl/): SHA-256 check =="
( cd "$HERE/tinygl" && sha256sum -c --quiet "$T/tinygl_vendored.sha256" ) || \
    fail "tinygl/ differs from the vendored tree - nothing built"
echo "PASS TinyGL ($(wc -l < "$T/tinygl_vendored.sha256" | tr -d ' ') files)"

B="$HERE/$(basename "$B")"
rm -rf "$B"
mkdir -p "$B/gen" "$B/q2obj" "$OUT"

echo "== TinyGL: patched copy =="
$PY "$T/zzq2tgl_patch_tinygl.py" "$HERE/tinygl" "$B/tinygl"
TGLI="-I$B/tinygl/include -I$B/tinygl/src"

# ---- headers of the release (same as ZZQuake2 A0) ----------------------
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

# ---- flags --------------------------------------------------------------
ARCH="-mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 -mfloat-abi=hard"
RELFLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O2 -ffreestanding \
-ffunction-sections -fdata-sections -w -I$B/gen -I$Z -DZZQ_PRODUCTION \
-DQUAKEGENERIC_RES_X=640 -DQUAKEGENERIC_RES_Y=480"
PMAP="-ffile-prefix-map=$Q2=quake2generic -ffile-prefix-map=$B=build \
-ffile-prefix-map=$A=Core1 -ffile-prefix-map=$Z=zzquake640 \
-ffile-prefix-map=$HERE=ZZQuake2TGL"
Q2FLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O3 -ffreestanding \
-ffunction-sections -fdata-sections -w -fno-strict-aliasing -fwrapv \
-Dstricmp=strcasecmp $PMAP -I$A -I$Z"
GLINC="-I$T/shim $TGLI -I$Q2"
TGLFLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O3 -ffreestanding \
-ffunction-sections -fdata-sections -w -fno-strict-aliasing -fwrapv $PMAP $RPROF $TGLI"
ADFLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O2 -ffreestanding \
-ffunction-sections -fdata-sections -w $PMAP -I$A -I$Z"

echo "== safe_mem.c: -O0, no builtins, linked FIRST (release rule) =="
"$AGCC" $ARCH -mno-unaligned-access -fno-short-enums -O0 -ffreestanding \
    -fno-builtin-memcpy -fno-builtin-memmove -fno-builtin-memset \
    -c "$Z/safe_mem.c" -o "$B/safe_mem.o"

echo "== release objects (compiled from ZZQUAKE_RELEASE, unmodified) =="
"$AGCC" $RELFLAGS -c "$Z/zzquake_entry.S" -o "$B/zzquake_entry.o"
"$AGCC" $RELFLAGS -c "$Z/zzquake_abort.S" -o "$B/zzquake_abort.o"
for f in zzquake_main zzquake_fs_mem zzquake_mmu zzquake_syscalls zzquake_a1_fstest; do
    "$AGCC" $RELFLAGS -c "$Z/$f.c" -o "$B/$f.o"
done
"$AGCC" $RELFLAGS \
    -DSys_SendKeyEvents=zzq1_Sys_SendKeyEvents_unused \
    -DSys_Quit=zzq1_Sys_Quit_unused \
    -c "$Z/zzquake_platform.c" -o "$B/zzquake_platform.o"

echo "== adapter objects =="
# Screen size the blob draws for (Core1/zzq2_res.h, guard in QG_Create):
# 320x240 for gl_mode 0, 640x480 otherwise (400x300 is enlarged).
if [ "$MODE" = 0 ]; then
    RESD='-DZZQ2_VID_W=320 -DZZQ2_VID_H=240 -DZZQ2_SW_MODE=0 -DZZQ2_SW_MODE_S="0"'
else
    RESD=""
fi
"$AGCC" $ADFLAGS $RESD -DZZQ2_TGL -DZZQ2TGL_MODE=$MODE -c "$A/zzq2_glue.c" -o "$B/zzq2_glue.o"
"$AGCC" $ADFLAGS -c "$A/zzq2_mem.c"  -o "$B/zzq2_mem.o"
# 0.5: the functions of ZZQuake2 A0 0.7.2, same files:
#   sound driver on the release PCM ring (replaces sound/snddma_null)
"$AGCC" $ADFLAGS -fno-strict-aliasing -fwrapv -Dstricmp=strcasecmp \
    -I"$Q2" -include "$A/zzq2_stdio.h" -c "$A/zzq2_snd.c" -o "$B/zzq2_snd.o"
#   music commands on the release music queue (replaces null/cd_null)
"$AGCC" $ADFLAGS -fno-strict-aliasing -fwrapv -Dstricmp=strcasecmp \
    -I"$Q2" -include "$A/zzq2_stdio.h" -c "$A/zzq2_cd.c" -o "$B/zzq2_cd.o"
#   save games on the release file service, compressed
"$AGCC" $ADFLAGS -fno-strict-aliasing -fwrapv -Dstricmp=strcasecmp \
    -I"$Q2" -include "$A/zzq2_stdio.h" -c "$A/zzq2_save.c" -o "$B/zzq2_save.o"
"$AGCC" $ADFLAGS -fno-strict-aliasing -fwrapv \
    -c "$A/zzq2_lz.c" -o "$B/zzq2_lz.o"
#   (0.6) functions and monster moves of the saves, by name (lookups)
"$AGCC" $ADFLAGS -c "$A/zzq2_svtab.c" -o "$B/zzq2_svtab.o"
# Quake II side of the GL adapter: compiled like the engine (q2obj, so
# their BSS goes to .q2bss), with the GL header shim.
"$AGCC" $Q2FLAGS $GLINC -DZZQ2TGL_MODE=$MODE -include "$A/zzq2_stdio.h" -c "$T/zzq2tgl_video.c" -o "$B/q2obj/zzq2tgl_video.o"
"$AGCC" $Q2FLAGS $GLINC -DZZQ2TGL_MODE=$MODE -include "$A/zzq2_stdio.h" -c "$T/zzq2tgl_qgl.c"   -o "$B/q2obj/zzq2tgl_qgl.o"
# TinyGL side
"$AGCC" $TGLFLAGS -c "$T/zzq2tgl_tgl.c" -o "$B/q2obj/zzq2tgl_tgl.o"

echo "== TinyGL objects (patched copy) =="
n=0
for f in "$B"/tinygl/src/*.c; do
    b=$(basename "$f" .c)
    case "$b" in memory|msghandling) continue ;; esac
    "$AGCC" $TGLFLAGS -c "$f" -o "$B/q2obj/tgl_$b.o"
    n=$((n + 1))
done
echo "  $n objects"

echo "== quake2generic system layer (other/q_system.c, patched copy) =="
$PY "$A/zzq2_patch_q2.py" "$Q2" "$B/gen/q_system_zz.c"

echo "== quake2generic objects (forced include zzq2_stdio.h) =="
Q2SRC="
client/cl_cin client/cl_ents client/cl_fx client/cl_newfx client/cl_input
client/cl_inv client/cl_main client/cl_parse client/cl_pred client/cl_tent
client/cl_scrn client/cl_view client/console client/keys client/menu
client/snd_dma client/snd_mem client/snd_mix client/qmenu
qcommon/cmd qcommon/cmodel qcommon/common qcommon/crc qcommon/cvar
qcommon/files qcommon/md4 qcommon/net_chan qcommon/pmove
server/sv_ccmds server/sv_ents server/sv_game server/sv_init server/sv_main
server/sv_send server/sv_user server/sv_world
other/vid_menu other/vid_lib other/glob
net/net_loopback
game/g_ai game/p_client game/g_cmds game/g_svcmds game/g_combat game/g_func
game/g_items game/g_main game/g_misc game/g_monster game/g_phys game/g_save
game/g_spawn game/g_target game/g_trigger game/g_turret game/g_utils
game/g_weapon game/m_actor game/m_berserk game/m_boss2 game/m_boss3
game/m_boss31 game/m_boss32 game/m_brain game/m_chick game/m_flipper
game/m_float game/m_flyer game/m_gladiator game/m_gunner game/m_hover
game/m_infantry game/m_insane game/m_medic game/m_move game/m_mutant
game/m_parasite game/m_soldier game/m_supertank game/m_tank game/p_hud
game/p_trail game/p_view game/p_weapon game/q_shared game/g_chase game/m_flash
ref_gl/gl_draw ref_gl/gl_image ref_gl/gl_light ref_gl/gl_mesh ref_gl/gl_model
ref_gl/gl_rmain ref_gl/gl_rmisc ref_gl/gl_rsurf ref_gl/gl_warp
"
echo "== sound mixer (client/snd_mix.c, patched copy: 3.21 volume fix) =="
$PY "$A/zzq2_patch_sound.py" "$Q2" "$B/gen/snd_mix_zz.c"
echo "== game saves (game/g_save.c, patched copy: functions and moves by name) =="
$PY "$A/zzq2_patch_save.py" "$Q2" "$B/gen/g_save_zz.c"
echo "== Video menu (other/vid_menu.c, patched copy: one renderer, one screen size) =="
$PY "$A/zzq2_patch_vidmenu.py" "$Q2" "$B/gen/vid_menu_zz.c"
echo "== ref_gl: gl_image.c and gl_draw.c, patched copies (stack buffers made static) =="
$PY "$T/zzq2tgl_patch_refgl.py" "$Q2" "$B/gen"
n=0
for s in $Q2SRC; do
    o=$(echo "$s" | tr '/' '_')
    case "$s" in
        ref_gl/gl_image|ref_gl/gl_draw)
            # same object names, the patched copy in place of the original
            "$AGCC" $Q2FLAGS $GLINC -I"$Q2/ref_gl" -include "$A/zzq2_stdio.h" \
                -c "$B/gen/$(basename "$s")_zz.c" -o "$B/q2obj/$o.o" ;;
        ref_gl/*) "$AGCC" $Q2FLAGS $GLINC -include "$A/zzq2_stdio.h" -c "$Q2/$s.c" -o "$B/q2obj/$o.o" ;;
        other/vid_menu)
            "$AGCC" $Q2FLAGS -I"$Q2/other" -include "$A/zzq2_stdio.h" \
                -c "$B/gen/vid_menu_zz.c" -o "$B/q2obj/$o.o" ;;
        game/g_save)
            "$AGCC" $Q2FLAGS -I"$Q2/game" -include "$A/zzq2_stdio.h" \
                -c "$B/gen/g_save_zz.c" -o "$B/q2obj/$o.o" ;;
        client/snd_mix)
            "$AGCC" $Q2FLAGS -I"$Q2/client" -include "$A/zzq2_stdio.h" \
                -c "$B/gen/snd_mix_zz.c" -o "$B/q2obj/$o.o" ;;
        *)        "$AGCC" $Q2FLAGS -include "$A/zzq2_stdio.h" -c "$Q2/$s.c" -o "$B/q2obj/$o.o" ;;
    esac
    n=$((n + 1))
done
"$AGCC" $Q2FLAGS -I"$Q2/other" -include "$A/zzq2_stdio.h" \
    -c "$B/gen/q_system_zz.c" -o "$B/q2obj/other_q_system_zz.o"
echo "  $((n + 1)) objects"

# (0.6) name tables of the saves, made from the game objects just built
# (Core1/zzq2_svtab.py). Objects with static functions are compiled
# again from a wrapper that includes the unchanged source and adds a
# global alias of them.
echo "== save name tables (Core1/zzq2_svtab.py) =="
rm -f "$B"/gen/zzsv_*.c "$B/gen/zzsv_rebuild.txt" "$B/gen/zzq2_svtab_gen.c"
ZZQ2_READELF="$AREADELF" $PY "$A/zzq2_svtab.py" "$Q2" "$B"
while read -r o w; do
    [ -n "$o" ] || continue
    echo "  $o <- $w"
    "$AGCC" $Q2FLAGS -I"$Q2/game" -include "$A/zzq2_stdio.h" \
        -c "$B/gen/$w" -o "$B/q2obj/$o"
done < "$B/gen/zzsv_rebuild.txt"
"$AGCC" $Q2FLAGS -c "$B/gen/zzq2_svtab_gen.c" -o "$B/q2obj/zz_svtab_gen.o"

# ---- object checks -------------------------------------------------------
echo "== unaligned-access and short-enums checks =="
FAIL=0
for o in "$B"/*.o "$B"/q2obj/*.o; do
    if "$AREADELF" -A "$o" 2>/dev/null | grep -q "Tag_CPU_unaligned_access: v6"; then
        echo "ERROR: $o was built WITHOUT -mno-unaligned-access"; FAIL=1; fi
    if "$AREADELF" -A "$o" 2>/dev/null | grep -q "Tag_ABI_enum_size: small"; then
        echo "ERROR: $o was built with short enums"; FAIL=1; fi
done
[ $FAIL = 0 ] && echo "PASS unaligned, enums" || exit 1

echo "== stdio / exit / malloc redirection check (Quake II and TinyGL objects) =="
FAIL=0
for o in "$B"/q2obj/*.o; do
    if "$ANM" "$o" | grep -wE \
"U (fopen|fclose|fread|fwrite|fseek|ftell|fgetc|getc|feof|fprintf|fscanf|fflush|printf|remove|rename|fputs|fputc|putc|puts|putchar|fgets|vfprintf|vprintf|perror)"
    then echo "ERROR: $o calls newlib stdio"; FAIL=1; fi
    if "$ANM" "$o" | grep -wE "U (exit|_exit|_Exit|abort)"
    then echo "ERROR: $o calls exit/abort"; FAIL=1; fi
    if "$ANM" "$o" | grep -wE "U (malloc|free|calloc|realloc)"
    then echo "ERROR: $o bypasses zzq2_malloc/zzq2_free"; FAIL=1; fi
done
[ $FAIL = 0 ] && echo "PASS redirection" || exit 1

# ---- link ------------------------------------------------------------------
WRAPPED="SV_Frame SCR_UpdateScreen
R_DrawWorld R_DrawBrushModel R_DrawAliasModel R_DrawAlphaSurfaces
zzq_publish_stats"
WRAPOPT=""
for s in $WRAPPED; do WRAPOPT="$WRAPOPT -Wl,--wrap=$s"; done
echo "== link (safe_mem.o FIRST, ASSERT 2MB blob, ASSERT .q2bss) =="
"$AGCC" $ARCH -nostartfiles -T "$A/zzq2a0.ld" \
    -Wl,--gc-sections -Wl,-Map="$B/zzq2a0.map" \
    $WRAPOPT \
    "$B/safe_mem.o" $(ls "$B"/*.o | grep -v safe_mem) \
    $(ls "$B"/q2obj/*.o) \
    -lgcc -lc -lm -o "$B/zzq2a0.elf" 2> "$B/link.log" || {
        grep -v "variable-size enums" "$B/link.log"; fail "link failed"; }
grep -v "variable-size enums" "$B/link.log" || true

echo "== interposers: the engine calls go through the adapter =="
"$AOBJDUMP" -d "$B/zzq2a0.elf" > "$B/dis.txt"
for s in $WRAPPED; do
    grep -Eq "[[:space:]]bl?[[:space:]].*<__wrap_$s>" "$B/dis.txt" || \
        fail "calls to $s are not interposed"
    grep -Eq "[[:space:]]bl?[[:space:]].*<$s>" "$B/dis.txt" || \
        fail "__wrap_$s does not reach the real $s"
done
echo "PASS interposers ($(echo $WRAPPED | wc -w) functions)"

"$AOBJCOPY" -O binary "$B/zzq2a0.elf" "$B/zzq2a0.bin"

echo "== mem* override check (no newlib mem* in the link) =="
if grep -qE "libc_a-(memcpy|memset|memmove)" "$B/zzq2a0.map"; then
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
$PY "$A/zzq2_footprint.py" "$B/zzq2a0.elf" "$B/zzq2a0.map" "$B/zzq2a0.bin" \
    > "$B/footprint.txt" || { cat "$B/footprint.txt"; fail "layout check failed"; }
cat "$B/footprint.txt"

cp "$B/zzq2a0.bin" "$OUT/$NAME.bin"
cp "$B/footprint.txt" "$OUT/${NAME}_footprint.txt"
echo "== output =="
ls -l "$OUT/$NAME.bin"
md5sum "$OUT/$NAME.bin"
sha256sum "$OUT/$NAME.bin"
echo "DONE: copy out/$NAME.bin next to the ZZQuake2_A0 launcher on the Amiga,"
if [ "$NAME" = zzq2tgl_320 ]; then echo "      then: ZZQuake2_A0 $NAME.bin 320"
else echo "      then: ZZQuake2_A0 $NAME.bin"; fi
