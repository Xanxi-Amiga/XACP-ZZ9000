#!/bin/sh
# build_all.sh - ZZPicoDriveSMS v1.2, complete offline build.
#
#   sh build_all.sh
#
# 1. ARM Core1 blob        build_arm.sh          -> bin/zzsms_v1.2.bin
# 2. blob -> C array       tools/bin2c.py         -> amiga/blob_data.c
# 3. 68k launcher          m68k-amigaos-gcc       -> release/ZZPicoDriveSMS/ZZPicoDriveSMS
# 4. release folder + SHA-256 of the binaries (release/SHA256SUMS.txt)
#
# Tools are called by their explicit Windows paths; PATH is not modified:
#   ARM : C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-gcc   (override ARM_BIN=<dir>)
#   68k : C:/amiga-gcc/bin/m68k-amigaos-gcc                     (override M68K_GCC=<file>)
#   68k C runtime: -mcrt=nix20                                  (override M68K_CRT=...)
#   Python 3 (bin2c, checksums): python3, else python           (override PYTHON=<file>)
# Everything needed is in this folder: no download, no git.
set -e
cd "$(dirname "$0")"
ARM_BIN="${ARM_BIN:-C:/ArmGNUToolchain/13.2 Rel1/bin}"
M68K_GCC="${M68K_GCC:-C:/amiga-gcc/bin/m68k-amigaos-gcc}"
M68K_CRT="${M68K_CRT:--mcrt=nix20}"
BLOB=bin/zzsms_v1.2.bin
REF_MD5=4a7a7b5158c75b022ada6ab2f9ea2bba   # reference blob built for this package (GCC 13.2.1)
export ARM_BIN

if [ -z "$PYTHON" ]; then
  if command -v python3 >/dev/null 2>&1; then PYTHON=python3
  elif command -v python >/dev/null 2>&1; then PYTHON=python
  fi
fi

echo "=== 1/4 ARM Core1 blob ==="
sh build_arm.sh

echo "=== 2/4 embed $BLOB ==="
if [ -n "$PYTHON" ]; then "$PYTHON" tools/bin2c.py "$BLOB" amiga/blob_data.c
else echo "(no Python: shell bin2c)"; bash tools/bin2c.sh "$BLOB" amiga/blob_data.c; fi

echo "=== 3/4 68k launcher ==="
mkdir -p release/ZZPicoDriveSMS
( cd amiga && "$M68K_GCC" -O2 -m68020 $M68K_CRT -Wno-pointer-sign \
      -o ../release/ZZPicoDriveSMS/ZZPicoDriveSMS ZZPicoDriveSMS.c blob_data.c -lamiga )

echo "=== 4/4 release folder ==="
cp README.md INSTALL.md RELEASE_NOTES_v1.2.md FM_GAMES.md COPYING_PICODRIVE.txt THIRD_PARTY_NOTICES.md release/ZZPicoDriveSMS/
cp "$BLOB" release/
if [ -n "$PYTHON" ]; then
  "$PYTHON" - "$REF_MD5" <<'PY'
import hashlib, sys
from pathlib import Path
ref = sys.argv[1]
files = [Path("release/zzsms_v1.2.bin"), Path("release/ZZPicoDriveSMS/ZZPicoDriveSMS")]
lines = []
for f in files:
    d = f.read_bytes()
    lines.append("%s  %s  (%d bytes)" % (hashlib.sha256(d).hexdigest(), f.as_posix(), len(d)))
Path("release/SHA256SUMS.txt").write_text("\n".join(lines) + "\n")
print("\n".join(lines))
m = hashlib.md5(files[0].read_bytes()).hexdigest()
if m == ref:
    print("blob identical to the reference (md5 %s)" % m)
else:
    print("NOTE: blob md5 %s differs from the reference %s (other toolchain/libc build):" % (m, ref))
    print("      check one PAL and one NTSC game before publishing, or embed reference/zzsms_v1.2_reference.bin")
PY
fi
echo "done: copy release/ZZPicoDriveSMS/ to the Amiga"
