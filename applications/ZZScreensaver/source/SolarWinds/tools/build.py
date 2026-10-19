#!/usr/bin/env python3
"""Fully offline build for Windows/MSYS using explicit local toolchains."""
import argparse, hashlib, pathlib, re, shutil, subprocess, sys
P=pathlib.Path(__file__).resolve().parents[1]
A=P/'ARM_RasterIX';L=P/'librixgpu';Z=P/'zzgl';S=P/'solarwinds';B=P/'build'
ap=argparse.ArgumentParser()
ap.add_argument('--arm-bin',default='C:/ArmGNUToolchain/13.2 Rel1/bin')
ap.add_argument('--m68k-bin',default='C:/amiga-gcc/bin')
ap.add_argument('--native',action='store_true',help='Linux CI toolchain filenames only')
args=ap.parse_args();suffix='' if args.native else '.exe'
def target_path(path):
    """MSYS Python uses /c/...; native Windows compilers need C:/....

    Keep Python file operations on their own pathlib paths. Convert paths only
    when constructing native-tool arguments, including -I and -Wl options.
    """
    value=str(path)
    if args.native:return value
    value=value.replace('\\','/')
    match=re.match(r'^/(?:cygdrive/)?([A-Za-z])(?:/|$)',value)
    if match:return match.group(1).upper()+':/'+value[match.end():]
    return value

def tool(folder,name):
    p=pathlib.Path(folder)/(name+suffix)
    if not p.is_file():raise SystemExit('Missing LOCAL compiler/tool: '+str(p))
    return target_path(p)
cc=tool(args.arm_bin,'arm-none-eabi-gcc')
objcopy=tool(args.arm_bin,'arm-none-eabi-objcopy')
size=tool(args.arm_bin,'arm-none-eabi-size')
nm=tool(args.arm_bin,'arm-none-eabi-nm')
m68k=tool(args.m68k_bin,'m68k-amigaos-gcc')
strip=tool(args.m68k_bin,'m68k-amigaos-strip')
B.mkdir(exist_ok=True)
def run(cmd,*,native_tool=True,**kw):
    argv=[target_path(v) if native_tool and isinstance(v,pathlib.PurePath) else str(v) for v in cmd]
    print('>',pathlib.Path(argv[0]).name,' '.join(argv[-3:]),flush=True)
    subprocess.run(argv,cwd=P,check=True,**kw)
f='-O2 -std=gnu99 -mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=softfp -mno-unaligned-access -fno-short-enums -ffreestanding -fno-builtin -fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -Wall -Wextra -Wno-misleading-indentation -Wno-unused-function -Wno-unused-variable'.split()
f+=['-I'+target_path(x) for x in [A,L,Z,S]]
sources=[A/x for x in ['startup_return.S','zzgear_app.c','zzgear_mmu.c','zzcore_runtime.c']]
sources += [L/(n+'.c') for n in ['rixgpu','rixframe','rixtex','rixdesc','rixclip']]
sources += [Z/'zzgl.c',S/'solarwinds.c',S/'history.c',S/'manager.c']
objects=[]
for src in sources:
    obj=B/(src.stem+'.o');run([cc,*f,'-c',src,'-o',obj]);objects.append(obj)
elf=B/'zzsolarwinds_core1.elf';binary=B/'zzsolarwinds_core1.bin'
run([cc,*f,'-nostartfiles','--specs=nosys.specs','-Wl,-T,'+target_path(A/'linker.ld'),
     '-Wl,--build-id=none','-Wl,--gc-sections','-Wl,-Map,'+target_path(B/'zzsolarwinds_core1.map'),
     '-o',elf,*objects,'-lm','-lc','-lgcc'])
run([objcopy,'-O','binary',elf,binary]);run([size,elf])
with (B/'zzsolarwinds_core1.sym').open('w') as out:run([nm,'-n',elf],stdout=out)
run([sys.executable,P/'tools/embed_blob.py',binary,B/'zzgear_blob.c'],native_tool=False)
exe=P/'ZZScreensaverS'
run([m68k,'-O2','-std=gnu99','-m68020','-mcrt=nix20','-fomit-frame-pointer',
     '-Wall','-Wextra','-Wno-unused-parameter','-I'+target_path(L),'-o',exe,
     A/'ZZGearRIX.c',B/'zzgear_blob.c','-lm'])
run([strip,exe])
shutil.copy2(exe,P/'ZZSolarWindsRIX')
(P/'BINARY_SHA256SUMS.txt').write_text(''.join(hashlib.sha256(x.read_bytes()).hexdigest()+'  '+str(x.relative_to(P))+'\n' for x in [exe,binary,elf]))
print('SUCCESS: ZZScreensaverS + ZZSolarWindsRIX compatibility copy (Core1 embedded). No external data file required.')
