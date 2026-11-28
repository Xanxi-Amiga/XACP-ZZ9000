/* prog.ld - synthetic 68000 test program, linked like a CPS-1 program ROM (tests only). */
ENTRY(_start)
MEMORY { rom : ORIGIN = 0x000000, LENGTH = 0x100000  ram : ORIGIN = 0xFF0000, LENGTH = 0x8000 }
SECTIONS {
  .text 0x000000 : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > rom
  .mid  0x040000 : { KEEP(*(.midrom)) } > rom      /* lands in 31.12f / 36.12h */
  .hi   0x080000 : { KEEP(*(.hirom)) } > rom       /* lands in st-14.8h        */
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(2); __bss_end = .; } > ram
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.data*) }
}
