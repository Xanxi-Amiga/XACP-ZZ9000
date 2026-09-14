/*
 * zzquake.ld - ZZQuake Core1 blob linker script.
 *
 * Base = the REAL ZZDoom linker.ld (md5 9227888971ac2ddc85f9b7d340
 * b16754: ENTRY core1_entry, KEEP .text.core1_entry first, only
 * _bss_start/_bss_end exported, no MEMORY, no stack symbol - the SP
 * comes from the firmware). Two ZZQuake additions:
 *   - .ARM.extab/.ARM.exidx kept before .bss (libgcc unwinder refs)
 *   - ASSERT on the 2MB blob reserve (audit #3)
 * ASCII only.
 */

/* A0.1: the asm VFP stub (zzquake_entry.S) is now the entry point and
   MUST be the very first code at 0x04900000. core1_entry (C) follows
   it immediately. */
ENTRY(zzq_entry)

SECTIONS {
    . = 0x04900000;

    /* Bornes de section, ajoutees pour le diagnostic E101 : elles
       permettent de sommer .text et .data separement, des deux cotes
       du pont, sur exactement les memes plages. Aucun effet sur le
       code genere. */
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
