# Building ZZPicoDriveMD v1.2

The finished program is one 68k executable with the ARM Core1 program
embedded in it. Three steps, all run from this folder in an MSYS2 shell
(Windows) or a Linux shell:

```text
./build_release.sh            all three steps
./build_release.sh blob       1. ARM Core1 program  -> bin/zzmd_v1.2.bin
./build_release.sh embed      2. -> C array          -> amiga/blob_data.c
./build_release.sh launcher   3. 68k launcher        -> release/ZZPicoDriveMD/
```

Copy `release/ZZPicoDriveMD/` to the Amiga.

## Tools

- Step 1: Arm GNU Toolchain **13.2.Rel1** (`arm-none-eabi-gcc` 13.2.1). On
  Windows, `C:\ArmGNUToolchain\13.2 Rel1\bin` is used automatically; otherwise
  set `TOOLCHAIN_BIN` to the toolchain `bin` folder or put it in `PATH`.
  `Cyclone.s` is shipped pre-generated: no host C/C++ compiler is needed.
- Step 2: `bash` with `od` and `awk` (`tools/bin2c.sh`), or compile
  `tools/bin2c.c` with any host C compiler. Both produce the same file.
- Step 3: bebbo `m68k-amigaos-gcc` with the NDK, Picasso96 and AHI headers,
  in `PATH` or given with `M68K_GCC=...`.

## Step by step (equivalent commands)

```text
./build_arm.sh
bash tools/bin2c.sh bin/zzmd_v1.2.bin amiga/blob_data.c
cd amiga
m68k-amigaos-gcc -O2 -noixemul -m68020 -Wno-pointer-sign \
    -o ZZPicoDriveMD ZZPicoDriveMD.c blob_data.c -lamiga
```

## Reference Core1 program

`reference/zzmd_v1.2_reference.bin` is the Core1 program of this package
(604984 bytes, MD5 `9368df3c1cb6487656376dd7e3beb4a2`, built with GCC 13.2.1;
the launcher log shows `sub=2`). `build_release.sh blob` compares your build with it. A different
MD5 only means a different toolchain or C library build: check one PAL and one
NTSC game before publishing, or embed the reference itself:

```text
BLOB=reference/zzmd_v1.2_reference.bin ./build_release.sh embed
./build_release.sh launcher
```

## Development build

`amiga/blob_data_stub.c` replaces `blob_data.c` when no program is embedded;
the Core1 program file is then given on the command line
(`ZZPicoDriveMD rom.bin bin/zzmd_v1.2.bin usa ahi`).

## Layout

```text
amiga/          68k launcher (ZZPicoDriveMD.c), blob_data_stub.c
Core1/          ARM Core1 program sources (zzpico_*.c/.h/.S/.ld, stubs/)
third_party/    PicoDrive and Cyclone 68000, complete source trees
tools/          bin2c (C and shell)
reference/      validated Core1 program
build_arm.sh, build_release.sh
```
