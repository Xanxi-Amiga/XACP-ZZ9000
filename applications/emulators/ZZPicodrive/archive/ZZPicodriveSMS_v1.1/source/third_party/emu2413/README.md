# emu2413 header provenance

ZZPicoDriveSMS 1.1 uses the exact `emu2413.h` header from:

```text
digital-sound-antiques/emu2413
a2dfc20ff507e4fd075cd325620bcea655e2c1f7
```

The file SHA-1 as stored by Git is:

```text
dbc0af1d9ea15abd165f5ddb6785b9c25a78f880
```

The YM2413 implementation is not linked into ZZPicoDriveSMS 1.1. The header is
needed because PicoDrive `pico/sms.c` includes it even when the ZZPicoDrive SMS
build stubs the YM2413 write interface.

See `LICENSE` for the MIT License.
