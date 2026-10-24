"""ZZOpenLaraRIX A2.18: sub-pixel vertices and perspective by 1/w in the TinyGL of ZZQuake2TGL.

Run on the copy that Core1/tgl/zzq2tgl_patch_tinygl.py has already patched:
    python3 Core1/tgl/ol_patch_tinygl_persp.py <patched tinygl dir>

Why. TinyGL's "perspective" rasterizers divide s*z and t*z by z, where z is the depth-buffer value of
the vertex (z/w mapped to the 16-bit range), not 1/w. z/w = A - B/w is affine in 1/w but not
proportional to it, so the division is only correct when A is small next to B/w. With OpenLara's
camera (near 32, far 46080) z/w stays between 0.94 and 1.0 for everything farther than one metre:
the division barely changes along a span and the textures are mapped almost affinely (they swim
when the camera moves). Quake II's tiled textures hide the same error much better.

What. Every vertex carries q = 1/w (ZBufferPoint.q, set where its screen position is computed);
q is interpolated like s*q and t*q (gradients and left-edge steps in ztriangle.h); the span loops
divide by q instead of z, with a true division every 8 pixels instead of the two Newton steps
(q can change by more than a factor of two over 8 pixels near the camera, where Newton does not
converge). The depth test still uses z. zb->zz_qpersp = 0 gives TinyGL's original arithmetic
back (zzopenlara.cfg TGLPERSP=0).

ASCII only.
"""
import os
import re
import sys

dst = sys.argv[1]


def fail(msg):
    sys.stderr.write("ol_patch_tinygl_persp: %s\n" % msg)
    sys.exit(1)


def load(rel):
    return open(os.path.join(dst, rel), encoding="ascii").read()


def save(rel, text):
    open(os.path.join(dst, rel), "w", encoding="ascii", newline="\n").write(text)


def rep(text, old, new, count, what):
    n = text.count(old)
    if n != count:
        fail("%s: expected %d match(es), found %d for:\n%s" % (what, count, n, old[:200]))
    return text.replace(old, new)


# ---------------------------------------------------------------- zbuffer.h
t = load("include/zbuffer.h")
t = rep(t, "    GLfloat sz, tz; /* temporary coordinates for mapping */\n",
        "    GLfloat sz, tz; /* temporary coordinates for mapping */\n"
        "    GLfloat q;      /* OL A2.18: 1/w of the vertex (perspective of s, t) */\n"
        "    GLfloat fx, fy; /* OL A2.18: screen position before rounding (sub-pixel) */\n", 1, "zbuffer.h point")
t = rep(t, "    GLuint zz_lw, zz_lh;   /* log2 of its size as uploaded, <= 8     */\n",
        "    GLuint zz_lw, zz_lh;   /* log2 of its size as uploaded, <= 8     */\n"
        "    GLuint zz_qpersp;      /* OL A2.18: 1 = s, t divided by 1/w      */\n"
        "    GLuint zz_subpix;      /* OL A2.18: 1 = vertices to the sub-pixel */\n"
        "    GLint zz_cx0, zz_cy0, zz_cx1, zz_cy1; /* OL A2.18: scissor, end excluded */\n", 1, "zbuffer.h buffer")
save("include/zbuffer.h", t)

# ---------------------------------------------------------------- zgl.h: unrounded screen position
t = load("src/zgl.h")
t = rep(t, "static inline GLint zz_viewport_xy(GLfloat r, GLint max)\n",
        "/* OL A2.18: the same clamp, without the rounding (sub-pixel position) */\n"
        "static inline GLfloat zz_viewport_f(GLfloat r, GLint max)\n"
        "{\n"
        "    if (!(r > 0.0f))\n"
        "        return 0.0f;\n"
        "    if (r >= (GLfloat) max)\n"
        "        return (GLfloat) max;\n"
        "    return r;\n"
        "}\n\n"
        "static inline GLint zz_viewport_xy(GLfloat r, GLint max)\n", 1, "zgl.h")
save("src/zgl.h", t)

# ---------------------------------------------------------------- clip.c, vertex.c: q = 1/w, fx, fy
for f in ("src/clip.c", "src/vertex.c"):
    t = load(f)
    mx = re.search(r"v->zp\.x = zz_viewport_xy\((.*?), c->zb->xsize\);", t, re.S)
    my = re.search(r"v->zp\.y = zz_viewport_xy\((.*?), c->zb->ysize\);", t, re.S)
    if not mx or not my:
        fail("%s: zp.x / zp.y assignments not found" % f)
    t, n = re.subn(r"(\n(\s*)v->zp\.y = zz_viewport_xy\([^;]*;\n)",
                   lambda m: m.group(1) + m.group(2) + "v->zp.q = winv; /* OL A2.18 */\n" +
                   m.group(2) + "v->zp.fx = zz_viewport_f(" + mx.group(1) + ", c->zb->xsize);\n" +
                   m.group(2) + "v->zp.fy = zz_viewport_f(" + my.group(1) + ", c->zb->ysize);\n", t)
    if n != 1:
        fail("%s: zp.y assignment found %d times, 1 expected" % (f, n))
    save(f, t)

# ---------------------------------------------------------------- ztriangle.h: sub-pixel template
here = os.path.dirname(os.path.abspath(__file__))
old = load("src/ztriangle.h")
if "nb_lines = p2->y - p1->y; /* ZZQ2TGL fill rule */" not in old:
    fail("ztriangle.h: not the ZZQuake2TGL template")
save("src/ztriangle.h", open(os.path.join(here, "ol_ztriangle.h"), encoding="ascii").read())

# ---------------------------------------------------------------- ztriangle.c: the span loops
t = load("src/ztriangle.c")
n_init = t.count("fdzdx = (GLfloat) dzdx;")
n_fzl = t.count("fzl = (GLfloat) z1;")
if n_init < 8 or n_fzl < 2:
    fail("ztriangle.c: %d fdzdx inits, %d fzl starts" % (n_init, n_fzl))
t = t.replace("fdzdx = (GLfloat) dzdx;", "fdzdx = zb->zz_qpersp ? dqdx : (GLfloat) dzdx;")
t = t.replace("fzl = (GLfloat) z1;", "fzl = zb->zz_qpersp ? q1 : (GLfloat) z1;")
t, n_nr = re.subn(r"zinv = zinv \* \(2\.0f - fzl \* zinv\);([ \t]*\\?\n[ \t]*)zinv = zinv \* \(2\.0f - fzl \* zinv\);",
                  r"zinv = zb->zz_qpersp ? 1.0f / fzl : zinv * (2.0f - fzl * zinv);\1"
                  r"zinv = zb->zz_qpersp ? zinv : zinv * (2.0f - fzl * zinv);", t)
if n_nr != n_fzl:
    fail("ztriangle.c: %d Newton pairs for %d span loops" % (n_nr, n_fzl))
save("src/ztriangle.c", t)

print("TinyGL sub-pixel template + perspective by 1/w (OL A2.18): %d span loops, %d set-ups" % (n_fzl, n_init))
