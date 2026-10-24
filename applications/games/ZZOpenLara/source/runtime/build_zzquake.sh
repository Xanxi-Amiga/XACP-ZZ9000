#!/bin/sh
# build_zzquake.sh - ZZQuake Core1 blob build, Build A0 rev 2.

# the forced stdio include on engine sources.
# Usage: ./build_zzquake.sh [path-to-quakegeneric/source]
# Toolchain: arm-none-eabi-gcc 13.2 (quote the path, it has a space:
#   export PATH="/c/ArmGNUToolchain/13.2 Rel1/bin:$PATH")
# ASCII only.

set -e

# Reproducible builds: WinQuake embeds __TIME__/__DATE__ (host.c
# version banner). gcc 13 honors SOURCE_DATE_EPOCH, freezing them so
# two builds of the same sources are bit-identical (MD5 parity between
# sandbox / Windows / any machine).
export SOURCE_DATE_EPOCH=1

Q="${1:-../quakegeneric/source}"
Z="$(pwd)"
B="$Z/build"
mkdir -p "$B" || true
if [ ! -d "$B" ]; then
    echo "ERROR: cannot create $B - create the directory manually:"
    echo "  mkdir build"
    exit 1
fi



EXCL="sys_null quakegeneric quakegeneric_sdl2 quakegeneric_w32 \
quakegeneric_dos quakegeneric_null"
# NOTE: "quakegeneric" (the upstream main) is EXCLUDED and replaced by
# our zzquake_qgmain.c - upstream mallocs 8MB with no NULL check and
# ignores ZZQ_HUNK_SIZE.

echo "== safe_mem.c: -O0, no builtins, linked FIRST (ZZDoom rule) =="
arm-none-eabi-gcc -mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 \
    -mfloat-abi=hard -mno-unaligned-access -fno-short-enums -O0 \
    -ffreestanding $ZZQ_EXTRA \
    -fno-builtin-memcpy -fno-builtin-memmove -fno-builtin-memset \
    -c "$Z/safe_mem.c" -o "$B/safe_mem.o"

# ---- diagnostic patches applied to COPIES of the engine sources.
# The user's quakegeneric tree is never modified: we copy, patch the
# copy, and compile from there. Purely observational except CVARFIX,
# which is gated by a shared slot.
PQ="$B/engine"
rm -rf "$PQ"; mkdir -p "$PQ"
cp "$Q"/*.c "$Q"/*.h "$PQ"/ 2>/dev/null



#





rm -f "$PQ"/snd_null.c
python3 "$Z/zzq_patch_engine.py" "$PQ" || exit 1
Q="$PQ"







# commande, et -w masquait meme l'avertissement de redefinition.
#




#


# rencontre ensuite redefinit la macro.
CFLAGS="-mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 -mfloat-abi=hard \
-mno-unaligned-access -fno-short-enums -O2 -ffreestanding \
-ffunction-sections -fdata-sections -w $ZZQ_EXTRA -I$Q -I$Z"
if [ "$ZZQ_PROD" = "1" ]; then
    CFLAGS="$CFLAGS -DZZQ_PRODUCTION"
    echo "== PRODUCTION: level-2 profiler disabled =="
fi
if [ "$ZZQ_HIRES" = "1" ]; then
    CFLAGS="$CFLAGS -DQUAKEGENERIC_RES_X=640 -DQUAKEGENERIC_RES_Y=480"
    echo "== HIRES : 640x480 =="
fi
echo "== extra flags: ${ZZQ_EXTRA:-<none>} =="


# Ce que le preprocesseur donne a zzquake_platform.c, ou vit la


echo "== resolution seen by zzquake_platform.c =="
RESSEEN=$(arm-none-eabi-gcc $CFLAGS -dM -E "$Z/zzquake_platform.c" \
          2>/dev/null | grep QUAKEGENERIC_RES)
echo "$RESSEEN" | sed 's/^/  /'
if [ "$ZZQ_HIRES" = "1" ]; then
    echo "$RESSEEN" | grep -q "QUAKEGENERIC_RES_X 640" || {
        echo "ERROR: HIRES build but the conversion path still sees 320."
        echo "  L include de quakegeneric.h ecrase le -D."
        exit 1
    }
fi

echo "== engine objects (forced include zzquake_stdio.h) =="
for f in "$Q"/*.c; do
    b=$(basename "$f" .c)
    skip=0
    for e in $EXCL; do [ "$b" = "$e" ] && skip=1; done
    [ $skip = 1 ] && continue










    # comparable : TEX -7 %% brut, environ -11 %% normalise par

    #





    XF=""
    if [ "$b" = "d_scan" ] && [ "$ZZQ_NO_O3" != "1" ]; then
        XF="-O3"
        echo "  d_scan.c : -O3 (validated)"
    fi




    arm-none-eabi-gcc $CFLAGS $XF -include "$Z/zzquake_stdio.h" \
        -c "$f" -o "$B/$b.o"
done

echo "== asm entry stub (A0.1: VFP before any C) =="
arm-none-eabi-gcc $CFLAGS -c "$Z/zzquake_entry.S" -o "$B/zzquake_entry.o"

echo "== asm abort handlers (A0.2: speaking exceptions) =="
arm-none-eabi-gcc $CFLAGS -c "$Z/zzquake_abort.S" -o "$B/zzquake_abort.o"

echo "== zzq objects (NO forced include) =="
for f in zzquake_main zzquake_platform zzquake_fs_mem zzquake_mmu zzquake_syscalls zzquake_a1_fstest zzquake_qgmain; do
    arm-none-eabi-gcc $CFLAGS -c "$Z/$f.c" -o "$B/$f.o"
done

echo "== unaligned-access check =="
# every object must be built with -mno-unaligned-access. The attribute
# Tag_CPU_unaligned_access must be absent/0 on all of them.
UAFAIL=0
for o in "$B"/*.o; do
    if arm-none-eabi-readelf -A "$o" 2>/dev/null | \
       grep -q "Tag_CPU_unaligned_access: v6"; then
        echo "ERROR: $o was built WITHOUT -mno-unaligned-access"
        UAFAIL=1
    fi
done
[ $UAFAIL = 0 ] && echo "PASS unaligned" || exit 1

echo "== short-enums check (LE bug du parcours MDL) =="
# arm-none-eabi-gcc active -fshort-enums PAR DEFAUT (ABI ARM EABI).




ESFAIL=0
for o in "$B"/*.o; do
    if arm-none-eabi-readelf -A "$o" 2>/dev/null | \
       grep -q "Tag_ABI_enum_size: small"; then
        echo "ERROR: $o was built with short enums"
        ESFAIL=1
    fi
done
[ $ESFAIL = 0 ] && echo "PASS enums" || exit 1

echo "== stdio redirection check (audit #1) =="
FAIL=0
for o in "$B"/*.o; do
    case "$o" in *zzquake_*|*safe_mem*) continue;; esac
    if arm-none-eabi-nm "$o" | grep -wE \
"U (fopen|fclose|fread|fwrite|fseek|ftell|fgetc|getc|feof|fprintf|fscanf|fflush|printf|remove)"
    then echo "ERROR: $o calls newlib stdio"; FAIL=1; fi
    if arm-none-eabi-nm "$o" | grep -qw "U exit"
    then echo "ERROR: $o calls exit()"; FAIL=1; fi
done
[ $FAIL = 0 ] && echo "PASS stdio" || exit 1

echo "== link (safe_mem.o FIRST, real ZZDoom order, ASSERT 2MB) =="
arm-none-eabi-gcc -mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 \
    -mfloat-abi=hard -nostartfiles -T "$Z/zzquake.ld" \
    -Wl,--gc-sections -Wl,-Map="$B/zzquake.map" \
    "$B/safe_mem.o" $(ls "$B"/*.o | grep -v safe_mem) \
    -lgcc -lc -lm -o "$B/zzquake.elf"

arm-none-eabi-objcopy -O binary "$B/zzquake.elf" "$B/zzquake.blob"

echo "== footprint =="
arm-none-eabi-size "$B/zzquake.elf"
arm-none-eabi-nm "$B/zzquake.elf" | grep -E " core1_entry$| _bss_end$"
echo "== ABI VFP =="
arm-none-eabi-readelf -A "$B/zzquake.elf" | grep -E \
    "Tag_ABI_VFP_args|Tag_FP_arch"
echo "== mem* override check (no newlib mem* in the link) =="
if grep -qE "libc_a-(memcpy|memset|memmove)" "$B/zzquake.map"; then
    echo "ERROR: newlib mem* linked - is safe_mem.o first?"
    exit 1
fi
echo "PASS memstubs"

echo "== A0.1 check: VFP enabled in asm, before any C =="
arm-none-eabi-objdump -d "$B/zzquake.elf" > "$B/dis.txt"
# 1. zzq_entry must be the very first thing in the blob
if ! grep -q "^04900000 <zzq_entry>:" "$B/dis.txt"; then
    echo "ERROR: zzq_entry is not at 0x04900000"
    exit 1
fi
# 2. no VFP/FP instruction may appear before the vmsr fpexc
sed -n '/<zzq_entry>:/,/vmsr/p' "$B/dis.txt" > "$B/entry_head.txt"
if grep -E "\\bv(push|pop|ldr|str|add|sub|mul|div|cvt|cmp|mov)" \
   "$B/entry_head.txt"; then
    echo "ERROR: VFP instruction before vmsr fpexc"
    exit 1
fi
# 3. the stub must not touch the stack (SP is the firmware's)



if grep -E "\\b(push|pop)\\b|, *sp\\b.*;|mov[[:space:]]+sp," \
   "$B/entry_head.txt"; then
    echo "ERROR: zzq_entry uses the stack"
    exit 1
fi
echo "PASS entry"

echo "== livrable =="
ls -la "$B/zzquake.blob"
md5sum "$B/zzquake.blob"

# --- Verification syntaxique du launcher 68k -------------------------




if [ -f "$Z/ZZQuake.c" ] && [ -d /tmp/amistub ]; then
    echo "== 68k launcher syntax =="
    if gcc -fsyntax-only -I/tmp/amistub -I"$Z" "$Z/ZZQuake.c" 2>/tmp/lsyn.txt; then
        echo "PASS launcher"
LAUNCHER_DIR=$(dirname "$0"); SRC_DIR=$(dirname "$0")




fail=0
for sym in zzq_ahi_init zzq_ahi_stop zzq_pcm_fill key_push raw_to_quake; do
    if ! grep -q "$sym" "$LAUNCHER_DIR/ZZQuake.c" 2>/dev/null && \
       ! grep -q "$sym" "$SRC_DIR/zzquake_platform.c" 2>/dev/null; then
        echo "ERROR: missing $sym"; fail=1
    fi
done
grep -q "zzq_ahi_init(sh" "$LAUNCHER_DIR/ZZQuake.c" 2>/dev/null \
    && echo "PASS audio-init" || { echo "ERROR: zzq_ahi_init is never CALLED"; fail=1; }
grep -q "SNDDMA_Submit" "$PQ/snd_zz9000.c" 2>/dev/null \
    && echo "PASS audio-submit" || { echo "ERROR: audio backend missing"; fail=1; }
[ $fail -eq 0 ] || exit 1

    else
        grep -E "error" /tmp/lsyn.txt | head -5
        echo "ERROR: launcher syntax check failed"
        exit 1
    fi
fi
