#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# zzq2_svtab.py - ZZQuake2 0.8: name tables of the save games.
#
#   usage: zzq2_svtab.py <quake2generic dir> <build dir>
#
# A Quake II save stores, for every entity, the functions it calls
# (think, touch, use, pain, die, the monster behaviour) and the animation a
# monster is playing (monsterinfo.currentmove, an mmove_t). Upstream
# writes them as offsets from InitGame and from mmove_reloc, which
# only the very program that wrote the save can read back. 0.8 writes
# their NAMES instead (patched copy of game/g_save.c): a save is then
# read by any blob built from the same game code - 320, 640, 800,
# software or TinyGL - and by later versions.
#
# This script makes the two tables, from the game objects already
# compiled in <build dir>/q2obj (game_*.o):
#   - every function of the game code (FUNC symbols; compiler copies
#     such as "x.part.0" are left out, their address is never stored),
#   - every mmove_t variable (OBJECT symbols whose definition in
#     game/*.c is "mmove_t name =").
# Global symbols are reached by name from the table file. A static
# symbol cannot be: its object is compiled again from a wrapper file
# that includes the unchanged source and adds a global alias of the
# static symbol (gcc alias attribute, same address).
#
# Written to <build dir>/gen:
#   zzq2_svtab_gen.c   the two tables, sorted by name
#   zzsv_<object>.c    one wrapper per object with static symbols
#   zzsv_rebuild.txt   "<object> <wrapper>" per line, for build_arm.sh
#                      (compiled with -I<quake2generic>/game)
#
# ZZQ2_READELF in the environment: the readelf of the toolchain
# (default arm-none-eabi-readelf).
#
# The quake2generic tree is NEVER modified. STRICT: a name longer than
# the reader allows, or a name found twice, stops the build. ASCII only.

import os
import re
import subprocess
import sys

NAME_MAX = 64            # with its 0, as ZZSV_NAME_MAX in zzq2_svtab.c


def die(msg):
    raise SystemExit("zzq2_svtab: " + msg)


def symbols(obj):
    """(name, kind, bind) of the defined FUNC and OBJECT symbols."""
    tool = os.environ.get("ZZQ2_READELF") or "arm-none-eabi-readelf"
    out = subprocess.check_output([tool, "-sW", obj]).decode("latin-1")
    res = []
    for line in out.splitlines():
        f = line.split()
        # Num: Value Size Type Bind Vis Ndx Name
        if len(f) < 8 or not f[0].endswith(":") or f[6] == "UND":
            continue
        if f[3] in ("FUNC", "OBJECT") and f[4] in ("GLOBAL", "LOCAL", "WEAK"):
            if "." in f[7] or f[7].startswith("$"):
                continue
            res.append((f[7], f[3], f[4]))
    return res


def main():
    if len(sys.argv) != 3:
        die("usage: zzq2_svtab.py <quake2generic dir> <build dir>")
    q2, b = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    gdir = os.path.join(q2, "game")

    moves = set()
    for n in sorted(os.listdir(gdir)):
        if n.endswith(".c"):
            txt = open(os.path.join(gdir, n), encoding="latin-1").read()
            moves.update(re.findall(r"^\s*(?:static\s+)?mmove_t\s+(\w+)\s*=", txt, re.M))
    if not moves:
        die("no mmove_t found in %s" % gdir)

    objs = sorted(o for o in os.listdir(os.path.join(b, "q2obj")) if o.startswith("game_") and o.endswith(".o"))
    if len(objs) < 40:
        die("only %d game objects in %s/q2obj" % (len(objs), b))

    funcs, mmoves = {}, {}           # table name -> C symbol
    statics = {}                     # object -> [(symbol, alias)]
    allsyms = [(o, symbols(os.path.join(b, "q2obj", o))) for o in objs]
    # globals first: a static of the same name gets "<file>:<name>"
    for want in ("GLOBAL", "LOCAL"):
        for o, syms in allsyms:
            for name, kind, bind in syms:
                if (bind == "LOCAL") != (want == "LOCAL"):
                    continue
                if kind == "OBJECT" and name not in moves:
                    continue
                table = funcs if kind == "FUNC" else mmoves
                if bind == "LOCAL":
                    alias = "zzsv_l_%s_%s" % (o[5:-2], name)
                    statics.setdefault(o, []).append((name, alias))
                    key = name if name not in table else "%s:%s" % (o[5:-2], name)
                    csym = alias
                else:
                    key, csym = name, name
                if key in table:
                    die("name '%s' found twice" % key)
                if len(key) + 1 > NAME_MAX:
                    die("name '%s' longer than %d" % (key, NAME_MAX - 1))
                table[key] = csym

    g = os.path.join(b, "gen")
    os.makedirs(g, exist_ok=True)
    rebuild = []
    for o, lst in sorted(statics.items()):
        src = o[5:-2]                                   # g_items
        # found through -I<quake2generic>/game (g_save: its patched copy,
        # next to the wrapper); no absolute path in the file, so that a
        # Windows compiler started from MSYS reads it too
        inc = "g_save_zz.c" if src == "g_save" else src + ".c"
        wn = "zzsv_%s.c" % src
        w = os.path.join(g, wn)
        with open(w, "w", newline="\n") as f:
            f.write("/* ZZQuake2 0.8 - generated by zzq2_svtab.py, do not edit.\n"
                    "   game/%s.c unchanged, plus a global alias of its static\n"
                    "   symbols for the save name tables. */\n" % src)
            f.write('#include "%s"\n\n' % inc)
            for name, alias in lst:
                f.write("extern __typeof__(%s) %s __attribute__((alias(\"%s\")));\n"
                        % (name, alias, name))
        rebuild.append("%s %s" % (o, wn))
    open(os.path.join(g, "zzsv_rebuild.txt"), "w", newline="\n").write(
        "".join(r + "\n" for r in rebuild))

    with open(os.path.join(g, "zzq2_svtab_gen.c"), "w", newline="\n") as f:
        f.write("/* ZZQuake2 0.8 - generated by zzq2_svtab.py, do not edit.\n"
                "   Functions and monster moves of the game code, by name, for\n"
                "   the save games (Core1/zzq2_svtab.c). Sorted by name. */\n\n"
                "typedef struct { const char *name; const void *addr; } zzsv_sym_t;\n\n")
        syms = sorted(set(funcs.values()) | set(mmoves.values()))
        for s in syms:
            f.write("extern char %s[];\n" % s)
        for tag, table in (("func", funcs), ("move", mmoves)):
            keys = sorted(table)
            f.write("\nconst zzsv_sym_t zzsv_%s_tab[%d] = {\n" % (tag, len(keys)))
            for k in keys:
                f.write("\t{ \"%s\", %s },\n" % (k, table[k]))
            f.write("};\nconst int zzsv_%s_n = %d;\n" % (tag, len(keys)))
            f.write("unsigned short zzsv_%s_byaddr[%d];\n" % (tag, len(keys)))
    print("  %d functions (%d static), %d monster moves, %d objects wrapped"
          % (len(funcs), sum(1 for v in funcs.values() if v.startswith("zzsv_l_")),
             len(mmoves), len(rebuild)))


if __name__ == "__main__":
    main()
