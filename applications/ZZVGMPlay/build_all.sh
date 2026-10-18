#!/bin/sh
set -e
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$ROOT/core1"
sh build_core1.sh
python -c "from pathlib import Path; import shutil; shutil.copy2('zzvgm_core1.bin', '../amiga/zzvgm_core1.bin')"
cd "$ROOT/amiga"
sh build_68k.sh
python -c "from pathlib import Path; print('Core1 bytes:',Path('zzvgm_core1.bin').stat().st_size);print('Amiga bytes:',Path('ZZVGMPlay').stat().st_size)"
