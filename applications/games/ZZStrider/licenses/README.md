# ZZStrider - Third-party licences and copyright notices

ZZStrider is freeware for non-commercial use. The ARM Core1 blob combines ZZStrider-specific code with third-party emulation components and has no single blanket licence.

ZZStrider-specific contributions are credited to Xanxi (2026). Third-party and derivative code remains subject to its applicable licence terms. No copyright notice here overrides those terms.

The separate AmigaOS launcher and GUI are proprietary (Copyright (c) 2026 Xanxi, all rights reserved). They may be redistributed unmodified as part of the complete non-commercial ZZStrider package. Their source is not included.

## ARM Core1 components

| Component | Where | Origin (commit) | Licence | File here |
|---|---|---|---|---|
| CPS-1 video / IO engine, ROM decoding and frame scheduling | `arm/cps1/`, `arm/zzs_rom.c`, `arm/zzs_cps_load.cpp`, `arm/zzs_cps_run.cpp` | libretro/fbalpha2012_cps1 `5542c1848ef81e92db311193b01dc349bc29d7cc` | FB Alpha: non-commercial, source of modifications must be published, licence text must accompany every distribution, no donation or payment may be solicited | `FBAlpha-License.txt` |
| Corrections taken from FBNeo | `arm/cps1/cps_draw.cpp`, `cps_pal.cpp` | finalburnneo/FBNeo `b4547931756884aa3d226063915ad9730519d296` | FBNeo (same terms) | `FBNeo-License.txt` |
| Hardware reference and adapted algorithms | `arm/cps1/cps_draw.cpp`, `cps_pal.cpp`, `arm/zzs_msm6295.c` | mamedev/mame `89185ed286c7830b4e6daa647d843a929077ecd2` | BSD-3-Clause (per-file headers) | `MAME-BSD-3-Clause.txt`, `MAME-License.txt` |
| Cyclone 68000 | `third_party/cyclone/` | notaz/cyclone68000 `3ac7cf1bdeecb60e2414980e8dc72ff092f69769` | dual GPLv2 / MAME licence: **used here under the MAME-licence option** (the GPL option is not compatible with the FB Alpha terms) | `Cyclone-License.txt`, `Cyclone-Credits.txt` |
| CZ80 | `third_party/cz80/` | notaz/picodrive `26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee` | non-commercial, credit required | `CZ80-readme.txt` |
| PicoDrive port headers | `third_party/pico/pico_port.h`, `pico_types.h` | same PicoDrive commit | MAME-like, non-commercial | `PicoDrive-COPYING.txt` |
| YM2151 (Jarek Burczynski) | `third_party/ym2151/` | as shipped in ZZRastan 1.0 (from FBNeo) | FBNeo terms | `FBNeo-License.txt` |
| MSM6295 | `arm/zzs_msm6295.c` | written for ZZStrider after MAME `okim6295.cpp` / `okiadpcm.cpp` | BSD-3-Clause | `MAME-BSD-3-Clause.txt` |

Consequences for distribution:

* ZZStrider is **non-commercial**; no donation may be solicited in connection with it.
* The **complete source of the Core1 blob** (`arm/`, `third_party/`) must be published with every
  binary release, together with this directory.
* These component-specific terms apply to the Core1 source and binary; no repository-wide licence overrides them.
* The launcher and the GUI are separate AmigaOS programs that only exchange data with the blob
  through the shared memory protocol (`arm/zzs_shared.h`); they contain none of the code above.
* **No ROM** is part of ZZStrider. The user supplies the files of the MAME set `strider`.

The individual licence texts take precedence over this summary. The ROM set name refers to compatible user-supplied game data, not to a requirement to install MAME.
