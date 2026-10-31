#!/usr/bin/env bash
# Applique les 2 micro-patchs V1.0 a l'arbre PicoDrive (2.04, commit 26ecb2b) et
# pose le header emu2413. Idempotent : ne fait rien si deja applique.
# Usage : bash picodrive_patches.sh [chemin_arbre_picodrive]   (defaut: ./picodrive)
set -e
P="${1:-./picodrive}"
HERE="$(cd "$(dirname "$0")" && pwd)"
[ -f "$P/pico/pico_int.h" ] || { echo "ERREUR: $P n'est pas un arbre PicoDrive"; exit 1; }

# Patch 1 : macro variadique Pico32xStateLoaded
if grep -q '^#define Pico32xStateLoaded()\s*$' "$P/pico/pico_int.h"; then
  sed -i 's/^#define Pico32xStateLoaded()\s*$/#define Pico32xStateLoaded(...)/' "$P/pico/pico_int.h"
  echo "patch1: pico_int.h Pico32xStateLoaded(...) applique"
else
  echo "patch1: deja applique (ou introuvable) - OK"
fi

# Patch 2 : garde #ifndef NO_32X autour du memset(p32x_event_times) dans state_load
if grep -q '^  memset(p32x_event_times, 0, sizeof(p32x_event_times));' "$P/pico/state.c"; then
  python3 - "$P/pico/state.c" << 'PY'
import sys
p=sys.argv[1]; s=open(p,encoding='latin1').read()
old="  memset(p32x_event_times, 0, sizeof(p32x_event_times));"
new="#ifndef NO_32X\n  memset(p32x_event_times, 0, sizeof(p32x_event_times));\n#endif"
s=s.replace(old,new,1); open(p,'w',encoding='latin1').write(s)
PY
  echo "patch2: state.c garde NO_32X applique"
else
  echo "patch2: deja applique (ou introuvable) - OK"
fi

# emu2413 header (pour #include \"sound/emu2413/emu2413.h\" de sms.c)
mkdir -p "$P/pico/sound/emu2413"
if [ ! -f "$P/pico/sound/emu2413/emu2413.h" ]; then
  cp "$HERE/emu2413_for_picodrive_tree.h" "$P/pico/sound/emu2413/emu2413.h"
  echo "emu2413: header pose dans l'arbre PicoDrive"
else
  echo "emu2413: header deja present - OK"
fi
echo "OK - arbre PicoDrive pret pour build_sms.sh"
