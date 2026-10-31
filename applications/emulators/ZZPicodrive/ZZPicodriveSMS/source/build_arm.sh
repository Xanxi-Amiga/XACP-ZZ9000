#!/bin/sh
# build_arm.sh - ZZPicoDriveSMS v1.2 Core1 blob (Master System + YM2413 FM)
#
# Everything is relative to the folder of this script.  Toolchain: arm-none-eabi
# GCC 13.2.1, called by its explicit Windows path (PATH is not modified):
#   C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-gcc  (override: ARM_BIN=<dir>)
# pico/pico_int_offs.h (struct offsets for the asm renderers) is shipped
# generated for exactly these flags; REGEN_OFFSETS=1 regenerates it (needs an
# ARM_BIN path without spaces and /tmp: PicoDrive's mkoffsets.sh).
# Cyclone.s is shipped pre-generated (third_party/picodrive/cpu/cyclone/Cyclone.s,
# md5 ff097962906336cf7879c22ff42bcc18): no host compiler needed.
#
#   ./build_arm.sh   -> bin/zzsms_v1.2.bin (+ .md5 / .sha256)
#
# Lineage: ZZPicoDriveMD v1.2 build_arm.sh with the ZZPicoDriveSMS build: the
# Master System code is compiled in, mode4.c + sms.c added, zzpico_stubs_sms.c
# instead of zzpico_stubs.c; pico/sound/emu2413/emu2413.h is the exact emu2413
# header (third_party/emu2413, MIT).
# FM support: pico/sound/ym2413.c is compiled (it #includes emu2413/emu2413.c, the
# emu2413 1.6.3 of ZZVGMPlay), -DZZPICO_FM_SAT (saturating PSG+FM mix in
# sound.c), linker wraps for the FM diagnostics.
set -e
cd "$(dirname "$0")"
ARM_BIN="${ARM_BIN:-C:/ArmGNUToolchain/13.2 Rel1/bin}"
CC="$ARM_BIN/arm-none-eabi-gcc"; OBJCOPY="$ARM_BIN/arm-none-eabi-objcopy"; SIZE="$ARM_BIN/arm-none-eabi-size"
"$CC" --version | head -1
PICO=third_party/picodrive
OUT=zzsms_v1.2
BUILD=build
mkdir -p $BUILD bin

CPU="-mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 -mfloat-abi=hard"
DEFS="-DEMU_C68K -D_USE_CZ80 -DNO_32X -D_ASM_DRAW_C -DZZPICO_AUDIO_REAL -D_ASM_YM2612_C -DZZPICO_NTSC_DIAG -DZZPICO_FM_SAT -DZZPICO_FM_NATIVE -DZZPICO_SMS_JAP_IO -DZZPICO_SMS_FM_MIX"
CFLAGS="$CPU -mno-unaligned-access -Os -std=gnu11 -ffreestanding -ffunction-sections -fdata-sections -w -ICore1/stubs -ICore1 -I$PICO -I$PICO/pico $DEFS"
LDFLAGS="$CPU -nostartfiles -T Core1/zzpico.ld -Wl,--wrap=YM2413_regWrite,--wrap=YM2413_dataWrite,--wrap=resampler_update -Wl,--gc-sections -Wl,-Map=$BUILD/$OUT.map -lgcc -lc -lm"

# struct offsets for the asm renderers (shipped; see header)
if [ "$REGEN_OFFSETS" = "1" ]; then
  CC="$CC" CFLAGS="$CPU -mno-unaligned-access -Os -std=gnu11 -ffreestanding -I$PICO -I$PICO/pico $DEFS" \
    sh $PICO/tools/mkoffsets.sh $PICO/pico >/dev/null 2>&1 || true
fi
test -s $PICO/pico/pico_int_offs.h || { echo "ERROR: pico_int_offs.h missing"; exit 1; }

# Build optimization: per-file optimisation.  -Os stays the default (the validated MD
# v1.2 build); the Master System hot paths are compiled for speed: emu2413
# (ym2413.c #includes it) at -O3 as in ZZVGMPlay 1.0 on the same CPU, the
# Z80 core, the mode 4 renderer, the SMS bus and the sound mixer at -O2.
# A later -O on the command line overrides the -Os of CFLAGS.
fopt() {
  case "$1" in
    ym2413) echo "-O3" ;;
    sound|resampler|sn76496|mode4|sms|cz80) echo "-O2" ;;
    *) echo "" ;;
  esac
}

rm -f $BUILD/*.o
for f in zzpico_core1 zzpico_platform zzpico_mmu zzpico_stubs_sms; do
  "$CC" $CFLAGS -c Core1/$f.c -o $BUILD/$f.o
done
"$CC" $CPU -c Core1/zzpico_vectors.S -o $BUILD/zzpico_vectors.o
for f in pico memory sek z80if videoport draw2 misc eeprom patch media cart state mode4 sms; do
  if [ "$f" = "memory" ]; then
    "$CC" $CFLAGS -include Core1/stubs/zzpico_bustrace.h -c $PICO/pico/$f.c -o $BUILD/$f.o
  else
    "$CC" $CFLAGS $(fopt $f) -c $PICO/pico/$f.c -o $BUILD/$f.o
  fi
done
for f in sound sn76496 ym2612 ym2413 mix resampler; do
  "$CC" $CFLAGS $(fopt $f) -c $PICO/pico/sound/$f.c -o $BUILD/$f.o
done
"$CC" $CPU -D_ASM_YM2612_C -I$PICO -I$PICO/pico -c $PICO/pico/sound/ym2612_arm.S -o $BUILD/ym2612_arm.o
"$CC" $CFLAGS $(fopt cz80) -c $PICO/cpu/cz80/cz80.c -o $BUILD/cz80.o
"$CC" $CFLAGS -include Core1/stubs/zzpico_draw32x_decl.h -c $PICO/pico/draw.c -o $BUILD/draw.o
"$CC" $CPU -D_ASM_DRAW_C -I$PICO/pico -c $PICO/pico/draw_arm.S  -o $BUILD/draw_arm.o
"$CC" $CPU -D_ASM_DRAW_C -I$PICO/pico -c $PICO/pico/draw2_arm.S -o $BUILD/draw2_arm.o
test -s $PICO/cpu/cyclone/Cyclone.s || { echo "ERROR: Cyclone.s missing (shipped pre-generated)"; exit 1; }
"$CC" $CPU -c $PICO/cpu/cyclone/Cyclone.s -o $BUILD/Cyclone.o
"$CC" $CPU -c $PICO/pico/m68kif_cyclone.s -o $BUILD/m68kif_cyclone.o
"$CC" $CPU -c $PICO/cpu/cyclone/tools/idle.s -o $BUILD/idle.o

OBJS=""
for o in zzpico_core1 zzpico_platform zzpico_mmu zzpico_stubs_sms zzpico_vectors pico memory sek z80if videoport draw2 misc eeprom patch media cart state mode4 sms sound sn76496 ym2612 ym2413 mix resampler cz80 draw draw_arm draw2_arm ym2612_arm Cyclone m68kif_cyclone idle; do
  OBJS="$OBJS $BUILD/$o.o"
done
"$CC" $OBJS $LDFLAGS -o $BUILD/$OUT.elf
"$OBJCOPY" -O binary $BUILD/$OUT.elf bin/$OUT.bin
"$SIZE" -A $BUILD/$OUT.elf | grep -E '\.text|\.data|\.bss' || true
echo "BIN: bin/$OUT.bin ($(wc -c < bin/$OUT.bin | tr -d ' ') bytes)"
