#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# build_arm.sh - ZZQuake2 A0, Core1 blob (zzq2a0.bin).
#
#   ZZQUAKE_RELEASE=/path/to/ZZQuake640_SOURCES_launcher_and_Core1 \
#   Q2=/path/to/quake2generic \
#   sh build_arm.sh
#
# ZZQUAKE_RELEASE : the ZZQuake640 release source bundle (the folder
#                   that holds Core1_GPL/ and launcher_68k_PRIVATE/).
# Q2              : ozkl/quake2generic at commit
#                   84f34ed5382323d96b81a44a44f73496791084a3
#
# Neither tree is modified. Release files are compiled straight from
# ZZQUAKE_RELEASE after their SHA-256 has been checked; Quake II files
# are compiled straight from Q2 after theirs has been checked.
#
# Toolchain: arm-none-eabi-gcc 13.2.1 (same as the release), python3.
# ASCII only.

set -e
export SOURCE_DATE_EPOCH=1
export LC_ALL=C

HERE="$(cd "$(dirname "$0")" && pwd)"
A="$HERE/Core1"
OUT="$HERE/out"

fail() { echo "ERROR: $*"; exit 1; }

# Quake II objects are compiled -O3 since 0.3.3 (measured on the
# hardware in 0.3.2: -1.3 % per frame, every phase of the renderer).
# Renderer optimisations are applied to COPIES of the engine files by
# Core1/zzq2_patch_render.py (0.3.3: D_DrawSpans16).
#
# ZZQ2_VARIANT (optional). A variant is ONE change against the
# reference blob, built next to it under its own name and never in its
# place. Used to compare on the hardware, one run per blob:
#   (not set)  0.4: -O3 + D_DrawSpans16 cachewidth local + model span
#              invariants (= 0.3.3 zzq2a0_alias.bin) + sound driver
#              (Core1/zzq2_snd.c) + R_DrawPoly (0.4.1) + music (0.5,
#              Core1/zzq2_cd.c) + save games (0.6, Core1/zzq2_save.c)
#                                                         out/zzq2a0.bin
# The 0.3.3 variants unroll and o3ref were measured and dropped
# (README_A0.txt); the patcher still has --unroll.
Q2OPT="-O3"
ADX=""
RPATCH=1
RPOPT=""
APATCH=1
case "${ZZQ2_VARIANT:-}" in
    "")     NAME="zzq2a0";        B="$HERE/build_arm" ;;
    *)      fail "ZZQ2_VARIANT: unknown variant '$ZZQ2_VARIANT' (none in 0.4)" ;;
esac
# ZZQ2_RES (optional, 0.7). Size of the screen the blob draws for;
# it must match the launcher (ZZQuake2_A0 opens 640x480, or 320x240 /
# 800x600 when started with the option 320 / 800):
#   (not set) or 640   640 x 480, sw_mode 3        out/zzq2a0.bin
#   320                320 x 240, sw_mode 0        out/zzq2_320.bin
#   800                800 x 600, sw_mode 4        out/zzq2_800.bin
# The release objects are compiled with QUAKEGENERIC_RES_X / _RES_Y of
# that size (320 x 240 are the values quakegeneric.h has when nothing
# is passed, the size of the ZZQuake 1.0 release); the adapter gets the
# same size through Core1/zzq2_res.h. Nothing else changes.
RESD=""
case "${ZZQ2_RES:-640}" in
    640)    RESX=640; RESY=480 ;;
    320)    RESX=320; RESY=240
            RESD='-DZZQ2_VID_W=320 -DZZQ2_VID_H=240 -DZZQ2_SW_MODE=0 -DZZQ2_SW_MODE_S="0"'
            [ -z "${ZZQ2_VARIANT:-}" ] || fail "ZZQ2_RES=320 is built without a variant"
            NAME="zzq2_320"; B="$HERE/build_arm_320" ;;
    800)    RESX=800; RESY=600
            RESD='-DZZQ2_VID_W=800 -DZZQ2_VID_H=600 -DZZQ2_SW_MODE=4 -DZZQ2_SW_MODE_S="4"'
            [ -z "${ZZQ2_VARIANT:-}" ] || fail "ZZQ2_RES=800 is built without a variant"
            NAME="zzq2_800"; B="$HERE/build_arm_800" ;;
    *)      fail "ZZQ2_RES: '$ZZQ2_RES' (640, 320 or 800)" ;;
esac

echo "== blob: $NAME, ${RESX}x${RESY} (Quake II objects $Q2OPT, renderer patches $([ $RPATCH = 1 ] && echo ON || echo OFF)) =="

[ -n "$ZZQUAKE_RELEASE" ] || fail "ZZQUAKE_RELEASE is not set"
[ -n "$Q2" ]              || fail "Q2 is not set"
[ -d "$ZZQUAKE_RELEASE" ] || fail "ZZQUAKE_RELEASE: no such directory: $ZZQUAKE_RELEASE"
[ -d "$Q2" ]              || fail "Q2: no such directory: $Q2"
ZZQUAKE_RELEASE="$(cd "$ZZQUAKE_RELEASE" && pwd)"
Q2="$(cd "$Q2" && pwd)"

for t in arm-none-eabi-gcc arm-none-eabi-objcopy arm-none-eabi-nm \
         arm-none-eabi-readelf arm-none-eabi-objdump arm-none-eabi-size \
         python3 sha256sum md5sum; do
    command -v "$t" >/dev/null 2>&1 || fail "$t not found in PATH"
done

# ---- locate the release Core1 tree (several bundle layouts) ---------
Z=""
for c in "$ZZQUAKE_RELEASE/Core1_GPL/640/zz9000" \
         "$ZZQUAKE_RELEASE/640/zz9000" \
         "$ZZQUAKE_RELEASE"; do
    if [ -f "$c/zzquake_main.c" ]; then Z="$c"; break; fi
done
[ -n "$Z" ] || fail "zzquake_main.c not found under $ZZQUAKE_RELEASE"
QG="$(cd "$Z/../.." 2>/dev/null && pwd)/quakegeneric_source"
[ -f "$QG/quakegeneric.h" ] || fail "quakegeneric_source/quakegeneric.h not found (expected $QG)"

echo "== ZZQuake2 A0 / Core1 =="
echo "  release Core1 : $Z"
echo "  release QG    : $QG"
echo "  quake2generic : $Q2"
arm-none-eabi-gcc --version | head -1 | sed 's/^/  compiler      : /'

# ---------------------------------------------------------------------
# 1. The release files must be EXACTLY the ZZQuake640 release.
# ---------------------------------------------------------------------
echo "== release files: SHA-256 check =="
( cd "$Z"  && sha256sum -c "$A/release_core1.sha256" ) || \
    fail "a release Core1 file differs from the ZZQuake640 release - nothing built"
( cd "$QG" && sha256sum -c "$A/release_qg.sha256" ) || \
    fail "a release quakegeneric header differs from the ZZQuake640 release - nothing built"
echo "PASS release (boot, vectors, MMU, main, platform, filesystem: untouched)"

# ---------------------------------------------------------------------
# 2. The Quake II files must be the pinned commit.
# ---------------------------------------------------------------------
echo "== quake2generic: SHA-256 check (commit 84f34ed5) =="
if [ "$Q2_SKIP_VERIFY" = "1" ]; then
    echo "SKIPPED (Q2_SKIP_VERIFY=1) - the result is NOT the audited build"
else
    ( cd "$Q2" && sha256sum -c --quiet "$A/q2_sources.sha256" ) || \
        fail "quake2generic differs from commit 84f34ed5 (CRLF checkout? other commit?) - nothing built"
    echo "PASS quake2generic ($(wc -l < "$A/q2_sources.sha256" | tr -d ' ') files)"
fi

rm -rf "$B"
mkdir -p "$B/gen" "$B/q2obj" "$OUT"

# ---------------------------------------------------------------------
# 3. Headers the release files include from the Quake I tree.
#    quakekeys.h is copied; quakegeneric.h gets the same two guards the
#    release patcher adds (zzq_patch_engine.py), and the result must be
#    the very header the release build compiled against.
# ---------------------------------------------------------------------
cp "$QG/quakekeys.h" "$B/gen/quakekeys.h"
python3 - "$QG/quakegeneric.h" "$B/gen/quakegeneric.h" <<'PYEOF'
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

# ---------------------------------------------------------------------
# 4. Flags. RELFLAGS is the release command line, character for
#    character (build_zzquake.sh with ZZQ_HIRES=1 ZZQ_PROD=1; with
#    ZZQ2_RES=320 or 800 the two sizes are 320 x 240 or 800 x 600).
# ---------------------------------------------------------------------
ARCH="-mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 -mfloat-abi=hard"
RELFLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O2 -ffreestanding \
-ffunction-sections -fdata-sections -w -I$B/gen -I$Z -DZZQ_PRODUCTION \
-DQUAKEGENERIC_RES_X=$RESX -DQUAKEGENERIC_RES_Y=$RESY"

# Quake II objects: same machine flags and -O2. Two additions, both
# standard for id Tech 2 code on a modern GCC (the 1997 source relies
# on type punning and on wrapping signed arithmetic):
#   -fno-strict-aliasing -fwrapv
# -Dstricmp=strcasecmp comes from the quake2generic Makefile.
#
# PMAP keeps the build machine's directory names out of the blob
# (assert() embeds __FILE__): the image is then the same whatever the
# folders are called, and its MD5 can be compared between machines.
PMAP="-ffile-prefix-map=$Q2=quake2generic -ffile-prefix-map=$B=build \
-ffile-prefix-map=$A=Core1 -ffile-prefix-map=$Z=zzquake640"
Q2FLAGS="$ARCH -mno-unaligned-access -fno-short-enums $Q2OPT -ffreestanding \
-ffunction-sections -fdata-sections -w -fno-strict-aliasing -fwrapv \
-Dstricmp=strcasecmp $PMAP -I$A -I$Z"

# Adapter objects (zzq2_*.c): release flags, plus the adapter folder.
ADFLAGS="$ARCH -mno-unaligned-access -fno-short-enums -O2 -ffreestanding \
-ffunction-sections -fdata-sections -w $PMAP -I$A -I$Z $RESD"

echo "== safe_mem.c: -O0, no builtins, linked FIRST (release rule) =="
arm-none-eabi-gcc $ARCH -mno-unaligned-access -fno-short-enums -O0 \
    -ffreestanding \
    -fno-builtin-memcpy -fno-builtin-memmove -fno-builtin-memset \
    -c "$Z/safe_mem.c" -o "$B/safe_mem.o"

echo "== release objects (compiled from ZZQUAKE_RELEASE, unmodified) =="
arm-none-eabi-gcc $RELFLAGS -c "$Z/zzquake_entry.S" -o "$B/zzquake_entry.o"
arm-none-eabi-gcc $RELFLAGS -c "$Z/zzquake_abort.S" -o "$B/zzquake_abort.o"
for f in zzquake_main zzquake_fs_mem zzquake_mmu zzquake_syscalls zzquake_a1_fstest; do
    arm-none-eabi-gcc $RELFLAGS -c "$Z/$f.c" -o "$B/$f.o"
done
# zzquake_platform.c: same file, same flags. Two of its functions have
# a Quake II counterpart with a different contract, so their NAMES are
# moved out of the way on the command line (the source is not edited,
# the two bodies are dropped by --gc-sections):
#   Sys_SendKeyEvents  calls Key_Event(key, down) with two arguments;
#                      Quake II's takes three and has its own
#                      Sys_SendKeyEvents (q_system.c).
#   Sys_Quit           returns to the caller; Quake II's must not.
arm-none-eabi-gcc $RELFLAGS \
    -DSys_SendKeyEvents=zzq1_Sys_SendKeyEvents_unused \
    -DSys_Quit=zzq1_Sys_Quit_unused \
    -c "$Z/zzquake_platform.c" -o "$B/zzquake_platform.o"

echo "== adapter objects (ZZQuake2_A0/Core1) =="
arm-none-eabi-gcc $ADFLAGS $ADX -c "$A/zzq2_glue.c" -o "$B/zzq2_glue.o"
arm-none-eabi-gcc $ADFLAGS -c "$A/zzq2_mem.c"  -o "$B/zzq2_mem.o"
arm-none-eabi-gcc $ADFLAGS -fno-strict-aliasing -fwrapv -Dstricmp=strcasecmp \
    -I"$Q2" -c "$A/zzq2_video.c" -o "$B/zzq2_video.o"
# 0.4: sound driver on the release PCM ring (replaces sound/snddma_null)
arm-none-eabi-gcc $ADFLAGS -fno-strict-aliasing -fwrapv -Dstricmp=strcasecmp \
    -I"$Q2" -include "$A/zzq2_stdio.h" -c "$A/zzq2_snd.c" -o "$B/zzq2_snd.o"
# 0.5: music commands on the release music queue (replaces null/cd_null)
arm-none-eabi-gcc $ADFLAGS -fno-strict-aliasing -fwrapv -Dstricmp=strcasecmp \
    -I"$Q2" -include "$A/zzq2_stdio.h" -c "$A/zzq2_cd.c" -o "$B/zzq2_cd.o"
# 0.6: save games on the release file service (flat names, listing,
# delete, version stamp)
arm-none-eabi-gcc $ADFLAGS -fno-strict-aliasing -fwrapv -Dstricmp=strcasecmp \
    -I"$Q2" -include "$A/zzq2_stdio.h" -c "$A/zzq2_save.c" -o "$B/zzq2_save.o"
# 0.7: compressor of the save files (LZ4 block layout, own code)
arm-none-eabi-gcc $ADFLAGS -fno-strict-aliasing -fwrapv \
    -c "$A/zzq2_lz.c" -o "$B/zzq2_lz.o"
# 0.8: functions and monster moves of the saves, by name (lookups)
arm-none-eabi-gcc $ADFLAGS -c "$A/zzq2_svtab.c" -o "$B/zzq2_svtab.o"

echo "== quake2generic system layer (other/q_system.c, patched copy) =="
python3 "$A/zzq2_patch_q2.py" "$Q2" "$B/gen/q_system_zz.c"

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
ref_soft/r_aclip ref_soft/r_alias ref_soft/r_bsp ref_soft/r_draw
ref_soft/r_edge ref_soft/r_image ref_soft/r_light ref_soft/r_main
ref_soft/r_misc ref_soft/r_model ref_soft/r_part ref_soft/r_poly
ref_soft/r_polyse ref_soft/r_rast ref_soft/r_scan ref_soft/r_sprite
ref_soft/r_surf
"
if [ "$RPATCH" = 1 ]; then
    echo "== renderer optimisations (ref_soft/r_scan.c, patched copy) =="
    if [ "$APATCH" = 1 ]; then
        RPOPT="$RPOPT --alias $B/gen/r_polyse_zz.c"
    fi
    python3 "$A/zzq2_patch_render.py" $RPOPT "$Q2" "$B/gen/r_scan_zz.c"
fi
echo "== sound mixer (client/snd_mix.c, patched copy: 3.21 volume fix) =="
python3 "$A/zzq2_patch_sound.py" "$Q2" "$B/gen/snd_mix_zz.c"
echo "== polygon renderer (ref_soft/r_poly.c, patched copy: R_DrawPoly re-enabled) =="
python3 "$A/zzq2_patch_poly.py" "$Q2" "$B/gen/r_poly_zz.c"
echo "== game saves (game/g_save.c, patched copy: functions and moves by name) =="
python3 "$A/zzq2_patch_save.py" "$Q2" "$B/gen/g_save_zz.c"
echo "== Video menu (other/vid_menu.c, patched copy: one renderer, one screen size) =="
python3 "$A/zzq2_patch_vidmenu.py" "$Q2" "$B/gen/vid_menu_zz.c"
n=0
for s in $Q2SRC; do
    o=$(echo "$s" | tr '/' '_')
    if [ "$s" = "ref_soft/r_poly" ]; then
        arm-none-eabi-gcc $Q2FLAGS -I"$Q2/ref_soft" -include "$A/zzq2_stdio.h" \
            -c "$B/gen/r_poly_zz.c" -o "$B/q2obj/$o.o"
    elif [ "$s" = "other/vid_menu" ]; then
        arm-none-eabi-gcc $Q2FLAGS -I"$Q2/other" -include "$A/zzq2_stdio.h" \
            -c "$B/gen/vid_menu_zz.c" -o "$B/q2obj/$o.o"
    elif [ "$s" = "game/g_save" ]; then
        arm-none-eabi-gcc $Q2FLAGS -I"$Q2/game" -include "$A/zzq2_stdio.h" \
            -c "$B/gen/g_save_zz.c" -o "$B/q2obj/$o.o"
    elif [ "$s" = "client/snd_mix" ]; then
        arm-none-eabi-gcc $Q2FLAGS -I"$Q2/client" -include "$A/zzq2_stdio.h" \
            -c "$B/gen/snd_mix_zz.c" -o "$B/q2obj/$o.o"
    elif [ "$RPATCH" = 1 ] && [ "$s" = "ref_soft/r_scan" ]; then
        # same object name, so the link order does not change
        arm-none-eabi-gcc $Q2FLAGS -I"$Q2/ref_soft" -include "$A/zzq2_stdio.h" \
            -c "$B/gen/r_scan_zz.c" -o "$B/q2obj/$o.o"
    elif [ "$APATCH" = 1 ] && [ "$s" = "ref_soft/r_polyse" ]; then
        arm-none-eabi-gcc $Q2FLAGS -I"$Q2/ref_soft" -include "$A/zzq2_stdio.h" \
            -c "$B/gen/r_polyse_zz.c" -o "$B/q2obj/$o.o"
    else
        arm-none-eabi-gcc $Q2FLAGS -include "$A/zzq2_stdio.h" \
            -c "$Q2/$s.c" -o "$B/q2obj/$o.o"
    fi
    n=$((n + 1))
done
# the patched system layer keeps its upstream relative includes
arm-none-eabi-gcc $Q2FLAGS -I"$Q2/other" -include "$A/zzq2_stdio.h" \
    -c "$B/gen/q_system_zz.c" -o "$B/q2obj/other_q_system_zz.o"
echo "  $((n + 1)) objects"

# 0.8: name tables of the saves, made from the game objects just built.
# Objects with static functions are compiled again from a wrapper that
# includes the unchanged source and adds a global alias of them.
echo "== save name tables (Core1/zzq2_svtab.py) =="
rm -f "$B"/gen/zzsv_*.c "$B/gen/zzsv_rebuild.txt" "$B/gen/zzq2_svtab_gen.c"
python3 "$A/zzq2_svtab.py" "$Q2" "$B"
while read -r o w; do
    [ -n "$o" ] || continue
    echo "  $o <- $w"
    arm-none-eabi-gcc $Q2FLAGS -I"$Q2/game" -include "$A/zzq2_stdio.h" \
        -c "$B/gen/$w" -o "$B/q2obj/$o"
done < "$B/gen/zzsv_rebuild.txt"
arm-none-eabi-gcc $Q2FLAGS -c "$B/gen/zzq2_svtab_gen.c" -o "$B/q2obj/zz_svtab_gen.o"

# ---------------------------------------------------------------------
# 5. Object checks (same three as the release build, plus malloc).
# ---------------------------------------------------------------------
echo "== unaligned-access check =="
UAFAIL=0
for o in "$B"/*.o "$B"/q2obj/*.o; do
    if arm-none-eabi-readelf -A "$o" 2>/dev/null | \
       grep -q "Tag_CPU_unaligned_access: v6"; then
        echo "ERROR: $o was built WITHOUT -mno-unaligned-access"
        UAFAIL=1
    fi
done
[ $UAFAIL = 0 ] && echo "PASS unaligned" || exit 1

echo "== short-enums check =="
ESFAIL=0
for o in "$B"/*.o "$B"/q2obj/*.o; do
    if arm-none-eabi-readelf -A "$o" 2>/dev/null | \
       grep -q "Tag_ABI_enum_size: small"; then
        echo "ERROR: $o was built with short enums"
        ESFAIL=1
    fi
done
[ $ESFAIL = 0 ] && echo "PASS enums" || exit 1

echo "== stdio / exit / malloc redirection check (Quake II objects) =="
FAIL=0
for o in "$B"/q2obj/*.o; do
    if arm-none-eabi-nm "$o" | grep -wE \
"U (fopen|fclose|fread|fwrite|fseek|ftell|fgetc|getc|feof|fprintf|fscanf|fflush|printf|remove|rename|fputs|fputc|putc|puts|putchar|fgets|vfprintf|vprintf|perror)"
    then echo "ERROR: $o calls newlib stdio"; FAIL=1; fi
    if arm-none-eabi-nm "$o" | grep -wE "U (exit|_exit|_Exit|abort)"
    then echo "ERROR: $o calls exit/abort"; FAIL=1; fi
    if arm-none-eabi-nm "$o" | grep -wE "U (malloc|free|calloc|realloc)"
    then echo "ERROR: $o bypasses zzq2_malloc/zzq2_free"; FAIL=1; fi
done
[ $FAIL = 0 ] && echo "PASS redirection" || exit 1

# ---------------------------------------------------------------------
# 6. Link. safe_mem.o FIRST (release rule), then the platform and
#    adapter objects, then Quake II. ASSERTs in zzq2a0.ld.
# ---------------------------------------------------------------------
#    Engine functions are interposed at link time for the measurements
#    of the adapter (zzq2_glue.c, the __wrap_ functions): two for the
#    frame time split, twelve for the release renderer profiler. The
#    engine sources are not touched.
WRAPPED="SV_Frame SCR_UpdateScreen
R_RenderWorld R_ScanEdges
R_DrawSolidClippedSubmodelPolygons R_DrawSubmodelPolygons
R_AliasDrawModel R_DrawSprite R_DrawParticles
D_CacheSurface D_DrawSpans16 D_DrawZSpans Turbulent8 NonTurbulent8"
WRAPOPT=""
for s in $WRAPPED; do WRAPOPT="$WRAPOPT -Wl,--wrap=$s"; done
echo "== link (safe_mem.o FIRST, ASSERT 2MB blob, ASSERT .q2bss) =="
arm-none-eabi-gcc $ARCH -nostartfiles -T "$A/zzq2a0.ld" \
    -Wl,--gc-sections -Wl,-Map="$B/zzq2a0.map" \
    $WRAPOPT \
    "$B/safe_mem.o" $(ls "$B"/*.o | grep -v safe_mem) \
    $(ls "$B"/q2obj/*.o) \
    -lgcc -lc -lm -o "$B/zzq2a0.elf" 2> "$B/link.log" || {
        grep -v "variable-size enums" "$B/link.log"; fail "link failed"; }
grep -v "variable-size enums" "$B/link.log" || true

echo "== interposers: the engine calls go through the adapter =="
arm-none-eabi-objdump -d "$B/zzq2a0.elf" > "$B/dis.txt"
for s in $WRAPPED; do
    grep -Eq "[[:space:]]bl?[[:space:]].*<__wrap_$s>" "$B/dis.txt" || \
        fail "calls to $s are not interposed"
    grep -Eq "[[:space:]]bl?[[:space:]].*<$s>" "$B/dis.txt" || \
        fail "__wrap_$s does not reach the real $s"
done
echo "PASS interposers ($(echo $WRAPPED | wc -w) functions)"

arm-none-eabi-objcopy -O binary "$B/zzq2a0.elf" "$B/zzq2a0.bin"

echo "== mem* override check (no newlib mem* in the link) =="
if grep -qE "libc_a-(memcpy|memset|memmove)" "$B/zzq2a0.map"; then
    fail "newlib mem* linked - is safe_mem.o first?"
fi
echo "PASS memstubs"

echo "== entry check: zzq_entry at 0x04900000, VFP enabled in asm, no stack =="
arm-none-eabi-objdump -d "$B/zzq2a0.elf" > "$B/dis.txt"
grep -q "^04900000 <zzq_entry>:" "$B/dis.txt" || fail "zzq_entry is not at 0x04900000"
sed -n '/<zzq_entry>:/,/vmsr/p' "$B/dis.txt" > "$B/entry_head.txt"
if grep -E "\\bv(push|pop|ldr|str|add|sub|mul|div|cvt|cmp|mov)" "$B/entry_head.txt"; then
    fail "VFP instruction before vmsr fpexc"
fi
if grep -E "\\b(push|pop)\\b|, *sp\\b.*;|mov[[:space:]]+sp," "$B/entry_head.txt"; then
    fail "zzq_entry uses the stack"
fi
echo "PASS entry"

# ---------------------------------------------------------------------
# 7. Layout proof and footprint, from the ELF and the map.
# ---------------------------------------------------------------------
echo "== layout and footprint =="
python3 "$A/zzq2_footprint.py" "$B/zzq2a0.elf" "$B/zzq2a0.map" "$B/zzq2a0.bin" \
    > "$B/footprint.txt" || { cat "$B/footprint.txt"; fail "layout check failed"; }
cat "$B/footprint.txt"

echo "== ABI VFP =="
arm-none-eabi-readelf -A "$B/zzq2a0.elf" | grep -E "Tag_ABI_VFP_args|Tag_FP_arch"

cp "$B/zzq2a0.bin" "$OUT/$NAME.bin"
cp "$B/footprint.txt" "$OUT/${NAME}_footprint.txt"
echo "== output =="
ls -l "$OUT/$NAME.bin"
md5sum "$OUT/$NAME.bin"
sha256sum "$OUT/$NAME.bin"
echo "DONE: copy out/$NAME.bin next to the ZZQuake2_A0 launcher on the Amiga."
