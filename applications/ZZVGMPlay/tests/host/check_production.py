#!/usr/bin/env python3
"""Offline Linux/native-GCC check of actual Core1 VGM parser and mixer.

Moonwalker fixtures are NOT distributed in the public/source build package.
Use a privately held original VGM/VGZ archive as --music-zip.
"""
import argparse
import gzip
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [
    'tests/host/production_render.c', 'tests/host/unused_chip_stubs.c',
    'core1/arcade_pcm.c', 'core1/system18_backend.c',
    'core1/system18_fm.c', 'core1/system18_rf.c',
    'core1/system18_resampler.c',
    'third_party/libvgm-system18/emu/logging.c',
]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--music-zip',required=True,type=Path,help='private Moonwalker original ZIP (not included)')
    args=p.parse_args()
    refs=json.loads((ROOT/'tests/host/production_results.json').read_text(encoding='utf-8'))
    baseline=json.loads((ROOT/'tests/host/production_results_pre_gain.json').read_text(encoding='utf-8'))
    assert [r['file'] for r in refs] == [r['file'] for r in baseline], 'reference file list mismatch' 
    with tempfile.TemporaryDirectory(prefix='zzvgm_prod_test_') as tmp:
        dest=Path(tmp);exe=dest/'production_render'
        subprocess.run(['python3',str(ROOT/'core1/prepare_system18.py')],check=True)
        cmd=['gcc','-O2','-ffunction-sections','-fdata-sections','-I'+str(ROOT/'include'),
             '-I'+str(ROOT/'core1'),'-I'+str(ROOT/'third_party/Nuked-OPL3-fast'),
             '-o',str(exe)] + [str(ROOT/n) for n in SOURCES] + ['-Wl,--gc-sections','-lm']
        subprocess.run(cmd,check=True)
        with zipfile.ZipFile(args.music_zip) as z:
            all_files={Path(n).name:z.read(n) for n in z.namelist() if n.lower().endswith(('.vgm','.vgz'))}
        count=0
        for row, old in zip(refs, baseline):
            fname=row['file'];vname=fname[:-1]+'z'
            if fname in all_files: data=all_files[fname]
            elif vname in all_files:data=gzip.decompress(all_files[vname])
            else:raise RuntimeError('Missing original track: '+fname)
            track=dest/fname;track.write_bytes(data)
            result=subprocess.run([str(exe),str(track)],check=True,text=True,capture_output=True)
            parsed=json.loads(result.stdout)
            expected=row['result']
            for key in ('frames','fnv64','peak','pool_used','chip_mask',
                        'original_fnv64','clipped_channels','pre_gain_clipped_channels','preclip_peak'):
                if parsed[key]!=expected[key]:
                    raise RuntimeError(f'{fname}: {key} got {parsed[key]}, expected {expected[key]}')
            for key in ('frames', 'pool_used', 'chip_mask'):
                if parsed[key] != old['result'][key]:
                    raise RuntimeError(f'{fname}: baseline {key} mismatch')
            if parsed['original_fnv64'] != old['result']['fnv64']:
                raise RuntimeError(f'{fname}: pre-gain production mixer differs from A07g System 18 reference')
            if parsed['clipped_channels'] or parsed['pre_gain_clipped_channels']:
                raise RuntimeError(f'{fname}: clipping detected before or after the gain')
            if parsed['peak'] != old['result']['peak'] * 2 or parsed['preclip_peak'] != parsed['peak']:
                raise RuntimeError(f'{fname}: output peak not precisely doubled')
            print(f'{fname}: PASS ({parsed["frames"]} frames, peak {parsed["peak"]}, clip=0)', flush=True)
            count+=1
            track.unlink()
        print(f'PASS {count}/{len(refs)} tracks: +6.02 dB, unchanged pre-gain mix, 0 clipped samples.')
if __name__=='__main__':main()
