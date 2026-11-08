# ZZVirtuaRacingRIX A2a - test build

Virtua Racing Deluxe (32X) on the ZZ9000 Core1, step A2: the high-level
emulation (HLE) of VirtuaRacingDeluxeEnhanced rebuilds the race's polygon list
from each scene the 68000 sends, and is checked against the picture the
emulated SH-2s draw. **The picture on screen does not change yet**: the SH-2s
still draw it, as in ZZPicoDrive32X. RasterIX comes at step A3.

Base: ZZPicoDrive32X v1.2.1 (same Core1 program and launcher: every frame
displayed, 50/60 Hz, Paula and AHI, savestates, Del reset), plus:

- `Core1/vrde/`: `hle.c`, `hud.c`, `sh2raster.c`, `hlecheck.c` of
  VirtuaRacingDeluxeEnhanced 1.2.0 (see THIRD_PARTY_NOTICES.md).
- `Core1/zzvr_hle.c`: the hooks (COMM, DREQ, frame buffer writes) and the
  check. After each race picture the 32X puts on screen, its polygon list is
  filled the way the SH-2s fill it (`sh2raster.c`) and compared pixel by pixel
  with the frame buffer on screen (`hlecheck.c`), HUD pixels left out.
- The HUD pixels are found from the master SH-2's writes into the frame
  buffer, through a hook in PicoDrive's `sh2_write*_dram` handlers: VRDE's own
  hook exists only in the SH-2 interpreter, ours sees the recompiler too.

Core1 program: `reference/zzvr_a2a_reference.bin` (the log shows `sub=7`).

## ROM

Virtua Racing Deluxe (USA), CRC32 `7896B62E`: the only ROM the HLE knows. The
launcher prints the CRC32 of the ROM and says whether it is that one. Any
other ROM still runs, the HLE then does not match. No ROM is included.

## Test on the A4000

From the Shell, in the drawer of the program:

```text
ZZVirtuaRacingRIX "Virtua Racing Deluxe (USA).32x" usa paula            (1) HLE + check
ZZVirtuaRacingRIX "Virtua Racing Deluxe (USA).32x" usa paula nocheck    (2) HLE alone
ZZVirtuaRacingRIX "Virtua Racing Deluxe (USA).32x" usa paula nohle      (3) as ZZPicoDrive32X
```

For each: let the demo run once, then play a race (2-3 laps, change the view
with X/Y/Z), Shift+Esc. Send the end of the log (from `=== BLOB DIAG ===`).

What to read in the log, block `=== VR HLE ===`:

- `scenes from the 68000`, `lists built`: the HLE follows the game (20 scenes a
  second in a race and in the demo). `avg/max us`: cost of building a list.
- `race pictures shown`, `polygons avg/max`.
- `check ... pictures / identical / below 90%` and `pixels equal ... %`: the A2
  criterion is **90 % or more of the pixels equal over a race**; VRDE gets
  identical pictures on a PC.
- `fill+compare avg/max us`: the cost of the check itself (test 1 only).
- Compare `PicoFrame window avg/max` of runs 1, 2 and 3: the cost of the HLE
  (2 minus 3) and of the check (1 minus 2). The HLE builds its list inside
  PicoFrame (at the scene's last DMA word); the check runs after it.

Run 1 may lose a few frames (the check fills and compares up to 61 440 pixels
20 times a second); runs 2 and 3 should not.

Flags of this build, besides those of ZZPicoDrive32X: `nohle`, `nocheck`.
Configuration `PROGDIR:ZZVirtuaRacingRIX.cfg`, Workbench log
`T:ZZVirtuaRacingRIX.log`.

## Build

```text
M68K_GCC="C:/amiga-gcc/bin/m68k-amigaos-gcc" ./build_release.sh
```

Step 1 compares the Core1 program with the reference; a different MD5 means a
different toolchain build (`BLOB=reference/zzvr_a2a_reference.bin
./build_release.sh embed` then `./build_release.sh launcher` embeds the
reference itself). The result is `release/ZZVirtuaRacingRIX/`.

Firmware: XX19d-RIX. Nothing in the firmware, bitstream or XACP protocol is
changed.
