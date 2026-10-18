"""Prepare the bounded-scratch Core1 copy; never changes upstream sources."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=root/'third_party/libvgm-system18/emu/Resampler.c'
s=p.read_text(encoding='utf-8')
old='Resmpl_EnsureBuffers(CAA, CAA->smpRateSrc / 1); // reserve initial buffer for 1 second of samples'
assert s.count(old)==1,'Unexpected upstream resampler; use the packaged version'
s=s.replace(old,'Resmpl_EnsureBuffers(CAA, 1024); // Core1: bounded initial scratch, DSP unchanged')
p.with_name('Resampler_baremetal.c').write_text(s,encoding='utf-8')
print('System 18: bounded resampler scratch prepared (offline).')
