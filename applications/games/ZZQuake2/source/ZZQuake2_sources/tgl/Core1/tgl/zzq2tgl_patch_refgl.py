#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
zzq2tgl_patch_refgl.py - ZZQuake2TGL: changes to COPIES of two ref_gl
files of quake2generic (the originals are not touched).

  usage: zzq2tgl_patch_refgl.py <quake2generic> <out dir>

  writes <out dir>/gl_image_zz.c and <out dir>/gl_draw_zz.c

Strict: each replacement must match exactly once, otherwise nothing is
written and the build stops.

Why: three image buffers of ref_gl live on the stack. GL_Upload8 keeps
512 KiB (trans), the GL_Upload32 it calls 256 KiB more (scaled), and
Draw_StretchRaw (cinematics) 256 KiB (image32). The emulator measured
842 KiB of stack in use while a map loads, out of the 1 MiB the release
gives Core1 (0x05F00000..0x06000000). The three buffers become static:
same code, same contents, no recursion is possible (one thread, the
functions do not call themselves). They are zero-initialised, so they
land in .q2bss (the reserve of the Quake II objects in the arena).

ASCII only.
"""
import os
import sys

q2, out = sys.argv[1], sys.argv[2]


def fail(msg):
    sys.stderr.write("zzq2tgl_patch_refgl: %s\n" % msg)
    sys.exit(1)


def patch(src, dst, pairs):
    text = open(os.path.join(q2, src), encoding="latin-1").read()
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            fail("%s: expected 1 match, found %d for: %s" % (src, n, old))
        text = text.replace(old, new)
        print("  %s : %s -> %s" % (src, old.strip(), new.strip()))
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, dst), "w", encoding="latin-1", newline="").write(text)


patch("ref_gl/gl_image.c", "gl_image_zz.c", [
    ("\tunsigned\tscaled[256*256];\n", "\tstatic unsigned\tscaled[256*256];\t/* ZZQ2TGL: off the stack */\n"),
    ("\tunsigned\ttrans[512*256];\n", "\tstatic unsigned\ttrans[512*256];\t/* ZZQ2TGL: off the stack */\n"),
])
patch("ref_gl/gl_draw.c", "gl_draw_zz.c", [
    ("\tunsigned\timage32[256*256];\n", "\tstatic unsigned\timage32[256*256];\t/* ZZQ2TGL: off the stack */\n"),
])
