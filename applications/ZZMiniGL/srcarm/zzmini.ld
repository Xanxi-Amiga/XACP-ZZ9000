ENTRY(core1_entry)
SECTIONS {
 . = 0x04500000;
 .text : { KEEP(*(.text.core1_entry)) *(.text*) }
 .rodata : { *(.rodata*) }
 .ARM.extab : { *(.ARM.extab*) }
 .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; }
 .data : { *(.data*) *(.sdata*) }
 .bss (NOLOAD) : { . = ALIGN(16); _bss_start = .; *(.bss*) *(COMMON) . = ALIGN(16); _bss_end = .; }
 /* A6.1: non-cached scratch at the bottom of section 0x046, below the A1 control block; NOLOAD,
    not part of core1.bin, not cleared by start.S */
 .zz_nc 0x04600000 (NOLOAD) : { _zz_nc_start = .; *(.zz_nc*) . = ALIGN(16); _zz_nc_end = .; }
 /* A9: ZZGL state (2560 texture names) in the Core1 arena, section 0x3E9 mapped by mmu.c, above the
    texture pool (0x3D800000..0x3E800000) and below the firmware guard; NOLOAD, cleared by zzgl_init */
 /* A8.17: Core1's vertex cache, section 0x3E8 of the arena (between the texture pool and the ZZGL state) */
 .zz_vcache 0x3E800000 (NOLOAD) : { _zz_vcache_start = .; *(.zz_vcache*) . = ALIGN(16); _zz_vcache_end = .; }
 .zz_arena 0x3E900000 (NOLOAD) : { _zz_arena_start = .; *(.zz_arena*) . = ALIGN(16); _zz_arena_end = .; }
 /* A8.21: the static store, sections 0x3EA..0x3EC of the arena (3 MiB above the ZZGL state) */
 .zz_vstatic 0x3EA00000 (NOLOAD) : { _zz_vstatic_start = .; *(.zz_vstatic*) . = ALIGN(16); _zz_vstatic_end = .; }
 _stack_top = 0x046ff000;    /* start-up and exception stack: non-cacheable section, unchanged */
 _stack_fast = 0x04600000;   /* A4R: service stack, top of the write-back blob section; A6: 32 KiB reserved (measured worst chain < 6 KiB with -fstack-usage) */
}
ASSERT(_bss_end <= 0x045f8000, "Core1 image overlaps the service stack (keep code+BSS under 0x045F8000)")
ASSERT(_zz_nc_end <= 0x04650000, "Non-cached scratch overlaps the A1 control block")
ASSERT(_zz_arena_end <= 0x3EA00000, "ZZGL arena state exceeds its section 0x3E9")
ASSERT(_zz_vcache_end <= 0x3E900000, "Core1 vertex cache exceeds its section 0x3E8")
ASSERT(_zz_vstatic_end <= 0x3ED00000, "Core1 static store exceeds its sections 0x3EA..0x3EC")
