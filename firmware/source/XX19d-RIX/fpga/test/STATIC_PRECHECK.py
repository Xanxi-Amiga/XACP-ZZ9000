"""Offline G4B-to-G4C source manifest and scope verification (not HDL sim)."""
import zipfile,hashlib,json,re
from pathlib import Path
root=Path(__file__).resolve().parent.parent
with zipfile.ZipFile(root/'G4B_RASTERIX_EXACT_SOURCES.zip') as z:
    m=json.loads(z.read('MANIFEST_G4B_SOURCES.json'))
    assert m['found']==93 and len(m['files'])==93
    assert m['xpr_sha256']=='b2590200057259ba23e515e3e883701b20e1e6d8cda000e9ead23c501a89d33f'
    for f in m['files']:
        assert hashlib.sha256(z.read(f['archive_path'])).hexdigest()==f['sha256'],f['archive_path']
    old=z.read('RasterIX_G1/RasterIX/rtl/RasterIX/RasterIX_EF.v').decode()
new=(root/'rtl/RasterIX_EF.v').read_text()
credit=(root/'rtl/XACPCommandReadCredit.v').read_text()
assert 'XACPCommandReadCredit #(' in new
assert old.count('commonAxiAdapter (')==new.count('commonAxiAdapter (')==1
assert old.count('mainXBar (')==new.count('mainXBar (')==1
assert new.count('cmdReadCredit (')==1
for part in ['.m_axi_awid(xbar_axi_awid[0 *', '.m_axi_wvalid(xbar_axi_wvalid[0 *', '.m_axi_bready(xbar_axi_bready[0 *']:
    assert part in new,part
for chan in ['arid','araddr','arlen','arsize','arburst','arlock','arcache','arprot','arvalid','arready','rid','rdata','rresp','rlast','rvalid','rready']:
    assert f'.m_axi_{chan}(cmd_rcredit_{chan})' in new,chan
assert 'assign m_rready = resetn;' in credit
assert 's_arlen' in credit and 'reserved_count' in credit
assert '.DATA_WIDTH(DATA_WIDTH)' in new
print('G4C_STATIC_PRECHECK_PASS: 93 SHA, patch scope, 16 AXI read endpoints, unchanged AW/W/B')
