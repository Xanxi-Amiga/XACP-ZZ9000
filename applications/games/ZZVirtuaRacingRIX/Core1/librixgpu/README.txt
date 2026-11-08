librixgpu 1.5 (0BSD, LICENSE_0BSD.txt), as in ZZOpenLaraRIX, unchanged.
ZZVirtuaRacingRIX uses the init (rix_init: ID, FIFO, fences; no texture
stages), the ring of display-list chunks (rixframe.c), the triangle
descriptors (rixdesc.c) and the screen clipping (rixclip.c).  No textures,
no PL330, no WFI.  The only symbol it needs from the program is
dcache_clean_range (Core1/zzvr_mmu.c).

ZZVirtuaRacingRIX A7a adds to rixgpu.c / rixgpu.h one function group, the
rest being unchanged: rix_dma_present, rix_copy_start and rix_copy_wait, a
PL330 copy from one DDR range to another (64-byte aligned, 64-byte bursts so
that no burst crosses a 4 KiB boundary), optionally swapping the bytes of
every 16-bit pixel (the GPU's little-endian RGB565 to the big-endian P96
screen).  It uses the in-place swap's channel and program area and needs only
the DMAC to be clocked and out of reset (whatever RIXF_NODMA says).

ZZVirtuaRacingRIX A8g adds to rixdesc.c / rixdesc.h rix_tri_desc_flat: the
descriptor of a triangle with no texture and no depth whose r, g, b, z, u, v
and 1/w are the same on its three vertices (only alpha may vary): the edge
set-up as rix_tri_desc, the constant planes written straight (value, +0
gradients) and, when the alphas differ, the alpha plane as rix_desc_delta = 1
computes it. The same pixels as rix_tri_desc for such triangles.

ZZVirtuaRacingRIX A8h adds to rixgpu.c / rixgpu.h rix_dup_rows_start(first,
pitch, pairs): row 2k copied onto row 2k+1 for `pairs` pairs from `first` (a
PL330 program with an outer loop over the pairs, an inner one over the row's
64-byte bursts, then DMAADDH of one more row to both addresses), on the copy's
channel and program area; waited with rix_copy_wait.
