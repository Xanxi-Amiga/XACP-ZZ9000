ENTRY(core1_entry)
MEMORY { RAM (rwx) : ORIGIN = 0x04500000, LENGTH = 0x001E0000 }
SECTIONS
{
  . = ORIGIN(RAM);
  .text : { KEEP(*(.text.core1_entry)) *(.text*) } > RAM
  .rodata : { *(.rodata*) } > RAM
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > RAM
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx* .gnu.linkonce.armexidx.*) __exidx_end = .; } > RAM
  .data : { *(.data*) } > RAM
  . = ALIGN(16);
  _bss_start = .;
  .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
  _bss_end = .;
  _end = .;
  _stack_top = 0x046FF000;
  ASSERT(_end < 0x046E0000, "ZZGear blob/BSS exceeds safe window")
}
