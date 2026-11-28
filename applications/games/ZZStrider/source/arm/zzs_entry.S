/*
 * zzs_entry.S - ZZStrider Core1 entry point.
 *
 * The firmware (core2.c, core1_loop) enters the blob with SP = 0x06000000, caches and MMU OFF.
 * ZZRastan kept that stack. Its own page table then mapped everything it did not list as
 * Strongly-Ordered, so section 0x05F - the stack - stayed UNCACHED for the whole session: every
 * push/pop of every C function and of every Cyclone memory handler was a raw DDR access.
 * ZZStrider switches at once to a private stack inside its BSS window (0x22000000..0x22400000,
 * mapped WB/WA by zzs_mmu.c). Nothing is saved: like ZZRastan this blob never returns to the
 * firmware, it parks in WFE with STATUS = STOPPED and is restarted by a Core1 reset (ARM_RUN).
 *
 * VFP is enabled here explicitly (CPACR cp10/cp11 + FPEXC.EN). The firmware already does it,
 * but the blob uses hard-float (YM2151 tables) and must not depend on that.
 *
 * ASCII only.
 */
    .syntax unified
    .arm

    .section .text.core1_entry,"ax",%progbits
    .global core1_entry
    .type   core1_entry,%function
core1_entry:
    cpsid   if                          /* no IRQ/FIQ source is ever enabled by this blob */
    ldr     sp, =_zzs_stack_top
    mrc     p15, 0, r0, c1, c0, 2       /* CPACR */
    orr     r0, r0, #(0xF << 20)        /* cp10 + cp11 full access */
    mcr     p15, 0, r0, c1, c0, 2
    isb
    mov     r0, #0x40000000             /* FPEXC.EN */
    vmsr    fpexc, r0
    bl      zzs_core1_main              /* never returns */
1:  wfe
    b       1b
    .size   core1_entry, .-core1_entry
    .ltorg
