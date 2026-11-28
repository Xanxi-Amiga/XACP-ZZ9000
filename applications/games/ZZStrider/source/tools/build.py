#!/usr/bin/env python3
"""
build.py - offline build of ZZStrider, started by build_all.sh.

  1. source checks (tools/check_sources.py)
  2. Core1 blob        -> out/zzstrider.bin        (arm-none-eabi-gcc / g++)
  3. launcher and GUI  -> out/ZZStrider, out/ZZStriderGUI   (m68k-amigaos-gcc, -mcrt=nix20)
  4. checksums         -> out/CHECKSUMS.txt
  5. Amiga drawer      -> out/ZZStrider_Amiga/
  6. release archives  -> out/ZZStrider_1.0.zip      (Amiga drawer: binaries, readme, licences, NO ROM)
                          out/ZZStrider_1.0_src.zip  (PUBLIC: Core1 blob sources only, never amiga_private/)

No download, no change to PATH. The compilers are called through the explicit paths below; each
one can be overridden with an environment variable of the same name (ZZS_ARM_GCC, ...), which is
how the blob part of this script is exercised on a non-Windows machine.
Options: --blob-only  build the Core1 blob only (automatic when amiga_private/ is absent)
ASCII only.
"""
import fnmatch
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ZZS_ARM_GCC = "C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-gcc"
ZZS_ARM_GXX = "C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-g++"
ZZS_ARM_OBJCOPY = "C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-objcopy"
ZZS_M68K_GCC = "C:/amiga-gcc/bin/m68k-amigaos-gcc"

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "out"
OBJ = OUT / "obj_arm"
BLOB_MAX = 0x1F0000
RELEASE = "1.0"
ZIP_DATE = (2026, 11, 26, 12, 0, 0)          # fixed date in the archives: same sources, same zip

# PUBLIC source archive: what goes in, relative to the package root. Nothing else is taken, and
# make_src_zip() refuses to write if a private file or a ROM name would end up inside.
SRC_TREES = ["arm", "third_party", "licenses", "tests/host", "tests/synth"]
SRC_FILES = ["build_all.sh", "tools/build.py", "tools/check_sources.py", "tests/run_host_test.sh",
             "tests/check_host.py", "tests/make_synth_roms.py", "docs/ZZStrider.readme", "docs/README_SOURCES.md"]
SRC_SKIP = ["*.o", "*.elf", "*.map", "__pycache__", "*.pyc", "work"]
ROM_NAMES = ["30.11f", "35.11h", "31.12f", "36.12h", "st-14.8h", "st-2.8a", "st-11.10a", "st-5.4a",
             "st-9.6a", "st-1.7a", "st-10.9a", "st-4.3a", "st-8.5a", "09.12b", "18.11c", "19.12c"]

CPU = ["-mcpu=cortex-a9", "-marm", "-mfpu=vfpv3-d16", "-mfloat-abi=hard"]
# Every compiler is started with the package root as current directory and is given RELATIVE
# paths with forward slashes only: no path has to be translated between MSYS and Windows.
INC = ["-I" + p for p in ("arm", "arm/burn_shim", "arm/cps1", "third_party",
                          "third_party/cyclone", "third_party/cz80", "third_party/ym2151")]
OBJ_REL = "out/obj_arm"
COMMON = ["-O2"] + CPU + INC + ["-ffreestanding", "-fno-builtin", "-fno-common", "-fomit-frame-pointer",
                                "-fno-stack-protector", "-fno-strict-aliasing"]
CFLAGS = COMMON + ["-Wall", "-Wno-unused"]
CXXFLAGS = COMMON + ["-std=gnu++14", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                     "-fno-use-cxa-atexit", "-Wall", "-Wno-unused", "-Wno-write-strings"]

# (source, extra flags). zzs_entry.S must stay first: it holds the entry point of the blob.
ARM_C = [
    ("arm/zzs_entry.S", []), ("arm/zzs_vectors.S", []), ("arm/zzs_main.c", []), ("arm/zzs_mmu.c", []),
    ("arm/zzs_platform.c", ["-fno-tree-loop-distribute-patterns"]),   # memset must not call memset
    ("arm/zzs_rom.c", []), ("arm/zzs_video.c", []), ("arm/zzs_audio.c", []), ("arm/zzs_sound.c", []),
    ("arm/zzs_msm6295.c", []), ("arm/zzs_ym_jarek.c", []),
    ("third_party/ym2151/ym2151.c", ["-w"]), ("third_party/cz80/cz80.c", ["-w"]),
    ("third_party/cyclone/generated/Cyclone.s", []),
]
ARM_CXX = ["arm/zzs_burn.cpp", "arm/zzs_cps_load.cpp", "arm/zzs_cps_run.cpp"] + \
          ["arm/cps1/%s.cpp" % n for n in ("cps_config", "cps_draw", "cps_mem", "cps_obj", "cps_pal", "cps_rw",
                                           "cps_scr", "cpsr", "cpsrd", "cpst", "ctv")]
AMIGA_FLAGS = ["-O2", "-mcrt=nix20", "-m68020", "-Wno-pointer-sign", "-I../arm"]


def tool(name):
    return os.environ.get(name, globals()[name])


def find_tool(name):
    t = tool(name)
    if Path(t).exists() or Path(t + ".exe").exists() or shutil.which(t):
        return t
    sys.exit("ERROR: compiler not found: %s\n       (edit the path at the top of tools/build.py, or set %s)" % (t, name))


def run(cmd, cwd=ROOT):
    print("  " + " ".join(Path(c).name if i == 0 else c for i, c in enumerate(cmd))[:150])
    r = subprocess.run(cmd, cwd=str(cwd))
    if r.returncode != 0:
        sys.exit("ERROR: command failed (%d)" % r.returncode)


def digest(path):
    data = path.read_bytes()
    return len(data), hashlib.md5(data).hexdigest(), hashlib.sha256(data).hexdigest()


def build_blob():
    gcc, gxx, objcopy = find_tool("ZZS_ARM_GCC"), find_tool("ZZS_ARM_GXX"), find_tool("ZZS_ARM_OBJCOPY")
    if OBJ.exists():
        shutil.rmtree(OBJ)
    OBJ.mkdir(parents=True)
    objs = []
    for src, extra in ARM_C:
        o = "%s/%s.o" % (OBJ_REL, Path(src).stem)
        run([gcc] + CFLAGS + extra + ["-c", src, "-o", o])
        objs.append(o)
    for src in ARM_CXX:
        o = "%s/%s.o" % (OBJ_REL, Path(src).stem)
        run([gxx] + CXXFLAGS + ["-c", src, "-o", o])
        objs.append(o)
    run([gcc] + CPU + ["-nostdlib", "-T", "arm/zzstrider.ld"] + objs +
        ["-lm", "-lc", "-lgcc", "-Wl,-Map," + OBJ_REL + "/zzstrider.map", "-o", OBJ_REL + "/zzstrider.elf"])
    blob = OUT / "zzstrider.bin"
    run([objcopy, "-O", "binary", OBJ_REL + "/zzstrider.elf", "out/zzstrider.bin"])
    size = blob.stat().st_size
    if size > BLOB_MAX or size % 2:
        sys.exit("ERROR: blob size %d is not acceptable (max %d, even)" % (size, BLOB_MAX))
    return blob


def build_amiga():
    gcc = find_tool("ZZS_M68K_GCC")
    src = ROOT / "amiga_private"
    # The compiler is started FROM the source directory with plain file names.
    run([gcc] + AMIGA_FLAGS + ["-c", "zzs_ahi.c", "-o", "../out/zzs_ahi.o"], cwd=src)
    run([gcc] + AMIGA_FLAGS + ["-o", "../out/ZZStrider", "ZZStrider.c", "../out/zzs_ahi.o", "-lamiga"], cwd=src)
    run([gcc] + AMIGA_FLAGS + ["-o", "../out/ZZStriderGUI", "ZZStriderGUI.c", "-lamiga"], cwd=src)
    return [OUT / "ZZStrider", OUT / "ZZStriderGUI"]


def zip_add(z, arc, data):
    info = zipfile.ZipInfo(arc, ZIP_DATE)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    z.writestr(info, data)


def skipped(rel):
    return any(fnmatch.fnmatch(part, pat) for part in Path(rel).parts for pat in SRC_SKIP)


def make_src_zip():
    top = "ZZStrider_%s_src/" % RELEASE
    files = []
    for t in SRC_TREES:
        for p in sorted((ROOT / t).rglob("*")):
            rel = p.relative_to(ROOT).as_posix()
            if p.is_file() and not skipped(rel):
                files.append(rel)
    files += SRC_FILES
    for rel in files:
        low = rel.lower()
        if low.startswith("amiga_private") or Path(rel).name.lower() in ROM_NAMES or low.startswith("out/"):
            sys.exit("ERROR: %s must not be in the public source archive" % rel)
        if ("PRIVATE" + " SOURCE").encode() in (ROOT / rel).read_bytes():
            sys.exit("ERROR: %s carries the private-source marker" % rel)
    path = OUT / ("ZZStrider_%s_src.zip" % RELEASE)
    with zipfile.ZipFile(path, "w") as z:
        zip_add(z, top + "README.md", (ROOT / "docs/README_SOURCES.md").read_bytes())
        for rel in files:
            zip_add(z, top + rel, (ROOT / rel).read_bytes())
    return path, len(files) + 1


def make_amiga_zip(drawer):
    top = "ZZStrider/"
    path = OUT / ("ZZStrider_%s.zip" % RELEASE)
    n = 0
    with zipfile.ZipFile(path, "w") as z:
        for p in sorted(drawer.rglob("*")):
            if p.is_file():
                zip_add(z, top + p.relative_to(drawer).as_posix(), p.read_bytes())
                n += 1
    return path, n


def main():
    blob_only = "--blob-only" in sys.argv or not (ROOT / "amiga_private").is_dir()
    OUT.mkdir(exist_ok=True)
    print("[1/6] source checks")
    run([sys.executable, "tools/check_sources.py"])
    print("[2/6] Core1 blob")
    products = [build_blob()]
    if blob_only:
        print("[3/6] launcher and GUI: skipped (blob only)")
    else:
        print("[3/6] launcher and GUI")
        products += build_amiga()
    print("[4/6] checksums")
    lines = []
    for p in products:
        size, md5, sha = digest(p)
        lines.append("%-14s %8d bytes  MD5 %s  SHA256 %s" % (p.name, size, md5, sha))
        print("  " + lines[-1][:78])
    (OUT / "CHECKSUMS.txt").write_text("\n".join(lines) + "\n", encoding="ascii")
    if blob_only:
        print("[5/6] Amiga drawer: skipped (blob only)")
    else:
        print("[5/6] Amiga drawer")
        drawer = OUT / "ZZStrider_Amiga"
        if drawer.exists():
            shutil.rmtree(drawer)
        (drawer / "roms").mkdir(parents=True)
        for p in products:
            shutil.copy2(p, drawer / p.name)
        shutil.copy2(ROOT / "docs/ZZStrider.readme", drawer / "ZZStrider.readme")
        shutil.copy2(ROOT / "docs/LISEZMOI.txt", drawer / "LISEZMOI.txt")
        shutil.copy2(OUT / "CHECKSUMS.txt", drawer / "CHECKSUMS.txt")
        (drawer / "licenses").mkdir()
        for lic in sorted((ROOT / "licenses").iterdir()):
            shutil.copy2(lic, drawer / "licenses" / lic.name)
        (drawer / "roms" / "PUT_THE_16_STRIDER_ROM_FILES_HERE.txt").write_text(
            "Unzip the MAME set \"strider\" (parent set) here. No ROM is shipped with ZZStrider.\n", encoding="ascii")
        print("  " + str(drawer))
    print("[6/6] release archives")
    if (ROOT / "amiga_private").is_dir():
        path, n = make_src_zip()
        print("  %s (%d files, public: no launcher or GUI source)  MD5 %s" % (path.name, n, digest(path)[1]))
    else:
        print("  source archive: skipped (this is already the public source tree)")
    if not blob_only:
        path, n = make_amiga_zip(drawer)
        print("  %s (%d files, no ROM)  MD5 %s" % (path.name, n, digest(path)[1]))
    print("BUILD OK")


if __name__ == "__main__":
    main()
