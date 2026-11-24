#!/usr/bin/env python3
"""Run transport/lifetime checks on the simulator: the three presets, an early stop and a long run, as
the screensaver ships (cells written by Core1) and on its fallback path (cells drawn by RasterIX)."""
import os,pathlib,subprocess
P=pathlib.Path(__file__).resolve().parents[1];out=P/'tests/validation';out.mkdir(exist_ok=True)
exe=P/'tests/build/sim_rix';fallback=P/'tests/build/sim_rix_fallback'
if not exe.is_file() or not fallback.is_file():raise SystemExit('First run: python3 tools/host_build.py --sanitize')
cases=[('transport_preset_%d'%n,exe,[str(n),'240']) for n in range(1,4)]
cases+=[('stop',exe,['2','240','30']),('longrun',exe,['2','6000'])]
cases+=[('fallback_preset_%d'%n,fallback,[str(n),'240']) for n in range(1,4)]
cases+=[('fallback_stop',fallback,['2','240','30']),('fallback_longrun',fallback,['2','6000'])]
for label,prog,args in cases:
    r=subprocess.run([str(prog),*args],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),text=True,capture_output=True)
    (out/(label+'.log')).write_text(r.stdout+r.stderr)
    text=r.stdout+r.stderr
    ok=not r.returncode and ('pixels: path on' in text)==(prog is exe)
    print(label,'PASS' if ok else 'FAIL',flush=True)
    if not ok:print(text);raise SystemExit(r.returncode or 1)
