#!/usr/bin/env python3
"""
make_synth_roms.py - build a SYNTHETIC ROM set for the host regression test.

Nothing here comes from a Capcom ROM. The 16 files only reuse the NAMES and SIZES of the
Strider set so that the unmodified chunk transport and decoders of the blob are exercised:

  68000 program : tests/synth/prog.c (our own test program), split like the real set
  graphics      : solid-colour tiles (pen = (unit >> 3) & 15, unit = 64-byte cell) plus two
                  asymmetric sprite tiles, written through the board's ROM interleave as MAME
                  documents it (ROM_LOAD64_WORD, planes {24,16,8,0}, MSB = left pixel)
  stars         : every star disabled except one per field
  Z80 program   : hand-assembled below (YM2151 tone + timer A IRQ, bank check, one OKI phrase)
  OKI samples   : one ADPCM phrase

usage: make_synth_roms.py <prog.bin> <output directory>
ASCII only.
"""
import sys
from pathlib import Path

GFX_SIZE = 0x400000


def solid(k):
    """4 plane bytes of 8 pixels that all have pen k."""
    return bytes(0xFF if (k >> j) & 1 else 0x00 for j in range(4))


def row8(pens):
    """4 plane bytes for 8 pixels, pens[0] = leftmost."""
    out = bytearray(4)
    for x, pen in enumerate(pens):
        for j in range(4):
            if (pen >> j) & 1:
                out[j] |= 0x80 >> x
    return bytes(out)


def build_gfx():
    g = bytearray(GFX_SIZE)
    for unit in range(GFX_SIZE // 64):
        g[unit * 64:(unit + 1) * 64] = solid((unit >> 3) & 15) * 16
    # sprite tile 0x100: quadrants  TL=1 TR=2 / BL=3 BR=4
    base = 0x100 * 128
    for r in range(16):
        left, right = (1, 2) if r < 8 else (3, 4)
        g[base + r * 8:base + r * 8 + 8] = solid(left) + solid(right)
    # sprite tile 0x104: horizontal ramp, pens 1..8 then 9..14,0,1
    base = 0x104 * 128
    for r in range(16):
        g[base + r * 8:base + r * 8 + 8] = row8([1, 2, 3, 4, 5, 6, 7, 8]) + row8([9, 10, 11, 12, 13, 14, 0, 1])
    # star fields live in region bytes 8*offs (field 1) and 8*offs+4 (field 2), offs < 0x1000:
    # switch every star off (0x0F), then place one star per field.
    for offs in range(0x1000):
        g[8 * offs] = 0x0F
        g[8 * offs + 4] = 0x0F
    g[8 * (2 * 256 + 100)] = 0x45          # field 1: column 2, line 100, x fine 5, colour group 2
    g[8 * (5 * 256 + 120) + 4] = 0x83      # field 2: column 5, line 120, x fine 3, colour group 4
    return g


def build_z80():
    rom = bytearray(0x10000)
    pos = [0]

    def at(addr):
        pos[0] = addr

    def emit(expect, *b):
        assert pos[0] == expect, "Z80 layout: at %04X, expected %04X" % (pos[0], expect)
        rom[pos[0]:pos[0] + len(b)] = bytes(b)
        pos[0] += len(b)

    emit(0x0000, 0xF3)                    # di
    emit(0x0001, 0x31, 0x00, 0xD8)        # ld sp,D800
    emit(0x0004, 0xED, 0x56)              # im 1
    emit(0x0006, 0xC3, 0x50, 0x00)        # jp 0050
    at(0x0038)                            # IM 1 handler: YM2151 timer A
    emit(0x0038, 0xF5)                    # push af
    emit(0x0039, 0xE5)                    # push hl
    emit(0x003A, 0x3E, 0x14)              # ld a,14
    emit(0x003C, 0x32, 0x00, 0xF0)        # ld (F000),a
    emit(0x003F, 0x3E, 0x15)              # ld a,15       reset flag A, IRQ A on, load A
    emit(0x0041, 0x32, 0x01, 0xF0)        # ld (F001),a
    emit(0x0044, 0x21, 0x00, 0xD0)        # ld hl,D000
    emit(0x0047, 0x34)                    # inc (hl)
    emit(0x0048, 0xE1)                    # pop hl
    emit(0x0049, 0xF1)                    # pop af
    emit(0x004A, 0xFB)                    # ei
    emit(0x004B, 0xC9)                    # ret
    at(0x0050)
    emit(0x0050, 0x21, 0xB0, 0x00)        # ld hl,00B0    YM register table
    emit(0x0053, 0x7E)                    # ld a,(hl)
    emit(0x0054, 0xFE, 0xFF)              # cp FF
    emit(0x0056, 0x28, 0x0B)              # jr z,0063
    emit(0x0058, 0x32, 0x00, 0xF0)        # ld (F000),a
    emit(0x005B, 0x23)                    # inc hl
    emit(0x005C, 0x7E)                    # ld a,(hl)
    emit(0x005D, 0x32, 0x01, 0xF0)        # ld (F001),a
    emit(0x0060, 0x23)                    # inc hl
    emit(0x0061, 0x18, 0xF0)              # jr 0053
    emit(0x0063, 0x3E, 0x01)              # ld a,01
    emit(0x0065, 0x32, 0x04, 0xF0)        # ld (F004),a   bank 1
    emit(0x0068, 0x3A, 0x00, 0x80)        # ld a,(8000)
    emit(0x006B, 0x32, 0x01, 0xD0)        # ld (D001),a
    emit(0x006E, 0xAF)                    # xor a
    emit(0x006F, 0x32, 0x04, 0xF0)        # ld (F004),a   bank 0
    emit(0x0072, 0x3A, 0x00, 0x80)        # ld a,(8000)
    emit(0x0075, 0x32, 0x02, 0xD0)        # ld (D002),a
    emit(0x0078, 0xFE, 0xB0)              # cp B0
    emit(0x007A, 0x20, 0xFE)              # jr nz,007A    hang: bank 0 wrong
    emit(0x007C, 0x3A, 0x01, 0xD0)        # ld a,(D001)
    emit(0x007F, 0xFE, 0xB1)              # cp B1
    emit(0x0081, 0x20, 0xFE)              # jr nz,0081    hang: bank 1 wrong
    emit(0x0083, 0xFB)                    # ei
    emit(0x0084, 0x3A, 0x08, 0xF0)        # ld a,(F008)   sound latch
    emit(0x0087, 0xFE, 0x42)              # cp 42
    emit(0x0089, 0x20, 0xF9)              # jr nz,0084
    emit(0x008B, 0x3A, 0x03, 0xD0)        # ld a,(D003)
    emit(0x008E, 0xB7)                    # or a
    emit(0x008F, 0x20, 0xF3)              # jr nz,0084
    emit(0x0091, 0x3E, 0x81)              # ld a,81       OKI phrase 1
    emit(0x0093, 0x32, 0x02, 0xF0)        # ld (F002),a
    emit(0x0096, 0x3E, 0x10)              # ld a,10       voice 0, no attenuation
    emit(0x0098, 0x32, 0x02, 0xF0)        # ld (F002),a
    emit(0x009B, 0x3E, 0x01)              # ld a,01
    emit(0x009D, 0x32, 0x03, 0xD0)        # ld (D003),a
    emit(0x00A0, 0x18, 0xE2)              # jr 0084
    table = [0x20, 0xC7, 0x28, 0x4A, 0x30, 0x00, 0x40, 0x01, 0x48, 0x01, 0x50, 0x01, 0x58, 0x01,
             0x60, 0x10, 0x68, 0x7F, 0x70, 0x7F, 0x78, 0x7F, 0x80, 0x1F, 0x88, 0x1F, 0x90, 0x1F,
             0x98, 0x1F, 0xA0, 0x00, 0xC0, 0x00, 0xE0, 0x0F, 0xE8, 0x0F, 0xF0, 0x0F, 0xF8, 0x0F,
             0x08, 0x78, 0x10, 0xF0, 0x11, 0x00, 0x14, 0x15, 0xFF]
    rom[0x00B0:0x00B0 + len(table)] = bytes(table)
    rom[0x8000] = 0xB0                    # first byte of bank 0
    rom[0xC000] = 0xB1                    # first byte of bank 1
    return rom


def build_oki():
    rom = bytearray(0x40000)
    rom[8:14] = bytes([0x00, 0x04, 0x00, 0x00, 0x13, 0xFF])      # phrase 1: 0x000400..0x0013FF
    for i in range(0x400, 0x1400, 4):
        rom[i:i + 4] = bytes([0x77, 0x77, 0xFF, 0xFF])
    return rom


def main():
    prog = bytearray(Path(sys.argv[1]).read_bytes())
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    prog += bytes(0x100000 - len(prog))
    files = {}
    files["30.11f"] = prog[0x00000:0x40000:2]
    files["35.11h"] = prog[0x00001:0x40000:2]
    files["31.12f"] = prog[0x40000:0x80000:2]
    files["36.12h"] = prog[0x40001:0x80000:2]
    hi = bytearray(prog[0x80000:0x100000])
    hi[0::2], hi[1::2] = hi[1::2], hi[0::2]                      # ROM_LOAD16_WORD_SWAP
    files["st-14.8h"] = hi
    gfx = build_gfx()
    names = [["st-2.8a", "st-11.10a", "st-5.4a", "st-9.6a"], ["st-1.7a", "st-10.9a", "st-4.3a", "st-8.5a"]]
    for half in range(2):
        blk = gfx[half * 0x200000:(half + 1) * 0x200000]
        for n in range(4):
            f = bytearray(0x80000)
            f[0::2] = blk[2 * n::8]
            f[1::2] = blk[2 * n + 1::8]
            files[names[half][n]] = f
    files["09.12b"] = build_z80()
    oki = build_oki()
    files["18.11c"] = oki[:0x20000]
    files["19.12c"] = oki[0x20000:]
    for name, data in files.items():
        (out / name).write_bytes(bytes(data))
    print("synthetic set written: %d files, %d bytes" % (len(files), sum(len(d) for d in files.values())))


if __name__ == "__main__":
    main()
