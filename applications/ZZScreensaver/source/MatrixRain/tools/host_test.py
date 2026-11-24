#!/usr/bin/env python3
"""PC bench of the scene adapter: the same frames with and without matrix/mv_gl.c.

Builds tests/test_matrix.c three times (reference, adapter, adapter with its pixel path), runs every
preset grid, compares the frames of the adapter with the reference byte for byte and those of the
pixel path within the rounding of RGB565, then writes previews to previews/ (needs Pillow; without it
the comparisons still run). Not a model of RasterIX: see tests/test_matrix.c.
"""
import pathlib, subprocess, sys
P = pathlib.Path(__file__).resolve().parents[1]
B = P / 'tests/build'
B.mkdir(parents=True, exist_ok=True)
GRIDS = [(80, 60), (64, 48), (40, 30)]
FRAMES, KEPT = 420, 4
san = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if '--sanitize' in sys.argv else []
inc = ['-I' + str(P / x) for x in ['ARM_RasterIX', 'librixgpu', 'zzgl', 'matrix']]
common = [str(P / 'tests/test_matrix.c'), str(P / 'matrix/matrixview.c'), str(P / 'matrix/mv_sys.c')]
flags = san + ['-O1', '-g', '-std=gnu99', '-Wall', '-Wextra', '-Wno-unused-parameter', '-DRIX_HOST_SIM'] + inc
subprocess.run(['gcc', *flags, '-DMX_REFERENCE', *common, '-lm', '-o', str(B / 'test_matrix_ref')], check=True)
adapter = [str(P / 'matrix/mv_gl.c'), str(P / 'librixgpu/rixclip.c'), str(P / 'librixgpu/rixdesc.c')]
subprocess.run(['gcc', *flags, *common, *adapter, '-lm', '-o', str(B / 'test_matrix_zzgl')], check=True)
subprocess.run(['gcc', *flags, '-DMX_PIXELS', *common, *adapter, '-lm', '-o', str(B / 'test_matrix_px')], check=True)
PIXEL_GRIDS = [(80, 60), (64, 48), (40, 30)]      # the presets whose cells Core1 writes itself
env = {'ASAN_OPTIONS': 'detect_leaks=0', 'PATH': '/usr/bin:/bin'}
bad = 0
for cols, rows in GRIDS:
    outs = []
    for exe in ('test_matrix_ref', 'test_matrix_zzgl'):
        out = B / ('%s_%dx%d.rgb' % (exe, cols, rows))
        r = subprocess.run([str(B / exe), str(cols), str(rows), str(FRAMES), str(FRAMES - KEPT), str(out)],
                           env=env, text=True, capture_output=True)
        print(exe, r.stdout.strip(), r.stderr.strip(), flush=True)
        if r.returncode:
            bad += 1
        if exe == 'test_matrix_zzgl':
            words = r.stdout.split()
            issued = int(words[words.index('quads_in_per_frame') + 1])
            sent = int(words[words.index('quads_sent_per_frame') + 1])
            # rain alone: part of the grid is unlit and is not sent; a fully lit grid means the
            # scene is drawing its picture mode without a picture
            if not (issued > 0 and issued // 10 < sent < issued - issued // 20):
                print('  grid %dx%d: %d of %d quads sent: not the rain alone' % (cols, rows, sent, issued))
                bad += 1
        outs.append(out)
    a, b = outs[0].read_bytes(), outs[1].read_bytes()
    same = len(a) == KEPT * 640 * 480 * 3 and a == b
    print('  grid %dx%d: %d frames, adapter output %s the reference' % (cols, rows, KEPT, 'IDENTICAL to' if same else 'DIFFERS from'), flush=True)
    if not same:
        bad += 1
    if not any(a):
        print('  grid %dx%d: reference frames are all black' % (cols, rows))
        bad += 1
    if (cols, rows) in PIXEL_GRIDS:
        # The pixel path rounds the brightness of a cell and the coverage of a glyph to 6 bits each
        # before the RGB565 pixel: against the 8-bit reference, at most about three steps of green
        # (4 per step) and two of red or blue (8 per step) in rare pixels, a fraction of a step on average.
        out = B / ('test_matrix_px_%dx%d.rgb' % (cols, rows))
        r = subprocess.run([str(B / 'test_matrix_px'), str(cols), str(rows), str(FRAMES), str(FRAMES - KEPT), str(out)],
                           env=env, text=True, capture_output=True)
        print('test_matrix_px', r.stdout.strip(), r.stderr.strip(), flush=True)
        c = out.read_bytes()
        if r.returncode or len(c) != len(a):
            bad += 1
        else:
            n = len(a) // 3
            worst = [0, 0, 0]
            total = [0, 0, 0]
            far = 0
            for i in range(0, len(a), 3):
                for ch in range(3):
                    d = a[i + ch] - c[i + ch]
                    if d < 0:
                        d = -d
                    total[ch] += d
                    if d > worst[ch]:
                        worst[ch] = d
                if a[i + 1] - c[i + 1] > 8 or c[i + 1] - a[i + 1] > 8:
                    far += 1
            mean = [x / n for x in total]
            ok = worst[1] <= 16 and worst[0] <= 16 and worst[2] <= 16 and mean[1] <= 2.5 and far * 100 <= n
            print('  grid %dx%d: pixel path against the reference: largest difference R %d G %d B %d of 255, mean G %.2f, '
                  'green more than 8 away in %.3f %% of the pixels: %s' % (cols, rows, worst[0], worst[1], worst[2], mean[1],
                                                                          100.0 * far / n, 'CLOSE' if ok else 'TOO FAR'), flush=True)
            if not ok:
                bad += 1
try:
    from PIL import Image
    (P / 'previews').mkdir(exist_ok=True)
    for n, (cols, rows) in enumerate(GRIDS, 1):
        out = B / ('preview_%dx%d.rgb' % (cols, rows))
        if (cols, rows) in PIXEL_GRIDS:       # what these presets show: cells written by Core1
            subprocess.run([str(B / 'test_matrix_px'), str(cols), str(rows), str(FRAMES), str(FRAMES - 1), str(out)],
                           env=env, check=True, capture_output=True)
        else:
            subprocess.run([str(B / 'test_matrix_zzgl'), str(cols), str(rows), str(FRAMES), str(FRAMES - 1), str(out), '565'],
                           env=env, check=True, capture_output=True)
        Image.frombytes('RGB', (640, 480), out.read_bytes()).save(P / 'previews' / ('preset_%d_%dx%d.png' % (n, cols, rows)))
    print('previews written (PC rendering, RGB565; not a capture of the hardware)')
except ImportError:
    print('Pillow not installed: no previews written')
print('PASS' if not bad else 'FAIL')
sys.exit(1 if bad else 0)
