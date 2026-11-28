# arm/cps1 - the CPS-1 engine

Source: **libretro/fbalpha2012_cps1**, commit `5542c1848ef81e92db311193b01dc349bc29d7cc`,
directory `src/burn/drv/capcom/` (FB Alpha licence, see `licenses/`).

| File | Status |
|---|---|
| `cps.h`, `cps_config.cpp`, `cps_obj.cpp`, `cps_scr.cpp`, `cpsr.cpp`, `cpsrd.cpp`, `cpst.cpp`, `ctv.cpp`, `ctv_do.h`, `cps_mem.cpp`, `cps_rw.cpp` | **verbatim** |
| `ctv.h` | verbatim output of `ctv_make.cpp` as shipped by upstream (`dep/generated`) |
| `ctv_make.cpp` | verbatim, kept for reference, **not compiled** |
| `cps_draw.cpp` | **adapted**, every change is marked `ZZS-n` |
| `cps_pal.cpp` | **adapted** (rewritten around the same interface) |

FB Alpha modules replaced by adapted or new ZZStrider files one directory up (the replacements retain FB Alpha-derived code where noted):

| FB Alpha | ZZStrider | Why |
|---|---|---|
| `cps.cpp` (ROM loading) | `zzs_rom.c`, `zzs_cps_load.cpp` | ROMs arrive in chunks from the 68k side; incremental decoding and graphics mask computation derive from FB Alpha |
| `cps_run.cpp`, `cps_driver.cpp`, `ps.cpp`, `ps_z.cpp`, `ps_m.cpp`, `qs*.cpp` | `zzs_cps_run.cpp`, `zzs_sound.c` | frame scheduling derived from FB Alpha / FBNeo; Z80 + YM2151 + MSM6295 board on CZ80 |
| `burn/` framework, `m68000_intf`, `z80_intf` | `burn_shim/`, `zzs_burn.cpp` | a small shim: the 68000 bus on Cyclone, a bump allocator |

Changes in the adapted files:

* **ZZS-1** `cps_draw.cpp`: out-of-bounds read of `Draw[i + 1]` on the last layer guarded.
* **ZZS-2** star fields redone after MAME `cps1_v.cpp` (presence test, colour cycling, order),
  written straight into the INDEX16 frame.
* **ZZS-3** video-control bits 2 and 3 gate scroll 2 and scroll 3 (MAME).
* **ZZS-4** instrumentation only (cycle counters, layer order, draw mask).
* `cps_pal.cpp`: the tile plotters write palette **indices** (identity `CpsPal`), the real
  colours live in a separate 0xFFRRGGBB table used by the final conversion; the palette page
  rule is the MAME / FBNeo one.
* Frame model (in `zzs_cps_run.cpp`): vblank to vblank, 262 lines, registers and sprite list
  latched at the end of the frame, as MAME and FBNeo do (FB Alpha 2012 used 259 lines).

The engine is not Strider-specific: `cps_config.cpp` still holds every CPS-B / mapper table.
What selects the game is `arm/zzs_game.h` (CPS-B id, mapper, star field flag, clocks, sizes)
and the ROM table of `arm/zzs_rom.c`.
