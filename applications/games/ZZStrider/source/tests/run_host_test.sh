#!/bin/sh
# run_host_test.sh - OPTIONAL regression test of the Core1 blob sources on a Linux host.
# Needs: arm-none-eabi-gcc (with newlib + rdimon), qemu-arm (user mode), m68k-linux-gnu-gcc,
# python3. It is NOT part of build_all.sh and is never needed to build ZZStrider.
# It uses a synthetic ROM set generated here: no commercial ROM is involved.
set -e
T=$(cd "$(dirname "$0")" && pwd)
P=$(dirname "$T")
W=${1:-$T/work}
mkdir -p "$W/obj" "$W/roms" "$W/out"

m68k-linux-gnu-gcc -m68000 -O1 -ffreestanding -fno-builtin -fno-pic -fno-pie -fomit-frame-pointer \
  -fno-tree-loop-distribute-patterns -nostdlib -static -Wl,-T,"$T/synth/prog.ld" -Wl,--build-id=none \
  -o "$W/prog.elf" "$T/synth/crt0.S" "$T/synth/prog.c" 2>/dev/null
m68k-linux-gnu-objcopy -O binary "$W/prog.elf" "$W/prog.bin"
python3 "$T/make_synth_roms.py" "$W/prog.bin" "$W/roms"

CPU="-mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 -mfloat-abi=hard"
INC="-I$P/arm -I$P/arm/burn_shim -I$P/arm/cps1 -I$P/third_party -I$P/third_party/cyclone -I$P/third_party/cz80 -I$P/third_party/ym2151"
COMMON="-O2 $CPU $INC -fno-common -fomit-frame-pointer -fno-strict-aliasing -DZZS_ROM_CRC_CHECK=0"
CXXF="$COMMON -std=gnu++14 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -Wno-write-strings"
OBJS=""
for f in arm/zzs_rom.c arm/zzs_video.c arm/zzs_audio.c arm/zzs_sound.c arm/zzs_msm6295.c arm/zzs_ym_jarek.c \
         third_party/ym2151/ym2151.c third_party/cz80/cz80.c third_party/cyclone/generated/Cyclone.s tests/host/harness.c; do
  o="$W/obj/$(basename "${f%.*}").o"; arm-none-eabi-gcc $COMMON -w -c "$P/$f" -o "$o"; OBJS="$OBJS $o"
done
for f in arm/zzs_burn.cpp arm/zzs_cps_load.cpp arm/zzs_cps_run.cpp arm/cps1/cps_config.cpp arm/cps1/cps_draw.cpp \
         arm/cps1/cps_mem.cpp arm/cps1/cps_obj.cpp arm/cps1/cps_pal.cpp arm/cps1/cps_rw.cpp arm/cps1/cps_scr.cpp \
         arm/cps1/cpsr.cpp arm/cps1/cpsrd.cpp arm/cps1/cpst.cpp arm/cps1/ctv.cpp; do
  o="$W/obj/$(basename "${f%.*}").o"; arm-none-eabi-g++ $CXXF -w -c "$P/$f" -o "$o"; OBJS="$OBJS $o"
done
arm-none-eabi-gcc $CPU --specs=rdimon.specs $OBJS -lm -o "$W/harness.elf"
qemu-arm "$W/harness.elf" "$W/roms" "$W/out" ${2:-130} > "$W/out/harness.log" || true
tail -n 3 "$W/out/harness.log"
mkdir -p "$W/out_scan"
qemu-arm "$W/harness.elf" "$W/roms" "$W/out_scan" ${2:-130} scan > "$W/out_scan/harness.log" || true
mkdir -p "$W/out_scan448"
qemu-arm "$W/harness.elf" "$W/roms" "$W/out_scan448" ${2:-130} scan448 > "$W/out_scan448/harness.log" || true
python3 "$T/check_host.py" "$W"
