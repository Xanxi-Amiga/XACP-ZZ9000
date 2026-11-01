/*
 * zzvr.ld - ZZVirtuaRacingRIX Core1 blob (XX19d-RIX / XACP v1.8)
 * Model = ZZPicoDriveMD 1.1 zzpico.ld (entry first, core1_entry C, no startup.S,
 * SP from firmware).  ONE change: .bss moved from the v1.6 private pool
 * (0x22000000) to the XACP v1.7 Core1 arena (zzvr_mem.h, ZZVR_ARENA_BSS).
 *
 * Entry ARM 0x04500000 (fb+0x04300000). REG_ARM_RUN HI=0x0450 LO=0x0000.
 * Blob window 0x04500000..0x04700000 (2 MB); shared at 0x04700000.
 */
ENTRY(core1_entry)
SECTIONS
{
    . = 0x04500000;
    .text : { KEEP(*(.text.core1_entry)) *(.text*) *(.rodata*) . = ALIGN(4); }
    .data : { *(.data*) . = ALIGN(4); }
    _blob_end = .;
    ASSERT(_blob_end <= 0x046F0000,
        "ZZVirtuaRacing blob reaches shared at 0x04700000. STOP, read .map.")

    .bss 0x30000000 (NOLOAD) : {
        _bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    }
    ASSERT(_bss_start == 0x30000000,
        "ZZVirtuaRacing BSS is not at ZZVR_ARENA_BSS (0x30000000).")
    ASSERT(_bss_end <= 0x31000000,
        "ZZVirtuaRacing BSS exceeds its 16 MiB arena region 0x30000000..0x31000000.")

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) *(.note*) *(.comment*) }
}
