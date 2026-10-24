/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2a0.ld - ZZQuake2 A0 Core1 blob linker script.
 *
 * This is the ZZQuake640 release script (zzquake.ld) with ONE added
 * output section, .q2bss. Every release line is kept as it is:
 *   - blob origin 0x04900000, entry zzq_entry
 *   - .text.zzq_entry first, .text.core1_entry second
 *   - .bss inside the 2 MiB blob window, cleared by core1_entry
 *   - ASSERT(_blob_end <= 0x04B00000)
 *
 * .q2bss holds the zero-initialised data of the Quake II objects only
 * (objects built under build/q2obj/). It is NOLOAD, it is not part of
 * the blob image, and the release bootstrap never touches it:
 * core1_entry clears _bss_start.._bss_end and nothing else. The
 * adapter clears .q2bss itself, at the QG_Create call point, after
 * the release MMU setup has mapped the arena WB/WA.
 *
 * Address: the TOP of the ARM-only arena that the release already
 * maps (L1 sections 0x300..0x3F7) and already owns
 * (ZZQ_PACK_ARENA_BASE..ZZQ_PACK_ARENA_END):
 *
 *     0x30000000  pak0, placed by the launcher, any size
 *     HEAP_START  = first MiB boundary at or after the real end of the pak
 *         ...     Quake II heap (Hunk + large malloc), zzq2_mem.c
 *     HEAP_LIMIT  = 0x3EC00000 = start of .q2bss
 *     0x3EC00000  .q2bss, 12 MiB reserve
 *     0x3F800000  end of the arena (firmware guard, never mapped)
 *
 * Only the start of .q2bss is fixed, because it is a link-time
 * address. The heap takes whatever lies between the pak and .q2bss;
 * the adapter checks the three zones against each other before it
 * clears .q2bss (zzq2_glue.c, QG_Create).
 * No new MMU section, no existing address moved.
 */

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
        . = ALIGN(4);
        __ol_init_start = .;
        KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array))
        __ol_init_end = .;
        _data_end = .;
    }

    .ARM.extab : { *(.ARM.extab*) }
    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    }

    /* ---- ZZQuake2 A0 addition: begin ---- */
    _zzq2_low_resume = .;
    .q2bss 0x3EC00000 (NOLOAD) : {
        _q2bss_start = .;
        *q2obj/*.o(.bss* COMMON)
        . = ALIGN(32);
        _q2bss_end = .;
    }
    . = _zzq2_low_resume;
    /* ---- ZZQuake2 A0 addition: end ---- */

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

/* ---- ZZQuake2 A0 additions ---- */
ASSERT(_q2bss_start == 0x3EC00000,
       "ZZQuake2: .q2bss is not at its documented address")
ASSERT(_q2bss_end <= 0x3F800000,
       "ZZQuake2: .q2bss exceeds its 12 MiB reserve (0x3EC00000..0x3F800000)")
