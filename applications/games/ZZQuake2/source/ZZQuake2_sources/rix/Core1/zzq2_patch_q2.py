#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# zzq2_patch_q2.py - ZZQuake2 A0: derive the bare-metal system layer
# from other/q_system.c of quake2generic.
#
#   usage: zzq2_patch_q2.py <quake2generic dir> <output file>
#
# The quake2generic tree is NEVER modified: the upstream file is read,
# a few functions are replaced or removed in memory, the result is written to the
# build directory. Everything else in q_system.c is kept as it is
# (key queue, Sys_SendKeyEvents, Sys_Milliseconds, Sys_GetGameAPI,
# Quake2_Init/Frame/SendKey ...).
#
# STRICT, like zzq_patch_engine.py of the release: every anchor must be
# found exactly once, otherwise the script stops and nothing is built.
#
# ASCII only.

import os
import re
import sys


def die(msg):
    raise SystemExit("zzq2_patch_q2: " + msg)


def span(txt, start, end, new, tag):
    """Replace txt[start-anchor : end-anchor) by new. Both anchors must
    occur exactly once and in this order."""
    if txt.count(start) != 1:
        die("%s: start anchor found %d times (need 1)" % (tag, txt.count(start)))
    if txt.count(end) != 1:
        die("%s: end anchor found %d times (need 1)" % (tag, txt.count(end)))
    a = txt.index(start)
    b = txt.index(end)
    if b <= a:
        die("%s: anchors out of order" % tag)
    print("  q_system.c : %s" % tag)
    return txt[:a] + new + txt[b:]


def sub1(txt, old, new, tag):
    if txt.count(old) != 1:
        die("%s: pattern found %d times (need 1)" % (tag, txt.count(old)))
    print("  q_system.c : %s" % tag)
    return txt.replace(old, new)


def main():
    if len(sys.argv) != 3:
        die("usage: zzq2_patch_q2.py <quake2generic dir> <output file>")
    q2, out = sys.argv[1], sys.argv[2]
    src = os.path.join(q2, "other", "q_system.c")
    s = open(src, encoding="latin-1").read()

    # 1. Sys_ConsoleOutput: there is no stdout. The text goes to the
    #    release zzq_printf (printf is redirected by the forced include),
    #    i.e. to the "last printf" shared slots.
    s = sub1(s, "\tfputs(string, stdout);",
             "\tprintf(\"%s\", string);\t/* ZZQ2: release zzq_printf */",
             "Sys_ConsoleOutput -> zzq_printf")

    # 2. Sys_Printf: provided by the release zzquake_platform.c.
    s = span(s, "void Sys_Printf (char *fmt, ...)", "void Sys_Quit (void)",
             "/* ZZQ2: Sys_Printf is the release one (zzquake_platform.c) */\n\n",
             "Sys_Printf removed (release function used)")

    # 3. Sys_Quit: no _Exit() on Core1. Leave through the release exit
    #    path (zzq2_quit -> zzq_fatal_end), see zzq2_glue.c.
    s = span(s, "void Sys_Quit (void)", "void Sys_Init(void)",
             "void Sys_Quit (void)\n"
             "{\n"
             "\t/* ZZQ2: Com_Quit has already shut the server and the client\n"
             "\t   down. No libc exit on Core1: release exit path. */\n"
             "\textern void zzq2_quit(void);\n"
             "\tzzq2_quit();\n"
             "}\n\n",
             "Sys_Quit -> zzq2_quit (no _Exit)")

    # 4. Sys_Error: provided by the release zzquake_platform.c (message
    #    and backtrace in the shared slots, then zzq_fatal_end).
    s = span(s, "void Sys_Error (char *error, ...)",
             "void Sys_Warn (char *warning, ...)",
             "/* ZZQ2: Sys_Error is the release one (zzquake_platform.c) */\n\n",
             "Sys_Error removed (release function used)")

    # 5. Sys_Warn: no stderr. A FILE* that is not a zzq file must never
    #    reach the redirected fprintf.
    s = sub1(s, "\tfprintf(stderr, \"Warning: %s\", string);",
             "\tprintf(\"Warning: %s\", string);\t/* ZZQ2: release zzq_printf */",
             "Sys_Warn -> zzq_printf")

    # 6. Sys_ConsoleInput: provided by the release zzquake_platform.c.
    s = span(s, "char *Sys_ConsoleInput(void)",
             "/*****************************************************************************/\n\n\n/*\n=================\nSys_UnloadGame",
             "/* ZZQ2: Sys_ConsoleInput is the release one (zzquake_platform.c) */\n\n",
             "Sys_ConsoleInput removed (release function used)")

    # 7. Sys_FindFirst / Sys_FindNext / Sys_FindClose: upstream they
    #    find nothing. 0.6: provided by Core1/zzq2_save.c, which lists
    #    the save files of a save directory (save games); every other
    #    pattern still finds nothing there.
    s = span(s, "char * Sys_FindFirst(char *path, unsigned musthave, unsigned canhave)",
             "void ProcessKeyEvent(int key, qboolean down)\n{",
             "/* ZZQ2: Sys_FindFirst/Next/Close are in zzq2_save.c (0.6) */\n\n",
             "Sys_FindFirst/Next/Close removed (zzq2_save.c)")

    # Nothing may be left that needs a hosted libc.
    for bad in (r"\b_Exit\b", r"\bstdout\b", r"\bstderr\b",
                r"\bfputs\b", r"\bputc\b"):
        if re.search(bad, s):
            die("'%s' is still present after patching" % bad)

    open(out, "w", encoding="latin-1", newline="\n").write(s)
    print("  -> %s" % out)


if __name__ == "__main__":
    main()
