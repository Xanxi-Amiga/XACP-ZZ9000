# Provenance and licenses

This is an **unmodified source snapshot** copied from the released
`applications/ZZMiniGL/vendor/zzgl/` and
`applications/ZZMiniGL/vendor/librixgpu/` trees at GitHub commit:

`158fe613a4ee8a5b17c73ffea341180991d33ab4`

The per-file Git blob object IDs in `SOURCE_MANIFEST.txt` are the authority
for this exact snapshot. This snapshot is not a new license grant for
any third-party work.

- ZZGL and most librixgpu sources contain `SPDX-License-Identifier: 0BSD`
  notices, or refer to their companion copyright terms. Preserve all original
  notices. The supplied 0BSD text is in `LICENSES/0BSD.txt`.
- **Exception:** `librixgpu/rix_walk_model.h` identifies itself as
  `GPL-3.0-or-later` and is derived from ToNi3141's RasterIX rasterizer
  reference. Its license text is in `LICENSES/GPL-3.0.txt`.
- ZZGL's `#include "rix_walk_model.h"` is guarded by
  `ZZGL_ENABLE_WALK_MODEL`; the ordinary documented build sets this to 0.
  Separate compilation and distribution choices must respect all applicable
  license obligations.
- No MiniGL Hyperion ABI header, external demo asset, original game source,
  closed-source launcher or third-party engine is added to this SDK folder.

This is an engineering inventory, not a legal opinion on combined works.
