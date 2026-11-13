/*
 * exc.S - exception vector table and low-level handlers
 *
 * Layout is the architectural one: 16 entries of 0x80 bytes. We only expect
 * IRQ (timer) and synchronous exceptions (faults / BRK) from EL1, but every
 * slot is filled so a stray exception is reported instead of silently
 * rebooting the machine.
 */

    .section .text.vectors, "ax", %progbits
    .align 11
    .global vectors
vectors:

    .macro fault_vector kind
        .balign 0x80
        mov x0, #\kind
        b .Lfault_common
    .endm

    /* Current EL with SP_EL0 */
    fault_vector 0
    .balign 0x80
    b .Lirq_entry
    .balign 0x80
    b .Lirq_entry              /* FIQ and IRQ share the GIC handler */
    .balign 0x80
    fault_vector 3

    /* Current EL with SP_ELx */
    fault_vector 4
    .balign 0x80
    b .Lirq_entry
    .balign 0x80
    b .Lirq_entry
    .balign 0x80
    fault_vector 7

    /* Lower EL using AArch64 */
    fault_vector 8
    .balign 0x80
    b .Lirq_entry
    .balign 0x80
    b .Lirq_entry
    .balign 0x80
    fault_vector 11

    /* Lower EL using AArch32 */
    fault_vector 12
    .balign 0x80
    b .Lirq_entry
    .balign 0x80
    b .Lirq_entry
    .balign 0x80
    fault_vector 15

/*
 * Save every general purpose register. This is more than strictly necessary
 * for a C IRQ handler, but it keeps the stack frame uniform and makes adding
 * context switches later straightforward.
 */
.Lirq_entry:
    sub sp, sp, #256
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]

    bl irq_handler

    ldr x30, [sp, #240]
    ldp x28, x29, [sp, #224]
    ldp x26, x27, [sp, #208]
    ldp x24, x25, [sp, #192]
    ldp x22, x23, [sp, #176]
    ldp x20, x21, [sp, #160]
    ldp x18, x19, [sp, #144]
    ldp x16, x17, [sp, #128]
    ldp x14, x15, [sp, #112]
    ldp x12, x13, [sp, #96]
    ldp x10, x11, [sp, #80]
    ldp x8, x9, [sp, #64]
    ldp x6, x7, [sp, #48]
    ldp x4, x5, [sp, #32]
    ldp x2, x3, [sp, #16]
    ldp x0, x1, [sp, #0]
    add sp, sp, #256
    eret

/*
 * Fatal exception path. x0 holds the vector kind, which C uses when printing
 * the report. We never return.
 */
.Lfault_common:
    mrs x1, esr_el1
    mrs x2, far_el1
    mrs x3, elr_el1
    mrs x4, spsr_el1
    bl exception_report
.Lfault_hang:
    wfi
    b .Lfault_hang
