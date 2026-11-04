#!/bin/bash
# build_arm.sh - ZZPicoDriveVR Core1 blob (ZZPicoDriveMD Virtua Racing Edition: Mega Drive + SVP; ZZPicoDrive32X lineage)
#
# Everything is relative to the folder of this script.  Toolchain: arm-none-eabi
# GCC 13.2.1 (Arm GNU Toolchain 13.2.Rel1 or xPack 13.2.1-1.1).  Windows/MSYS2:
# "C:/ArmGNUToolchain/13.2 Rel1/bin" is used automatically when it exists;
# otherwise set TOOLCHAIN_BIN to the bin/ folder, or have it in PATH.
# Cyclone.s is shipped pre-generated: no native compiler needed.
#
#   ./build_arm.sh            -> bin/zzvr_v1.2_interp.bin  (SH-2 interpreter, tests only)
#   ZZVR_DRC=1 ./build_arm.sh -> bin/zzvr_v1.2.bin  (SH-2 and SVP recompilers: the ZZPicoDriveVR blob)
#
# Lineage: ZZPicoDriveMD 1.1 build_cyc_audioreal.sh (same CPU flags, same object
# list for the Mega Drive part, Cyclone + asm renderers + asm YM2612), with:
#   - the PicoDrive tree of VirtuaRacingDeluxeEnhanced (irixxxx 8b5e91bc + VRDE hooks)
#   - -DNO_32X removed; pico/32x/*.c, cpu/sh2/sh2.c, cpu/sh2/mame/sh2pico.c,
#     cpu/drc/cmn.c added; with ZZVR_DRC=1: -DDRC_SH2 -DZZVR_DRC -DZZVR_FAR_CALLS and
#     cpu/sh2/compiler.c (ZZVR_FAR_CALLS: far-call sequence in cpu/drc/emit_arm.c,
#     the code cache in the arena is beyond the +-32 MiB BL range of the blob)
#   - the ZZ platform files renamed zzvr_* (Core1/), linker script zzvr.ld
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
# Toolchain: TOOLCHAIN_BIN if given, else the Windows/MSYS2 install of Arm GNU
# Toolchain 13.2.Rel1 if present (as the other ZZ build scripts), else PATH.
if [ -z "$TOOLCHAIN_BIN" ] && [ -d "/c/ArmGNUToolchain/13.2 Rel1/bin" ]; then
  TOOLCHAIN_BIN="/c/ArmGNUToolchain/13.2 Rel1/bin"
fi
if [ -n "$TOOLCHAIN_BIN" ]; then PATH="$TOOLCHAIN_BIN:$PATH"; fi
CROSS=arm-none-eabi-
CC=${CROSS}gcc
OBJCOPY=${CROSS}objcopy
SIZE=${CROSS}size
PICO=third_party/picodrive
C1=Core1
BUILD=build
if [ "${ZZVR_DRC:-0}" = "1" ]; then OUT=zzvr_v1.2; DRCFLAGS="-DDRC_SH2 -DZZVR_DRC -DZZVR_FAR_CALLS -DLOOP_DETECTION=0 -DLOOP_OPTIMIZER=0 -D_SVP_DRC"; else OUT=zzvr_v1.2_interp; DRCFLAGS=""; fi
mkdir -p "$BUILD" bin

command -v $CC >/dev/null 2>&1 || { echo "ERROR: $CC not found (set TOOLCHAIN_BIN)"; exit 1; }
$CC --version | head -1

CPU="-mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 -mfloat-abi=hard"
# 1.1 flags, minus NO_32X, plus the 32X build; ZZPICO_NTSC_DIAG keeps the 1.1 PicoFrame histogram
DEFS="-DEMU_C68K -D_USE_CZ80 -DNO_SMS -D_ASM_DRAW_C -DZZPICO_AUDIO_REAL -D_ASM_YM2612_C -DZZPICO_NTSC_DIAG $DRCFLAGS"
CFLAGS="$CPU -mno-unaligned-access -Os -std=gnu11 -ffreestanding -ffunction-sections -fdata-sections -w -I$C1/stubs -I$C1 -I$PICO -I$PICO/pico $DEFS"
LDFLAGS="$CPU -nostartfiles -T $C1/zzvr.ld -Wl,--gc-sections -Wl,-Map=$BUILD/$OUT.map -lgcc -lc -lm"

# --- 0) structure offsets for the asm files, generated from THIS tree with THESE flags ---
CC="$CC" CFLAGS="$CPU -mno-unaligned-access -Os -std=gnu11 -ffreestanding -I$PICO -I$PICO/pico $DEFS" \
  bash $PICO/tools/mkoffsets.sh $PICO/pico >/dev/null 2>&1
test -s $PICO/pico/pico_int_offs.h || { echo "ERROR: pico_int_offs.h not generated"; exit 1; }

# --- 1) Cyclone.s: shipped pre-generated (deterministic output of the Cyclone
#        generator for cpu/cyclone_config.h); regenerated here only if absent
#        (needs a native C/C++ compiler and make) ---
if [ ! -s "$PICO/cpu/cyclone/Cyclone.s" ]; then
  test -f "$PICO/cpu/cyclone/Makefile" || { echo "ERROR: Cyclone sources missing in $PICO/cpu/cyclone"; exit 1; }
  command -v make >/dev/null 2>&1 || { echo "ERROR: make is required to generate Cyclone.s"; exit 1; }
  echo "Generating Cyclone.s from the Cyclone sources..."
  make -C "$PICO/cpu/cyclone" CXX="${HOST_CXX:-g++}" CONFIG_FILE=../cyclone_config.h Cyclone.s
fi
test -s "$PICO/cpu/cyclone/Cyclone.s" || { echo "ERROR: Cyclone.s not generated"; exit 1; }

rm -f $BUILD/*.o
OBJS=""
cc1() { # cc1 <src> <obj> [extra flags]
  $CC $CFLAGS $3 -c "$1" -o "$BUILD/$2.o"; OBJS="$OBJS $BUILD/$2.o"; }
as1() { # as1 <src> <obj> [extra flags]
  $CC $CPU $3 -c "$1" -o "$BUILD/$2.o"; OBJS="$OBJS $BUILD/$2.o"; }

# ZZ platform (Core1/)
for f in zzvr_core1 zzvr_platform zzvr_mmu zzvr_stubs; do cc1 $C1/$f.c $f; done
as1 $C1/zzvr_vectors.S zzvr_vectors

# PicoDrive core: the Mega Drive part, as 1.1
for f in pico memory sek z80if videoport draw2 misc eeprom patch media cart state; do
  cc1 $PICO/pico/$f.c $f
done
for f in sound sn76496 ym2612 mix resampler; do cc1 $PICO/pico/sound/$f.c $f; done
as1 $PICO/pico/sound/ym2612_arm.S ym2612_arm "-D_ASM_YM2612_C -I$PICO -I$PICO/pico"
cc1 $PICO/cpu/cz80/cz80.c cz80
cc1 $PICO/pico/draw.c draw
as1 $PICO/pico/draw_arm.S  draw_arm  "-D_ASM_DRAW_C -I$PICO/pico"
as1 $PICO/pico/draw2_arm.S draw2_arm "-D_ASM_DRAW_C -I$PICO/pico"

# 32X: adapter, memory maps, layer draw, SH-2 SoC peripherals, PWM; SH-2 core
for f in 32x memory draw sh2soc pwm; do cc1 $PICO/pico/32x/$f.c x32_$f; done
cc1 $PICO/cpu/sh2/sh2.c sh2
cc1 $PICO/cpu/sh2/mame/sh2pico.c sh2pico
cc1 $PICO/cpu/drc/cmn.c drc_cmn
if [ -n "$DRCFLAGS" ]; then cc1 $PICO/cpu/sh2/compiler.c sh2_compiler; fi

# SVP (Virtua Racing Mega Drive cartridge DSP, SSP1601) and its ARM recompiler
for f in svp memory ssp16; do cc1 $PICO/pico/carthw/svp/$f.c svp_$f; done
if [ -n "$DRCFLAGS" ]; then
  cc1 $PICO/pico/carthw/svp/compiler.c svp_compiler
  as1 $PICO/pico/carthw/svp/stub_arm.S svp_stub_arm "-I$PICO -I$PICO/pico"
fi

# Cyclone 68000
as1 $PICO/cpu/cyclone/Cyclone.s Cyclone
as1 $PICO/pico/m68kif_cyclone.s m68kif_cyclone
as1 $PICO/cpu/cyclone/tools/idle.s idle

$CC $OBJS $LDFLAGS -o $BUILD/$OUT.elf
$OBJCOPY -O binary $BUILD/$OUT.elf bin/$OUT.bin
$SIZE -A $BUILD/$OUT.elf | grep -E '^\.text|^\.data|^\.bss' || true
echo "BIN: $(stat -c%s bin/$OUT.bin) bytes -> bin/$OUT.bin"
(cd bin && md5sum $OUT.bin > $OUT.md5 && sha256sum $OUT.bin > $OUT.sha256 && cat $OUT.md5 $OUT.sha256)
