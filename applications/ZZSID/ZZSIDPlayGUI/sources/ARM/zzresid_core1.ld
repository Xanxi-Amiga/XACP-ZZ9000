/*
 * zzresid_core1.ld - linker script for ZZReSID Core1 blob
 *
 * Blob at ARM 0x04200000. Must NOT reach the shared mailbox at 0x05A00000.
 * Two ASSERTs enforce that: one on the image, one on the heap ceiling.
 *
 * The heap is now bounded. It used to start after the stack and grow with
 * nothing between it and the mailbox, so a large enough allocation would
 * have silently overwritten the 68k protocol. _sbrk in the blob refuses to
 * cross _heap_limit.
 *
 * Author: Xanxi / 2026
 */

ENTRY(_start)

MEMORY
{
    BLOB (rwx) : ORIGIN = 0x04200000, LENGTH = 0x01800000  /* 24 MB */
}

_stack_size = 0x00010000;  /* 64 KB */
_heap_size  = 0x00200000;  /*  2 MB - residfp allocates via new on the
                            * MOS8580 path; 2 MB is ample and bounded. */

SECTIONS
{
    . = 0x04200000;

    .text : {
        KEEP(*(.text.startup))
        *(.text*)
        *(.gnu.linkonce.t.*)
    } > BLOB

    .rodata : {
        *(.rodata*)
        *(.gnu.linkonce.r.*)
        . = ALIGN(8);
    } > BLOB

    /* .data holds fw_sp from startup.S, the firmware stack pointer parking
     * slot. It must stay OUT of .bss, which is cleared at startup. */
    .data : {
        *(.data*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(8);
    } > BLOB

    .ARM.extab : {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
        . = ALIGN(8);
    } > BLOB

    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > BLOB

    .bss (NOLOAD) : {
        . = ALIGN(8);
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } > BLOB

    .stack (NOLOAD) : {
        . = ALIGN(8);
        _stack_bottom = .;
        . = . + _stack_size;
        . = ALIGN(8);
        _stack_top = .;
    } > BLOB

    .heap (NOLOAD) : {
        . = ALIGN(8);
        _heap_start = .;
        . = . + _heap_size;
        . = ALIGN(8);
        _heap_limit = .;
    } > BLOB

    /* newlib still wants these symbols even though the blob provides its
     * own bounded _sbrk. */
    end = _heap_start;
    _end = _heap_start;
    PROVIDE(end = _heap_start);

    _blob_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}

ASSERT(_blob_end   < 0x05A00000, "ZZReSID image reaches the shared mailbox at 0x05A00000")
ASSERT(_heap_limit < 0x05A00000, "ZZReSID heap ceiling reaches the shared mailbox at 0x05A00000")
