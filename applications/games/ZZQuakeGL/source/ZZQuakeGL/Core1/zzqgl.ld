/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzqgl.ld - ZZQuakeGL Core1 blob linker script.
 *
 * This is the ZZQuake640 release script (zzquake.ld) with ONE added
 * output section, .glbss, exactly as zzq2a0.ld of ZZQuake2 A0 added
 * .q2bss.  Every release line is kept as it is:
 *   - blob origin 0x04900000, entry zzq_entry
 *   - .text.zzq_entry first, .text.core1_entry second
 *   - .bss inside the 2 MiB blob window, cleared by core1_entry
 *   - ASSERT(_blob_end <= 0x04B00000)
 *
 * .glbss holds the zero-initialised data of the engine objects only
 * (objects built under build/q1obj/: quakegeneric files, id gl_*.c,
 * librixgpu, ZZGL, the GPU adapter and the GL binding).  It is NOLOAD,
 * it is not part of the blob image, and the release bootstrap never
 * touches it: core1_entry clears _bss_start.._bss_end and nothing
 * else.  zzqgl_qgmain.c clears .glbss itself, at the QG_Create call
 * point, after the release MMU setup has mapped the arena WB/WA.
 *
 * Address: the TOP of the ARM-only arena the release already maps (L1
 * sections 0x300..0x3F7) and already owns (ZZQ_PACK_ARENA_BASE..
 * ZZQ_PACK_ARENA_END), the same 12 MiB ZZQuake2RIX uses for .q2bss:
 *
 *     0x30000000  pak0 (+ pak1, mission packs, mods), placed by the launcher
 *     0x3D800000  GPU reservation: texture pool 16 MiB, chunk ring,
 *                 Z-buffer, bind lists (zzq2rix_gpu.c), up to 0x3EC00000
 *     0x3EC00000  .glbss, 12 MiB reserve
 *     0x3F800000  end of the arena (firmware guard, never mapped)
 *
 * The Quake hunk stays where the release puts it (newlib heap at
 * 0x07000000, malloc in QG_Create).  No new MMU section, no existing
 * address moved.
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
        _data_end = .;
    }

    .ARM.extab : { *(.ARM.extab*) }
    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    }

    /* ---- ZZQuakeGL addition: begin ---- */
    _zzqgl_low_resume = .;
    .glbss 0x3EC00000 (NOLOAD) : {
        _glbss_start = .;
        *q1obj/*.o(.bss* COMMON)
        . = ALIGN(32);
        _glbss_end = .;
    }
    . = _zzqgl_low_resume;
    /* ---- ZZQuakeGL addition: end ---- */

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

/* ---- ZZQuakeGL additions ---- */
ASSERT(_glbss_start == 0x3EC00000,
       "ZZQuakeGL: .glbss is not at its documented address")
ASSERT(_glbss_end <= 0x3F800000,
       "ZZQuakeGL: .glbss exceeds its 12 MiB reserve (0x3EC00000..0x3F800000)")
