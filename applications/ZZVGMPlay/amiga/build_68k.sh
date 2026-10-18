#!/bin/sh
set -e
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
"C:/amiga-gcc/bin/m68k-amigaos-gcc" -O2 -mcrt=nix20 -m68020 -I"$ROOT/include" -c -o ZZVGMPlay.o "$ROOT/amiga/ZZVGMPlay.c"
"C:/amiga-gcc/bin/m68k-amigaos-gcc" -mcrt=nix20 -m68020 -o ZZVGMPlay ZZVGMPlay.o -lamiga
python -c "from pathlib import Path; print('ZZVGMPlay bytes:', Path('ZZVGMPlay').stat().st_size)"
