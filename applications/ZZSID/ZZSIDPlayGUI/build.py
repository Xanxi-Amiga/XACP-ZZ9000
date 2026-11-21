#!/usr/bin/env python3
"""Offline ZZSIDPlayGUI build. Only the fixed local Windows toolchains are used.

  python build.py             -> build A2 (the validated 2SID source edition)
  python build.py --reference-a1 -> build historical A1 for regression
  python build.py --check     -> static checks + reference ARM blob integrity
"""
from __future__ import annotations
import hashlib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ARM = ROOT / 'sources' / 'ARM'
WORK = ROOT / '.build'
OUT = ROOT / 'output'
KNOWN = '933eae03c0e93aadc1ddc467ecf62d15f7f69b177d49c1c7dcf5eacf3ec4324f'
M68 = 'C:/amiga-gcc/bin/m68k-amigaos-gcc'
ARM_PREFIX = 'C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-'


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run(args: list[str], cwd: Path):
    print('RUN:', ' '.join(args), flush=True)
    subprocess.run(args, cwd=str(cwd), check=True)


def verify_inputs():
    blob = (ROOT/'sources'/'zzresid_core1_WORKING.bin').read_bytes()
    if len(blob) != 261448 or sha(blob) != KNOWN:
        raise SystemExit('ERROR: original working Core1 blob is NOT the Amiga reference.')
    h1 = (ROOT/'sources'/'zzresid_shared.h').read_bytes()
    h2 = (ARM/'zzresid_shared.h').read_bytes()
    if h1 != h2 or b'#define ZZRESID_ABI_VERSION  5' not in h1:
        raise SystemExit('ERROR: ARM and Amiga shared headers/ABI mismatch.')
    src = (ROOT/'sources'/'ZZSIDPlay.c').read_text(encoding='latin1')
    if 'g_sid1_base = g_si.sid2 ? g_si.sid2 : 0xD420UL;' not in src:
        raise SystemExit('ERROR: missing 2SID routing correction in Amiga source.')
    arm_src = (ARM/'zzresid_core1.cpp').read_text(encoding='latin1')
    if arm_src.count('int chip = sid_find_chip(address, &reg);') < 2:
        raise SystemExit('ERROR: ARM SID read/write routing code not present.')
    print('OK: retained Oct 22 original ARM blob matches its verified SHA-256.')
    print('OK: ARM and Amiga ABI5 headers are exactly identical.')
    print('OK: patched sources are in place.')


def make_embedded(blob: bytes):
    WORK.mkdir(exist_ok=True)
    gen = WORK/'zzresid_blob.c'
    with gen.open('w', encoding='ascii', newline='\n') as f:
        f.write('/* Generated offline by build.py; ARM blob is baked into launcher. */\n')
        f.write('const unsigned char zzresid_core1_bin[] = {\n')
        for i in range(0, len(blob), 16):
            f.write('    ' + ', '.join('0x%02x'%x for x in blob[i:i+16]) + ',\n')
        f.write('};\n')
        f.write('const unsigned long zzresid_core1_bin_len = %dUL;\n' % len(blob))
    return gen


def build_gui(blob: bytes, name: str):
    OUT.mkdir(exist_ok=True)
    make_embedded(blob)
    dest = OUT/name
    if dest.exists():
        dest.unlink()
    cmd = [M68, '-O2', '-m68020', '-mcrt=nix20', '-Wno-pointer-sign',
           '-DZZSID_GUI', '-DZZRESID_EMBED_ARRAY', '-Isources',
           '-o', str(dest.relative_to(ROOT)), 'sources/ZZSIDPlay.c',
           '.build/zzresid_blob.c', '-lamiga']
    run(cmd, ROOT)
    result = dest.read_bytes()
    if result.count(blob) != 1:
        dest.unlink(missing_ok=True)
        raise SystemExit('ERROR: the expected Core1 blob is not present EXACTLY once in the Amiga executable.')
    if len(result) < len(blob) + 10000:
        dest.unlink(missing_ok=True)
        raise SystemExit('ERROR: the compiled executable is implausibly small.')
    print('SUCCESS:', dest.relative_to(ROOT))
    print('  GUI size:', len(result), 'bytes')
    print('  GUI SHA256:', sha(result))
    print('  embedded Core1 SHA256:', sha(blob))
    return dest


def build_arm() -> bytes:
    """Rebuild changed ARM code using only the locally installed toolchain.
    The original working blob is never overwritten or mutated.
    """
    objdir = ARM/'.build_arm'
    objdir.mkdir(exist_ok=True)
    cpu = ['-mcpu=cortex-a9','-marm','-mfpu=vfpv3-d16','-mfloat-abi=hard']
    cppflags = cpu + ['-O2','-ffunction-sections','-fdata-sections',
        '-fno-unwind-tables','-fno-asynchronous-unwind-tables',
        '-std=c++11','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics',
        '-DNDEBUG','-DNMOS6502','-DZZRESID_BUILD_C=1','-I.','-Iresid']
    ccflags = cpu + ['-O2','-DNMOS6502','-ffunction-sections','-fdata-sections','-I.']
    cpp = ['zzresid_core1.cpp','resid/sid.cc','resid/voice.cc',
      'resid/wave.cc','resid/envelope.cc','resid/filter.cc',
      'resid/extfilt.cc','resid/pot.cc','resid/dac.cc']
    objects=[]
    for source in cpp:
        obj='.build_arm/'+Path(source).stem+'.o'
        run([ARM_PREFIX+'g++',*cppflags,'-c','-o',obj,source], ARM)
        objects.append(obj)
    run([ARM_PREFIX+'gcc',*ccflags,'-c','-o','.build_arm/fake6502.o','fake6502.c'], ARM)
    objects.append('.build_arm/fake6502.o')
    run([ARM_PREFIX+'gcc',*cpu,'-c','-o','.build_arm/startup.o','startup.S'], ARM)
    objects.append('.build_arm/startup.o')
    elf = '.build_arm/zzresid_core1_A2.elf'
    memmap = '.build_arm/zzresid_core1_A2.map'
    run([ARM_PREFIX+'g++',*cpu,'-nostartfiles','--specs=nosys.specs',
         '-Wl,--gc-sections','-T','zzresid_core1.ld',
         '-Wl,-Map,'+memmap,'-o',elf,*objects], ARM)
    binpath='.build_arm/zzresid_core1_A2.bin'
    run([ARM_PREFIX+'objcopy','-O','binary',elf,binpath], ARM)
    data=(ARM/binpath).read_bytes()
    if not (100000 <= len(data) < 1024*1024):
        raise SystemExit('ERROR: ARM blob size outside expected limits.')
    print('ARM A2:',len(data),'bytes; SHA-256:',sha(data))
    return data


def main():
    verify_inputs()
    if '--check' in sys.argv[1:]:
        return
    unknown = set(sys.argv[1:]) - {'--reference-a1'}
    if unknown:
        raise SystemExit('Unknown flags: ' + ', '.join(sorted(unknown)))
    print('Build is 100% offline; no PATH modifications, no downloads.')
    if '--reference-a1' in sys.argv[1:]:
        known=(ROOT/'sources'/'zzresid_core1_WORKING.bin').read_bytes()
        build_gui(known, 'ZZSIDPlayGUI_A1_REFERENCE_NOT_FOR_RELEASE')
        print('WARNING: A1 reference is missing the DE00 routing fix. Do not release it as v1.0.')
    else:
        fresh=build_arm()
        build_gui(fresh, 'ZZSIDPlayGUI')
        print('A2 built: the validated source revision with extended SID2 address routing.')


if __name__ == '__main__':
    try:
        main()
    except FileNotFoundError as exc:
        raise SystemExit('Missing file or toolchain program: '+str(exc))
    except subprocess.CalledProcessError as exc:
        raise SystemExit('CROSS COMPILATION FAILED: '+str(exc))
