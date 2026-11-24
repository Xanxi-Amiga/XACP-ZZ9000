#!/usr/bin/env python3
"""Build the command-stream simulator, not a GPU performance predictor.

Two programs: sim_rix as the screensaver ships, and sim_rix_fallback with -DMV_NO_PIXELS, the path the
adapter falls back to when its pixel path refuses a grid (every cell drawn by RasterIX)."""
import pathlib, subprocess, sys
P=pathlib.Path(__file__).resolve().parents[1];B=P/'tests/build';B.mkdir(exist_ok=True)
san=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if '--sanitize' in sys.argv else []
f=san+['-O1','-g','-std=gnu99','-DRIX_HOST_SIM']+['-I'+str(P/x) for x in ['ARM_RasterIX','librixgpu','zzgl','matrix']]
sources=[P/'tests/sim_rix.c',P/'ARM_RasterIX/zzgear_app.c']
sources+=list((P/'librixgpu').glob('*.c'))+[P/'zzgl/zzgl.c']+list((P/'matrix').glob('*.c'))
for name,extra in (('sim_rix',[]),('sim_rix_fallback',['-DMV_NO_PIXELS'])):
    objects=[]
    for src in sources:
        obj=B/(name+'_'+src.stem+'.o')
        subprocess.run(['gcc',*f,*extra,'-c',str(src),'-o',str(obj)],check=True);objects.append(str(obj))
    subprocess.run(['gcc',*san,*objects,'-lm','-o',str(B/name)],check=True)
