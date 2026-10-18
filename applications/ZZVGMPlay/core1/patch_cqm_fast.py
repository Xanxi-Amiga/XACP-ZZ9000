#!/usr/bin/env python3
"""Speed-neutral synthesis, faster static slot indexing for official Nuked-CQM 0.9.
No modifications to vendor sources on disk. Generates a compilation-only copy.
Rejects unknown revisions instead of silently applying a possibly wrong patch.
"""
from pathlib import Path
import sys
if len(sys.argv)!=3:
    raise SystemExit('usage: python patch_cqm_fast.py third_party/Nuked-CQM/cqm.c cqm_zzfast.c')
src=Path(sys.argv[1]); dst=Path(sys.argv[2]); s=src.read_text(encoding='utf-8-sig')
start_marker='''        {
            int idx2 = idx;
            if (idx2 >= 24)
'''
end_marker='''        if (op == 0)
            modptr = chip->slotz[idx + 1].mod;
'''
if s.count(start_marker)!=1 or s.count(end_marker)!=1:
    raise SystemExit('Unsupported CQM source layout; refusing to alter it. Retain A08 source.')
a=s.index(start_marker); b=s.index(end_marker,a)
original=s[a:b]
required=['ch += idx2 >> 2;','ch += (idx2 >> 1) - 3;','keybit = ch + 2;','keybit = ch * 2 + op - 7;','is4op = (chip->mode >> (ch - 6)) & 1;']
for line in required:
    if line not in original:raise SystemExit('CQM slot mapping changed: '+line)
meta=[]
for idx in range(48):
    n=idx - (24 if idx>=24 else 0); bank=9 if idx>=24 else 0
    if n<12:
        ch=bank+(n>>2);op=n&3
        ms=ch if ch<9 else ch-6
    else:
        ch=bank+(n>>1)-3;op=n&1
        ms=(ch-3 if 3<=ch<6 else ch-9 if 12<=ch<15 else 7)
    keybit=ch if ch<7 else ch*2+op-7 if ch<9 else ch+2
    meta.append((ch,op,keybit,ms,(ch%9)<3))
assert len(meta)==48 and max(x[3] for x in meta)==7
# CQM mode always masks 0x3f on write, so bit 7 always zero. Mode 7 means 'no four-op'.
static='''/* ZZVGMPlay A08 FIX3: metadata constant per pipeline slot.
   Values derived exactly from the original dynamic mapping below.
   No synthesis operator/envelope/noise behavior changed. */
typedef struct { unsigned char ch, op, keybit, modebit, is4opch; } zz_cqm_slot_meta_t;
static const zz_cqm_slot_meta_t zz_cqm_slot_meta[48] = {
'''
static+='\n'.join('    {%d,%d,%d,%d,%d},'%t for t in meta)+'\n};\n\n'
old='''    for (idx = 0; idx < 48; idx++)
'''
if s.count(old)!=1:raise SystemExit('Unknown CQM_Generate loop format')
s=s.replace(old,old+'        /* Hoist precomputed slot metadata into the iteration. */\n',1)
# Replace only dynamic sub-block, then references to channel modulus.
replacement='''        {
            const zz_cqm_slot_meta_t* m = &zz_cqm_slot_meta[idx];
            ch = m->ch;
            op = m->op;
            keybit = m->keybit;
            is4op = (chip->mode >> m->modebit) & 1;
        }

'''
s=s[:s.index(start_marker)]+replacement+s[s.index(end_marker,s.index(start_marker)):]
old2='''int is4opch = (ch % 9) < 3;'''
if s.count(old2)!=1:raise SystemExit('Unknown CQM operator output mapping')
s=s.replace(old2,'int is4opch = zz_cqm_slot_meta[idx].is4opch;',1)
# When an operator's 17-bit envelope is zero, output is mathematically zero:
# after subtracting TL, tremolo and KSL, its gain is still (0x4000>>15)==0.
# IMPORTANT: never skip in rhythm mode: HH/TC mutate shared rhythm bits.
# Phase, envelope, noise and modulation still run unchanged.
wave_start="""        {
            int wf;
            int phase_in;
            int phase;
"""
wave_end="""        pmulti = chip->regs[access[4]];"""
if s.count(wave_start)!=1 or s.count(wave_end)!=1:
    raise SystemExit('CQM waveform block changed; refusing unsafe transform')
s=s.replace(wave_start,wave_start.replace('        {\n','        if (isrhy || ((slot->env & 0x1ffff) != 0)) {\n',1),1)
# Insert complementary zero-output branch after the original waveform block.
s=s.replace(wave_end,'        else { wave = 0; }\n\n'+wave_end,1)
needle='void CQM_Generate(cqm_t* chip, int16_t* sample)'
if s.count(needle)!=1:raise SystemExit('Unknown CQM synthesis function')
s=s.replace(needle,static+needle,1)
dst.write_text(s,encoding='utf-8')
print('CQM optimized compile copy generated:',dst,'bytes:',len(s),'slot maps:',len(meta))
