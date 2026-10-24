"""Fully offline build. No PATH change, MSYS utilities, downloads or /c paths."""
from pathlib import Path
import os,sys,subprocess,shutil,hashlib
ROOT=Path(__file__).resolve().parent.parent
os.chdir(ROOT)
# __DATE__ (core.h) from a fixed date: same sources, same blob
os.environ.setdefault('SOURCE_DATE_EPOCH','1792324800')
ARM=os.environ.get('OL_ARM_BIN','C:/ArmGNUToolchain/13.2 Rel1/bin')
M68K=os.environ.get('OL_M68K_CC','C:/amiga-gcc/bin/m68k-amigaos-gcc')
def tool(name):
    p=Path(ARM)/(name+'.exe' if os.name=='nt' else name)
    if not p.exists(): p=Path(ARM)/name
    if not p.exists(): p=Path(ARM)/(name+'.exe')
    if not p.exists(): raise SystemExit('Missing compiler/tool: '+str(p))
    return str(p)
def run(args):
    print(' '.join(map(str,args)),flush=True);subprocess.run(list(map(str,args)),check=True)
def arm(render='rix'):
    cc,cxx,objcopy=tool('arm-none-eabi-gcc'),tool('arm-none-eabi-g++'),tool('arm-none-eabi-objcopy')
    stem='zzopenlara_viewer' if VIEWER else {'rix':'zzopenlara','rix320':'zzopenlara_rix320','rix800':'zzopenlara_rix800','sw':'zzopenlara_sw','tgl':'zzopenlara_tgl'}[render]
    b=Path('build')/stem;(b/'q2obj').mkdir(parents=True,exist_ok=True);Path('out').mkdir(exist_ok=True)
    arch=['-mcpu=cortex-a9','-marm','-mfpu=vfpv3-d16','-mfloat-abi=hard','-mno-unaligned-access','-fno-short-enums']
    flags=arch+['-O2','-ffreestanding','-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-fno-tree-vectorize','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fomit-frame-pointer','-Iruntime','-Iquake_headers','-ICore1','-Ilibrixgpu','-Izzgl','-Iopenlara/src','-DZZQ_PRODUCTION','-DQUAKEGENERIC_RES_X=640','-DQUAKEGENERIC_RES_Y=480']
    objs=[];srcs=[]
    def compile(src,hi=False,extra=()):
        out=b/('q2obj' if hi else '')/(Path(src).stem+'.o')
        cpp=src.endswith('.cpp')
        f=flags+list(extra)
        if cpp:f+=['-nostdinc++','-std=c++11','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-use-cxa-atexit','-fpermissive']
        run([cxx if cpp else cc,*f,'-c',src,'-o',out]);objs.append(out);srcs.append(src)
    compile('runtime/safe_mem.c',extra=['-O0','-fno-builtin'])
    for f in ['zzquake_entry.S','zzquake_abort.S','zzquake_main.c','zzquake_fs_mem.c','zzquake_mmu.c','zzquake_syscalls.c','zzquake_a1_fstest.c']:
        compile('runtime/'+f)
    compile('runtime/zzquake_platform.c',extra=['-DSys_SendKeyEvents=zzq1_Sys_SendKeyEvents_unused','-DSys_Quit=zzq1_Sys_Quit_unused'])
    # SW/TGL/RIX320: copy at the screen size; A2.15 RIX800: 800 x 600, 1:1
    compile('Core1/present.c',extra=(['-DOL_PRESENT_800'] if render=='rix800' else (['-DOL_SOFT_PRESENT'] if render!='rix' else [])))
    compile('Core1/ol_screen.c')
    for f in ['zzq2_mem.c','cxx_runtime.cpp']:compile('Core1/'+f)
    compile('Core1/platform.c',extra=(['-DOL_VIEWER'] if VIEWER else []))
    gapi={'rix320':['-DOL_RIX320'],'rix800':['-DOL_RIX800']}.get(render,[])
    if render in ('rix','rix320','rix800'):
        for f in ['zzq2rix_gpu.c','zzq2rix_mmu.c']:compile('Core1/'+f,True,['-fno-builtin',*gapi])
        for f in ['rixtex.c','rixframe.c','rixdesc.c','rixgpu.c','rixclip.c']:compile('librixgpu/'+f,True,['-fno-builtin'])   # A2.2 link order
        compile('zzgl/zzgl.c',True,['-fno-builtin'])
    else:
        compile('Core1/present_soft.c',extra=(['-DOL_PRESENT_SW'] if render=='sw' else []))
        # sw.h (upstream) steps 16.16 values (z, w) through int32 and relies on wrap-around: -fwrapv makes it defined
        gapi=['-DOL_GAPI_SW','-fwrapv']
        if render=='tgl':
            tgli=tgl_prepare(b/'tinygl')
            gapi=['-DOL_GAPI_TGL',*tgli]
            # the TinyGL of ZZQuake2TGL 0.6.1, built with its flags (tgl/build_arm.sh: TGLFLAGS, no profiling)
            tglflags=arch+['-O3','-ffreestanding','-ffunction-sections','-fdata-sections','-w','-fno-strict-aliasing','-fwrapv',*tgli]
            for f in sorted((b/'tinygl'/'src').glob('*.c')):
                if f.stem in ('memory','msghandling'):continue
                o=b/'q2obj'/('tgl_'+f.stem+'.o');run([cc,*tglflags,'-c',str(f),'-o',o]);objs.append(o)
            o=b/'q2obj'/'zzq2tgl_tgl.o';run([cc,*tglflags,'-c','Core1/tgl/zzq2tgl_tgl.c','-o',o]);objs.append(o)
    compile('Core1/ol_fs.c',True)
    compile('Core1/ol_snd.c')   # A2.5: release PCM ring
    compile('Core1/ol_fsat.c')  # A2.19: part of a large file through the file service
    compile('Core1/ol_music.c') # A2.19: music tracks of the level in memory
    compile('openlara/src/libs/tinf/tinflate.c',True)
    if VIEWER:
        compile('Core1/ol_viewer.cpp',True,['-include','runtime/zzquake_stdio.h'])
    else:
        compile('Core1/ol_game.cpp',True,[*gapi,'-include','Core1/ol_stdio.h','-fno-delete-null-pointer-checks','-Wno-unused-variable','-Wno-unused-but-set-variable','-Wno-sign-compare','-Wno-unused-function','-Wno-parentheses','-Wno-misleading-indentation','-Wno-class-memaccess','-Wno-narrowing'])
    if render=='sw' and not VIEWER:
        # A2.12: the same counting wrappers and report file for the software renderer
        compile('Core1/ol_memprof.c',extra=['-fno-builtin'])
        compile('Core1/ol_stat.c')
        for o,src in zip(objs,srcs):
            if src.startswith('runtime/') or src=='Core1/ol_memprof.c':continue
            run([objcopy,'--redefine-sym','memcpy=ol_prof_memcpy','--redefine-sym','memset=ol_prof_memset','--redefine-sym','memmove=ol_prof_memmove',o])
    if render in ('rix','rix320','rix800') and not VIEWER:
        # A2.7 measurement: our objects call counting wrappers (Core1/ol_memprof.c) instead of the release's
        # byte-loop mem*; the release runtime/ objects are left as they are
        compile('Core1/ol_memprof.c',extra=['-fno-builtin'])
        compile('Core1/ol_stat.c')
        for o,src in zip(objs,srcs):
            if src.startswith('runtime/') or src=='Core1/ol_memprof.c':continue
            run([objcopy,'--redefine-sym','memcpy=ol_prof_memcpy','--redefine-sym','memset=ol_prof_memset','--redefine-sym','memmove=ol_prof_memmove',o])
    elf=b/(stem+'.elf')
    run([cc,*arch,'-nostartfiles','-T','Core1/openlara.ld','-Wl,--gc-sections','-Wl,--wrap=zzq_publish_stats','-Wl,-Map=build/'+stem+'.map',*objs,'-Wl,--start-group','-lgcc','-lc','-lm','-Wl,--end-group','-o',elf])
    run([objcopy,'-O','binary',elf,'out/'+stem+'.bin'])
    blob=Path('out/'+stem+'.bin');assert 0<blob.stat().st_size<=2*1024*1024
    Path('out/'+stem+'.bin.sha256').write_text(hashlib.sha256(blob.read_bytes()).hexdigest()+'  '+stem+'.bin\n')
    print('ARM COMPILED:',stem+'.bin',blob.stat().st_size,'bytes')
    if VIEWER:print('(reference viewer: copy out/zzopenlara_viewer.bin over PROGDIR:zzopenlara.bin to run it)')
def m68k():
    cc=Path(M68K)
    if not cc.exists() and Path(str(cc)+'.exe').exists():cc=Path(str(cc)+'.exe')
    if not cc.exists():raise SystemExit('Missing 68k compiler: '+str(cc)+'; ARM output, if built, is preserved.')
    common=['-O2','-m68020','-mcrt=nix20','-Wno-pointer-sign','-Ilauncher']
    Path('build').mkdir(exist_ok=True);Path('out').mkdir(exist_ok=True)
    run([cc,*common,'-Dmain=zzq_launcher_main','-DZZQ_OPENLARA_LAUNCHER=1','-c','launcher/ZZQuake2_A0.c','-o','build/launcher.o'])
    run([cc,*common,'launcher/ZZOpenLara.c','build/launcher.o','launcher/zzq_mhi.c','launcher/zzq_mp3.c','launcher/zzq_ahi.c','-lamiga','-o','out/ZZOpenLara'])
def tgl_prepare(dst):
    """TinyGL of ZZQuake2TGL 0.6.1: tinygl/ checked against Core1/tgl/tinygl_vendored.sha256, then patched on a
    copy (dst) by Core1/tgl/zzq2tgl_patch_tinygl.py, as tgl/build_arm.sh does. Returns the include flags."""
    for line in Path('Core1/tgl/tinygl_vendored.sha256').read_text().split('\n'):
        if not line.strip():continue
        h,name=line.split(None,1)
        if hashlib.sha256((Path('tinygl')/name.strip()).read_bytes()).hexdigest()!=h:
            raise SystemExit('tinygl/'+name.strip()+' differs from the vendored TinyGL of ZZQuake2: nothing built')
    run([sys.executable,'Core1/tgl/zzq2tgl_patch_tinygl.py','tinygl',str(dst)])
    run([sys.executable,'Core1/tgl/ol_patch_tinygl_persp.py',str(dst)])   # A2.18: perspective by 1/w
    return ['-I'+str(dst/'include'),'-I'+str(dst/'src')]
VIEWER=False
if __name__=='__main__':
    args=sys.argv[1:]
    renders=['rix','rix320','rix800','sw']   # A2.20: TinyGL dropped from the release (--tgl still builds it)
    if '--viewer' in args:VIEWER=True;args.remove('--viewer');renders=['rix']
    picked=[r for r in ('rix','rix320','rix800','sw','tgl') if '--'+r in args]
    for r in picked:args.remove('--'+r)
    if VIEWER and picked:raise SystemExit('--viewer builds the A1 viewer on RasterIX only: no --rix/--sw/--tgl with it')
    if picked:renders=picked
    if args not in ([],['--arm-only'],['--68k-only']):raise SystemExit('usage: sh build_all.sh [--arm-only|--68k-only] [--rix|--rix320|--rix800|--sw|--tgl] [--viewer]')
    if args!=['--68k-only']:
        for r in renders:arm(r)
    if args!=['--arm-only']:m68k()
