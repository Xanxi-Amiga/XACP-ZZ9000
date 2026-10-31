#!/bin/bash
# build_arm.sh - ZZPicoDriveMD v1.2 Core1 blob (Sega Mega Drive on the ZZ9000 Core1)
#
# Everything is relative to the folder of this script.  Toolchain: arm-none-eabi
# GCC 13.2.1 (Arm GNU Toolchain 13.2.Rel1 or xPack 13.2.1-1.1).  Windows/MSYS2:
# "C:/ArmGNUToolchain/13.2 Rel1/bin" is used automatically when it exists;
# otherwise set TOOLCHAIN_BIN to the bin/ folder, or have it in PATH.
# Cyclone.s is shipped pre-generated (third_party/picodrive/cpu/cyclone/Cyclone.s,
# md5 ff097962906336cf7879c22ff42bcc18): no host compiler needed.
#
#   ./build_arm.sh   -> bin/zzmd_v1.2.bin (+ .md5 / .sha256)
#
# Uses PicoDrive/Cyclone with ARM-optimised rendering and YM2612 audio.
set -e
cd "$(dirname "$0")"
if [ -z "$TOOLCHAIN_BIN" ] && [ -d "/c/ArmGNUToolchain/13.2 Rel1/bin" ]; then
  TOOLCHAIN_BIN="/c/ArmGNUToolchain/13.2 Rel1/bin"
fi
if [ -n "$TOOLCHAIN_BIN" ]; then export PATH="$TOOLCHAIN_BIN:$PATH"; fi
CROSS=arm-none-eabi-
CC=${CROSS}gcc; OBJCOPY=${CROSS}objcopy; SIZE=${CROSS}size
$CC --version | head -1
PICO=third_party/picodrive
OUT=zzmd_v1.2
BUILD=build
mkdir -p $BUILD bin

CPU="-mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 -mfloat-abi=hard"
DEFS="-DEMU_C68K -D_USE_CZ80 -DNO_32X -DNO_SMS -D_ASM_DRAW_C -DZZPICO_AUDIO_REAL -D_ASM_YM2612_C -DZZPICO_NTSC_DIAG"
CFLAGS="$CPU -mno-unaligned-access -Os -std=gnu11 -ffreestanding -ffunction-sections -fdata-sections -w -ICore1/stubs -ICore1 -I$PICO -I$PICO/pico $DEFS"
LDFLAGS="$CPU -nostartfiles -T Core1/zzpico.ld -Wl,--gc-sections -Wl,-Map=$BUILD/$OUT.map -lgcc -lc -lm"

# struct offsets for the asm renderers, generated with our exact configuration
CC="$CC" CFLAGS="$CPU -mno-unaligned-access -Os -std=gnu11 -ffreestanding -I$PICO -I$PICO/pico $DEFS" \
  bash $PICO/tools/mkoffsets.sh $PICO/pico >/dev/null 2>&1 || true
test -s $PICO/pico/pico_int_offs.h || { echo "ERROR: pico_int_offs.h not generated"; exit 1; }

rm -f $BUILD/*.o
for f in zzpico_core1 zzpico_platform zzpico_mmu zzpico_stubs; do
  $CC $CFLAGS -c Core1/$f.c -o $BUILD/$f.o
done
$CC $CPU -c Core1/zzpico_vectors.S -o $BUILD/zzpico_vectors.o
for f in pico memory sek z80if videoport draw2 misc eeprom patch media cart state; do
  if [ "$f" = "memory" ]; then
    $CC $CFLAGS -include Core1/stubs/zzpico_bustrace.h -c $PICO/pico/$f.c -o $BUILD/$f.o
  else
    $CC $CFLAGS -c $PICO/pico/$f.c -o $BUILD/$f.o
  fi
done
for f in sound sn76496 ym2612 mix resampler; do
  $CC $CFLAGS -c $PICO/pico/sound/$f.c -o $BUILD/$f.o
done
$CC $CPU -D_ASM_YM2612_C -I$PICO -I$PICO/pico -c $PICO/pico/sound/ym2612_arm.S -o $BUILD/ym2612_arm.o
$CC $CFLAGS -c $PICO/cpu/cz80/cz80.c -o $BUILD/cz80.o
$CC $CFLAGS -include Core1/stubs/zzpico_draw32x_decl.h -c $PICO/pico/draw.c -o $BUILD/draw.o
$CC $CPU -D_ASM_DRAW_C -I$PICO/pico -c $PICO/pico/draw_arm.S  -o $BUILD/draw_arm.o
$CC $CPU -D_ASM_DRAW_C -I$PICO/pico -c $PICO/pico/draw2_arm.S -o $BUILD/draw2_arm.o
test -s $PICO/cpu/cyclone/Cyclone.s || { echo "ERROR: Cyclone.s missing (shipped pre-generated)"; exit 1; }
$CC $CPU -c $PICO/cpu/cyclone/Cyclone.s -o $BUILD/Cyclone.o
$CC $CPU -c $PICO/pico/m68kif_cyclone.s -o $BUILD/m68kif_cyclone.o
$CC $CPU -c $PICO/cpu/cyclone/tools/idle.s -o $BUILD/idle.o

OBJS=""
for o in zzpico_core1 zzpico_platform zzpico_mmu zzpico_stubs zzpico_vectors pico memory sek z80if videoport draw2 misc eeprom patch media cart state sound sn76496 ym2612 mix resampler cz80 draw draw_arm draw2_arm ym2612_arm Cyclone m68kif_cyclone idle; do
  OBJS="$OBJS $BUILD/$o.o"
done
$CC $OBJS $LDFLAGS -o $BUILD/$OUT.elf
$OBJCOPY -O binary $BUILD/$OUT.elf bin/$OUT.bin
$SIZE -A $BUILD/$OUT.elf | grep -E '\.text|\.data|\.bss' || true
echo "BIN: $(stat -c%s bin/$OUT.bin) bytes -> bin/$OUT.bin"
(cd bin && md5sum $OUT.bin > $OUT.md5 && sha256sum $OUT.bin > $OUT.sha256 && cat $OUT.md5 $OUT.sha256)
