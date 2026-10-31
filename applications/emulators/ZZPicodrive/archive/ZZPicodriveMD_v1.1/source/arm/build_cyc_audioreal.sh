#!/bin/bash
# build ZZPicoDrive Core1 - CYCLONE + asm_render (draw_arm.S/draw2_arm.S)
# Un seul changement vs baseline cyc : renderer ASM ARM (define _ASM_DRAW_C).
set -e
cd /tmp/zp
CROSS=arm-none-eabi-
CC=${CROSS}gcc
OBJCOPY=${CROSS}objcopy
SIZE=${CROSS}size
PICO=picodrive
OUT=${1:-zzpicodrive_asmrender}

CPU="-mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 -mfloat-abi=hard"
# SEUL AJOUT vs build_cyc.sh : -D_ASM_DRAW_C (global, car pico.h le teste)
CFLAGS="$CPU -mno-unaligned-access -Os -std=gnu11 -ffreestanding -ffunction-sections -fdata-sections -w -Istubs -I. -I$PICO -I$PICO/pico -DEMU_C68K -D_USE_CZ80 -DNO_32X -DNO_SMS -D_ASM_DRAW_C -DZZPICO_AUDIO_REAL -D_ASM_YM2612_C -DZZPICO_NTSC_DIAG"
LDFLAGS="$CPU -nostartfiles -T zzpico.ld -Wl,--gc-sections -Wl,-Map=$OUT.map -lgcc -lc -lm"

# --- 0) offsets structs pour l'ASM, generes avec NOTRE config exacte ---
CC="$CC" CFLAGS="$CPU -mno-unaligned-access -Os -std=gnu11 -ffreestanding -I$PICO -I$PICO/pico -DEMU_C68K -D_USE_CZ80 -DNO_32X -DNO_SMS -D_ASM_DRAW_C -DZZPICO_AUDIO_REAL -D_ASM_YM2612_C -DZZPICO_NTSC_DIAG" \
  bash $PICO/tools/mkoffsets.sh $PICO/pico >/dev/null 2>&1
test -s $PICO/pico/pico_int_offs.h || { echo "ERREUR: pico_int_offs.h non genere"; exit 1; }

rm -f *.o
# sources ZZ
for f in zzpico_core1 zzpico_platform zzpico_mmu zzpico_stubs; do
  $CC $CFLAGS -c $f.c -o $f.o
done
$CC $CPU -c zzpico_vectors.S -o zzpico_vectors.o
# pico core (draw2 recompile avec _ASM_DRAW_C via CFLAGS global)
for f in pico memory sek z80if videoport draw2 misc eeprom patch media cart state; do
  if [ "$f" = "memory" ]; then
    $CC $CFLAGS -include stubs/zzpico_bustrace.h -c $PICO/pico/$f.c -o $f.o
  else
    $CC $CFLAGS -c $PICO/pico/$f.c -o $f.o
  fi
done
for f in sound sn76496 ym2612 mix resampler; do
  $CC $CFLAGS -c $PICO/pico/sound/$f.c -o $f.o
done
$CC $CPU -D_ASM_YM2612_C -I$PICO -I$PICO/pico -c $PICO/pico/sound/ym2612_arm.S -o ym2612_arm.o
$CC $CFLAGS -c $PICO/cpu/cz80/cz80.c -o cz80.o
$CC $CFLAGS -include stubs/zzpico_draw32x_decl.h -c $PICO/pico/draw.c -o draw.o

# --- asm_render : renderer ASM ARM ---
$CC $CPU -D_ASM_DRAW_C -I$PICO/pico -c $PICO/pico/draw_arm.S  -o draw_arm.o
$CC $CPU -D_ASM_DRAW_C -I$PICO/pico -c $PICO/pico/draw2_arm.S -o draw2_arm.o

# --- CYCLONE ---
# Cyclone.s is a generated source file. The upstream repository does not store
# it at the revision used by ZZPicoDriveMD 1.1. Build the host-side generator
# and emit Cyclone.s when the file is absent.
if [ ! -s "$PICO/cpu/cyclone/Cyclone.s" ]; then
  test -f "$PICO/cpu/cyclone/Makefile" || {
    echo "ERREUR: sources Cyclone absentes de $PICO/cpu/cyclone"
    exit 1
  }
  command -v make >/dev/null 2>&1 || {
    echo "ERREUR: make requis pour generer Cyclone.s"
    exit 1
  }
  test -f "$PICO/cpu/cyclone_config.h" || {
    echo "ERREUR: configuration PicoDrive Cyclone absente: $PICO/cpu/cyclone_config.h"
    exit 1
  }
  echo "Generation de Cyclone.s depuis les sources Cyclone..."
  make -C "$PICO/cpu/cyclone" \
    CXX="${HOST_CXX:-g++}" \
    CONFIG_FILE=../cyclone_config.h \
    Cyclone.s
fi
test -s "$PICO/cpu/cyclone/Cyclone.s" || {
  echo "ERREUR: Cyclone.s non genere"
  exit 1
}

$CC $CPU -c $PICO/cpu/cyclone/Cyclone.s -o Cyclone.o
$CC $CPU -c $PICO/pico/m68kif_cyclone.s -o m68kif_cyclone.o
$CC $CPU -c $PICO/cpu/cyclone/tools/idle.s -o idle.o

OBJS="zzpico_core1.o zzpico_platform.o zzpico_mmu.o zzpico_stubs.o zzpico_vectors.o pico.o memory.o sek.o z80if.o videoport.o draw2.o misc.o eeprom.o patch.o media.o cart.o state.o sound.o sn76496.o ym2612.o mix.o resampler.o cz80.o draw.o draw_arm.o draw2_arm.o ym2612_arm.o Cyclone.o m68kif_cyclone.o idle.o"
$CC $OBJS $LDFLAGS -o $OUT.elf
$OBJCOPY -O binary $OUT.elf $OUT.bin
$SIZE -A $OUT.elf | grep -E '\.text|\.data|\.bss' || true
echo "BIN: $(stat -c%s $OUT.bin) bytes -> $OUT.bin"
