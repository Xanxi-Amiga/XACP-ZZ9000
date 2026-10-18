# Copyright and licensing

The source distribution combines several upstream projects:

- MNT Research's ZZ9000 firmware and related components: retain the copyright
  and GPL notices found in the original source tree.
- RasterIX FPGA renderer, by ToNi3141: GPL-3.0-or-later. The upstream commit
  used for this integration is recorded in `PROVENANCE.json`.
- XACP-specific FPGA, firmware and image-service additions by Xanxi: retain
  their source-file license declarations.
- The public XACP SDK headers and 68k image-service client: 0BSD, where marked.
- stb_image by Sean Barrett and contributors: upstream public-domain/MIT terms
  are preserved within the file.
- AMD/Xilinx BSP sources: retain their original vendor notices. IP cores must
  be regenerated under an appropriate licensed toolchain.

No original license header or attribution line in third-party source has
been removed. The source ZIP is not a blanket relicensing of any component.
