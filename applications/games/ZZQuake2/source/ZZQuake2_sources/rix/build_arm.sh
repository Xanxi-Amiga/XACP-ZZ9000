#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# build_arm.sh - ZZQuake2RIX, Core1 blob (Quake II, ref_gl on ZZGL 0.7.6 /
# librixgpu 1.4.0: the RasterIX GPU of the ZZ9000 G4C bitstream).
# From ZZQuake2TGL 0.4's build_arm.sh: TinyGL replaced by librixgpu/ and
# zzgl/ (compiled here, hard-float like the engine, VFP only: no NEON,
# see RIXFLAGS), Core1/tgl/ replaced by Core1/rix/.
#
#   ZZQUAKE_RELEASE=/path/to/ZZQuake640_SOURCES_launcher_and_Core1 \
#   Q2=/path/to/quake2generic \
#   sh build_arm.sh
#
# ZZQUAKE_RELEASE : the ZZQuake640 release source bundle (the folder
#                   that holds Core1_GPL/ and launcher_68k_PRIVATE/).
# Q2              : ozkl/quake2generic at commit
#                   84f34ed5382323d96b81a44a44f73496791084a3
# ARMBIN          : folder of arm-none-eabi-gcc. Default: the MSYS
#                   install "C:/ArmGNUToolchain/13.2 Rel1/bin" when it
#                   exists, else the PATH. PATH is never changed.
#
# ZZQ2_RES (optional, 0.10): size of the screen the blob draws for, as
# in ZZQuake2 A0 0.7.1; it must match the launcher option:
#   (not set) or 640   640 x 480, gl_mode 3   out/zzq2rix.bin
#   320                320 x 240, gl_mode 0   out/zzq2rix_320.bin  (option 320)
#   800                800 x 600, gl_mode 4   out/zzq2rix_800.bin  (option 800)
# The release objects get QUAKEGENERIC_RES_X / _RES_Y of that size, the
# adapter gets it through Core1/zzq2_res.h.
# 0.10: music (Core1/zzq2_cd.c, replaces null/cd_null: MUSIC=CD, MP3 or
# MHI of the launcher) and save games (Core1/zzq2_save.c, zzq2_lz.c,
# patched copy of game/g_save.c), from ZZQuake2 A0 0.7.1 unchanged.
# Nothing outside this folder is modified: the release and Quake II
# files are compiled from where they are after their SHA-256 has been
# checked.  Offline: no download of any kind.
# ASCII only.

set -e
export SOURCE_DATE_EPOCH=1
export LC_ALL=C

# Everything is relative to this folder, as in the ZZGear/ZZDoom builds:
# no path conversion of any kind (MSYS build of 14/10: cygpath and
# pwd -W both failed on the Windows side of this machine).
cd "$(dirname "$0")"
A="Core1"
T="Core1/rix"
LR="librixgpu"
ZG="zzgl"
OUT="out"
B="build_rix"
NAME="zzq2rix"
RESD=""
case "${ZZQ2_RES:-640}" in
    640) RESX=640; RESY=480 ;;
    320) RESX=320; RESY=240; NAME="zzq2rix_320"; B="build_rix_320"
         RESD='-DZZQ2_VID_W=320 -DZZQ2_VID_H=240 -DZZQ2_SW_MODE=0 -DZZQ2_SW_MODE_S="0"' ;;
    800) RESX=800; RESY=600; NAME="zzq2rix_800"; B="build_rix_800"
         RESD='-DZZQ2_VID_W=800 -DZZQ2_VID_H=600 -DZZQ2_SW_MODE=4 -DZZQ2_SW_MODE_S="4"' ;;
    *)   echo "ERROR: ZZQ2_RES: '$ZZQ2_RES' (640, 320 or 800)"; exit 1 ;;
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
    AREADELF="$ARMBIN/arm-none-eabi-readelf$X"
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

echo "== blob: $NAME (${RESX}x${RESY}, RasterIX) =="

# ---- the two source trees, RELATIVE to this folder ---------------------
#   ZZQUAKE_RELEASE  default ../ZZQuake640_SOURCES_launcher_and_Core1
#   Q2               default ../quake2generic
[ -n "$ZZQUAKE_RELEASE" ] || ZZQUAKE_RELEASE="../ZZQuake640_SOURCES_launcher_and_Core1"
[ -n "$Q2" ] || Q2="../quake2generic"
[ -d "$ZZQUAKE_RELEASE" ] || fail "ZZQUAKE_RELEASE: no such directory: $ZZQUAKE_RELEASE"
[ -d "$Q2" ] || fail "Q2: no such directory: $Q2"

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
[ -n "$QG" ] || fail "quakegeneric.h (Quake I tree of the release) not found"
echo "  release Core1 : $Z"
echo "  release QG    : $QG"
echo "  quake2generic : $Q2"

# SHA-256 lists are relative to their tree: prefix them, check from here
shacheck() {   # shacheck <list> <tree> <what>
    sed "s#^\([0-9a-f]*\)  #\1  $2/#" "$1" > "$B/check.sha"
    sha256sum -c --quiet "$B/check.sha" || fail "$3 differs from the expected sources - nothing built"
}
rm -rf "$B"
mkdir -p "$B/gen" "$B/q2obj" "$OUT"
echo "== release files: SHA-256 check =="
shacheck "$A/release_core1.sha256" "$Z" "a release Core1 file"
shacheck "$A/release_qg.sha256" "$QG" "a release quakegeneric header"
echo "PASS release"
echo "== quake2generic: SHA-256 check (commit 84f34ed5, ref_gl included) =="
shacheck "$A/q2_sources_rix.sha256" "$Q2" "quake2generic (commit 84f34ed5; CRLF checkout? other commit?)"
echo "PASS quake2generic ($(wc -l < "$A/q2_sources_rix.sha256" | tr -d ' ') files)"
echo "== librixgpu / zzgl: SHA-256 check =="
sha256sum -c --quiet "$T/rix_vendored.sha256" || fail "librixgpu/ or zzgl/ differs from the vendored tree (ZZGear D2g) - nothing built"
echo "PASS librixgpu/zzgl ($(wc -l < "$T/rix_vendored.sha256" | tr -d ' ') files)"

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
-DQUAKEGENERIC_RES_X=$RESX -DQUAKEGENERIC_RES_Y=$RESY"
PMAP="-ffile-prefix-map=$Q2=quake2generic -ffile-prefix-map=$B=build \
-ffile-prefix-map=$A=Core1 -ffile-prefix-map=$Z=zzquake640"
Q2FLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O3 -ffreestanding \
-ffunction-sections -fdata-sections -w -fno-strict-aliasing -fwrapv \
-Dstricmp=strcasecmp $PMAP -I$A -I$Z"
GLINC="-I$T/shim -I$LR -I$ZG -I$Q2"
# librixgpu and ZZGL: EXACTLY the engine's FPU and ABI (VFPv3-d16, hard
# float), never NEON: the release's Core1 stack is 8-byte misaligned at
# entry (0x05ffff4c) and GCC's NEON code faults on it (run of 14/10,
# DFSR 0x801).  -fno-tree-vectorize as a second guard.  The library's
# NEON byte swap compiles to its word fallback (never called here).
RIXFLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O2 -ffreestanding -fno-builtin \
-fno-tree-vectorize -fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables \
-ffunction-sections -fdata-sections -Wall -Wno-misleading-indentation -Wno-unused-function \
-Wno-missing-braces $PMAP -I$LR -I$ZG"
ADFLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O2 -ffreestanding \
-ffunction-sections -fdata-sections -w $PMAP -I$A -I$Z"
# RESD (the size macros of zzq2_res.h) goes unquoted on the adapter and GL backend command lines,
# as in ZZQuake2 A0: the shell splits it at the spaces and leaves the quotes of "0" / "4" in place

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
"$AGCC" $ADFLAGS $RESD -DZZQ2_RIX -c "$A/zzq2_glue.c" -o "$B/zzq2_glue.o"
"$AGCC" $ADFLAGS -c "$A/zzq2_mem.c"  -o "$B/zzq2_mem.o"
# 0.8: sound driver of ZZQuake2 A0 0.4+ on the release PCM ring (replaces sound/snddma_null);
# without NOSOUND the launcher's AHI process plays what Quake II mixes here
"$AGCC" $ADFLAGS -fno-strict-aliasing -fwrapv -Dstricmp=strcasecmp \
    -I"$Q2" -include "$A/zzq2_stdio.h" -c "$A/zzq2_snd.c" -o "$B/zzq2_snd.o"
# 0.10: music commands on the release music queue (A0 0.5, replaces null/cd_null): the launcher plays
# them with MUSIC=CD, MP3 or MHI
"$AGCC" $ADFLAGS -fno-strict-aliasing -fwrapv -Dstricmp=strcasecmp \
    -I"$Q2" -include "$A/zzq2_stdio.h" -c "$A/zzq2_cd.c" -o "$B/zzq2_cd.o"
# 0.10: save games on the release file service (A0 0.6/0.7: flat names, listing, delete, version
# stamp, compression)
"$AGCC" $ADFLAGS -fno-strict-aliasing -fwrapv -Dstricmp=strcasecmp \
    -I"$Q2" -include "$A/zzq2_stdio.h" -c "$A/zzq2_save.c" -o "$B/zzq2_save.o"
"$AGCC" $ADFLAGS -fno-strict-aliasing -fwrapv -c "$A/zzq2_lz.c" -o "$B/zzq2_lz.o"
# 0.10.1: functions and monster moves of the saves, by name (lookups), as ZZQuake2 A0 0.8
"$AGCC" $ADFLAGS -c "$A/zzq2_svtab.c" -o "$B/zzq2_svtab.o"
# Quake II side of the GL adapter: compiled like the engine (q2obj, so
# their BSS goes to .q2bss), with the GL header shim.
"$AGCC" $Q2FLAGS $RESD $GLINC -I"$T" -include "$A/zzq2_stdio.h" -c "$T/zzq2rix_video.c" -o "$B/q2obj/zzq2rix_video.o"
"$AGCC" $Q2FLAGS $GLINC -I"$T" -include "$A/zzq2_stdio.h" -c "$T/zzq2rix_qgl.c"   -o "$B/q2obj/zzq2rix_qgl.o"
# GPU side (no Quake II header): librixgpu's ABI, BSS in .q2bss
"$AGCC" $RIXFLAGS $RESD -I"$T" -I"$A" -c "$T/zzq2rix_gpu.c" -o "$B/q2obj/zzq2rix_gpu.o"
"$AGCC" $RIXFLAGS -I"$T" -c "$T/zzq2rix_mmu.c" -o "$B/q2obj/zzq2rix_mmu.o"

echo "== librixgpu 1.4.0 and ZZGL 0.7.6 objects (ZZGear D2g sources, VFP hard float, no NEON) =="
n=0
for f in rixgpu rixframe rixtex rixdesc rixclip; do
    "$AGCC" $RIXFLAGS -c "$LR/$f.c" -o "$B/q2obj/rix_$f.o"
    n=$((n + 1))
done
"$AGCC" $RIXFLAGS -c "$ZG/zzgl.c" -o "$B/q2obj/zzgl.o"
n=$((n + 1))
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
echo "== sound mixer (client/snd_mix.c, patched copy: 3.21 volume fix, as ZZQuake2 A0 0.4) =="
$PY "$A/zzq2_patch_sound.py" "$Q2" "$B/gen/snd_mix_zz.c"
echo "== game saves (game/g_save.c, patched copy: functions and moves by name, as ZZQuake2 A0 0.8) =="
$PY "$A/zzq2_patch_save.py" "$Q2" "$B/gen/g_save_zz.c"
echo "== Video menu (other/vid_menu.c, patched copy: one renderer, one screen size, as ZZQuake2 A0 0.8.5) =="
$PY "$A/zzq2_patch_vidmenu.py" "$Q2" "$B/gen/vid_menu_zz.c"
echo "== ref_gl: gl_image.c and gl_draw.c, patched copies (stack buffers made static) =="
$PY "$T/zzq2rix_patch_refgl.py" "$Q2" "$B/gen"
n=0
for s in $Q2SRC; do
    o=$(echo "$s" | tr '/' '_')
    case "$s" in
        ref_gl/gl_image|ref_gl/gl_draw)
            # same object names, the patched copy in place of the original
            "$AGCC" $Q2FLAGS $GLINC -I"$Q2/ref_gl" -include "$A/zzq2_stdio.h" \
                -c "$B/gen/$(basename "$s")_zz.c" -o "$B/q2obj/$o.o" ;;
        client/snd_mix)
            "$AGCC" $Q2FLAGS -I"$Q2/client" -include "$A/zzq2_stdio.h" \
                -c "$B/gen/snd_mix_zz.c" -o "$B/q2obj/$o.o" ;;
        other/vid_menu)
            "$AGCC" $Q2FLAGS -I"$Q2/other" -include "$A/zzq2_stdio.h" \
                -c "$B/gen/vid_menu_zz.c" -o "$B/q2obj/$o.o" ;;
        game/g_save)
            "$AGCC" $Q2FLAGS -I"$Q2/game" -include "$A/zzq2_stdio.h" \
                -c "$B/gen/g_save_zz.c" -o "$B/q2obj/$o.o" ;;
        ref_gl/*) "$AGCC" $Q2FLAGS $GLINC -include "$A/zzq2_stdio.h" -c "$Q2/$s.c" -o "$B/q2obj/$o.o" ;;
        *)        "$AGCC" $Q2FLAGS -include "$A/zzq2_stdio.h" -c "$Q2/$s.c" -o "$B/q2obj/$o.o" ;;
    esac
    n=$((n + 1))
done
"$AGCC" $Q2FLAGS -I"$Q2/other" -include "$A/zzq2_stdio.h" \
    -c "$B/gen/q_system_zz.c" -o "$B/q2obj/other_q_system_zz.o"
echo "  $((n + 1)) objects"

# 0.10.1: name tables of the saves (Core1/zzq2_svtab.py, as ZZQuake2 A0 0.8
# and ZZQuake2TGL 0.6), made from the game objects just built. Objects
# with static functions are compiled again from a wrapper that includes
# the unchanged source and adds a global alias of them.
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
case "$NAME" in
    zzq2rix_320) OPT=" 320" ;;
    zzq2rix_800) OPT=" 800" ;;
    *)           OPT="" ;;
esac
echo "      then: ZZQuake2_A0 $NAME.bin$OPT   (G4C bitstream, GPU idle: power cycle after any RIX fault)"
