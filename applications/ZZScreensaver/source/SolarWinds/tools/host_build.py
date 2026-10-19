#!/usr/bin/env python3
"""Build the command-stream simulator, not a GPU performance predictor."""
import pathlib, subprocess, sys
P=pathlib.Path(__file__).resolve().parents[1];B=P/'tests/build';B.mkdir(exist_ok=True)
san=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if '--sanitize' in sys.argv else []
f=san+['-O1','-g','-std=gnu99','-DRIX_HOST_SIM']+['-I'+str(P/x) for x in ['ARM_RasterIX','librixgpu','zzgl','solarwinds']]
sources=[P/'tests/sim_rix.c',P/'ARM_RasterIX/zzgear_app.c']
sources+=list((P/'librixgpu').glob('*.c'))+[P/'zzgl/zzgl.c']+list((P/'solarwinds').glob('*.c'))
objects=[]
for src in sources:
    obj=B/(src.stem+'.o')
    subprocess.run(['gcc',*f,'-c',str(src),'-o',str(obj)],check=True);objects.append(str(obj))
subprocess.run(['gcc',*san,*objects,'-lm','-o',str(B/'sim_rix')],check=True)
