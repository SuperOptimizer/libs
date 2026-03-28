/*
 * mock_entry.S - Exception vector table and entry stubs
 * Exercises: .balign 2048/128, vector table layout, exception entry/exit,
 * kernel_entry/kernel_exit macros, mrs/msr for EL1 state save/restore
 */

.macro kernel_entry
    stp x29, x30, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x17, x18, [sp, #-16]!
    stp x15, x16, [sp, #-16]!
    stp x13, x14, [sp, #-16]!
    stp x11, x12, [sp, #-16]!
    stp x9, x10, [sp, #-16]!
    stp x7, x8, [sp, #-16]!
    stp x5, x6, [sp, #-16]!
    stp x3, x4, [sp, #-16]!
    stp x1, x2, [sp, #-16]!
    str x0, [sp, #-8]!
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #-16]!
.endm

.macro kernel_exit
    ldp x0, x1, [sp], #16
    msr elr_el1, x0
    msr spsr_el1, x1
    ldr x0, [sp], #8
    ldp x1, x2, [sp], #16
    ldp x3, x4, [sp], #16
    ldp x5, x6, [sp], #16
    ldp x7, x8, [sp], #16
    ldp x9, x10, [sp], #16
    ldp x11, x12, [sp], #16
    ldp x13, x14, [sp], #16
    ldp x15, x16, [sp], #16
    ldp x17, x18, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x29, x30, [sp], #16
    eret
.endm

.macro ventry, label
    .balign 128
    b \label
.endm

/*
 * Exception vector table - must be 2048-byte aligned
 * 4 groups of 4 entries = 16 entries, each 128 bytes
 */
.section ".text"
.balign 2048
.global vectors
vectors:
    /* Current EL with SP0 */
    ventry el1_sync_sp0
    ventry el1_irq_sp0
    ventry el1_fiq_sp0
    ventry el1_error_sp0

    /* Current EL with SPx */
    ventry el1_sync_spx
    ventry el1_irq_spx
    ventry el1_fiq_spx
    ventry el1_error_spx

    /* Lower EL using AArch64 */
    ventry el0_sync_64
    ventry el0_irq_64
    ventry el0_fiq_64
    ventry el0_error_64

    /* Lower EL using AArch32 */
    ventry el0_sync_32
    ventry el0_irq_32
    ventry el0_fiq_32
    ventry el0_error_32

/*
 * Exception handlers
 */
el1_sync_sp0:
    kernel_entry
    mov x0, sp
    bl handle_sync
    kernel_exit

el1_irq_sp0:
el1_fiq_sp0:
el1_error_sp0:
el1_sync_spx:
el1_irq_spx:
el1_fiq_spx:
el1_error_spx:
el0_sync_64:
el0_irq_64:
el0_fiq_64:
el0_error_64:
el0_sync_32:
el0_irq_32:
el0_fiq_32:
el0_error_32:
    kernel_entry
    mov x0, sp
    bl handle_exception
    kernel_exit

/*
 * C handler stubs (would be in C code normally)
 */
.type handle_sync, %function
handle_sync:
    /* Read exception syndrome register */
    mrs x1, elr_el1
    ret

.type handle_exception, %function
handle_exception:
    ret

/*
 * Install vector table
 */
.global install_vectors
.type install_vectors, %function
install_vectors:
    adrp x0, vectors
    add x0, x0, :lo12:vectors
    msr vbar_el1, x0
    isb
    ret
