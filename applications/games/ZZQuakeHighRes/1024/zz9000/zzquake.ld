ENTRY(zzq_entry)

SECTIONS {
    . = 0x04900000;

    .text : {
        _text_start = .;
        KEEP(*(.text.zzq_entry))
        KEEP(*(.text.core1_entry))
        *(.text*)
        *(.rodata*)
        _text_end = .;
    }

    .data : {
        _data_start = .;
        *(.data*)
        _data_end = .;
    }

    .ARM.extab : { *(.ARM.extab*) }
    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    }

    .bss : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    }

    _blob_end = .;
}

ASSERT(_blob_end <= 0x04B00000,
       "ZZQuake blob overflow: 2MB reserve exceeded (shared at 0x04B00000)")
