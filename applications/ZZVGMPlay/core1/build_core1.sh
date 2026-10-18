#!/bin/sh
set -e
: "${ARM_PREFIX:=arm-none-eabi-}"
CC="C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-gcc"
CXX="C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-g++"
OBJCOPY="C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-objcopy"
SIZE="C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-size"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
# All Python calls below use paths relative to core1/. Do not pass MSYS
# absolute paths (/c/...) to Python on this Windows installation.
cd "$ROOT/core1"
OPL="$ROOT/third_party/Nuked-OPL3-fast"
PUFF="$ROOT/third_party/puff"
GENS="$ROOT/third_party/GME-GENS"
JAREK="$ROOT/third_party/Jarek-YM2151"
OPLL="$ROOT/third_party/emu2413"
V3="$ROOT/third_party/VGMPlay-3CONSOLES"
QHLE="$ROOT/third_party/QSound-HLE"

for f in "$OPL/opl3.c" "$OPL/opl3.h" \
         "$PUFF/puff.c" "$PUFF/puff.h" \
         "$GENS/Ym2612_GENS.cpp" "$GENS/Ym2612_GENS.h" \
         "$JAREK/ym2151.c" "$JAREK/ym2151.h" "$JAREK/zzr_jarek_shim.h" \
         "$OPLL/emu2413.c" "$OPLL/emu2413.h" \
         "$V3/mamedef.h" "$V3/gb.c" "$V3/gb.h" \
         "$V3/nes_apu.c" "$V3/nes_apu.h" "$V3/nes_defs.h" \
         "$V3/Ootake_PSG.c" "$V3/Ootake_PSG.h" \
         "$QHLE/qsound.c" "$QHLE/qsound.h"; do
  if [ ! -f "$f" ]; then
    echo "Missing: $f"
    echo "Required third-party sources must already be available locally in third_party/."
    exit 1
  fi
done

CFLAGS="-O3 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -marm \
-ffunction-sections -fdata-sections -fno-strict-aliasing \
-fno-unwind-tables -fno-asynchronous-unwind-tables -DNDEBUG \
-DOPL_WF_TABLE_RUNTIME=1 \
-I$ROOT/include -I$ROOT/core1 -I$OPL -I$PUFF -I$GENS -I$JAREK -I$OPLL -I$V3 -I$QHLE"

CXXFLAGS="$CFLAGS -std=gnu++11 -fno-exceptions -fno-rtti -fno-use-cxa-atexit"

"$CC"  $CFLAGS   -c "$ROOT/core1/startup.S" -o startup.o
"$CC"  $CFLAGS   -c "$ROOT/core1/zzvgm_core1.c" -o zzvgm_core1.o
"$CC"  $CFLAGS   -c "$ROOT/core1/arcade_pcm.c" -o arcade_pcm.o
"$CC"  $CFLAGS   -c "$ROOT/core1/neogeo2610.c" -o neogeo2610.o
"$CC"  $CFLAGS   -c "$ROOT/core1/qsound_pcm.c" -o qsound_pcm.o
"$CC"  $CFLAGS   -c "$ROOT/core1/es5503_zz.c" -o es5503_zz.o
"$CC"  $CFLAGS   -c "$ROOT/core1/k053260_zz.c" -o k053260_zz.o
"$CC"  $CFLAGS   -c "$ROOT/core1/k054539_zz.c" -o k054539_zz.o
"$CC"  $CFLAGS   -c "$ROOT/core1/k051649_zz.c" -o k051649_zz.o
"$CC"  $CFLAGS   -c "$QHLE/qsound.c" -o qsound_hle.o
"$CC"  $CFLAGS   -c "$ROOT/core1/sn76489.c" -o sn76489.o
"$CC"  $CFLAGS   -c "$ROOT/core1/ay8910.c" -o ay8910.o
"$CC"  $CFLAGS   -c "$ROOT/core1/ay2149_ref.c" -o ay2149_ref.o
"$CC"  $CFLAGS   -c "$ROOT/core1/gzip_puff.c" -o gzip_puff.o
"$CC"  $CFLAGS   -c "$ROOT/core1/baremetal_stubs.c" -o baremetal_stubs.o
"$CC"  $CFLAGS   -c "$OPL/opl3.c" -o opl3.o
"$CC"  $CFLAGS   -c "$JAREK/ym2151.c" -o ym2151_jarek.o
"$CC"  $CFLAGS   -c "$ROOT/core1/jarek2151_wrap.c" -o jarek2151_wrap.o
"$CC"  $CFLAGS   -c "$ROOT/core1/opll2413_alloc.c" -o opll2413_alloc.o
"$CC"  $CFLAGS   -c "$ROOT/core1/emu2413_wrap.c" -o emu2413_wrap.o
"$CC"  $CFLAGS   -c "$PUFF/puff.c" -o puff.o

"$CC"  $CFLAGS   -c "$ROOT/core1/gb_dmg_wrap.c" -o gb_dmg_wrap.o
"$CC"  $CFLAGS   -c "$V3/gb.c" -o gb_legacy.o
"$CC"  $CFLAGS   -c "$ROOT/core1/nes_apu_wrap.c" -o nes_apu_wrap.o
"$CC"  $CFLAGS   -c "$ROOT/core1/huc6280_wrap.c" -o huc6280_wrap.o

# Prepare bundled libvgm resampler without downloads or upstream edits.
python ./prepare_system18.py
"$CC" $CFLAGS -c "$ROOT/core1/system18_backend.c" -o system18_backend.o
"$CC" $CFLAGS -c "$ROOT/core1/system18_fm.c" -o system18_fm.o
"$CC" $CFLAGS -c "$ROOT/core1/system18_rf.c" -o system18_rf.o
"$CC" $CFLAGS -c "$ROOT/core1/system18_resampler.c" -o system18_resampler.o
"$CC" $CFLAGS -c "$ROOT/third_party/libvgm-system18/emu/logging.c" -o system18_logging.o

# Generate bare-metal adaptations with a strict, cross-platform Python patcher.
# Run via relative paths only. The Python installed here does not reliably
# resolve POSIX /c/... absolute paths handed to it by MSYS shell scripts.
if [ ! -f ./patch_baremetal_alloc.py ]; then
  echo "ERROR: core1/patch_baremetal_alloc.py absent. Unzip FIX2 at project root."
  exit 1
fi
python ./patch_baremetal_alloc.py nes ../third_party/VGMPlay-3CONSOLES/nes_apu.c nes_apu_baremetal.c
"$CC" $CFLAGS -c nes_apu_baremetal.c -o nes_apu_legacy.o
python -c "from pathlib import Path; Path('nes_apu_baremetal.c').unlink()"

python ./patch_baremetal_alloc.py pce ../third_party/VGMPlay-3CONSOLES/Ootake_PSG.c Ootake_PSG_baremetal.c
"$CC" $CFLAGS -c Ootake_PSG_baremetal.c -o pce_ootake.o
python -c "from pathlib import Path; Path('Ootake_PSG_baremetal.c').unlink()"

python ./patch_baremetal_alloc.py opll ../third_party/emu2413/emu2413.c emu2413_baremetal.c
"$CC" $CFLAGS -c emu2413_baremetal.c -o emu2413.o
python -c "from pathlib import Path; Path('emu2413_baremetal.c').unlink()"

# Fast YM2612 (GME GENS). Upstream has exactly one malloc/free pair.
# Do not use -Dmalloc/-Dfree: it rewrites declarations inside <cstdlib>.
GENS_BM="ym2612_gens_baremetal.cpp"
python ./patch_baremetal_alloc.py gens ../third_party/GME-GENS/Ym2612_GENS.cpp "$GENS_BM"
"$CXX" $CXXFLAGS -DVGM_YM2612_GENS -c "$GENS_BM" -o ym2612_gens.o
python -c "from pathlib import Path; Path('ym2612_gens_baremetal.cpp').unlink()"
"$CXX" $CXXFLAGS -DVGM_YM2612_GENS -c "$ROOT/core1/gens2612_wrap.cpp" -o gens2612_wrap.o

"$CC" -nostartfiles -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -marm \
  -Wl,-T,"$ROOT/core1/core1.ld" -Wl,--gc-sections -Wl,-Map,zzvgm_core1.map \
  startup.o zzvgm_core1.o arcade_pcm.o neogeo2610.o qsound_pcm.o es5503_zz.o k053260_zz.o k054539_zz.o k051649_zz.o qsound_hle.o sn76489.o ay8910.o ay2149_ref.o gzip_puff.o baremetal_stubs.o \
  opl3.o ym2151_jarek.o jarek2151_wrap.o opll2413_alloc.o emu2413_wrap.o emu2413.o puff.o ym2612_gens.o gens2612_wrap.o \
  gb_legacy.o gb_dmg_wrap.o nes_apu_legacy.o nes_apu_wrap.o pce_ootake.o huc6280_wrap.o \
  system18_backend.o system18_fm.o system18_rf.o system18_resampler.o system18_logging.o \
  -Wl,--start-group -lm -lc -lgcc -Wl,--end-group \
  -o zzvgm_core1.elf

"$OBJCOPY" -O binary zzvgm_core1.elf zzvgm_core1.bin
"$SIZE" zzvgm_core1.elf
python -c "from pathlib import Path; print('Core1 bytes:', Path('zzvgm_core1.bin').stat().st_size)"
echo "DONE: zzvgm_core1.bin (ZZVGMPlay System 18 candidate)"
