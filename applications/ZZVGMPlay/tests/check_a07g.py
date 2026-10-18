from pathlib import Path
import difflib
root = Path(__file__).resolve().parents[1]
c = (root/'core1/zzvgm_core1.c').read_text()
b = (root/'core1/build_core1.sh').read_text()
scc = (root/'core1/k051649_zz.c').read_text()
ay = (root/'core1/ay2149_ref.c').read_text()
assert '#include "ay2149_ref.h"' in c
assert 'if(g_use_ay_ref)zz_ay2149_write(&g_ay_ref,s->reg,d[0]);' in c
assert 'case 0xA0:' in c and 'zz_ay2149_write(&g_ay_ref,g_vgm[g_pos],g_vgm[g_pos+1])' in c
assert 'g_use_ay_ref=(ay && scc)?1u:0u;' in c
assert 'zz_ay8910_render(&g_ay,ay,n);' in c, 'Non-SCC AY preserved'
assert 'zz_ay2149_render(&g_ay_ref,ay,n);' in c
assert 'g_vol_k051649=' in c and '#define VOL_K051649  0x100u' in c
assert 'zz_scc_render' in c
assert 'ay2149_ref.o' in b
assert 'ay2149_ref.c' in b
assert 'antialias' not in scc.lower() and 'update_step' in scc, 'SCC A07e renderer restored'
assert 'Mitsutaka Okazaki' in ay
assert 'LICENSE_EMU2149_MIT' in ay
assert (root/'LICENSE_EMU2149_MIT.txt').exists()
assert 'ZZVGM_FLAG_REPEAT' in c
print('PASS: AY reference for SCC mixed tracks, original AY fallback, SCC A07e restored, build and credits')
