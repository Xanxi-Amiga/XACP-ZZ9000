#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# zzqgl_footprint.py - ZZQuakeGL: layout proof and footprint report
# (zzq2_footprint.py of ZZQuake2 A0, adapted: .glbss, q1obj/).
#
#   usage: zzqgl_footprint.py <elf> <map> <bin> [<nm>]
#
# Reads the linker map and the symbol table, prints the sizes (.text /
# .rodata / .data / .bss / .glbss, blob file) and FAILS (exit 1) if the
# image does not respect the release layout.
#
# ASCII only.

import os
import re
import subprocess
import sys

BLOB_BASE     = 0x04900000
BLOB_LIMIT    = 0x04B00000      # shared block: release ASSERT
BLOB_FILE_MAX = 0x200000        # launcher: "Bad blob size" above this
ARENA_BASE    = 0x30000000
ARENA_END     = 0x3F800000      # firmware guard, never mapped
GPU_LO        = 0x3D800000      # GPU reservation (zzq2rix_gpu.c)
GLBSS_BASE    = 0x3EC00000
GLBSS_LIMIT   = 0x3F800000      # 12 MiB reserve, up to the end of the arena
HEAP_BASE     = 0x07000000      # release newlib heap (hunk)
HEAP_SIZE     = 0x01000000
PAK_SHARE     = 18689235        # pak0.pak shareware 1.06
PAK_ID1       = 18689235 + 34257856   # pak0 + pak1 registered (sizes as commonly published)


def symbols(elf, nm):
    out = subprocess.check_output([nm, elf]).decode("latin-1")
    syms = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    return syms


def parse_map(path):
    """Yield (output_section, input_section, address, size, file)."""
    txt = open(path, encoding="latin-1").read()
    txt = txt[txt.index("Linker script and memory map"):]
    cur = None
    pend = None
    rows = []
    for line in txt.splitlines():
        m = re.match(r"^(\.[\w.]+)\s", line + " ")
        if m and not line.startswith(" "):
            cur = m.group(1)
            pend = None
            continue
        if cur is None:
            continue
        m = re.match(r"^ (\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", line)
        if m:
            rows.append((cur, m.group(1), int(m.group(2), 16),
                         int(m.group(3), 16), m.group(4)))
            pend = None
            continue
        m = re.match(r"^ (\S+)$", line)
        if m:
            pend = m.group(1)
            continue
        m = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", line)
        if m and pend:
            rows.append((cur, pend, int(m.group(1), 16),
                         int(m.group(2), 16), m.group(3)))
        pend = None
    return rows


def launcher_sum(data):
    """Same value as 'empreinte du blob : somme=' printed by the launcher."""
    s = 0
    for k in range(len(data) >> 2):
        v = int.from_bytes(data[k * 4:k * 4 + 4], "little")
        s = (((s << 1) & 0xFFFFFFFF) ^ (s >> 31) ^ v) & 0xFFFFFFFF
    return s


def main():
    elf, mapf, binf = sys.argv[1:4]
    nm = sys.argv[4] if len(sys.argv) > 4 else os.environ.get("ZZQGL_NM", "arm-none-eabi-nm")
    syms = symbols(elf, nm)
    rows = parse_map(mapf)
    data = open(binf, "rb").read()
    errors = []

    def is_q1(f):
        return "q1obj/" in f.replace("\\", "/")

    code = ro = dat = bss = glbss = 0
    bss_leak = []
    glbss_foreign = []
    bss_big = []
    glbss_big = []
    for osec, isec, addr, size, f in rows:
        if size == 0:
            continue
        if osec == ".text":
            if isec.startswith(".rodata"):
                ro += size
            else:
                code += size
        elif osec == ".data":
            dat += size
        elif osec == ".bss":
            bss += size
            bss_big.append((size, isec, f))
            if is_q1(f):
                bss_leak.append((isec, f))
        elif osec == ".glbss":
            glbss += size
            glbss_big.append((size, isec, f))
            if not is_q1(f):
                glbss_foreign.append((isec, f))

    text_len = syms["_text_end"] - syms["_text_start"]
    data_len = syms["_data_end"] - syms["_data_start"]
    bss_len  = syms["_bss_end"] - syms["_bss_start"]
    gl_len   = syms["_glbss_end"] - syms["_glbss_start"]
    blob_end = syms["_blob_end"]

    if syms.get("zzq_entry") != BLOB_BASE:
        errors.append("zzq_entry is not at 0x%08x" % BLOB_BASE)
    if blob_end > BLOB_LIMIT:
        errors.append("_blob_end 0x%08x passes the shared block" % blob_end)
    if len(data) > BLOB_FILE_MAX:
        errors.append("blob file %d bytes > launcher limit %d" % (len(data), BLOB_FILE_MAX))
    if BLOB_BASE + len(data) > syms["_bss_start"]:
        errors.append("blob file overlaps .bss")
    if syms["_glbss_start"] != GLBSS_BASE:
        errors.append(".glbss is not at 0x%08x" % GLBSS_BASE)
    if syms["_glbss_end"] > GLBSS_LIMIT:
        errors.append(".glbss exceeds its 12 MiB reserve")
    if bss_leak:
        errors.append("%d engine .bss sections ended up in the blob .bss, first: %s"
                      % (len(bss_leak), bss_leak[0]))
    if glbss_foreign:
        errors.append(".glbss holds non-engine sections, first: %s" % (glbss_foreign[0],))
    l1 = syms.get("l1_table")
    if l1 is not None and not (syms["_bss_start"] <= l1 and l1 + 16384 <= syms["_bss_end"]):
        errors.append("l1_table (MMU) is outside the blob .bss")
    if not (syms["_glbss_start"] >= syms["_bss_end"]):
        errors.append(".glbss is not above the blob .bss")

    print("  section sizes (bytes)")
    print("    .text   code                 : %9d" % code)
    print("    .rodata (kept inside .text)  : %9d" % ro)
    print("    .data                        : %9d" % dat)
    print("    .bss    blob window          : %9d   0x%08x..0x%08x"
          % (bss_len, syms["_bss_start"], syms["_bss_end"]))
    print("    .glbss  arena, NOLOAD        : %9d   0x%08x..0x%08x"
          % (gl_len, syms["_glbss_start"], syms["_glbss_end"]))
    print("  release symbols")
    print("    _text_start..end = 0x%08x..0x%08x (%d)" % (syms["_text_start"], syms["_text_end"], text_len))
    print("    _data_start..end = 0x%08x..0x%08x (%d)" % (syms["_data_start"], syms["_data_end"], data_len))
    print("    core1_entry      = 0x%08x" % syms["core1_entry"])
    if l1 is not None:
        print("    l1_table (MMU)   = 0x%08x  (inside blob .bss, as in the release)" % l1)
    print("  blob window 0x%08x..0x%08x" % (BLOB_BASE, BLOB_LIMIT))
    print("    _blob_end        = 0x%08x  margin %d bytes" % (blob_end, BLOB_LIMIT - blob_end))
    print("  blob file")
    print("    size             = %d bytes (launcher limit %d, margin %d)"
          % (len(data), BLOB_FILE_MAX, BLOB_FILE_MAX - len(data)))
    print("    launcher 'somme' = 0x%08x" % launcher_sum(data))
    print("  hunk: release heap 0x%08x..0x%08x (malloc, HUNK= of the launcher, default 12 MiB, 14 max)"
          % (HEAP_BASE, HEAP_BASE + HEAP_SIZE))
    print("  arena 0x%08x..0x%08x" % (ARENA_BASE, ARENA_END))
    print("    paks (launcher)  = 0x%08x.. real end, at most 0x%08x (%d MiB)"
          % (ARENA_BASE, GPU_LO, (GPU_LO - ARENA_BASE) >> 20))
    for name, size in (("shareware pak0, %d bytes" % PAK_SHARE, PAK_SHARE),
                       ("registered pak0+pak1, %d bytes" % PAK_ID1, PAK_ID1)):
        print("      %-36s paks end 0x%08x" % (name, ARENA_BASE + size))
    print("    GPU reservation  = 0x%08x..0x%08x (texture pool 16 MiB, ring, Z, bind lists)"
          % (GPU_LO, GLBSS_BASE))
    print("    .glbss           = 0x%08x..0x%08x  (reserve to 0x%08x, margin %d bytes)"
          % (syms["_glbss_start"], syms["_glbss_end"], GLBSS_LIMIT, GLBSS_LIMIT - syms["_glbss_end"]))
    glbss_big.sort(reverse=True)
    print("  largest items in .glbss")
    for size, isec, f in glbss_big[:8]:
        print("    %8d  %-28s %s" % (size, isec[:28], f.split("/")[-1]))
    bss_big.sort(reverse=True)
    print("  largest items in the blob .bss")
    for size, isec, f in bss_big[:6]:
        print("    %8d  %-28s %s" % (size, isec[:28], f.split("/")[-1]))

    if errors:
        for e in errors:
            print("LAYOUT ERROR: " + e)
        sys.exit(1)
    print("PASS layout (engine BSS only in .glbss, release window respected)")


if __name__ == "__main__":
    main()
