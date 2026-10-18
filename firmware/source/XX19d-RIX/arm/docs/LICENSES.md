# Licensing notes

This development package contains code from multiple upstream components.

- The ZZ9000 firmware tree is derived from the MNT ZZ9000 firmware and keeps
  its GPL-3.0-or-later notices where present. New firmware integration code in
  `zz_image.c` and `zz_image_jpeg_stream.inc` is marked GPL-3.0-or-later for
  compatibility with that tree.
- The private JPEG row-output adapter is compiled after the pinned
  `stb_image.h` implementation and uses stb_image private JPEG structures and
  routines. `stb_image.h` retains its upstream public-domain/MIT terms in the
  file itself.
- The small XACP public ABI headers and 68k client under `sdk/` are marked
  0BSD by Xanxi.
- Xilinx/AMD BSP files retain their upstream notices.

Before a public GitHub release, keep all upstream notices/license texts intact
and add the repository-level license presentation appropriate to the complete
firmware fork.
