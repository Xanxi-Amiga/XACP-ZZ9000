#!/usr/bin/env python3
"""
check_sources.py - pre-build checks of the ZZStrider sources (run by tools/build.py).

  - our own sources are pure ASCII and have balanced braces / parentheses
  - every file the build needs is present (the build is offline: nothing can be fetched)
  - the generated Cyclone core is the documented one (SHA-256)
Third-party files kept verbatim (CZ80, Cyclone, YM2151, the unmodified FB Alpha files) are not
subject to the ASCII rule: they are byte-identical to their upstream. ASCII only.
"""
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CYCLONE_SHA256 = "3984169530a6c6e4b8cf373de66ed686644b34cf375331be37aef52fe06c1f4e"
OWN = ["arm/*.c", "arm/*.h", "arm/*.S", "arm/*.cpp", "arm/*.ld", "arm/burn_shim/*.h", "arm/cps1/cps_draw.cpp",
       "arm/cps1/cps_pal.cpp", "amiga_private/*.c", "tools/*.py", "tests/*.py", "tests/*.sh", "tests/host/*.c",
       "tests/synth/*", "third_party/pico/pico_int.h", "third_party/pico/memory.h",
       "third_party/cyclone/cyclone_config_zzstrider.h", "build_all.sh", "docs/ZZStrider.readme",
       "docs/LISEZMOI.txt", "docs/README_SOURCES.md", "licenses/README.md"]
NEEDED = ["arm/zzstrider.ld", "arm/cps1/ctv.h", "third_party/cyclone/generated/Cyclone.s",
          "third_party/cz80/cz80.c", "third_party/cz80/cz80jmp.c", "third_party/ym2151/ym2151.c",
          "third_party/pico/pico_port.h", "third_party/pico/pico_types.h",
          "docs/README_SOURCES.md"]
if (ROOT / "amiga_private").is_dir():
    NEEDED.append("docs/ZZStrider.readme")
errors = []
count = 0
for pattern in OWN:
    for path in sorted(ROOT.glob(pattern)):
        if not path.is_file():
            continue
        count += 1
        data = path.read_bytes()
        bad = [i for i, b in enumerate(data) if b > 126 or (b < 32 and b not in (9, 10, 13))]
        if bad:
            errors.append("%s: non-ASCII byte at offset %d" % (path.relative_to(ROOT), bad[0]))
        if path.suffix in (".c", ".h", ".cpp"):
            text = data.decode("ascii", "replace")
            for a, b in ("{}", "()"):
                if text.count(a) != text.count(b):
                    errors.append("%s: unbalanced %s%s (%d / %d)" % (path.relative_to(ROOT), a, b, text.count(a), text.count(b)))
for rel in NEEDED:
    if not (ROOT / rel).is_file():
        errors.append("missing file: " + rel)
cyc = ROOT / "third_party/cyclone/generated/Cyclone.s"
if cyc.is_file() and hashlib.sha256(cyc.read_bytes()).hexdigest() != CYCLONE_SHA256:
    errors.append("third_party/cyclone/generated/Cyclone.s is not the documented generated file")
for e in errors:
    print("  CHECK FAILED: " + e)
if errors:
    sys.exit(1)
print("  %d source files checked: ASCII, balanced, complete" % count)
