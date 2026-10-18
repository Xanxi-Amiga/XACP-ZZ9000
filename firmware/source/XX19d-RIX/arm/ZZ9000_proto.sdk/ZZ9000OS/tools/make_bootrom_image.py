#!/usr/bin/env python3
from pathlib import Path
import re, sys

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "src"
ROM_SIZE = 0x2000
DRIVER_OFFSET = 0x0300
DIAG_POINT = 0x0056
BOOT_POINT = 0x003C
NAME_OFFSET = 0x000E

def extract_array(path: Path, symbol: str) -> bytes:
    text = path.read_text(encoding="utf-8", errors="strict")
    m = re.search(r"\b" + re.escape(symbol) + r"\s*\[\s*\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit(f"Cannot find array {symbol} in {path}")
    vals = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{1,2})\b", m.group(1))]
    return bytes(vals)

def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} OUTPUT.bin", file=sys.stderr)
        return 2
    diag = extract_array(SRC / "diag-code.h", "diag_code")
    driver = extract_array(SRC / "zzsd-device.h", "zzsd_device")
    header = bytes([
        0x90, 0x00,
        (ROM_SIZE >> 8) & 0xFF, ROM_SIZE & 0xFF,
        (DIAG_POINT >> 8) & 0xFF, DIAG_POINT & 0xFF,
        (BOOT_POINT >> 8) & 0xFF, BOOT_POINT & 0xFF,
        (NAME_OFFSET >> 8) & 0xFF, NAME_OFFSET & 0xFF,
        0x00, 0x00, 0x00, 0x00,
    ]) + b"zzsd.device\0"
    rom = bytearray(ROM_SIZE)
    def put(off: int, data: bytes, name: str):
        if off < 0 or off + len(data) > len(rom):
            raise SystemExit(f"{name} does not fit ROM: off=0x{off:X} len=0x{len(data):X}")
        rom[off:off+len(data)] = data
    put(0, header, "header")
    put(0x20, diag, "diag_code")
    put(DRIVER_OFFSET, driver, "zzsd_device")
    out = Path(sys.argv[1])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(rom)
    print(f"[bootrom] wrote {out} ({len(rom)} bytes)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
