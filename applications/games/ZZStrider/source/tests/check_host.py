#!/usr/bin/env python3
"""
check_host.py - verdict of the host regression test (tests/run_host_test.sh).

The expected values below are an INDEPENDENT model written from the MAME documentation of the
CPS-1 board (ROM interleave, gfx layouts, tilemap scan orders, sprite list, layer order, star
fields, row scroll, palette). They are compared with what the blob sources produced under
qemu-arm for the synthetic scene of tests/synth/prog.c. ASCII only.
"""
import re
import struct
import sys
import zlib
from pathlib import Path

W = Path(sys.argv[1])
OUT = W / "out"
ROMS = W / "roms"
fails = []


def check(ok, msg):
    if not ok:
        fails.append(msg)


def png(path, w, h, argb):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            v = argb[y * w + x]
            raw += bytes(((v >> 16) & 255, (v >> 8) & 255, v & 255))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


# ---------------------------------------------------------------- 1. the decoded ROM store
prog = (W / "prog.bin").read_bytes()
prog += bytes(0x100000 - len(prog))
sp = (OUT / "store_prg.bin").read_bytes()
exp = bytearray(0x100000)
exp[0::2] = prog[1::2]
exp[1::2] = prog[0::2]
check(sp == bytes(exp), "68000 program store differs from the linked program")

names = [["st-2.8a", "st-11.10a", "st-5.4a", "st-9.6a"], ["st-1.7a", "st-10.9a", "st-4.3a", "st-8.5a"]]
region = bytearray(0x400000)
for half in range(2):
    for n in range(4):
        f = (ROMS / names[half][n]).read_bytes()
        lo, hi = half * 0x200000, (half + 1) * 0x200000
        region[lo + 2 * n:hi:8] = f[0::2]
        region[lo + 2 * n + 1:hi:8] = f[1::2]
store = (OUT / "store_gfx.bin").read_bytes()
# 8 pixels = 4 plane bytes b0..b3 (MAME planes {24,16,8,0}: b0 is bit 0 of the pen, bit 7 of a
# byte is the left pixel). Packed store: one 32-bit LE word, nibble k = 15 - pen of bit k.
spread = []
for b in range(256):
    v = 0
    for k in range(8):
        if (b >> k) & 1:
            v |= 1 << (4 * k)
    spread.append(v)
bad = 0
for n in range(0, 0x400000, 4):
    b0, b1, b2, b3 = region[n], region[n + 1], region[n + 2], region[n + 3]
    pens = spread[b0] | (spread[b1] << 1) | (spread[b2] << 2) | (spread[b3] << 3)
    if struct.unpack_from("<I", store, n)[0] != (pens ^ 0xFFFFFFFF):
        bad += 1
check(bad == 0, "tile store: %d pixel groups differ from the reference decode" % bad)
check(all(store[0x400000 + k] == region[8 * k] and store[0x401000 + k] == region[8 * k + 4]
          for k in range(0x1000)), "star store differs")
check((OUT / "store_z80.bin").read_bytes() == (ROMS / "09.12b").read_bytes(), "Z80 store differs")
check((OUT / "store_oki.bin").read_bytes() ==
      (ROMS / "18.11c").read_bytes() + (ROMS / "19.12c").read_bytes(), "OKI store differs")


# ---------------------------------------------------------------- 2. the rendered frames
def pal(page, group, pen):
    return 0xFF000000 | (((page * 2 + 3) * 17) << 16) | ((pen * 17) << 8) | ((group & 15) * 17)


RAMP = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0, 1]
SPRITES = [  # x, y, w, h, kind, flipx, flipy, group  (first = front)
    (100, 100, 16, 16, "solid7", 0, 0, 4), (200, 100, 32, 32, "block", 0, 0, 5),
    (40, 150, 16, 16, "quad", 0, 0, 6), (60, 150, 16, 16, "quad", 1, 0, 6),
    (80, 150, 16, 16, "quad", 0, 1, 6), (100, 150, 16, 16, "quad", 1, 1, 6),
    (140, 150, 16, 16, "ramp", 0, 0, 6), (160, 150, 16, 16, "ramp", 1, 0, 6),
    (300, 40, 16, 16, "solid7", 0, 0, 4)]
BACKDROP = pal(5, 31, 15)       # hardware palette entry 0xBFF


def model(sx, sy, st):
    X, Y = sx + 64, sy + 16
    if st["s1"]:
        tx, ty = (st["s1x"] + X) & 511, Y & 511
        if ty // 8 in (6, 7):
            return pal(1, 1, ((tx // 8) & 7) + 1)
    for (x, y, w, h, kind, fx, fy, grp) in SPRITES:
        if x <= sx < x + w and y <= sy < y + h:
            u, v = sx - x, sy - y
            if kind == "solid7":
                pen = 7
            elif kind == "block":
                pen = 0 if v < 16 else 4
            else:
                if fx:
                    u = 15 - u
                if fy:
                    v = 15 - v
                pen = RAMP[u] if kind == "ramp" else (1 if u < 8 else 2) + (2 if v >= 8 else 0)
            return pal(0, grp, pen)
    if st["s2"]:
        ty = (st["s2y"] + Y) & 1023
        tx = (st["s2x"] + X + (Y if st["rows"] else 0)) & 1023
        if ty // 16 in (4, 5):
            return pal(2, 2, ((tx // 16) & 7) + 1)
    if st["s3"]:
        tx, ty = (st["s3x"] + X) & 2047, Y & 2047
        return pal(3, 3, ((tx // 32) + (ty // 32)) & 7)
    return BACKDROP


A = dict(s1=1, s2=1, s3=1, s1x=0, s2x=0, s2y=0, s3x=0, rows=0)
B = dict(A, s1x=3, s2x=37, s2y=5, s3x=100)
C = dict(B, s2=0, s3=0)
D = dict(B, rows=1)
STARS = {(5, 84): (4, 2), (99, 104): (5, 4)}      # screen position -> (palette page, group)
XMAP = [(x // 5) * 3 + (0, 0, 1, 2, 2)[x % 5] for x in range(640)]
YMAP = [(y // 15) * 7 + (0, 0, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6)[y % 15] for y in range(480)]

for frame, st, tag in ((25, A, "A"), (50, B, "B"), (80, C, "C"), (110, D, "D")):
    idx = struct.unpack("<%dH" % (384 * 224), (OUT / ("native_%03d.idx" % frame)).read_bytes())
    palette = struct.unpack("<4096I", (OUT / ("palette_%03d.bin" % frame)).read_bytes())
    native = [palette[i & 0xFFF] for i in idx]
    png(OUT / ("native_%03d.png" % frame), 384, 224, native)
    wrong = 0
    first = None
    for sy in range(224):
        for sx in range(384):
            got = native[sy * 384 + sx]
            if tag == "C" and (sx, sy) in STARS:
                page, grp = STARS[(sx, sy)]
                ok = ((got >> 16) & 255) == (page * 2 + 3) * 17 and (got & 255) == grp * 17
            else:
                ok = got == model(sx, sy, st)
            if not ok:
                wrong += 1
                if first is None:
                    first = (sx, sy, got, model(sx, sy, st))
    check(wrong == 0, "phase %s (frame %d): %d of 86016 pixels differ from the model, first at %s"
          % (tag, frame, wrong, first and "(%d,%d) got %08X expected %08X" % first))
    scr = struct.unpack("<%dI" % (640 * 480), (OUT / ("screen_%03d.argb" % frame)).read_bytes())
    bad = sum(1 for y in range(480) for x in range(640) if scr[y * 640 + x] != native[YMAP[y] * 384 + XMAP[x]])
    check(bad == 0, "phase %s: 640x480 conversion differs on %d pixels" % (tag, bad))
    if tag == "D":
        png(OUT / "screen_110.png", 640, 480, scr)

# ---------------------------------------------------------------- 2b. 640x240 line-doubled path
for SCAN, ROWS in ((W / "out_scan", 240), (W / "out_scan448", 224)):
  if (SCAN / "harness.log").exists():
    check("harness failures: 0" in (SCAN / "harness.log").read_text(), "scan run: the harness reported failures")
    XM = [(x // 5) * 3 + (0, 0, 1, 2, 2)[x % 5] for x in range(640)]
    B = (ROWS - 224) // 2
    for frame in (25, 50, 80, 110):
        idx = struct.unpack("<%dH" % (384 * 224), (SCAN / ("native_%03d.idx" % frame)).read_bytes())
        palette = struct.unpack("<4096I", (SCAN / ("palette_%03d.bin" % frame)).read_bytes())
        scr = struct.unpack("<%dI" % (640 * ROWS), (SCAN / ("screen_%03d.argb" % frame)).read_bytes())
        bad = 0
        for y in range(ROWS):
            for x in range(640):
                want = 0xFF000000 if (y < B or y >= B + 224) else palette[idx[(y - B) * 384 + XM[x]] & 0xFFF]
                if scr[y * 640 + x] != want:
                    bad += 1
        check(bad == 0, "640x%d frame %d: %d pixels differ" % (ROWS, frame, bad))

# ---------------------------------------------------------------- 3. sound and diagnostics
log = (OUT / "harness.log").read_text()
slot = {int(a): int(b, 16) for a, b in re.findall(r"slot\s+(\d+) = ([0-9a-f]{8})", log)}
check("harness failures: 0" in log, "the harness reported failures")
check(slot[103] > 1000, "YM2151 timer IRQs: %d" % slot[103])
check(slot[108] == 1, "MSM6295 phrase starts: %d" % slot[108])
check(slot[111] == 2, "Z80 bank changes: %d" % slot[111])
check(0x84 <= slot[100] <= 0xA1 or 0x38 <= slot[100] <= 0x4B, "Z80 PC %04X is outside its main loop" % slot[100])
check(slot[56] == 0 and slot[57] == 0 and slot[58] == 0 and slot[60] == 0, "68000 faults reported")
m = re.search(r"ram: in1=(\w+) in0=(\w+) dswa=(\w+) dswb=(\w+) dswc=(\w+) f=(\d+) mid=(\w+),(\w+) hi=(\w+),(\w+) magic=(\w+) trap=(\w+)", log)
check(m is not None and m.group(1, 2, 3, 4, 5) == ("dfef", "eeff", "ffff", "8dff", "ffff"), "input ports / DIP read-back")
check(m is not None and m.group(7, 8, 9, 10, 11, 12) == ("1234", "abcd", "55aa", "c0de", "c0fe", "0000"), "program ROM read-back")
pcm = (OUT / "audio_s16le_22050_mono.raw").read_bytes()
smp = struct.unpack("<%dh" % (len(pcm) // 2), pcm[:len(pcm) // 2 * 2])
check(len(smp) > 40000, "PCM ring: only %d samples" % len(smp))
if len(smp) > 6000:
    seg = smp[1000:5000]                           # YM2151 alone (before the sound command)
    peak = max(abs(v) for v in seg)
    zc = sum(1 for i in range(1, len(seg)) if (seg[i - 1] < 0) != (seg[i] < 0))
    freq = zc * 22050.0 / (2 * len(seg))
    check(peak > 1000, "YM2151 tone too quiet (peak %d)" % peak)
    check(400 < freq < 480, "YM2151 tone at %.1f Hz, expected 440 (A4)" % freq)
    print("audio: %d samples, YM tone %.1f Hz peak %d, overall peak %d" % (len(smp), freq, peak, max(abs(v) for v in smp)))

for f in fails:
    print("FAIL:", f)
print("HOST TEST:", "PASS" if not fails else "%d FAILURE(S)" % len(fails))
sys.exit(1 if fails else 0)
