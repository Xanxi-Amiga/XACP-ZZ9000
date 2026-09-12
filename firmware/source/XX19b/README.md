# XX19b / XACP v1.7 firmware source

This directory contains the publication source tree for firmware **XX19b**, implementing the **XACP v1.7** baseline.

XX19b keeps the XX19a / XACP v1.6 layout below `0x30000000` unchanged and adds a documented ARM-only Core1 arena.

## High Core1 arena

```text
0x30000000 - 0x3F800000   248 MiB usable
0x3F800000 - 0x3FC00000     4 MiB guard
0x3FC00000 - 0x40000000     4 MiB firmware/hardware reserved
```

The normative header is:

```text
ZZ9000_proto.sdk/ZZ9000OS/src/xacp_memory_map_v1_7.h
```

## SMUSH Codec1/37/47

The legacy SMUSH codec implementation used fixed scratch buffers in the `0x30000000+` range. It is removed from the XX19b build so that range can be assigned to Core1 applications. Existing `ACC_CMPTYPE` values remain reserved; those requests are unsupported. IMA ADPCM and unrelated decompression paths are unchanged.

## Source layout

```text
ZZ9000_proto.sdk/   firmware, FSBL, BSP and platform sources
util/               host-side build helper source
build_firmware.sh   firmware ELF build helper
COPYING             GNU GPL v3 license text
```

Generated object and ELF files from the development archive are not published.

## License

The firmware source follows the licensing of the underlying ZZ9000/XACP firmware tree and the notices contained in the source. The repository firmware license file is GNU GPL v3.
