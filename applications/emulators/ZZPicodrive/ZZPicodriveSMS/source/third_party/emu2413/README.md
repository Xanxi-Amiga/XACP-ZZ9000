emu2413 1.6.3 (YM2413 / OPLL emulator), MIT License, Mitsutaka Okazaki.
Upstream: https://github.com/digital-sound-antiques/emu2413
Commit:   83254be0de24b3506438c45b2870017885cb778c (the copy of ZZVGMPlay 1.0,
          see README_ZZVGM.txt).
Copied unchanged to third_party/picodrive/pico/sound/emu2413/, where
PicoDrive's pico/sound/ym2413.c #includes emu2413.c.  Same API and same OPLL /
OPLL_SLOT structures as the revision PicoDrive 26ecb2b6 references
(a2dfc20ff507e4fd075cd325620bcea655e2c1f7); 1.6.3 only adds
OPLL_save_state / OPLL_load_state, unused here.
