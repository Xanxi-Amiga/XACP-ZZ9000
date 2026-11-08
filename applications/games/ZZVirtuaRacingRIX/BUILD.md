# Building ZZVirtuaRacingRIX 1.0

The build is offline: every source is in this package.

## Tools

- Arm GNU Toolchain 13.2 (`arm-none-eabi-gcc` 13.2.1). On Windows
  `C:/ArmGNUToolchain/13.2 Rel1/bin` is picked up automatically, else set
  `TOOLCHAIN_BIN`.
- bebbo `m68k-amigaos-gcc` (libnix, `-noixemul`).
- bash (MSYS2 on Windows, or Linux).

## One command

```text
M68K_GCC="C:/amiga-gcc/bin/m68k-amigaos-gcc" ./build_release.sh
```

The result is `release/ZZVirtuaRacingRIX/`: copy that drawer to the Amiga.

Steps (each can be run alone):

1. `./build_release.sh blob`: the Core1 program `bin/zzvr_a3a.bin`
   (PicoDrive, Cyclone, the SH-2 recompiler, the HLE, librixgpu). The script
   compares its MD5 with `reference/zzvr_a3a_reference.bin`; a different MD5
   means a different toolchain build.
2. `./build_release.sh embed`: the Core1 program into `amiga/blob_data.c`,
   and the game's icon `amiga/icons/ZZVirtuaRacingRIX.info` into
   `amiga/icon_data.c` (the setup writes that icon next to the game).
   `BLOB=reference/zzvr_a3a_reference.bin ./build_release.sh embed` embeds the
   reference instead.
3. `./build_release.sh launcher`: the 68k program
   `ZZVirtuaRacingRIX_Setup`, its icon and the documents. Started with the
   ROM inside (written by the setup), the same program is the game.

## Layout

| Path | |
|---|---|
| `amiga/` | 68k program (`ZZVirtuaRacingRIX.c`), `icons/` (game icon with its tool types, setup icon) |
| `Core1/` | Core1 program: platform layer, HLE glue, RasterIX path |
| `Core1/vrde/` | VirtuaRacingDeluxeEnhanced 1.2.0 HLE files |
| `Core1/librixgpu/` | RasterIX driver (0BSD) |
| `third_party/picodrive/` | PicoDrive (irixxxx `8b5e91bc`, VRDE hooks) with Cyclone |
| `reference/` | the Core1 program of this package, MD5 / SHA-256 |
| `tools/` | `bin2c` |
| `docs/history/` | development notes (A1 to A8j) |

Nothing of the ZZ9000 firmware, bitstream or XACP protocol is changed or
built here.

## The game icon

`amiga/icons/ZZVirtuaRacingRIX.info` is embedded in the program and written
by the setup next to the game it builds. To change it, replace that file with
another tool icon that keeps the tool types `FARVIEW=6`, `(FARVIEW=7)`,
`(FARVIEW=8)`, `AUDIO=PAULA` and `(AUDIO=AHI)`,
then run the whole build (or `embed` then `launcher`).
`amiga/icons/ZZVirtuaRacingRIX_Setup.info` is the setup program's own icon (a plain
4-colour disk icon).
