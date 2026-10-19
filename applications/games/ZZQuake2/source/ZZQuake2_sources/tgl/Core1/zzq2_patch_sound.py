#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# zzq2_patch_sound.py - ZZQuake2 0.4: one correction to a COPY of
# client/snd_mix.c of quake2generic.
#
#   usage: zzq2_patch_sound.py <quake2generic dir> <output file>
#
# The quake2generic tree is NEVER modified. STRICT: the anchor must be
# found exactly the number of times stated, otherwise nothing is built.
#
# Quake II 3.19 (the base of quake2generic) mixes 8-bit sounds in C
# with
#     lscale = snd_scaletable[ ch->leftvol >> 11];
#     rscale = snd_scaletable[ ch->rightvol >> 11];
# The channel volumes are 0..255: ">> 11" is always 0, so every 8-bit
# sound is mixed at volume 0. The Windows build of 1997 never ran this
# C code (it used the x86 assembler mixer, id386), so the defect did
# not show there. Quake II defaults to s_loadas8bit 1 (all sounds are
# loaded as 8 bits): with this code the game is silent (seen on the
# emulator in 0.4: channels playing, mixed output all zero).
# id Software corrected it in the later source release (3.21), with
# the comment "ZOID-- >>11 has been changed to >>3, >>11 didn't make
# much sense as it would always be zero": snd_scaletable has 32 rows,
# 255 >> 3 = 31. The same change is made here, nothing else.
#
# ASCII only.

import os
import sys

q2, out = sys.argv[1], sys.argv[2]
src = os.path.join(q2, "client", "snd_mix.c")
t = open(src, encoding="latin-1").read()
for old, new, n in (
        ("\tlscale = snd_scaletable[ ch->leftvol >> 11];\n",
         "\tlscale = snd_scaletable[ ch->leftvol >> 3];\t/* ZZQ2: >> 11 was always 0 (3.21 fix) */\n", 1),
        ("\trscale = snd_scaletable[ ch->rightvol >> 11];\n",
         "\trscale = snd_scaletable[ ch->rightvol >> 3];\t/* ZZQ2: >> 11 was always 0 (3.21 fix) */\n", 1)):
    if t.count(old) != n:
        sys.stderr.write("zzq2_patch_sound: anchor found %d times, %d expected: %r\n"
                         % (t.count(old), n, old))
        sys.exit(1)
    t = t.replace(old, new)
# relative includes of the copy ("../client/...") keep working with -I<q2>/client
open(out, "w", encoding="latin-1", newline="").write(t)
print("  client/snd_mix.c : >> 11 -> >> 3 (2 lines) -> %s" % out)
