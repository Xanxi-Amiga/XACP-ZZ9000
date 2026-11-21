#!/usr/bin/env python3
"""Package the actual compiled A2 Amiga executable, offline. No fake binaries."""
from pathlib import Path
import hashlib
import zipfile

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'output'
BINARY = OUT / 'ZZSIDPlayGUI'
ARM = ROOT / 'sources' / 'ARM' / '.build_arm' / 'zzresid_core1_A2.bin'
ZIP = OUT / 'ZZSIDPlayGUI_1.0_AmigaOS3.zip'


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if not BINARY.is_file() or not ARM.is_file():
        raise SystemExit('ERROR: missing compiled A2 executable/ARM blob. Run: sh build_all.sh')
    exe = BINARY.read_bytes()
    blob = ARM.read_bytes()
    if len(exe) < len(blob) + 10000 or exe.count(blob) != 1:
        raise SystemExit('ERROR: release binary does not embed the newly compiled A2 Core1 blob exactly once.')
    if blob == (ROOT/'sources'/'zzresid_core1_WORKING.bin').read_bytes():
        raise SystemExit('ERROR: output uses reference A1 ARM blob. Refusing to package 1.0.')
    OUT.mkdir(exist_ok=True)
    checksum = f'{sha(BINARY)}  ZZSIDPlayGUI\n'
    checksum += f'{sha(ARM)}  zzresid_core1_A2.bin (embedded; not a separate install file)\n'
    (OUT/'SHA256SUMS.txt').write_text(checksum)
    # Release ZIP has the exact built binary plus documentation/license notices.
    with zipfile.ZipFile(ZIP, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.write(BINARY, 'ZZSIDPlayGUI_1.0/ZZSIDPlayGUI')
        icon = ROOT / 'assets' / 'ZZSIDPlayGUI.info'
        if not icon.is_file():
            raise SystemExit('ERROR: missing NewIcons icon: assets/ZZSIDPlayGUI.info')
        z.write(icon, 'ZZSIDPlayGUI_1.0/ZZSIDPlayGUI.info')
        for src, dst in [
            ('RELEASE_NOTES.md', 'README.txt'),
            ('THIRD_PARTY.md', 'THIRD_PARTY.txt'),
            ('COPYING.GPL-2.0.txt', 'COPYING.GPL-2.0.txt'),
        ]:
            z.write(ROOT/src, 'ZZSIDPlayGUI_1.0/'+dst)
        z.writestr('ZZSIDPlayGUI_1.0/SHA256SUMS.txt', checksum)
    print('Release asset:', ZIP)
    print('AmigaOS 68k SHA256:', sha(BINARY))
    print('Embedded ARM A2 SHA256:', sha(ARM))

if __name__ == '__main__':
    main()
