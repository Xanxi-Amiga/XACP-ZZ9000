#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# zzq2_footprint.py - ZZQuake2 A0: layout proof and footprint report.
#
#   usage: zzq2_footprint.py <elf> <map> <bin>
#
# Reads the linker map and the symbol table, prints the sizes the A0
# brief asks for (.text / .rodata / .data / .bss separately, blob size)
# and FAILS (exit 1) if the image does not respect the release layout.
#
# ASCII only.

import os
import re
import subprocess
import sys

BLOB_BASE   = 0x04900000
BLOB_LIMIT  = 0x04B00000      # shared block: release ASSERT
BLOB_FILE_MAX = 0x200000      # launcher: "Bad blob size" above this
ARENA_BASE  = 0x30000000
ARENA_END   = 0x3F800000      # firmware guard, never mapped
Q2BSS_BASE  = 0x3EC00000      # HEAP_LIMIT: the heap ends here
Q2BSS_LIMIT = 0x3F800000      # 12 MiB reserve, up to the end of the arena
HEAP_MIN    = 0x02000000      # ZZQ2_HEAP_MIN of zzq2_glue.c and of the launcher
PAK_DEMO    = 49951322        # pak0.pak of the 3.14 demo, size as announced
PAK_RETAIL  = 183997730       # pak0.pak of the retail game, size to be confirmed


def symbols(elf):
    nm = os.environ.get("ZZQ2_NM", "arm-none-eabi-nm")
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
    syms = symbols(elf)
    rows = parse_map(mapf)
    data = open(binf, "rb").read()
    errors = []

    def is_q2(f):
        return "q2obj/" in f.replace("\\", "/")

    code = ro = dat = bss = q2bss = 0
    bss_q2_leak = []
    q2bss_foreign = []
    bss_big = []
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
            if is_q2(f):
                bss_q2_leak.append((isec, f))
        elif osec == ".q2bss":
            q2bss += size
            if not is_q2(f):
                q2bss_foreign.append((isec, f))

    text_len = syms["_text_end"] - syms["_text_start"]
    data_len = syms["_data_end"] - syms["_data_start"]
    bss_len  = syms["_bss_end"] - syms["_bss_start"]
    q2_len   = syms["_q2bss_end"] - syms["_q2bss_start"]
    blob_end = syms["_blob_end"]

    if syms.get("zzq_entry") != BLOB_BASE:
        errors.append("zzq_entry is not at 0x%08x" % BLOB_BASE)
    if blob_end > BLOB_LIMIT:
        errors.append("_blob_end 0x%08x passes the shared block" % blob_end)
    if len(data) > BLOB_FILE_MAX:
        errors.append("blob file %d bytes > launcher limit %d"
                      % (len(data), BLOB_FILE_MAX))
    if BLOB_BASE + len(data) > syms["_bss_start"]:
        errors.append("blob file overlaps .bss")
    if syms["_q2bss_start"] != Q2BSS_BASE:
        errors.append(".q2bss is not at 0x%08x" % Q2BSS_BASE)
    if syms["_q2bss_end"] > Q2BSS_LIMIT:
        errors.append(".q2bss exceeds its 12 MiB reserve")
    if bss_q2_leak:
        errors.append("%d Quake II .bss sections ended up in the blob .bss, first: %s"
                      % (len(bss_q2_leak), bss_q2_leak[0]))
    if q2bss_foreign:
        errors.append(".q2bss holds non-Quake-II sections, first: %s"
                      % (q2bss_foreign[0],))
    l1 = syms.get("l1_table")
    if l1 is not None and not (syms["_bss_start"] <= l1 and l1 + 16384 <= syms["_bss_end"]):
        errors.append("l1_table (MMU) is outside the blob .bss")
    # core1_entry clears _bss_start.._bss_end only: prove .q2bss is disjoint
    if not (syms["_q2bss_start"] >= syms["_bss_end"]):
        errors.append(".q2bss is not above the blob .bss")

    print("  section sizes (bytes)")
    print("    .text   code                 : %9d" % code)
    print("    .rodata (kept inside .text)  : %9d" % ro)
    print("    .data                        : %9d" % dat)
    print("    .bss    blob window          : %9d   0x%08x..0x%08x"
          % (bss_len, syms["_bss_start"], syms["_bss_end"]))
    print("    .q2bss  arena, NOLOAD        : %9d   0x%08x..0x%08x"
          % (q2_len, syms["_q2bss_start"], syms["_q2bss_end"]))
    print("  release symbols")
    print("    _text_start..end = 0x%08x..0x%08x (%d)"
          % (syms["_text_start"], syms["_text_end"], text_len))
    print("    _data_start..end = 0x%08x..0x%08x (%d)"
          % (syms["_data_start"], syms["_data_end"], data_len))
    print("    core1_entry      = 0x%08x" % syms["core1_entry"])
    if l1 is not None:
        print("    l1_table (MMU)   = 0x%08x  (inside blob .bss, as in the release)" % l1)
    print("  blob window 0x%08x..0x%08x" % (BLOB_BASE, BLOB_LIMIT))
    print("    _blob_end        = 0x%08x  margin %d bytes"
          % (blob_end, BLOB_LIMIT - blob_end))
    print("  blob file")
    print("    size             = %d bytes (launcher limit %d, margin %d)"
          % (len(data), BLOB_FILE_MAX, BLOB_FILE_MAX - len(data)))
    print("    launcher 'somme' = 0x%08x" % launcher_sum(data))
    print("  arena 0x%08x..0x%08x" % (ARENA_BASE, ARENA_END))
    lim = syms["_q2bss_start"]
    print("    pak0 (launcher)  = 0x%08x.. real end, at most 0x%08x (%d bytes)"
          % (ARENA_BASE, lim - HEAP_MIN, lim - HEAP_MIN - ARENA_BASE))
    print("    Quake II heap    = HEAP_START..0x%08x, HEAP_START = next MiB after the pak,"
          % lim)
    print("                       at least %d MiB (ZZQ2_HEAP_MIN)" % (HEAP_MIN >> 20))
    for name, size in (("demo 3.14 pak, %d bytes" % PAK_DEMO, PAK_DEMO),
                       ("retail pak, %d bytes" % PAK_RETAIL, PAK_RETAIL)):
        end = ARENA_BASE + size
        lo = (end + 0xFFFFF) & 0xFFF00000
        print("      %-32s pak ends 0x%08x, heap 0x%08x..0x%08x = %d MiB"
              % (name, end, lo, lim, (lim - lo) >> 20))
    print("    .q2bss           = 0x%08x..0x%08x  (reserve to 0x%08x, margin %d bytes)"
          % (syms["_q2bss_start"], syms["_q2bss_end"], Q2BSS_LIMIT,
             Q2BSS_LIMIT - syms["_q2bss_end"]))
    bss_big.sort(reverse=True)
    print("  largest items left in the blob .bss")
    for size, isec, f in bss_big[:6]:
        print("    %8d  %-28s %s" % (size, isec[:28], f.split("/")[-1]))

    if errors:
        for e in errors:
            print("LAYOUT ERROR: " + e)
        sys.exit(1)
    print("PASS layout (Quake II BSS only in .q2bss, release window respected)")


if __name__ == "__main__":
    main()
