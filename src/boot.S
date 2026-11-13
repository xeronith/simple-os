/*
 * boot.S - entry point for simple-os
 *
 * QEMU's "virt" machine loads our ELF and jumps to _start. Depending on the
 * machine configuration we may arrive at EL1 or EL2, so we drop to EL1 first,
 * install the vector table, set up a stack, clear .bss and call kmain().
 */

    .section .text.boot, "ax", %progbits
    .global _start
    .type _start, %function

_start:
    /* Interrupts off until the kernel is ready. */
    msr daifset, #0xf

    /* Are we at EL2? If so, arrange to return to EL1. */
    mrs x0, CurrentEL
    lsr x0, x0, #2
    cmp x0, #2
    b.ne .Lel1

    mov x0, #(1 << 31)          /* HCR_EL2.RW = 1: EL1 executes AArch64 */
    msr hcr_el2, x0
    mov x0, #0x3c5              /* SPSR_EL2: DAIF masked, EL1h */
    msr spsr_el2, x0
    ldr x0, =.Lel1
    msr elr_el2, x0
    eret

.Lel1:
    /* VBAR_EL1 must point at a 2 KiB aligned table. */
    ldr x0, =vectors
    msr vbar_el1, x0

    /* Dedicated 16 KiB boot stack, defined in .bss below. */
    ldr x0, =__stack_top
    mov sp, x0

    /* Clear .bss. The linker rounds __bss_end up to 16 bytes. */
    ldr x0, =__bss_start
    ldr x1, =__bss_end
.Lbss_loop:
    cmp x0, x1
    b.hs .Lbss_done
    str xzr, [x0], #8
    b .Lbss_loop
.Lbss_done:
    bl kmain

    /* kmain is not supposed to return. */
.Lhang:
    wfi
    b .Lhang

    .size _start, . - _start

    .section .bss, "aw", %nobits
    .align 12
    .global __stack_bottom
    .global __stack_top
__stack_bottom:
    .skip 0x4000
__stack_top:
