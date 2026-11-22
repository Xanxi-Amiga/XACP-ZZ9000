# Third-party licenses - ZZMiniGL Beta 1

This release contains the corresponding sources for `minigl.library`, `ZZMiniGLLoad` and the embedded Cortex-A9 Core1 program. Components are subject to their **own** original license terms; this document does not replace those licenses.

| Component | Origin and obligations |
|---|---|
| `vendor/minigl/minigl_dispatch.h` | MiniGL 29.1 ABI from the Hyperion / Steffen Haeuser MiniGL project. The Hyperion MiniGL Open Source License requires public functional source of modifications and restricts compilation/incorporation to AmigaOS code. Original terms: `vendor/minigl/LICENSE.txt`. |
| MiniGL frontend and ABI adapters in `src68k/`, `tools/` | ZZMiniGL/XACP integration by Xanxi. Preserve the licensed MiniGL interface attribution and any third-party notices. |
| `srcarm/start.S`, `srcarm/mmu.c` | Adapted from earlier GPL-2.0-or-later XACP Core1 startup/MMU components by Xanxi. Corresponding source provided here; GPLv2 license text at `vendor/COPYING.GPL2`. |
| `vendor/zzgl/`, `vendor/librixgpu/` | Renderer and low-level driver components with 0BSD portions; preserve each in-file notice and `vendor/LICENSE.0BSD`. |
| `vendor/librixgpu/rix_walk_model.h` | GPL-3.0-or-later optional host-model code. The production Core1 build explicitly sets `ZZGL_ENABLE_WALK_MODEL=0`. GPLv3 text in `LICENSES/GPL-3.0.txt`. |
| Other build files and shared protocol | Preserve any in-file notices and author attributions. |

The ARM Core1 payload is compiled as a separate executable for a different processor and embedded in the 68k library as installable data. It communicates with the 68k frontend through a packet protocol. This architectural description is **not** a legal determination of how the different licenses apply to a combined distribution.

Upstream references:
- https://github.com/SteffenHaeuser/MiniGL_Library_68k
- https://github.com/bubbob42/RadeonGL
- https://github.com/ToNi3141/RasterIX
- https://github.com/Xanxi-Amiga/XACP-ZZ9000

The executable `gears` and the texture `data/chrome.ppm` are copies of the
unmodified PiStorm3D distribution (`util/libs/PiStorm3D.lha` on Aminet),
provided by Steffen Haeuser. The archive's original Hyperion MiniGL license
and upstream README_Licences.txt are preserved here under `LICENSES/`.
See also https://github.com/SteffenHaeuser/PiStorm3D . This attribution
does not claim ownership of the demo or its texture by the ZZMiniGL author.
Any separate asset rights remain subject to the upstream authors' terms.

Other third-party demo binaries and commercial Quake data are not redistributed.
Users obtain them separately from their respective copyright holders.
