#!/usr/bin/env python3
"""Run transport/lifetime checks; see VALIDATION.md for model limitations."""
import os,pathlib,subprocess
P=pathlib.Path(__file__).resolve().parents[1];out=P/'tests/validation';out.mkdir(exist_ok=True)
exe=P/'tests/build/sim_rix'
if not exe.is_file():raise SystemExit('First run: python3 tools/host_build.py --sanitize')
cases=[('transport_preset_%d'%n,[str(n),'240']) for n in range(1,7)]
cases += [('stop',['1','240','30']),('longrun',['1','6000'])]
for label,args in cases:
    r=subprocess.run([str(exe),*args],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),text=True,capture_output=True)
    (out/(label+'.log')).write_text(r.stdout+r.stderr)
    print(label,'PASS' if not r.returncode else 'FAIL',flush=True)
    if r.returncode:print(r.stdout+r.stderr);raise SystemExit(r.returncode)
