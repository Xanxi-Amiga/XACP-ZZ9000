# Building ZZQuake HighRes

ARM toolchain: `arm-none-eabi-gcc` 13.2.1.

The package already contains the required GPL Quake sound sources (`snd_dma.c`, `snd_mem.c`, `snd_mix.c`) in `quakegeneric_source/`.

## 640x480

```sh
cd 640/zz9000
ZZQ_HIRES=1 ZZQ_PROD=1 bash build_zzquake.sh ../../quakegeneric_source
cp build/zzquake.blob ../zzq640.bin
```

Expected blob: 395228 bytes, MD5 `5d0f7c422d0db562669eb3a4f1e8e223`.

## 800x600

```sh
cd 800/zz9000
ZZQ_RES=800x600 ZZQ_HEAPBUF=1 ZZQ_PROD=1 bash build_zzquake.sh ../../quakegeneric_source
cp build/zzquake.blob ../zzq800.bin
```

Expected blob: 395564 bytes, MD5 `e77a6ced32f129009d9bfea4e9f493c8`.

## 1024x768

```sh
cd 1024/zz9000
ZZQ_RES=1024x768 ZZQ_HEAPBUF=1 ZZQ_PROD=1 bash build_zzquake.sh ../../quakegeneric_source
cp build/zzquake.blob ../zzq1024.bin
```

Expected blob: 395676 bytes, MD5 `43f0a0f017a9fc744de93ac64fc09df9`.

The public source archive contains only the GPL-covered Core1 side. The Amiga 68k launcher source is private and is not part of this build tree.
