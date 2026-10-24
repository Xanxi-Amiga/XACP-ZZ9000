ZZOpenLara 1.0 -- ARM Engine Sources (Software UI bars correction)
===================================
Xanxi | 24 October 2026
https://github.com/Xanxi-Amiga/XACP-ZZ9000

Sources for the four ARM Cortex-A9 renderer variants distributed as part
of ZZOpenLara 1.0. The 68k programs ZZOpenLara and ZZOpenLaraGUI are
separate freeware executables; their implementation sources are not in
this source archive. No Tomb Raider commercial game assets are included.

Build environment: Windows/MSYS; fully OFFLINE, with the toolchain already
installed at:
  C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-gcc
  C:/ArmGNUToolchain/13.2 Rel1/bin/arm-none-eabi-objcopy

From this source folder, run:
  sh build_all.sh

In this ARM-only source package, build_all.sh invokes the ARM-only build
of all four renderer variants. The upstream tools/build.py also contains
historical 68k build steps not selected by the supplied script; those
require the separately maintained private 68k sources.
No git clone, network download, PATH override or MSYS /c/... compiler
paths are required. Build output goes under out/.

The exact ARM binary files distributed in the Amiga ZIP were built and
validated by Xanxi on his Windows/MSYS PC. Byte-for-byte reproduction may
depend on toolchain installation and build environment; source package
contents and the release files can be checked against SHA256SUMS.txt.

Read LICENSES.txt and ATTRIBUTION.txt. OpenLara is BSD-2-Clause, some
runtime files contain GPL-2.0-or-later notices, RasterIX helpers have
0BSD notices, and expressly marked original Xanxi port material carries
GPL-3.0-only notices plus the section 7(b) attribution term. Preserve all
notices, including those in other third-party sources.

Release source update 24 October 2026:
  The 640x480 software renderer now draws the health, oxygen and menu
  adjustment bars correctly. The changes are limited to
  openlara/src/mesh.h and openlara/src/gapi/sw.h.
  The matching newly compiled zzopenlara_sw.bin must accompany the
  public binary archive; do not publish an older Software blob.
