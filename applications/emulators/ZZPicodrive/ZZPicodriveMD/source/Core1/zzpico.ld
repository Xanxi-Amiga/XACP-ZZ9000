/*
 * zzpico.ld - ZZPicoDrive Core1 blob (XX19 / XACP v1.5 map)
 * Model = ZZDoom (entry first, core1_entry C, no startup.S, SP from firmware).
 *
 * Entry ARM 0x04500000 (fb+0x04300000). REG_ARM_RUN HI=0x0450 LO=0x0000.
 * Blob window 0x04500000..0x04700000 (2 MB); shared at 0x04700000.
 * Big .bss -> ARM private pool 0x22000000 (NOT a shared hole).
 */
ENTRY(core1_entry)
SECTIONS
{
    . = 0x04500000;
    .text : { KEEP(*(.text.core1_entry)) *(.text*) *(.rodata*) . = ALIGN(4); }
    .data : { *(.data*) . = ALIGN(4); }
    _blob_end = .;
    ASSERT(_blob_end <= 0x046F0000,
        "ZZPicoDrive blob reaches shared at 0x04700000. STOP, read .map (lock #5).")

    .bss 0x22000000 (NOLOAD) : {
        _bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    }
    ASSERT(_bss_end <= 0x22400000,
        "ZZPicoDrive BSS exceeds private pool window 0x22000000..0x22400000.")

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) *(.note*) *(.comment*) }
}
