# SPDX-License-Identifier: GPL-3.0-only
# Original ZZOpenLara conversion tool: Copyright (C) 2026 Xanxi.
# GPLv3 section 7(b) attribution: see ../ATTRIBUTION.txt
#!/usr/bin/env python3
"""ZZOpenLara: copies the loading screens of the PlayStation disc (DELDATA/*.RAW) out of YOUR disc image.

The PC game has no loading screens; the PlayStation version has five (Lara before the mine for Peru, Greece,
Egypt, Atlantis, the manor). OpenLara shows them before each level when they are in PROGDIR:data/.

    python ol_psx_screens.py "Tomb Raider (Europe).cue"        (or the "(Track 01).bin" itself)
    python ol_psx_screens.py                                     (looks for a .cue / Track 01 .bin next to the script)

Writes AZTECLOA.RAW, GREEKLOA.RAW, EGYPTLOA.RAW, ATLANLOA.RAW, GYMLOAD.RAW into a folder "psx_screens"
next to the image: copy them into PROGDIR:data/ on the Amiga. Made from your own disc: do not distribute.
Python 3 only, nothing to install. ASCII only.

Les ecrans de chargement du disque PlayStation, tires de TON image : copier le dossier "psx_screens" produit
dans PROGDIR:data/ sur l'Amiga.
"""
import os, sys, struct
from pathlib import Path

WANTED = ('AZTECLOA.RAW', 'GREEKLOA.RAW', 'EGYPTLOA.RAW', 'ATLANLOA.RAW', 'GYMLOAD.RAW')
SYNC = b'\x00' + b'\xff' * 10 + b'\x00'


def say(fr, en):
    print(fr + '  /  ' + en)


class Disc:
    """2048-byte logical sectors of a raw (2352) or cooked (2048) data track."""
    def __init__(self, path):
        self.f = open(path, 'rb')
        head = self.f.read(2352 * 17)
        if head[:12] == SYNC:
            self.size = 2352
            mode = head[15]
            self.data = 24 if mode == 2 else 16    # MODE2 form 1: 16 header + 8 subheader
        else:
            self.size, self.data = 2048, 0
        if self.read(16, 1)[1:6] != b'CD001':
            raise SystemExit(f'{path}: not a CD data track (no ISO 9660 volume)')

    def read(self, lba, count):
        out = bytearray()
        for i in range(count):
            self.f.seek((lba + i) * self.size + self.data)
            out += self.f.read(2048)
        return bytes(out)

    def extent(self, lba, size):
        return self.read(lba, (size + 2047) // 2048)[:size]


def records(disc, lba, size):
    d = disc.extent(lba, size)
    i = 0
    while i < len(d):
        n = d[i]
        if n == 0:                     # records do not cross sectors: jump to the next one
            i = (i // 2048 + 1) * 2048
            continue
        r = d[i:i + n]
        elba, esize, flags, nlen = struct.unpack_from('<I', r, 2)[0], struct.unpack_from('<I', r, 10)[0], r[25], r[32]
        name = r[33:33 + nlen].decode('ascii', 'replace').split(';')[0]
        yield name, elba, esize, bool(flags & 2)
        i += n


def find_track(arg):
    p = Path(arg)
    if p.suffix.lower() == '.cue':
        for line in p.read_text(errors='replace').splitlines():
            line = line.strip()
            if line.upper().startswith('FILE'):
                name = line[line.index('"') + 1:line.rindex('"')] if '"' in line else line.split()[1]
                return p.parent / name     # the first FILE is the data track
        raise SystemExit(f'{p}: no FILE line')
    return p


def main():
    args = sys.argv[1:]
    if not args:
        entry = sys.executable if getattr(sys, "frozen", False) else __file__
        here = [Path(entry).resolve().parent, Path.cwd()]
        found = [q for d in here for q in sorted(d.glob('*.cue'))] + \
                [q for d in here for q in sorted(d.glob('*Track 01*.bin')) + sorted(d.glob('*track 01*.bin'))]
        if not found:
            say('Aucune image trouvee : deposer le .cue du disque PSX sur ZZOpenLara-PSX-Screens.bat',
                'No image found: drop the PSX disc .cue on ZZOpenLara-PSX-Screens.bat')
            return 1
        args = [str(found[0])]
    track = find_track(args[0])
    if not track.exists():
        raise SystemExit(f'{track}: not found')
    disc = Disc(track)
    pvd = disc.read(16, 1)
    root = pvd[156:156 + 34]
    rlba, rsize = struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0]
    deldata = [(l, s) for n, l, s, d in records(disc, rlba, rsize) if d and n.upper() == 'DELDATA']
    if not deldata:
        say('Pas de dossier DELDATA : ce n\'est pas le disque PlayStation de Tomb Raider',
            'No DELDATA folder: not the Tomb Raider PlayStation disc')
        return 1
    out = track.parent / 'psx_screens'
    out.mkdir(exist_ok=True)
    got = []
    for n, l, s, d in records(disc, *deldata[0]):
        if not d and n.upper() in WANTED:
            data = disc.extent(l, s)
            (out / n.upper()).write_bytes(data)
            got.append(n.upper())
            print(f'  {n.upper():14s} {len(data):7d}')
    if not got:
        say('Aucun ecran trouve dans DELDATA', 'No screen found in DELDATA')
        return 1
    say(f'{len(got)} ecrans dans {out} : les copier dans PROGDIR:data/',
        f'{len(got)} screens in {out}: copy them into PROGDIR:data/')
    return 0


if __name__ == '__main__':
    sys.exit(main())
