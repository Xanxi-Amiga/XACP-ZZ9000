# Building ZZPicoDriveSMS v1.2

One command, from this folder, in an MSYS2 shell (or Linux), fully offline:

```text
sh build_all.sh
```

Result: `release/ZZPicoDriveSMS/ZZPicoDriveSMS` (the 68k executable with the
ARM Core1 program embedded) + the docs, `release/zzsms_v1.2.bin` and
`release/SHA256SUMS.txt`. Copy `release/ZZPicoDriveSMS/` to the Amiga.

## Tools (explicit Windows paths, PATH is not modified)

| Tool | Default path | Override |
| --- | --- | --- |
| ARM GCC 13.2.Rel1 | `C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-gcc` (+ objcopy, size) | `ARM_BIN=<bin folder>` |
| bebbo m68k-amigaos-gcc | `C:/amiga-gcc/bin/m68k-amigaos-gcc` | `M68K_GCC=<file>` |
| 68k C runtime | `-mcrt=nix20` | `M68K_CRT=...` |
| Python 3 (bin2c, SHA-256) | `python3`, else `python` | `PYTHON=<file>` |

Without Python, step 2 falls back to `tools/bin2c.sh` (od + awk) and the
checksums are skipped.

## Steps (what build_all.sh does)

1. `sh build_arm.sh` - ARM Core1 program `bin/zzsms_v1.2.bin`.
   PicoDrive (with `pico/sound/ym2413.c` and emu2413 1.6.3), Cyclone
   (`Cyclone.s` shipped pre-generated), the Core1 sources; the struct offset
   header `pico/pico_int_offs.h` is shipped generated for these flags.
2. `python tools/bin2c.py bin/zzsms_v1.2.bin amiga/blob_data.c`.
3. `m68k-amigaos-gcc -O2 -m68020 -mcrt=nix20 -Wno-pointer-sign
   -o ZZPicoDriveSMS ZZPicoDriveSMS.c blob_data.c -lamiga` (in `amiga/`).
4. Release folder and SHA-256.

## Reference Core1 program

`reference/zzsms_v1.2_reference.bin`: 657016 bytes, MD5
`4a7a7b5158c75b022ada6ab2f9ea2bba` (Arm GCC 13.2.1). build_all.sh compares
your build with it; a different MD5 only means another toolchain / C library
build. To embed the reference instead of your build:

```text
python tools/bin2c.py reference/zzsms_v1.2_reference.bin amiga/blob_data.c
```

then run step 3 by hand.

## Development build

`amiga/blob_data_stub.c` replaces `blob_data.c` when no program is embedded;
give the Core1 file on the command line
(`ZZPicoDriveSMS game.sms bin/zzsms_v1.2.bin jap ahi fm`).

## Layout

```text
amiga/          68k launcher (ZZPicoDriveSMS.c), blob_data_stub.c
Core1/          ARM Core1 sources (zzpico_*.c/.h/.S/.ld, stubs/)
third_party/    PicoDrive + Cyclone (complete trees), emu2413 1.6.3
tools/          bin2c (Python, C and shell)
reference/      reference Core1 program
docs/           architecture notes and focused PicoDrive patches
build_arm.sh, build_all.sh
```
