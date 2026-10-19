#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# zzq2_patch_poly.py - ZZQuake2 0.4.1: one correction to a COPY of
# ref_soft/r_poly.c of quake2generic.
#
#   usage: zzq2_patch_poly.py <quake2generic dir> <output file>
#
# The quake2generic tree is NEVER modified. STRICT: the anchor must be
# found exactly once, otherwise nothing is built.
#
# In quake2generic, R_DrawPoly() of the software renderer starts with
#     return;
# placed before its declarations. Nothing is ever drawn by it, and it
# draws every polygon that the edge renderer does not:
#   - translucent world surfaces: water, slime, glass (R_DrawAlphaSurfaces)
#   - sprites: explosions, blaster bolts, flares (r_sprite.c)
#   - beams: lasers (R_IMFlatShadedQuad)
# Seen on the hardware in 0.4: the surface of the water is not drawn
# (the view still warps under water: that is D_WarpScreen, another
# path). Reproduced on the emulator (demo1, pool at z -252).
# The id Software source of Quake II has no such line. In the function
# that follows it, the vertex step is
#     pv += sizeof (vec5_t) / sizeof (pv);
# (sizeof of the POINTER): 5 floats with 4-byte pointers, as on the
# Cortex-A9 and on the 32-bit PCs of 1997, but 2 on a 64-bit PC, where
# the function would read wrong vertices; this is very probably why it
# was switched off there. On Core1 (32 bits) the original code is
# correct: the line is removed, nothing else changes.
#
# ASCII only.

import os
import sys

q2, out = sys.argv[1], sys.argv[2]
src = os.path.join(q2, "ref_soft", "r_poly.c")
t = open(src, encoding="latin-1").read()
old = "static void R_DrawPoly( qboolean iswater )\n{\n\treturn;\n\tint\t\t\ti, nump;\n"
new = ("static void R_DrawPoly( qboolean iswater )\n{\n"
       "\t/* ZZQ2: \"return;\" of quake2generic removed (zzq2_patch_poly.py) */\n"
       "\tint\t\t\ti, nump;\n")
if t.count(old) != 1:
    sys.stderr.write("zzq2_patch_poly: anchor found %d times, 1 expected\n" % t.count(old))
    sys.exit(1)
t = t.replace(old, new)
if "sizeof (vec5_t) / sizeof (pv)" not in t:
    sys.stderr.write("zzq2_patch_poly: vertex step not found (unexpected source)\n")
    sys.exit(1)
open(out, "w", encoding="latin-1", newline="").write(t)
print("  ref_soft/r_poly.c : early return of R_DrawPoly removed -> %s" % out)
