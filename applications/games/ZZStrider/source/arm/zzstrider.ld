/* ZZStrider Core1 linker script - firmware XX19d-RIX / XACP v1.8. ASCII only.
 * Derived from zzrastan.ld. Changes: .ARM.extab/.ARM.exidx kept with __exidx_start/__exidx_end,
 * a private stack section, and explicit asserts for every boundary of docs/MEMORY_MAP.md. */
ENTRY(core1_entry)
SECTIONS
{
    . = 0x04500000;
    .text : {
        KEEP(*(.text.core1_entry))
        *(.text*)
        *(.glue_7) *(.glue_7t) *(.vfp11_veneer) *(.v4_bx)
        *(.rodata*)
        . = ALIGN(4);
    }
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) . = ALIGN(4); }
    __exidx_start = .;
    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) . = ALIGN(4); }
    __exidx_end = .;
    .init_array : {
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP(*(.preinit_array))
        __preinit_array_end = .;
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        __fini_array_start = .;
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;
        . = ALIGN(4);
    }
    .data : { *(.data*) *(.got*) *(.igot*) *(.iplt*) *(.rel.iplt*) . = ALIGN(4); }
    _blob_end = .;
    ASSERT(_blob_end <= 0x046F0000,
        "ZZStrider blob reaches the shared section at 0x04700000.")

    .bss 0x22000000 (NOLOAD) : {
        _bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    }
    /* Private Core1 stack. NOT part of .bss: zero_bss() must not clear the stack it runs on. */
    .stack (NOLOAD) : {
        . = ALIGN(4096);
        _zzs_stack_bottom = .;
        . += 0x00040000;
        _zzs_stack_top = .;
    }
    ASSERT(_zzs_stack_top <= 0x22400000,
        "ZZStrider BSS + stack exceed 0x22000000..0x22400000.")

    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) *(.ARM.attributes*) }
}
