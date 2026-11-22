"""Offline Windows/MSYS build with the published local XACP toolchain paths."""
from pathlib import Path
import hashlib,subprocess,os
import dispatch,veneers,binutil
root=Path(__file__).resolve().parents[1]
os.chdir(root)
arm=Path('C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-gcc')
objcopy=arm.with_name('arm-none-eabi-objcopy')
m68=Path('C:/amiga-gcc/bin/m68k-amigaos-gcc')
def executable(path):
    for p in (path.with_suffix('.exe'),path):
        if p.is_file():return str(p)
    raise SystemExit('Local compiler or utility missing: '+str(path))
def main():
    ag=executable(arm);ac=executable(objcopy);mg=executable(m68)
    work=Path('build/gnu');work.mkdir(parents=True,exist_ok=True)
    (root/'logs').mkdir(exist_ok=True);out=work/'amiga';out.mkdir(exist_ok=True)
    Path('amiga').mkdir(exist_ok=True)
    dispatch.generate(root);veneers.generate(root)
    with open('logs/windows-build.log','w',encoding='utf-8') as log:
        def run(args):
            args=list(map(str,args));line=subprocess.list2cmdline(args)
            log.write(line+'\n');log.flush()
            res=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            msg=res.stdout.decode(errors='replace');log.write(msg);log.flush()
            if res.returncode:raise SystemExit(line+'\n'+msg)
        for tool in (ag,mg,ac):run([tool,'--version'])
        armflags=['-mcpu=cortex-a9','-marm','-mfpu=vfpv3','-mfloat-abi=hard','-O2','-ffreestanding','-fno-builtin','-fno-common','-fno-strict-aliasing','-fno-unwind-tables','-DZZGL_MAX_TEXTURES=2560u','-DZZGL_ENABLE_WALK_MODEL=0','-DZZGL_HALF_BUF_NC=1','-DZZGL_STATE_ARENA=1','-Ishared','-Ivendor/zzgl','-Ivendor/librixgpu']
        files=[Path('srcarm/start.S'),*sorted(Path('srcarm').glob('*.c')),*sorted(Path('vendor/librixgpu').glob('*.c')),Path('vendor/zzgl/zzgl.c'),Path('shared/wire.c')]
        objs=[]
        for src in files:
            obj=work/('arm_'+src.stem+'.o');run([ag,*armflags,'-c',src,'-o',obj]);objs.append(obj)
        elf=work/'core1.elf';blob=work/'core1.bin'
        run([ag,*armflags,'-nostdlib','-Wl,-T,srcarm/zzmini.ld','-Wl,-Map,build/gnu/core1.map','-o',elf,*objs,'-Wl,--start-group','-lm','-lc','-lgcc','-Wl,--end-group'])
        run([ac,'-O','binary',elf,blob])
        binutil.embed(blob,work/'image.c')
        flags=['-m68060','-m68881','-O2','-std=gnu99','-ffreestanding','-fno-builtin','-fno-common','-fomit-frame-pointer','-fno-strict-aliasing','-Ishared','-Isrc68k','-Ibuild']
        def compile68(path,tag=None,defines=()):
            src=Path(path);obj=work/('m68_'+(tag or src.stem)+'.o');run([mg,*flags,*defines,'-c',src,'-o',obj]);return obj
        def link(name,entry,objects):
            run([mg,*flags,'-mcrt=nix20','-nostdlib','-nostartfiles','-Wl,-e,'+entry,'-Wl,-Map,build/gnu/'+name+'.map','-o',out/name,*objects,'-lgcc'])
            if (out/name).read_bytes()[:4]!=bytes.fromhex('000003f3'):raise SystemExit('Not an Amiga HUNK: '+name)
        osobj=compile68('build/os.S');logger=compile68('src68k/log.c');pf=compile68('src68k/platform.c')
        link('minigl.library','_lib_first',[compile68('src68k/resident.S'),compile68('src68k/trap.S'),compile68('src68k/front.c'),compile68('build/dispatch.c'),compile68('build/slot_names.c'),pf,osobj,logger,compile68(work/'image.c')])
        cli=compile68('src68k/cli.S')
        # A8: puts PROGDIR:minigl.library in memory (identity checked) so that an unmodified application
        # opening "minigl.library" by name gets it without anything copied to LIBS:
        link('ZZMiniGLLoad','_cli_first',[cli,compile68('src68k/loader.c'),compile68('build/slot_names.c'),osobj,logger])   # A8.4: reads the black box of the previous run
        for file in sorted(out.iterdir()):
            dest=Path('amiga')/file.name;stage=dest.with_suffix('.new')
            stage.write_bytes(file.read_bytes());stage.replace(dest)
            log.write(hashlib.sha256(dest.read_bytes()).hexdigest()+'  '+str(dest)+'\n')
    # Refuse a successful exit if a stale A1a binary survived in the output.
    libdata=(Path('amiga')/'minigl.library').read_bytes()
    if b'ZZMiniGL XACP A8' not in libdata or b'p96_a8_marker' not in libdata or b'calls_' not in libdata or b'core1_xform_us' not in libdata:
        raise SystemExit('Build produced a stale or mislabeled image, refusing success')
    if b'ZZMiniGLLoad A8.29' not in (Path('amiga')/'ZZMiniGLLoad').read_bytes():
        raise SystemExit('ZZMiniGLLoad missing')
    print('Built ZZMiniGL Beta 1: minigl.library and ZZMiniGLLoad in amiga/')
if __name__=='__main__':main()
