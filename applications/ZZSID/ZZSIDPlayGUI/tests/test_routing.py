#!/usr/bin/env python3
"""Static regressions for the PSID v3/v4 SID2 address decoder.

No Amiga or ARM compiler required. These tests do not substitute for a
real Amiga audio test, and deliberately don't distribute copyrighted tunes.
"""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
SRC=(ROOT/'sources/ZZSIDPlay.c').read_text(encoding='latin1')
ARM=(ROOT/'sources/ARM/zzresid_core1.cpp').read_text(encoding='latin1')


def psid_header(version=3,sid2=0,sid3=0):
    b=bytearray(0x7C)
    b[:4]=b'PSID';b[4:6]=version.to_bytes(2,'big')
    b[0x7A]=sid2;b[0x7B]=sid3
    return bytes(b)


def parse_v3_v4(data):
    version=int.from_bytes(data[4:6],'big')
    def good(addr):
        return addr%2==0 and (0x42<=addr<=0x7E or 0xE0<=addr<=0xFE)
    if not (3<=version<=4):return []
    b2,b3=data[0x7A:0x7C]
    result=[]
    if good(b2):result.append(0xD000+(b2<<4))
    if version>=4 and good(b3) and b3!=b2:result.append(0xD000+(b3<<4))
    return result


def route(address,n=2,sid2=0xD500):
    bases=[0xD400,sid2]
    for i,base in enumerate(bases[:n]):
        if base<=address<base+0x20:return (i,address-base)
    if 0xD400<=address<=0xD7FF:return (0,address & 31)
    return None

assert parse_v3_v4(psid_header(3,0x42))==[0xD420]
assert parse_v3_v4(psid_header(3,0x50))==[0xD500]
assert parse_v3_v4(psid_header(3,0xE0))==[0xDE00]
assert parse_v3_v4(psid_header(3,0xFF))==[]
assert parse_v3_v4(psid_header(3,0x80))==[]
assert parse_v3_v4(psid_header(4,0x50,0xE0))==[0xD500,0xDE00]
assert parse_v3_v4(psid_header(5,0x50))==[]
assert route(0xD400)==(0,0)
assert route(0xD41B)==(0,0x1B)
assert route(0xD500)==(1,0)
assert route(0xD518)==(1,0x18)
assert route(0xD420)==(0,0)  # mirror to SID0 when SID2 at D500
assert route(0xDE00,sid2=0xDE00)==(1,0)
assert route(0xDF18,sid2=0xDF00)==(1,0x18)
assert route(0xDE00,sid2=0xD500) is None
assert 'g_sid1_base = g_si.sid2 ? g_si.sid2 : 0xD420UL;' in SRC
assert SRC.count('GTCY_Active, (ULONG)g_sid_sel, TAG_END')==1
assert 'req_b1 >= 0xD800u && req_b1 < 0xDE00u' in ARM
assert 'int chip = sid_find_chip(address, &reg);' in ARM
assert '#define ZZRESID_ABI_VERSION  5' in (ROOT/'sources/zzresid_shared.h').read_text()
print('PASS: synthetic PSID v3/v4 addresses, valid ranges, SID decoder and patch integrity')
print('WARNING: this is not a test of audible stereo or on-device timing.')
