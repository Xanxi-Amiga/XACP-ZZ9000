# Building the GPL Core1 blob

This document covers only the GPL-covered ARM/Core1 part of ZZDarkForcesNEXT.

The 68k launcher is closed source and is **not** part of the public source build.

## Upstream base

The release is based on The Force Engine commit:

```text
ed9e51c315078d6460551593e48fd294e131fc83
```

## Reference toolchain

The release build uses:

```text
C:\ArmGNUToolchain\13.2 Rel1\bin
Python 3
```

with `arm-none-eabi-gcc` 13.2.

## Fetch the pinned TFE source

From the project root:

```bat
FETCH_UPSTREAM.cmd
```

This downloads the pinned upstream revision into:

```text
upstream\src\TheForceEngine
```

The upstream tree is not modified in place.

## Build

Run:

```bat
BUILD_NEXT_WINDOWS.cmd
```

The production result is:

```text
build_next\zzdf.bin
```

The build system copies the selected upstream source into the build tree, applies the ZZ9000/XACP port changes, compiles the selected TFE units and emits the Core1 binary.

## Reference release binary

```text
zzdf.bin
size    1467096 bytes
MD5     5b243a0bb0506e4ecdc479d13d2b1f3f
SHA256  d513ac045bff02f15b42f7c70276267842eba6ae1f21a34895212a214eae72e0
```

## Host tests

Run:

```bat
tests\BUILD_TESTS.cmd
```

or under MSYS2:

```sh
sh tests/build_tests.sh
```

The test suite covers the Core1-side support code and shared protocols used by the blob.

## Public source package

The public source package should contain the complete corresponding source and build scripts needed to reproduce `zzdf.bin`, together with the upstream GPL-2.0 license text.

The closed-source 68k launcher source must not be included in the public source archive.
