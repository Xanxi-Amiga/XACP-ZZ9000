#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# zzq2_patch_render.py - ZZQuake2: renderer optimisations, applied to a
# COPY of ref_soft/r_scan.c of quake2generic.
#
#   usage: zzq2_patch_render.py <quake2generic dir> <output file>
#
# The quake2generic tree is NEVER modified. STRICT: every anchor must be
# found exactly once, otherwise the script stops and nothing is built.
#
# Rule of these optimisations (the same as the ZZQuake 1 campaign,
# "baseline L"): the image must not change. No change to the
# perspective computation, the texture coordinates, the rounding, the
# clamps or the subdivision. Only how the compiler can schedule the
# same arithmetic.
#
# 1. D_DrawSpans16: cachewidth read once, in a local variable.
#    The inner pixel loop
#        *pdest++ = *(pbase + (s >> 16) + (t >> 16) * cachewidth);
#    stores through an unsigned char pointer. Such a store may alias any
#    object, the global int cachewidth included (and the build uses
#    -fno-strict-aliasing), so GCC reloads cachewidth from memory for
#    EVERY pixel. Seen in the 0.3.2 code, -O2 and -O3 alike:
#        ldr  ip, [r7]          <- cachewidth, again
#        mla  r3, ip, r3, fp
#        ldrb r3, [r3, r5, asr #16]
#    Nothing in D_DrawSpans16 writes cachewidth (it is set by the
#    caller, r_edge.c, before the call), so reading it once at entry
#    gives exactly the same values. Same change, same reason as in
#    ZZQuake 1, where it removed 7 % of TEX on its own.
#
# 2. (option --unroll, measurement variant only) the same inner loop
#    unrolled by the compiler: "#pragma GCC unroll 8" in front of it.
#    The loop runs 1 to 8 times (8-pixel segments); in ZZQuake 1 it was
#    -O3 that unrolled it and it gave the other 7 % of TEX. Here -O3
#    does not unroll it. Same arithmetic, same order: only the loop
#    control changes. Costs code size (instruction cache: the reason
#    the ZZQuake 1 "spans16" attempt lost elsewhere what it gained),
#    hence a separate blob.
#
# 3. (option --alias) R_PolysetDrawSpans8_Opaque, in a copy of
#    ref_soft/r_polyse.c: the span loop of the alias models (monsters,
#    items, the weapon) has the cachewidth defect seven times over.
#    Its pixel loop writes a byte (*lpdest) and a short (*lpz), so GCC
#    reloads from memory, for EVERY pixel: r_zistepx, r_lstepx,
#    a_ststepxwhole, a_sstepxfrac, a_tstepxfrac,
#    r_affinetridesc.skinwidth, vid.colormap, r_newrefdef.rdflags and
#    currententity->flags. None of them is written by the function
#    (R_PolysetCalcGradients and the caller set them before), so they
#    are read once at entry into locals. Same arithmetic, same order,
#    same pixels. The infrared-goggles test keeps its short-circuit:
#    currententity is only read when the goggles flag is set, as
#    before.
#
# ASCII only.

import os
import sys


def die(msg):
    raise SystemExit("zzq2_patch_render: " + msg)


def sub1(txt, old, new, tag):
    n = txt.count(old)
    if n != 1:
        die("%s: pattern found %d times (need 1)" % (tag, n))
    print("  r_scan.c : %s" % tag)
    return txt.replace(old, new)


def func_body(txt, header, tag):
    """(start, end) of the function that starts with header: up to the
    first line that is exactly '}' after it."""
    if txt.count(header) != 1:
        die("%s: function header found %d times (need 1)" % (tag, txt.count(header)))
    a = txt.index(header)
    b = txt.index("\n}\n", a) + 3
    return a, b


def patch_polyse(q2, out):
    s = open(os.path.join(q2, "ref_soft", "r_polyse.c"), encoding="latin-1").read()
    a, b = func_body(s, "void R_PolysetDrawSpans8_Opaque (spanpackage_t *pspanpackage)\n{\n",
                     "R_PolysetDrawSpans8_Opaque")
    f = s[a:b]

    def fsub(f, old, new, tag):
        n = f.count(old)
        if n != 1:
            die("%s: pattern found %d times in R_PolysetDrawSpans8_Opaque (need 1)" % (tag, n))
        print("  r_polyse.c : %s" % tag)
        return f.replace(old, new)

    f = fsub(f, "{\n\tint\t\tlcount;\n",
             "{\n\tint\t\tlcount;\n"
             "\t/* ZZQuake2: loop invariants read once (zzq2_patch_render.py, 3) */\n"
             "\tint\t\tzistepx = r_zistepx, lstepx = r_lstepx;\n"
             "\tint\t\tststepxwhole = a_ststepxwhole, sstepxfrac = a_sstepxfrac;\n"
             "\tint\t\ttstepxfrac = a_tstepxfrac, skinw = r_affinetridesc.skinwidth;\n"
             "\tbyte\t*cmap = (byte *)vid.colormap;\n"
             "\tint\t\tirvis = (r_newrefdef.rdflags & RDF_IRGOGGLES) &&\n"
             "\t\t\t\t(currententity->flags & RF_IR_VISIBLE);\n",
             "locals declared and read at entry")
    f = fsub(f,
             "\t\t\t\t\tif(r_newrefdef.rdflags & RDF_IRGOGGLES && currententity->flags & RF_IR_VISIBLE)\n"
             "\t\t\t\t\t\t*lpdest = ((byte *)vid.colormap)[irtable[*lptex]];\n"
             "\t\t\t\t\telse\n"
             "\t\t\t\t\t*lpdest = ((byte *)vid.colormap)[*lptex + (llight & 0xFF00)];\n",
             "\t\t\t\t\tif(irvis)\n"
             "\t\t\t\t\t\t*lpdest = cmap[irtable[*lptex]];\n"
             "\t\t\t\t\telse\n"
             "\t\t\t\t\t*lpdest = cmap[*lptex + (llight & 0xFF00)];\n",
             "pixel write uses irvis and cmap")
    f = fsub(f, "\t\t\t\tlzi += r_zistepx;\n", "\t\t\t\tlzi += zistepx;\n", "zistepx")
    f = fsub(f, "\t\t\t\tllight += r_lstepx;\n", "\t\t\t\tllight += lstepx;\n", "lstepx")
    f = fsub(f, "\t\t\t\tlptex += a_ststepxwhole;\n", "\t\t\t\tlptex += ststepxwhole;\n", "ststepxwhole")
    f = fsub(f, "\t\t\t\tlsfrac += a_sstepxfrac;\n", "\t\t\t\tlsfrac += sstepxfrac;\n", "sstepxfrac")
    f = fsub(f, "\t\t\t\tltfrac += a_tstepxfrac;\n", "\t\t\t\tltfrac += tstepxfrac;\n", "tstepxfrac")
    f = fsub(f, "\t\t\t\t\tlptex += r_affinetridesc.skinwidth;\n",
             "\t\t\t\t\tlptex += skinw;\n", "skinwidth")
    for g in ("r_zistepx", "r_lstepx", "a_ststepxwhole", "a_sstepxfrac", "a_tstepxfrac",
              "vid.colormap", "r_affinetridesc.skinwidth", "currententity"):
        # each global is now read exactly once, in the declarations
        if f.count(g) != 1:
            die("R_PolysetDrawSpans8_Opaque: %s still read %d times" % (g, f.count(g)))
    open(out, "w", encoding="latin-1", newline="").write(s[:a] + f + s[b:])


def main():
    args = sys.argv[1:]
    unroll = "--unroll" in args
    alias = None
    rest = []
    i = 0
    while i < len(args):
        if args[i] == "--unroll":
            pass
        elif args[i] == "--alias":
            if i + 1 >= len(args):
                die("--alias needs the output file of the patched r_polyse.c")
            alias = args[i + 1]
            i += 1
        else:
            rest.append(args[i])
        i += 1
    if len(rest) != 2:
        die("usage: zzq2_patch_render.py [--unroll] [--alias <r_polyse out>]"
            " <quake2generic dir> <r_scan out>")
    q2, out = rest
    if alias:
        patch_polyse(q2, alias)
    s = open(os.path.join(q2, "ref_soft", "r_scan.c"), encoding="latin-1").read()

    # 1. D_DrawSpans16: local copy of cachewidth
    s = sub1(s,
             "void D_DrawSpans16 (espan_t *pspan)\n{\n"
             "\tint\t\t\t\tcount, spancount;\n"
             "\tunsigned char\t*pbase, *pdest;\n",
             "void D_DrawSpans16 (espan_t *pspan)\n{\n"
             "\tint\t\t\t\tcount, spancount;\n"
             "\tunsigned char\t*pbase, *pdest;\n"
             "\tint\t\t\t\tcw;\t/* ZZQuake2: cachewidth, read once */\n",
             "D_DrawSpans16: local cw declared")
    s = sub1(s,
             "\tpbase = (unsigned char *)cacheblock;\n",
             "\tpbase = (unsigned char *)cacheblock;\n"
             "\tcw = cachewidth;\n",
             "D_DrawSpans16: cw = cachewidth at entry")
    s = sub1(s,
             "*pdest++ = *(pbase + (s >> 16) + (t >> 16) * cachewidth);",
             "*pdest++ = *(pbase + (s >> 16) + (t >> 16) * cw);",
             "D_DrawSpans16: inner loop uses cw")

    # 2. option: unroll the inner pixel loop
    if unroll:
        s = sub1(s,
                 "\t\t\tdo\n\t\t\t{\n"
                 "\t\t\t\t*pdest++ = *(pbase + (s >> 16) + (t >> 16) * cw);\n",
                 "#pragma GCC unroll 8\n"
                 "\t\t\tdo\n\t\t\t{\n"
                 "\t\t\t\t*pdest++ = *(pbase + (s >> 16) + (t >> 16) * cw);\n",
                 "D_DrawSpans16: inner loop unrolled by 8 (--unroll)")

    open(out, "w", encoding="latin-1", newline="").write(s)


if __name__ == "__main__":
    main()
