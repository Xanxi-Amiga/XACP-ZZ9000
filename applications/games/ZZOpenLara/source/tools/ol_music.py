# SPDX-License-Identifier: GPL-3.0-only
# Original ZZOpenLara conversion tool: Copyright (C) 2026 Xanxi.
# GPLv3 section 7(b) attribution: see ../ATTRIBUTION.txt
"""ZZOpenLara: builds PROGDIR:data/ol_music.pak, the music of the game, from YOUR discs.

    Windows: drop the .cue file(s) and/or the folder of the PC tracks on ZZOpenLara-Music.bat
    or:      python ol_music.py <file.cue | folder> ...      (PSX or PC: found by themselves)
    or:      python ol_music.py --psx "Tomb Raider (Europe).cue" --pc-wav <folder> [--out ol_music.pak]

Sources (at least one; Python 3 alone, nothing to install):
  --psx  <file.cue>   TR1 PlayStation disc image (BIN/CUE, one .bin per track or a single .bin). Its audio
                      tracks carry OpenLara's own numbers: title 2, ambients, cues, voices, cutscenes...
                      The PSX disc has no track 58 (Wind) or 59 (Pyramid): take them from the PC disc.
  --pc-wav <folder>   track02.wav .. track10.wav of the TR1 PC disc (44100 Hz, 16-bit, stereo), numbered
                      as on the PC disc; converted to OpenLara's numbers (OpenLara's remapTrack).
  --pc   <file.cue>   the TR1 PC disc image itself (BIN/CUE), same numbering as --pc-wav.
A track given by the PSX disc is never replaced by a PC one. Track 60 (not used by TR1) is left out
unless --all.

Each track: the 2 s pause before it on the disc is left out (INDEX 01 of the .cue), the silence at its end
too; 44100 -> 22050 Hz (filter 1/4 1/2 1/4), then IMA ADPCM 4 bits, stereo kept: 22050 bytes per second.
Format read by Core1 (openlara/src/sound.h, struct ZZM): "ZZM1", rate, channels, frames (LE32), then
2 bytes per 2 frames (left byte, right byte; first sample in the high nibble).
The container is a Quake PAK like ol_level.pak: entries track_NN.zzm, directory right after the header.

Copy the result into PROGDIR:data/ of the game. It is made from your discs: do not give it away.
ASCII only.
"""
import argparse
import array
import os
import struct
import sys
import time

try:
    import audioop          # Python <= 3.12: the C encoder (same algorithm, about 50 times faster)
except ImportError:
    audioop = None

# OpenLara (gameflow.h, remapTrack, TR1): OpenLara's track number -> track of the PC disc
TR1_PC_OF = [2, 2, 2, 11, 12, 3, 13, 14, 15, 16, 17, 18, 19, 60, 20, 21, 22, 23, 24, 25, 26, 27,
             7, 8, 9, 10, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46,
             47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 28, 4, 5, 6]
PC_TO_OL = {}
for _ol, _pc in enumerate(TR1_PC_OF):
    if _ol >= 2:
        PC_TO_OL.setdefault(_pc, _ol)

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
        73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
        494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
        2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
        10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def fail(msg):
    sys.stderr.write("ol_music: " + msg + "\n")
    sys.exit(1)


# ------------------------------------------------------------------ reading the discs
def msf_bytes(s):
    m, sec, f = (int(x) for x in s.split(":"))
    return ((m * 60 + sec) * 75 + f) * 2352


def parse_cue(path):
    """-> list of (track number, bin path, start byte, end byte or None) for the AUDIO tracks"""
    base = os.path.dirname(os.path.abspath(path))
    files = []          # [bin path, [(track, type, index01 byte)]]
    cur = None
    trk = None
    for line in open(path, encoding="latin-1"):
        w = line.strip().split()
        if not w:
            continue
        k = w[0].upper()
        if k == "FILE":
            name = line.strip()[5:]
            name = name[:name.rfind(" ")].strip().strip('"')
            cur = [os.path.join(base, name), []]
            files.append(cur)
        elif k == "TRACK" and cur is not None:
            trk = [int(w[1]), w[2].upper(), None]
            cur[1].append(trk)
        elif k == "INDEX" and trk is not None and w[1] == "01":
            trk[2] = msf_bytes(w[2])
    out = []
    for binpath, tracks in files:
        if not os.path.exists(binpath):
            fail("missing " + binpath)
        size = os.path.getsize(binpath)
        for i, (n, kind, start) in enumerate(tracks):
            if kind != "AUDIO" or start is None:
                continue
            end = tracks[i + 1][2] if i + 1 < len(tracks) else None   # next track's INDEX 01 (single .bin)
            out.append((n, binpath, start, end if end is not None else size))   # silence at the end: trim_end
    return out


def read_cd_audio(binpath, start, end):
    with open(binpath, "rb") as f:
        f.seek(start)
        data = f.read(end - start)
    data = data[:len(data) & ~3]
    a = array.array("h")
    a.frombytes(data)
    if sys.byteorder != "little":
        a.byteswap()
    return a


def read_wav(path):
    import wave
    w = wave.open(path, "rb")
    if w.getsampwidth() != 2 or w.getnchannels() != 2 or w.getframerate() != 44100:
        fail("%s: 44100 Hz 16-bit stereo expected" % path)
    a = array.array("h")
    a.frombytes(w.readframes(w.getnframes()))
    if sys.byteorder != "little":
        a.byteswap()
    return a


# ------------------------------------------------------------------ conversion
def trim_end(a):
    """drops the silence at the end (|s| <= 8), keeps 0.05 s"""
    n = len(a) // 2
    i = n - 1
    while i > 0 and abs(a[2 * i]) <= 8 and abs(a[2 * i + 1]) <= 8:
        i -= 1
    n = min(n, i + 1 + 2205)
    return a[:2 * n]


def half_rate(x):
    """44100 -> 22050 Hz, one channel: y[i] = (x[2i-1] + 2 x[2i] + x[2i+1]) / 4"""
    e = x[0::2]
    o = x[1::2]
    if len(o) < len(e):
        o.append(e[-1])
    p = [o[0]] + list(o[:-1])
    return array.array("h", [(2 * a + b + c) >> 2 for a, b, c in zip(e, o, p)])


def ima_encode(x):
    """IMA ADPCM, state 0 / 0, 2 samples per byte, first in the high nibble (len(x) even)"""
    if audioop is not None:
        return audioop.lin2adpcm(x.tobytes() if sys.byteorder == "little" else _swapped(x), 2, None)[0]
    out = bytearray(len(x) // 2)
    pred = 0
    idx = 0
    step_t = STEP
    index_t = INDEX
    hi = 0
    k = 0
    first = True
    for v in x:
        step = step_t[idx]
        diff = v - pred
        if diff < 0:
            sign = 8
            diff = -diff
        else:
            sign = 0
        delta = 0
        vp = step >> 3
        if diff >= step:
            delta = 4
            diff -= step
            vp += step
        step >>= 1
        if diff >= step:
            delta |= 2
            diff -= step
            vp += step
        step >>= 1
        if diff >= step:
            delta |= 1
            vp += step
        if sign:
            pred -= vp
            if pred < -32768:
                pred = -32768
        else:
            pred += vp
            if pred > 32767:
                pred = 32767
        delta |= sign
        idx += index_t[delta]
        if idx < 0:
            idx = 0
        elif idx > 88:
            idx = 88
        if first:
            hi = delta << 4
            first = False
        else:
            out[k] = hi | delta
            k += 1
            first = True
    return bytes(out)


def _swapped(x):
    y = array.array("h", x)
    y.byteswap()
    return y.tobytes()


def ima_decode(data):
    """the decoder of Core1 (sound.h, ZZM::step), for the check"""
    pred = 0
    idx = 0
    out = []
    for b in data:
        for nib in (b >> 4, b & 15):
            sp = STEP[idx]
            d = sp >> 3
            if nib & 4:
                d += sp
            if nib & 2:
                d += sp >> 1
            if nib & 1:
                d += sp >> 2
            pred = pred - d if nib & 8 else pred + d
            pred = max(-32768, min(32767, pred))
            idx = max(0, min(88, idx + INDEX[nib]))
            out.append(pred)
    return out


def make_zzm(a):
    a = trim_end(a)
    left = half_rate(a[0::2])
    right = half_rate(a[1::2])
    n = len(left)
    if n & 1:
        left.append(left[-1])
        right.append(right[-1])
        n += 1
    lb = ima_encode(left)
    rb = ima_encode(right)
    body = bytearray(2 * len(lb))
    body[0::2] = lb
    body[1::2] = rb
    return b"ZZM1" + struct.pack("<III", 22050, 2, n) + bytes(body), left


# ------------------------------------------------------------------ main
NAMES = {2: "title", 5: "Caves", 57: "Cistern", 58: "Wind", 59: "Pyramid"}


def say(fr, en):
    print(fr)
    print("  " + en)


def classify(paths):
    """-> (psx cues, pc cues, wav folders) among files / folders given or found"""
    psx, pc, wav = [], [], []
    for p in paths:
        if os.path.isdir(p):
            names = [n.lower() for n in os.listdir(p)]
            if any(n.startswith("track") and n.endswith(".wav") and n[5:-4].isdigit() for n in names):
                wav.append(p)
            for n in sorted(os.listdir(p)):
                if n.lower().endswith(".cue"):
                    classify_cue(os.path.join(p, n), psx, pc)
        elif p.lower().endswith(".cue"):
            classify_cue(p, psx, pc)
        elif p.lower().endswith(".wav"):
            d = os.path.dirname(os.path.abspath(p))
            if d not in wav:
                wav.append(d)
        else:
            say("Ignore (ni .cue, ni dossier) : " + p, "Ignored (not a .cue nor a folder): " + p)
    return psx, pc, wav


def classify_cue(path, psx, pc):
    """PSX discs are CD-ROM XA (data track MODE2), the TR1 PC CD is MODE1 with 9 audio tracks (2..10)"""
    try:
        text = open(path, encoding="latin-1").read().upper()
    except OSError:
        return
    nums = [int(w.split()[1]) for w in text.splitlines() if w.strip().startswith("TRACK") and "AUDIO" in w]
    if not nums:
        return
    if "MODE2" in text or (("MODE1" not in text) and max(nums) > 10):
        psx.append(path)          # the PSX disc: 56 audio tracks, OpenLara's own numbers
    else:
        pc.append(path)           # the PC disc: tracks 2..10, renumbered by remapTrack


def main():
    ap = argparse.ArgumentParser(description="ZZOpenLara: music of the game (data/ol_music.pak) from your TR1 discs. "
                                 "Give .cue files (PSX and/or PC disc) and/or the folder of the PC tracks (track02.wav ...); "
                                 "nothing: they are looked for next to this script and in the current folder.")
    ap.add_argument("sources", nargs="*", help=".cue files or folders (found by themselves: PSX or PC)")
    ap.add_argument("--psx", help="TR1 PSX disc image (.cue)")
    ap.add_argument("--pc", help="TR1 PC disc image (.cue)")
    ap.add_argument("--pc-wav", help="folder with track02.wav .. of the TR1 PC disc")
    ap.add_argument("--out", help="output file (default: ol_music.pak next to this script)")
    ap.add_argument("--all", action="store_true", help="keep track 60 (not used by TR1)")
    ap.add_argument("--check", action="store_true", help="decode every track again and compare (slow)")
    o = ap.parse_args()

    # PyInstaller --onefile extracts the script to a temporary _MEIPASS folder.
    # The output must instead remain next to the actual EXE on Windows.
    entry = sys.executable if getattr(sys, "frozen", False) else __file__
    here = os.path.dirname(os.path.abspath(entry))
    paths = list(o.sources)
    if not (paths or o.psx or o.pc or o.pc_wav):
        for d in (here, os.getcwd()):
            for sub in ("", "pc", "audio", os.path.join("game", "audio")):
                p = os.path.join(d, sub) if sub else d
                if os.path.isdir(p) and p not in paths:
                    paths.append(p)
    psx, pc, wav = classify(paths)
    if o.psx:
        psx.insert(0, o.psx)
    if o.pc:
        pc.insert(0, o.pc)
    if o.pc_wav:
        wav.insert(0, o.pc_wav)
    if not (psx or pc or wav):
        say("Aucune source trouvee. Deposez sur ZZOpenLara-Music.bat le .cue du disque PSX de Tomb Raider,",
            "No source found. Drop on ZZOpenLara-Music.bat the .cue of the Tomb Raider PSX disc,")
        say("et/ou le .cue du CD PC, et/ou le dossier des pistes du CD PC (track02.wav ...).",
            "and/or the .cue of the PC CD, and/or the folder of the PC tracks (track02.wav ...).")
        return 1
    out = o.out or os.path.join(here, "ol_music.pak")

    src = {}      # OpenLara track -> (label, loader)
    for cue in psx:
        say("Disque PSX : " + cue, "PSX disc: " + cue)
        for n, binpath, s, e in parse_cue(cue):
            src.setdefault(n, ("PSX track %02d" % n, (lambda b=binpath, s=s, e=e: read_cd_audio(b, s, e))))
    pcs = {}
    for cue in pc:
        say("CD PC : " + cue, "PC CD: " + cue)
        for n, binpath, s, e in parse_cue(cue):
            pcs.setdefault(n, ("PC track %02d" % n, (lambda b=binpath, s=s, e=e: read_cd_audio(b, s, e))))
    for folder in wav:
        say("Pistes du CD PC : " + folder, "PC CD tracks: " + folder)
        for name in sorted(os.listdir(folder)):
            low = name.lower()
            if low.startswith("track") and low.endswith(".wav") and low[5:-4].isdigit():
                n = int(low[5:-4])
                pcs.setdefault(n, ("PC %s" % name, (lambda p=os.path.join(folder, name): read_wav(p))))
    for cdn, v in sorted(pcs.items()):
        ol = PC_TO_OL.get(cdn)
        if ol is not None and ol not in src:
            src[ol] = v
    if not o.all:
        src.pop(60, None)
    if not src:
        fail("no audio track found / aucune piste audio trouvee")

    entries = []
    t0 = time.time()
    total_s = 0.0
    say("Conversion (%s), quelques minutes..." % ("audioop" if audioop else "Python"),
        "Converting (%s), a few minutes..." % ("audioop" if audioop else "Python"))
    for n in sorted(src):
        label, load = src[n]
        a = load()
        z, left = make_zzm(a)
        secs = (len(z) - 16) / 22050.0           # stereo: 2 bytes per 2 frames
        total_s += secs
        entries.append(("track_%02d.zzm" % n, z))
        msg = "track %02d  <- %-26s %6.1f s  %8d bytes" % (n, label, secs, len(z))
        if o.check:
            dec = ima_decode(z[16::2])
            err = sum(abs(p - q) for p, q in zip(dec, left)) / max(1, len(left))
            msg += "  mean error %.1f" % err
        print(msg, flush=True)

    # A2.19.1: the directory right after the header (the game reads it first, then the tracks in order)
    dirlen = 64 * len(entries)
    pos = 12 + dirlen
    offs = []
    for name, z in entries:
        offs.append(pos)
        pos += (len(z) + 3) & ~3
    with open(out, "wb") as f:
        f.write(b"PACK" + struct.pack("<II", 12, dirlen))
        for (name, z), off in zip(entries, offs):
            e = bytearray(64)
            e[:len(name)] = name.encode("ascii")
            struct.pack_into("<II", e, 56, off, len(z))
            f.write(e)
        for name, z in entries:
            f.write(z)
            f.write(b"\0" * (((len(z) + 3) & ~3) - len(z)))
    size = os.path.getsize(out)
    print("")
    say("%s : %d pistes, %.1f minutes, %d octets (%.0f s)" % (out, len(entries), total_s / 60.0, size, time.time() - t0),
        "%s: %d tracks, %.1f minutes, %d bytes" % (out, len(entries), total_s / 60.0, size))
    missing = [NAMES[k] for k in sorted(NAMES) if k not in src]
    if missing:
        say("Absent : %s (le jeu marche quand meme, sans ce fond musical)." % ", ".join(missing),
            "Missing: %s (the game runs anyway, without that music)." % ", ".join(missing))
        if 58 in [k for k in NAMES if k not in src] or 59 in [k for k in NAMES if k not in src]:
            say("Wind et Pyramid sont sur le CD PC (pistes 4 et 5) : deposez aussi ses pistes.",
                "Wind and Pyramid are on the PC CD (tracks 4 and 5): drop its tracks too.")
    say("Copiez ol_music.pak dans le dossier data du jeu sur l'Amiga (PROGDIR:data/), a cote des .PHD.",
        "Copy ol_music.pak into the game's data folder on the Amiga (PROGDIR:data/), next to the .PHD files.")
    say("Verifiez que la taille sur l'Amiga est exactement %d octets. Fait avec vos disques : ne pas distribuer." % size,
        "Check that its size on the Amiga is exactly %d bytes. Made from your discs: do not give it away." % size)
    return 0


if __name__ == "__main__":
    sys.exit(main())
