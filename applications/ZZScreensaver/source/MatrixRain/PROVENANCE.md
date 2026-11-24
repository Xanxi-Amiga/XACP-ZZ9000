# ZZScreensaverR source provenance

## The scene

Original program: **Matrix GL / MatrixView**, Copyright (C) 2003 Alex Zolotov
<nightradio@knoppix.ru>, "mucked with" by Tugrul Galatali (rss-glx), ported
to an MPlayer video-out plugin by Pigeon (August 2006). GPL-2.0-or-later.

Source used: the MPlayer copy, from the Git mirror of svn.mplayerhq.hu at
https://github.com/Distrotech/MPlayer, commit
`5f14f72a5ff6f5e93c549f6516265efed729c8db` (2 May 2016):

| File | MD5 |
|---|---|
| `libvo/matrixview.c` | `0ebb294c5840450f62f27c23175ced37` |
| `libvo/matrixview.h` | `793a6ea915cb714ee689cfc2bf0fd708` |
| `libvo/matrixview_font.h` | `e732f4c7174da1738c196c9968ba60ed` |

The three files are kept untouched in `upstream/matrixview/`.

Changes in `matrix/`:

- `matrixview.c`: a notice of modification, and one line of `draw_text()`:
  `int pic_fade = 255;` became `int pic_fade = pic ? 255 : 0;`. MPlayer always
  gives the scene a picture (the video); without one the unmodified line draws
  every character at full brightness. With 0 the rain is drawn alone.
- `matrixview.h`, `matrixview_font.h`: unchanged.
- `gl_common.h` is not MPlayer's: it is the ZZ9000 header of the same name,
  written for this program, so that `matrixview.c` needs no other change.

The pictures that the rss-glx version of MatrixView can display are not part
of this program.

## Everything else

`ARM_RasterIX/`, `zzgl/`, `librixgpu/`, `tools/` and `tests/sim_rix.c` come
from the published ZZScreensaverS (Solar Winds) 1.1 source,
https://github.com/Xanxi-Amiga/XACP-ZZ9000 at commit `158fe613`,
`applications/ZZScreensaver/source/SolarWinds/`. Differences:

- `ARM_RasterIX/zzgear_app.c`: one include line (`matrix_scene.h`).
- `ARM_RasterIX/ZZGearRIX.c`: program name, version string, credits, usage
  text, presets 1..3 with 2 as the default, two labels of the DEBUG report.
  One change of logic: the presenter loop ends when the screen of the program
  is no longer the front one (`screen_in_front()`), the per-frame PAN is
  issued under `Forbid()` after that test, and the closing PAN is skipped in
  that case. `TOBACK n` is a test option for it.
- `ARM_RasterIX/rix_shared.h`, `zzgl/zzgldemo.h`: one comment each.
- `zzgl/zzgl.c`, `zzgl/zzgl.h`: `zzgl_direct_begin()`,
  `zzgl_direct_tri()` and `zzgl_target_color()` added at the end of each file;
  no existing line changed.
  `librixgpu/` is byte-identical to the published tree.
- `tools/build.py`, `tools/host_build.py`, `tools/host_validate.py`: source
  list and output names; a second simulator for the fallback path.
- `tests/sim_rix.c`: three presets; this scene must never copy the previous
  frame; running hashes of what the simulated GPU receives; a check of every
  frame buffer Core1 writes itself.
- The Solar Winds scene, its upstream copy, its previews and its reference
  test are not in this tree.

New: `matrix/` (except `history.c`, `history.h`), `tests/test_matrix.c`,
`tools/host_test.py`, `tools/host_stream.py`, `ZZScreensaverR_Test`.
