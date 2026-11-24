#!/usr/bin/env python3
"""PC bench of what is sent to RasterIX.

Builds the transport simulator three times from the same sources, all with -DMX_FIXED_DT so that the
rain does not depend on how long a frame took: as the screensaver ships (cells written by Core1),
with -DMV_NO_PIXELS (the fallback: cells drawn by RasterIX through the direct path) and with
-DMV_NO_DIRECT (every quad through ZZGL, the 0.1 behaviour). Every word of every list the simulated
GPU receives goes into a running hash (tests/sim_rix.c).
 - The fallback and the 0.1 behaviour must print the same hashes on the three presets, i.e. send the
   same commands in the same order, and the direct path must be in use.
 - As shipped, the pixel path is in use, carries nearly all the quads, drops none, and RasterIX
   receives a small fraction of the triangles.
"""
import pathlib, re, subprocess, sys
P = pathlib.Path(__file__).resolve().parents[1]
B = P / 'tests/build'
B.mkdir(parents=True, exist_ok=True)
FRAMES = sys.argv[sys.argv.index('--frames') + 1] if '--frames' in sys.argv else '360'
san = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if '--sanitize' in sys.argv else []
inc = ['-I' + str(P / x) for x in ['ARM_RasterIX', 'librixgpu', 'zzgl', 'matrix']]
src = [P / 'tests/sim_rix.c', P / 'ARM_RasterIX/zzgear_app.c'] + sorted((P / 'librixgpu').glob('*.c'))
src += [P / 'zzgl/zzgl.c'] + sorted((P / 'matrix').glob('*.c'))
flags = san + ['-O2', '-g', '-std=gnu99', '-DRIX_HOST_SIM', '-DMX_FIXED_DT'] + inc
builds = (('sim_stream_pixels', []), ('sim_stream_direct', ['-DMV_NO_PIXELS']), ('sim_stream_general', ['-DMV_NO_DIRECT']))
for name, extra in builds:
    subprocess.run(['gcc', *flags, *extra, *map(str, src), '-lm', '-o', str(B / name)], check=True)
env = {'ASAN_OPTIONS': 'detect_leaks=0', 'PATH': '/usr/bin:/bin'}
RE_PIXELS = re.compile(r'pixels: path (\w+); frames written by Core1=(\d+); last frame: (\d+) cells in pixels; quads lost=(\d+)')
RE_CLEAN = re.compile(r'violations=0\b')
RE_STREAM = re.compile(r'stream: triangles=(\d+) tri_hash=(\w+) list_hash=(\w+)')
RE_ADAPTER = re.compile(r'adapter: direct cells=(\d+); last frame: (\d+) quads drawn, (\d+) of them direct')
bad = 0
for preset in (3, 2, 1):
    seen = {}
    for name, _ in builds:
        r = subprocess.run([str(B / name), str(preset), FRAMES], env=env, text=True, capture_output=True)
        out = r.stdout + r.stderr
        m, a = RE_STREAM.search(out), RE_ADAPTER.search(out)
        if r.returncode or not m or not a:
            print(out)
            print('preset %d %s: run FAILED' % (preset, name))
            bad += 1
            continue
        seen[name] = (m.groups(), tuple(int(x) for x in a.groups()), RE_PIXELS.search(out), bool(RE_CLEAN.search(out)))
    if len(seen) != 3:
        continue
    g, d, p = seen['sim_stream_general'], seen['sim_stream_direct'], seen['sim_stream_pixels']
    same = g[0] == d[0]
    cells, drawn, direct = d[1]
    used = cells > 0 and direct > 0
    print('preset %d, fallback: %s triangles; lists %s; direct path %s (%d cells, %d of %d quads of the last frame)' % (
        preset, d[0][0], 'IDENTICAL to the 0.1 behaviour' if same else 'DIFFERENT', 'in use' if used else 'NOT USED', cells, direct, drawn), flush=True)
    if not same or g[1][0] != 0 or g[1][2] != 0 or not used or direct * 10 < drawn * 8:
        bad += 1
    if (d[2] and d[2].group(1) == 'on') or (g[2] and g[2].group(1) == 'on'):
        print('  preset %d: the pixel path is on in a build that must not have it' % preset)
        bad += 1
    px = p[2]
    on = bool(px) and px.group(1) == 'on'
    written, pcells, lost = (int(px.group(2)), int(px.group(3)), int(px.group(4))) if px else (0, 0, 0)
    tg, tp = int(g[0][0]), int(p[0][0])
    print('preset %d, as shipped: pixel path %s: %d frames written by Core1, %d of %d quads of the last frame in pixels, %d lost; '
          'triangles sent %d against %d' % (preset, 'in use' if on else 'NOT USED', written, pcells, p[1][1], lost, tp, tg), flush=True)
    if not on or not p[3] or lost or written < int(FRAMES) // 2 or pcells * 10 < p[1][1] * 8 or tp * 4 > tg:
        bad += 1
print('PASS' if not bad else 'FAIL')
sys.exit(1 if bad else 0)
