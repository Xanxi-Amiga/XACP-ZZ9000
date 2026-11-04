# Building ZZPicoDriveMegaCD v1.2

The finished program is one 68k executable with the ARM Core1 program
embedded in it. Three steps, all run from this folder in an MSYS2 shell
(Windows) or a Linux shell:

```text
./build_release.sh            all three steps
./build_release.sh blob       1. ARM Core1 program  -> bin/zzmcd.bin
./build_release.sh embed      2. -> C array          -> amiga/blob_data.c
./build_release.sh launcher   3. 68k launcher        -> release/ZZPicoDriveMegaCD/
```

Copy `release/ZZPicoDriveMegaCD/` to the Amiga and add the `BIOS` drawer.

## Tools

- Step 1: Arm GNU Toolchain **13.2.Rel1** (`arm-none-eabi-gcc` 13.2.1). On
  Windows, `C:\ArmGNUToolchain\13.2 Rel1\bin` is used automatically; otherwise
  set `TOOLCHAIN_BIN` to the toolchain `bin` folder or put it in `PATH`.
  `Cyclone.s` is shipped pre-generated: no host C/C++ compiler is needed.
- Step 2: `bash` with `od` and `awk` (`tools/bin2c.sh`), or compile
  `tools/bin2c.c` with any host C compiler. Both produce the same file.
- Step 3: bebbo `m68k-amigaos-gcc` with the NDK, Picasso96 and AHI headers,
  in `PATH` or given with `M68K_GCC=...`.

Everything is local: the build downloads nothing. libchdr and its decoders
(zlib, LZMA, zstd, dr_flac) are part of the tree.

## Step by step (equivalent commands)

```text
ZZVR_DRC=1 ./build_arm.sh
bash tools/bin2c.sh bin/zzmcd.bin amiga/blob_data.c
cd amiga
m68k-amigaos-gcc -O2 -noixemul -m68020 -Wno-pointer-sign \
    -o ZZPicoDriveMegaCD ZZPicoDriveMegaCD.c blob_data.c -lamiga
```

`ZZVR_DRC=1` selects the SH-2 and SVP recompilers (the program of the
release). `./build_arm.sh` alone builds `bin/zzmcd_interp.bin` with the SH-2
interpreter: far too slow for 32X games, kept for tests only.

## Reference Core1 program

`reference/zzmcd_reference.bin` is the Core1 program of this package
(994540 bytes, MD5 `2e509f1b0bf5b2d197f3bfa781d10007`, built with GCC 13.2.1;
the launcher log shows `sub=22`). `build_release.sh blob` compares your build
with it. A different MD5 only means a different toolchain or C library build:
check one game before publishing, or embed the reference itself:

```text
BLOB=reference/zzmcd_reference.bin ./build_release.sh embed
./build_release.sh launcher
```

## Development build

`amiga/blob_data_stub.c` replaces `blob_data.c` when no program is embedded;
the Core1 program file is then given on the command line
(`ZZPicoDriveMegaCD Dune.chd bin/zzmcd.bin usa ahi`).

## Layout

- `amiga/ZZPicoDriveMegaCD.c`: the 68k launcher (GUI, Core1 start, display
  loop, audio backends, input, saves, the CD file server process).
- `Core1/`: the ARM Core1 program (PicoDrive glue, memory map, MMU, the
  file-access layer `zzmcd_fs.c` that turns PicoDrive's file calls into
  requests to the 68k).
- `third_party/picodrive/`: PicoDrive with Cyclone, the SH-2 recompiler, the
  Mega-CD core (`pico/cd/`) and libchdr (`pico/cd/libchdr/`).
