ENTRY(_start)

SECTIONS
{
    . = 0x04200000;

    .text : ALIGN(16) {
        KEEP(*(.text.startup))
        *(.text*)
    }

    .rodata : ALIGN(16) {
        *(.rodata*)
    }

    /* ARM EABI unwind sections.  Normally empty/minimal because every C
       file is built with -fno-unwind-tables, but libgcc expects the boundary
       symbols to exist if one of its helper objects refers to them. */
    .ARM.extab : ALIGN(4) {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    }

    . = ALIGN(4);
    __exidx_start = .;
    .ARM.exidx : ALIGN(4) {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    }
    __exidx_end = .;

    .data : ALIGN(16) {
        *(.data*)
    }

    . = ALIGN(16);
    _bss_start = .;
    .bss (NOLOAD) : {
        *(.bss*)
        *(COMMON)
    }
    . = ALIGN(16);
    _bss_end = .;

    /* Keep the complete visible blob comfortably inside 0x042..0x043. */
    ASSERT(_bss_end < 0x043C0000, "ZZVGM Core1 blob/BSS too large")

    _stack_bottom = 0x043C0000;
    _stack_top    = 0x043E0000;
}
